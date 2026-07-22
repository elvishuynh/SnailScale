cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SnailScale)

set(FLPR_BIN_PATH "${CMAKE_BINARY_DIR}/../app_flpr/zephyr/zephyr.bin")
set(FLPR_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/flpr_firmware.h")
set(BIN2HDR  "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/bin_to_header.py")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")

add_custom_command(
    OUTPUT  "${FLPR_HDR}"
    COMMAND "${PYTHON_EXECUTABLE}" -X utf8 "${BIN2HDR}"
            --input      "${FLPR_BIN_PATH}"
            --output     "${FLPR_HDR}"
            --array-name flpr_firmware
    DEPENDS "${FLPR_BIN_PATH}" "${BIN2HDR}"
    COMMENT "Generating FLPR firmware header from zephyr.bin"
    VERBATIM
)

target_sources(app PRIVATE 
    src/main.c
    src/flpr_control.c
    src/scale_logic.c
    src/touch_sensor.c
    src/bluetooth.c
    src/motion_ipc.c
    src/fault_handler.c
    src/display_manager.c
)

target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
target_sources(app PRIVATE "${FLPR_HDR}")
