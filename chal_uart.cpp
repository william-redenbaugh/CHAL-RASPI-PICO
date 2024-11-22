#include "global_includes.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "CHAL_SHARED/os_uart.h"

void on_uart0_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        // Can we send it back?
        if (uart_is_writable(UART_ID)) {
            // Change it slightly first!
            ch++;
            uart_putc(UART_ID, ch);
        }
        chars_rxed++;
    }
}

void on_uart1_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        // Can we send it back?
        if (uart_is_writable(UART_ID)) {
            // Change it slightly first!
            ch++;
            uart_putc(UART_ID, ch);
        }
        chars_rxed++;
    }
}


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
        return OS_RET_NULL_PTR;
    }

    if(target_uart(uart->fd) == NULL){
        return OS_RET_NOT_INITIALIZED;
    }

    sprintf(printf_out_arr, format);

    uart_puts(target_uart(uart->fd), printf_out_arr
}