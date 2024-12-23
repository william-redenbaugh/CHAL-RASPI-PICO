#include "global_includes.h"
#include "thread_definitions.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hub75_chal_pico.h"

#include <stdio.h>
#include <string.h>
#include "stdint.h"
#include "stdlib.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "ps_debug.h"
#include "hub_75_internal_includes.h"

char logTimeBuf[32];

uint8_t overlayBuffer[4096] = {0};

static inline void pio_sm_put_blocking_rtos(PIO pio, uint sm, uint32_t data)
{
    check_pio_param(pio);
    check_sm_param(sm);
    while (pio_sm_is_tx_fifo_full(pio, sm))
        _os_yield();
    pio_sm_put(pio, sm, data);
}

int hub75_init_pico(void *ptr, int width, int height)
{
    // Ignore width and heigh for now, its 64, 64

    hub75_pico_t *mod = (hub75_pico_t *)ptr;

    hub75_config(8);

    return OS_RET_OK;
}

int hub75_set_pixel_pico(void *ptr, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{

    hub75_pico_t *mod = (hub75_pico_t *)ptr;
    int pos = x * 64 + y;

    int value = (r) | (g << 8) | (b << 16);
    mod->img[pos] = value;

    return OS_RET_OK;
}

int hub75_update_pico(void *ptr)
{
    hub75_pico_t *mod = (hub75_pico_t *)ptr;
    hub75_update(mod->img, overlayBuffer);
    return OS_RET_OK;
}

#if HUB75_SIZE == 4040
uint32_t frameBuffer[DISPLAY_MAXPLANES * (DISPLAY_WIDTH / 4) * DISPLAY_SCAN]; // each entry contains RGB data for 2 or 4 consective pixels on one HUB75 channel
#elif HUB75_SIZE == 8080
uint32_t frameBuffer[DISPLAY_MAXPLANES * (DISPLAY_WIDTH / 2) * DISPLAY_SCAN]; // each entry contains RGB data for 2 pixels on two HUB75 channels
#else
#error "V2 board supports 64x64 or 128x128 layouts"
#endif
rgb_int_t *addrBuffer[(1 << DISPLAY_MAXPLANES)];
uint16_t bcmCounter = 1; // index in addrBuffer array

uint32_t ctrlBuffer[DISPLAY_MAXPLANES * DISPLAY_SCAN]; // N bit planes * # of scan lines

uint16_t masterBrightness = 0;
uint16_t bitPlanes = DISPLAY_MAXPLANES;

static PIO display_pio = pio0;
static uint display_sm_data;
static uint display_offset_data;
static uint display_sm_ctrl;
static uint display_offset_ctrl;

static int display_dma_chan;
static int ctrl_dma_chan;

static rgb_int_t overlayColors[16];

static void dma_hub75_handler()
{
    // Clear the interrupt request.
    if (dma_hw->ints0 & (1u << display_dma_chan))
    {
        dma_hw->ints0 = 1u << display_dma_chan;
        // start next display cycle
        dma_channel_set_read_addr(display_dma_chan, addrBuffer[bcmCounter], true);
        if (++bcmCounter >= (1 << bitPlanes))
        {
            gpio_xor_mask(1 << 15); // debug LED for frame time measurement
            bcmCounter = 1;
        }
    }
    if (dma_hw->ints0 & (1u << ctrl_dma_chan))
    {
        dma_hw->ints0 = 1u << ctrl_dma_chan;
        // start next display cycle
        dma_channel_set_read_addr(ctrl_dma_chan, &ctrlBuffer[0], true);
    }
}

static void hub75_init()
{
    // Initialize PIO
    display_sm_data = pio_claim_unused_sm(display_pio, true);
    display_sm_ctrl = pio_claim_unused_sm(display_pio, true);

    display_offset_data = pio_add_program(display_pio, &ps_64_data_program);
    ps_64_data_program_init(
        display_pio,
        display_sm_data,
        display_offset_data,
        PIO_DATA_OUT_BASE, PIO_DATA_OUT_CNT,
        PIO_DATA_SET_BASE, PIO_DATA_SET_CNT,
        PIO_DATA_SIDE_BASE, PIO_DATA_SIDE_CNT);

    display_offset_ctrl = pio_add_program(display_pio, &ps_64_ctrl_program);
    ps_64_ctrl_program_init(
        display_pio,
        display_sm_ctrl,
        display_offset_ctrl,
        PIO_CTRL_OUT_BASE, PIO_CTRL_OUT_CNT,
        PIO_CTRL_SET_BASE, PIO_CTRL_SET_CNT,
        PIO_CTRL_SIDE_BASE, PIO_CTRL_SIDE_CNT);

    // Initialize data port DMA
    display_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(display_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_PIO0_TX0 + display_sm_data);

    //    channel_config_set_ring(&c, false, 15);     // ring size is 8192

    dma_channel_configure(
        display_dma_chan,
        &c,
        &pio0_hw->txf[display_sm_data],
        NULL, // Will be set later for each transfer
#if HUB75_SIZE == 4040
        DISPLAY_SCAN * ((DISPLAY_WIDTH / 4)), // complete frame buffer for 1 bit plane
#elif HUB75_SIZE == 8080
        DISPLAY_SCAN * ((DISPLAY_WIDTH / 2)), // complete frame buffer for 1 bit plane
#endif
        false);
    dma_channel_set_irq0_enabled(display_dma_chan, true);

    // Initialize control port DMA
    ctrl_dma_chan = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config(ctrl_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_PIO0_TX0 + display_sm_ctrl);

    dma_channel_configure(
        ctrl_dma_chan,
        &c,
        &pio0_hw->txf[display_sm_ctrl],
        NULL,         // Will be set later for each transfer
        DISPLAY_SCAN, // complete frame buffer for 1 bit plane
        false);
    dma_channel_set_irq0_enabled(ctrl_dma_chan, true);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_hub75_handler);
    irq_set_priority(DMA_IRQ_0, 1);
    irq_set_enabled(DMA_IRQ_0, true);
}

