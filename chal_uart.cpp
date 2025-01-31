#include "global_includes.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "CHAL_SHARED/os_uart.h"
#include "dma_uart.hpp"

typedef struct {
    DmaUart *uart;
    byte_array_fifo *rx_fifo_safe_slow;
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

static uint8_t uart_rx_buffer[320]; 
void uart_rx_task(void *parameters){
    int_uart_t *uart = (int_uart_t*)parameters;
    //uart_inst_t *int_uart = target_uart(uart->uart->fd);

    for(;;){
        vTaskDelay(30);
        int num_bytes = uart->uart->read_all(uart_rx_buffer);
        if(num_bytes > 0){
            enqueue_bytes_bytearray_fifo(uart->rx_fifo_safe_slow, uart_rx_buffer, num_bytes);
        }    
    }    
}


int os_uart_begin(os_uart_t *uart, os_uart_config_t cfg, int fd, int baud){
    // Initialize UART 0 (default pins are GPIO0 for TX and GPIO1 for RX)
    
    uart_inst_t *int_uart = target_uart(fd);
    internal_uarts[fd].uart = new DmaUart(int_uart, baud, cfg.rx_gpio, cfg.tx_gpio);
    // To be consumed and used by the OS at a later date
    internal_uarts[fd].rx_fifo_safe_slow = create_byte_array_fifo(320);

    os_add_thread(uart_rx_task, &internal_uarts[fd], 320, NULL);

    return OS_RET_OK;
}

int os_uart_end(os_uart_t *uart){

    if(uart == NULL){
        return OS_RET_NULL_PTR;
    }

    if(target_uart(uart->fd) == NULL){
        return OS_RET_NOT_INITIALIZED;
    }


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

}

int os_uart_readstring(os_uart_t *uart, uint8_t *data, size_t size){
    return OS_RET_OK;
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
    block_until_n_bytes_fifo(internal_uarts[uart->fd].rx_fifo_safe_slow, size);
    return dequeue_bytes_bytearray_fifo(internal_uarts[uart->fd].rx_fifo_safe_slow, buf, size);
}

int os_uart_recieve_timeout(os_uart_t *uart, uint8_t *buf, size_t size, uint32_t timeout_ms){
    
    block_until_n_bytes_fifo_timeout(internal_uarts[uart->fd].rx_fifo_safe_slow, size, timeout_ms);
    return dequeue_bytes_bytearray_fifo(internal_uarts[uart->fd].rx_fifo_safe_slow, buf, size);
}