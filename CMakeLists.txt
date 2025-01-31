target_sources(${NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_uart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_i2c.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_pico.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hub75.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os_kvs.cpp
    )

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/ps_hub75_64_BCM.pio)
pico_btstack_make_gatt_header(${NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/nordic_spp_le_streamer.gatt")
