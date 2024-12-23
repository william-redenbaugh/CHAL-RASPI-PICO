target_sources(${NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_uart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_i2c.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ble_spp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chal_pico.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hub75.cpp
    )

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/ps_hub75_64_BCM.pio)
