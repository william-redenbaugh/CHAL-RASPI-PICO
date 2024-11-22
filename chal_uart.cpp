#include "global_includes.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "CHAL_SHARED/os_uart.h"

typedef struct {
    os_uart_t *uart;
    unsafe_fifo_t rx_fifo_unsafe_fast;
    safe_fifo_t rx_fifo_safe_slow;
    os_setbits_t rx_trigger;

}int_uart_t;

int_uart_t internal_uarts[2];

uart_inst_t *target_uart(int fd){
    switch(fd){
        case 0:
        return uart0;

        case 1: 
        return uart1;

        default: 
        return NULL;
    }
}

void uart_rx_task(void *parameters){
    int_uart_t *uart = (int_uart_t*)parameters;
    uart_inst_t *int_uart = target_uart(uart->uart->fd);

    for(;;){
        os_waitbits_indefinite(&uart->rx_trigger, BIT0);
        
        // Make some memory rq for coping over the data
        int num_available_bytes = uart->rx_fifo_unsafe_fast.num_elements_in_queue;
        uint8_t bytes[num_available_bytes];

        // Quick lil transfer into a queue we can use more safely
        unsafe_fifo_dequeue(&uart->rx_fifo_unsafe_fast, sizeof(uint8_t), bytes);
        safe_fifo_enqueue(&uart->rx_fifo_safe_slow, num_available_bytes, bytes);
    }    
}

void on_uart0_rx() {
    while (uart_is_readable(uart0)) {
        uint8_t ch = uart_getc(uart0);
        // Can we send it back?
        unsafe_fifo_enqueue(&internal_uarts[0].rx_fifo_unsafe_fast, 1, &ch);
    }
    os_setbits_signal(&internal_uarts[0].rx_trigger, BIT0);
}

void on_uart1_rx() {
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        // Can we send it back?
        unsafe_fifo_enqueue(&internal_uarts[1].rx_fifo_unsafe_fast, 1, &ch);
    }

    os_setbits_signal(&internal_uarts[1].rx_trigger, BIT0);
}

int os_uart_begin(os_uart_t *uart, os_uart_config_t cfg, int fd, int baud){
    // Initialize UART 0 (default pins are GPIO0 for TX and GPIO1 for RX)
    
    uart_inst_t *int_uart = target_uart(fd);

    // Set up our UART with the required speed.
    uart_init(int_uart, baud);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(cfg.tx_gpio, UART_FUNCSEL_NUM(int_uart, cfg.tx_gpio));
    gpio_set_function(cfg.rx_gpio, UART_FUNCSEL_NUM(int_uart, cfg.rx_gpio));

    uart->cfg = cfg;
    uart->baud = baud; 
    uart->fd = fd;

    internal_uarts[fd].uart = uart;

    // Small little queue used for any data coming straight from the RX handler
    unsafe_fifo_queue_init(&internal_uarts[fd].rx_fifo_unsafe_fast, 64, sizeof(uint8_t));

    // To be consumed and used by the OS at a later date
    safe_fifo_init(&internal_uarts[fd].rx_fifo_safe_slow, 256, sizeof(uint8_t));

    // Letting thread know that we have new data
    os_setbits_init(&internal_uarts[fd].rx_trigger);

    // Need a thread to help with any incoming UART data
    os_add_thread(uart_rx_task, &internal_uarts[fd], 1536, NULL);    

    // Setting interrupt handler for incoming data on the RX line
    int uart_irq = UART0_IRQ;
    if(int_uart == uart0){
        uart_irq = UART0_IRQ;
        irq_set_exclusive_handler(uart_irq, on_uart0_rx);
    }
    else if(int_uart == uart1){
        irq_set_exclusive_handler(uart_irq, on_uart1_rx);
        uart_irq = UART1_IRQ;
    }
    irq_set_enabled(uart_irq, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(int_uart, true, false);

    return OS_RET_OK;
}

int os_uart_end(os_uart_t *uart){

    if(uart == NULL){
        return OS_RET_NULL_PTR;
    }

    if(target_uart(uart->fd) == NULL){
        return OS_RET_NOT_INITIALIZED;
    }

    uart_deinit(
        target_uart(uart->fd)
    );

    return OS_RET_OK;
}

static char printf_out_arr[256];
void os_uart_printf(os_uart_t *uart, const char *format, ...){
    if(uart == NULL){
        return;
    }

    if(target_uart(uart->fd) == NULL){
        return;
    }

    sprintf(printf_out_arr, format);

    uart_puts(target_uart(uart->fd), printf_out_arr);
}

int os_uart_readstring(os_uart_t *uart, uint8_t *data, size_t size){
    return safe_fifo_dequeue_notimeout(
        &internal_uarts[uart->fd].rx_fifo_safe_slow, 
        size, 
        data
    );
}

int os_uart_send(os_uart_t *uart, uint8_t *buf, size_t size){
    if(uart == NULL){
        return OS_RET_INVALID_PARAM;
    }

    if(target_uart(uart->fd) == NULL){
        return OS_RET_INT_ERR;
    }

    for(int n = 0; n < size; n++){
        uart_putc_raw(
            target_uart(uart->fd), 
            *buf);
        
        buf++;
    }

    return OS_RET_OK;
}

int os_uart_recieve(os_uart_t *uart, uint8_t *buf, size_t size){
    return safe_fifo_dequeue_notimeout(
        &internal_uarts[uart->fd].rx_fifo_safe_slow, 
        size, 
        buf
    );
}

int os_uart_recieve_timeout(os_uart_t *uart, uint8_t *buf, size_t size, uint32_t timeout_ms){
    return safe_fifo_dequeue_timeout(
        &internal_uarts[uart->fd].rx_fifo_safe_slow, 
        size, 
        buf, 
        timeout_ms
    );
}