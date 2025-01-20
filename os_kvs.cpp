#include "global_includes.h"

int kv_store_init(void){
    return OS_RET_OK;
}

int kv_store_uninit(void){
    return OS_RET_OK;    
}

int os_kv_put_uint32(char* key, uint32_t value){
    return OS_RET_OK;    
}

int os_kv_put_uint64(char* key, uint64_t value){
    return OS_RET_OK;    
}

int os_kv_put_string(char* key, char* value, size_t len){
    return OS_RET_OK;    
}

int os_kv_get_uint32(char* key, uint32_t* value){
    return OS_RET_OK;
}

int os_kv_get_uint64(char* key, uint64_t* value){
    return OS_RET_OK;
}

int os_kv_get_string(char* key, char* value, size_t *len){
    return OS_RET_OK;  
}

int os_kv_remove(char* key){
    return OS_RET_OK;   
}

int os_kv_flush_data(void){
    return OS_RET_OK;   
}