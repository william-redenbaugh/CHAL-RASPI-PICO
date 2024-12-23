#ifndef _HUB75_CHAL_PICO_H
#define _HUB75_CHAL_PICO_H

#include "global_includes.h"
#include "stdint.h"
#include "stdlib.h"

typedef struct
{
    os_thread_id_t id;
    uint32_t img[4096];
} hub75_pico_t;

int hub75_init_pico(void *ptr, int width, int height);
int hub75_set_pixel_pico(void *ptr, int x, int y, uint8_t r, uint8_t g, uint8_t b);
int hub75_update_pico(void *ptr);

#endif