static void hub75_start()
{
    dma_channel_set_read_addr(display_dma_chan, frameBuffer, true);
    dma_channel_set_read_addr(ctrl_dma_chan, ctrlBuffer, true);
}

void hub75_config(int bpp)
{
    if (bpp < 4)
        bpp = 4;
    if (bpp > 8)
        bpp = 8;

    bitPlanes = bpp;

    irq_set_enabled(DMA_IRQ_0, false); // stop interrupts on DMA channels
    gpio_init(DISPLAY_OENPIN);         // switch display OFF
    gpio_set_dir(DISPLAY_OENPIN, GPIO_OUT);
    gpio_put(DISPLAY_OENPIN, 1);

    if (pio_sm_is_claimed(display_pio, display_sm_ctrl))
    {
        pio_sm_set_enabled(display_pio, display_sm_ctrl, false);
        pio_sm_init(display_pio, display_sm_ctrl, 0, NULL);
        pio_sm_unclaim(display_pio, display_sm_ctrl);
    }

    if (pio_sm_is_claimed(display_pio, display_sm_data))
    {
        pio_sm_set_enabled(display_pio, display_sm_data, false);
        pio_sm_init(display_pio, display_sm_data, 0, NULL);
        pio_sm_unclaim(display_pio, display_sm_data);
    }

    pio_clear_instruction_memory(display_pio);

    if (dma_channel_is_claimed(display_dma_chan))
    {
        dma_channel_abort(display_dma_chan);
        dma_channel_config c = dma_channel_get_default_config(display_dma_chan);
        channel_config_set_enable(&c, false);
        dma_channel_set_config(display_dma_chan, &c, false);
        dma_channel_unclaim(display_dma_chan);
    }

    if (dma_channel_is_claimed(ctrl_dma_chan))
    {
        dma_channel_abort(ctrl_dma_chan);
        dma_channel_config c = dma_channel_get_default_config(ctrl_dma_chan);
        channel_config_set_enable(&c, false);
        dma_channel_set_config(ctrl_dma_chan, &c, false);
        dma_channel_unclaim(ctrl_dma_chan);
    }
    irq_remove_handler(DMA_IRQ_0, dma_hub75_handler);

#if HUB75_SIZE == 4040
    memset(frameBuffer, 0, bitPlanes * (DISPLAY_WIDTH / 4) * DISPLAY_SCAN * sizeof(uint32_t));
#elif HUB75_SIZE == 8080
    memset(frameBuffer, 0, bitPlanes * (DISPLAY_WIDTH / 2) * DISPLAY_SCAN * sizeof(uint32_t));
#else
#error "V2 board supports 64x64 or 128x128 layouts"
#endif
    memset(ctrlBuffer, 0, bitPlanes * DISPLAY_SCAN * sizeof(uint32_t));
    memset(addrBuffer, 0, (1 << bitPlanes) * sizeof(uint32_t *));

    for (int bPos = 0; bPos < bitPlanes; bPos++)
    {
        for (int i = 1; i < (1 << bitPlanes); i++)
        {
            if (i & (1 << bPos) && (addrBuffer[i] == 0))
            {
                // LOG_DEBUG("addrBuffer[%3d] = plane %d\n", i, (bitPlanes - bPos));
#if HUB75_SIZE == 4040
                addrBuffer[i] = &frameBuffer[(bitPlanes - 1 - bPos) * (DISPLAY_WIDTH / 4) * DISPLAY_SCAN];
#elif HUB75_SIZE == 8080
                addrBuffer[i] = &frameBuffer[(bitPlanes - 1 - bPos) * (DISPLAY_WIDTH / 2) * DISPLAY_SCAN];
#endif
            }
        }
    }

    hub75_init();
    hub75_start();
}

void hub75_set_masterbrightness(int brt)
{
    brt += 4; // OE will be always HIGH during the last 4 pixels
    if (brt < 4)
        brt = 4;
    if (brt > (DISPLAY_WIDTH - 1))
        brt = (DISPLAY_WIDTH - 1);
    masterBrightness = brt;
}

