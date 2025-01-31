#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"


#define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))

bool initialize_pico(void){

    return true;
}

void reboot_pico(void){
    AIRCR_Register = 0x5FA0004;
}