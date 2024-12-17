#include <stdio.h>
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/lwiperf.h"

bool initialize_pico(void){
    if (cyw43_arch_init()) {
        printf("failed to initialise cyw43_arch\n");
        return false;
    }

    return true;
}