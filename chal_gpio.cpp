#include "global_includes.h"
#include "pico/stdlib.h"

static gpio_drive_strength map_drive_strength(os_gpio_drive_strength_t strength){
    switch(strength){
        case OS_GPIO_NORMAL_STRENGTH:
        return GPIO_DRIVE_STRENGTH_4MA;

        case OS_GPIO_HIGH_STRENGTH:
        return GPIO_DRIVE_STRENGTH_12MA;

        default:
        return GPIO_DRIVE_STRENGTH_2MA;
    }
}

int os_gpio_config(os_gpio_conf_t *conf){

    gpio_init(conf->gpio_pin);
    if(conf->dir == OS_GPIO_INPUT){
        gpio_set_dir(conf->gpio_pin, false); 
    }
    else{
        gpio_set_dir(conf->gpio_pin, true);
        gpio_set_drive_strength(conf->gpio_pin, map_drive_strength(conf->strength));
    }

    return OS_RET_OK;
}

int os_gpio_get_config(os_gpio_conf_t *conf){

}

int os_gpio_isr_config(os_gpio_isr_conf_t *conf){

}

int os_gpio_get_isr_conf(os_gpio_isr_conf_t *conf){

}

int os_gpio_enable_int(int pin, bool en){
}

int os_gpio_set(int gpio_pin, os_gpio_set_t set){
    gpio_put(gpio_pin, (int)set);

    return OS_RET_Ok;
}

int os_gpio_read(int gpio_pin){
    return gpio_get(gpio_pin);   
}