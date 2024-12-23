#include "global_includes.h"
#include "thread_definitions.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hub75.pio.h"
#include "hub75_chal_pico.h"

#define DATA_BASE_PIN 0
#define DATA_N_PINS 6
#define ROWSEL_BASE_PIN 6
#define ROWSEL_N_PINS 5
#define CLK_PIN 11
#define STROBE_PIN 12
#define OEN_PIN 13

#define WIDTH 64
#define HEIGHT 64

static inline uint32_t gamma_correct_565_888(uint16_t pix)
{
    uint32_t r_gamma = pix & 0xf800u;
    r_gamma *= r_gamma;
    uint32_t g_gamma = pix & 0x07e0u;
    g_gamma *= g_gamma;
    uint32_t b_gamma = pix & 0x001fu;
    b_gamma *= b_gamma;
    return (b_gamma >> 2 << 16) | (g_gamma >> 14 << 8) | (r_gamma >> 24 << 0);
}

void hub75_matrix_thread(void *params)
{

    hub75_pico_t *mod = (hub75_pico_t *)params;

    uint16_t *img = mod->img;

    PIO pio = pio0;
    uint sm_data = 0;
    uint sm_row = 1;

    uint data_prog_offs = pio_add_program(pio, &hub75_data_rgb888_program);
    uint row_prog_offs = pio_add_program(pio, &hub75_row_program);
    hub75_data_rgb888_program_init(pio, sm_data, data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);

    static uint32_t gc_row[2][WIDTH];
    while (1)
    {
        for (int rowsel = 0; rowsel < (1 << ROWSEL_N_PINS); ++rowsel)
        {
            for (int x = 0; x < WIDTH; ++x)
            {
                gc_row[0][x] = gamma_correct_565_888(img[rowsel * WIDTH + x]);
                gc_row[1][x] = gamma_correct_565_888(img[((1u << ROWSEL_N_PINS) + rowsel) * WIDTH + x]);
            }
            for (int bit = 0; bit < 8; ++bit)
            {
                hub75_data_rgb888_set_shift(pio, sm_data, data_prog_offs, bit);
                for (int x = 0; x < WIDTH; ++x)
                {
                    pio_sm_put_blocking(pio, sm_data, gc_row[0][x]);
                    pio_sm_put_blocking(pio, sm_data, gc_row[1][x]);
                }
                // Dummy pixel per lane
                pio_sm_put_blocking(pio, sm_data, 0);
                pio_sm_put_blocking(pio, sm_data, 0);
                // SM is finished when it stalls on empty TX FIFO
                hub75_wait_tx_stall(pio, sm_data);
                // Also check that previous OEn pulse is finished, else things can get out of sequence
                hub75_wait_tx_stall(pio, sm_row);

                // Latch row data, pulse output enable for new row.
                pio_sm_put_blocking(pio, sm_row, rowsel | (100u * (1u << bit) << 5));
            }
        }
    }
}

int hub75_init_pico(void *ptr, int width, int height)
{
    // Ignore width and heigh for now, its 64, 64

    hub75_pico_t *mod = (hub75_pico_t *)ptr;
    mod->id = os_add_thread(hub75_matrix_thread, ptr, 4096, NULL);

    return OS_RET_OK;
}

int hub75_set_pixel_pico(void *ptr, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{

    hub75_pico_t *mod = (hub75_pico_t *)ptr;

    int pos = x * 64 + y;
    mod->img[pos] = rgb888_to_rgb565(r, g, b);
    return OS_RET_OK;
}

int hub75_update_pico(void *ptr)
{

    // Don't really do anything
    return OS_RET_OK;
}