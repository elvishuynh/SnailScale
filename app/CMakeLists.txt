cmake_minimum_required(VERSION 3.20.0)

# explicitly link the t2 topology snippet root
list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SnailScale)

target_sources(app PRIVATE 
    src/main.c
    src/scale_logic.c
    src/touch_sensor.c
    src/bluetooth.c
)