void hub75_set_overlaycolor(int index, rgb_int_t color)
{
    if (index < 1 || index > 15) // index 0 is used internally for 'no overlay'
        return;
    overlayColors[index] = color;
}

int hub75_update(rgb_int_t *image, uint8_t *overlay)
{
    int x, y, b, plane;
    rgb_int_t *ip;
    rgb_int_t *fp, *cp;
    uint8_t flag = 0;
    uint8_t brtCnt = 0;

    for (b = (8 - bitPlanes); b < 8; b++) // only MSB bits of RGB color
    {
        ip = image;
        fp = &frameBuffer[(b - (8 - bitPlanes)) * DISPLAY_SCAN * (DISPLAY_WIDTH / 4)];
        cp = &ctrlBuffer[(b - (8 - bitPlanes)) * DISPLAY_SCAN];

        for (y = 0; y < DISPLAY_SCAN; y++)
        {
            rgb_int_t *ip_uu = image + (y * DISPLAY_WIDTH);
            rgb_int_t *ip_lu = image + ((y + DISPLAY_SCAN) * DISPLAY_WIDTH);
            uint8_t *op_uu = overlay + (y * DISPLAY_WIDTH);
            uint8_t *op_lu = overlay + ((y + DISPLAY_SCAN) * DISPLAY_WIDTH);

            brtCnt = 0;
            for (x = 0; x < DISPLAY_WIDTH / 4; x++) // 4 pixels per framebuffer word
            {
                rgb_int_t ipu = *ip_uu++;
                rgb_int_t ipl = *ip_lu++;

                if (*op_uu != 0)
                    ipu = overlayColors[*op_uu];
                op_uu++;
                if (*op_lu != 0)
                    ipl = overlayColors[*op_lu];
                op_lu++;

                rgb_int_t img = (((ipu & (1 << b)) >> b) << 2 |
                                 (((ipu >> 8) & (1 << b)) >> b) << 1 |
                                 ((ipu >> 16) & (1 << b)) >> b) |
                                ((((ipl & (1 << b)) >> b) << 2 |
                                  (((ipl >> 8) & (1 << b)) >> b) << 1 |
                                  (((ipl >> 16) & (1 << b))) >> b)
                                 << 3);
                if (++brtCnt > masterBrightness)
                    img |= (1 << 7);

                ipu = *ip_uu++;
                ipl = *ip_lu++;
                if (*op_uu != 0)
                    ipu = overlayColors[*op_uu];
                op_uu++;
                if (*op_lu != 0)
                    ipl = overlayColors[*op_lu];
                op_lu++;
                img |= ((((ipu & (1 << b)) >> b) << 2 |
                         (((ipu >> 8) & (1 << b)) >> b) << 1 |
                         ((ipu >> 16) & (1 << b)) >> b) |
                        ((((ipl & (1 << b)) >> b) << 2 |
                          (((ipl >> 8) & (1 << b)) >> b) << 1 |
                          (((ipl >> 16) & (1 << b))) >> b)
                         << 3))
                       << 8;
                if (++brtCnt > masterBrightness)
                    img |= (1 << 15);

                ipu = *ip_uu++;
                ipl = *ip_lu++;
                if (*op_uu != 0)
                    ipu = overlayColors[*op_uu];
                op_uu++;
                if (*op_lu != 0)
                    ipl = overlayColors[*op_lu];
                op_lu++;
                img |= ((((ipu & (1 << b)) >> b) << 2 |
                         (((ipu >> 8) & (1 << b)) >> b) << 1 |
                         ((ipu >> 16) & (1 << b)) >> b) |
                        ((((ipl & (1 << b)) >> b) << 2 |
                          (((ipl >> 8) & (1 << b)) >> b) << 1 |
                          (((ipl >> 16) & (1 << b))) >> b)
                         << 3))
                       << 16;
                if (++brtCnt > masterBrightness)
                    img |= (1 << 23);

                ipu = *ip_uu++;
                ipl = *ip_lu++;
                if (*op_uu != 0)
                    ipu = overlayColors[*op_uu];
                op_uu++;
                if (*op_lu != 0)
                    ipl = overlayColors[*op_lu];
                op_lu++;
                img |= ((((ipu & (1 << b)) >> b) << 2 |
                         (((ipu >> 8) & (1 << b)) >> b) << 1 |
                         ((ipu >> 16) & (1 << b)) >> b) |
                        ((((ipl & (1 << b)) >> b) << 2 |
                          (((ipl >> 8) & (1 << b)) >> b) << 1 |
                          (((ipl >> 16) & (1 << b))) >> b)
                         << 3))
                       << 24;
                if (++brtCnt > masterBrightness)
                    img |= (1 << 31);

                *fp++ = img;
            }

            uint32_t ctrl = ((y) & 0x1F); // ADDR lines: bits 0..4

            *cp++ = ctrl;
        }
    }

    return 0;
}