cmake_minimum_required(VERSION 3.20.0)

# explicitly link the t2 topology snippet root
list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SnailScale)

set(FLPR_BIN_PATH "${CMAKE_BINARY_DIR}/../app_flpr/zephyr/zephyr.bin")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/flpr_firmware.h
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/bin_to_header.py ${FLPR_BIN_PATH} ${CMAKE_CURRENT_BINARY_DIR}/flpr_firmware.h
    DEPENDS ${FLPR_BIN_PATH}
)

target_sources(app PRIVATE 
    src/main.c
    src/scale_logic.c
    src/touch_sensor.c
    src/bluetooth.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/flpr_firmware.h)
