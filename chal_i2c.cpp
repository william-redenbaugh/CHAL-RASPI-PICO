#include "global_includes.h"

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"

static i2c_inst_t* map_i2c_fd(int fd){
    switch(fd){
        case 0:
            return &i2c0_inst;
        case 1:
            return &i2c1_inst;
        default: 
            return NULL;
    }
}

int os_i2c_begin(os_i2c_t *i2c, int fd, int speed){
    i2c->fd = fd;
    i2c->speed = speed;

    i2c_init(map_i2c_fd(fd), speed);
    
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    return OS_RET_OK;
}

int os_i2c_end(os_i2c_t *i2c){
    i2c_deinit(map_i2c_fd(fd));

    return OS_RET_OK;
}

int os_i2c_setbus(os_i2c_t *i2c, uint32_t freq_hz){
    i2c_set_baudrate(map_i2c_fd(fd), freq_hz);

    return OS_RET_OK;
}

int os_i2c_send(os_i2c_t *i2c, uint8_t addr, uint8_t *buf, size_t size){
    int ret = i2c_write_blocking((map_i2c_fd(fd), addr, &buf, size, false);

    if(ret == PICO_ERROR_GENERIC){
        return OS_RET_INT_ERR;
    }

    return ret;
}

int os_i2c_recieve(os_i2c_t *i2c, uint8_t addr, uint8_t *buf, size_t size){
    int ret = i2c_read_blocking(map_i2c_fd(fd), addr, &buf, size, false);

    if(ret == PICO_ERROR_GENERIC){
        return OS_RET_INT_ERR;
    }

    return ret;
}