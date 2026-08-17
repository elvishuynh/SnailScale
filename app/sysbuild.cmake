# SnailScale/app/sysbuild.cmake

# select flpr board based on main board
if("${BOARD}" MATCHES "xiao_nrf54lm20a")
    set(FLPR_BOARD "xiao_nrf54lm20a/nrf54lm20a/cpuflpr")
elseif("${BOARD}" MATCHES "xiao_nrf54lm20b")
    set(FLPR_BOARD "xiao_nrf54lm20b/nrf54lm20b/cpuflpr")
elseif("${BOARD}" MATCHES "xiao_nrf54l15")
    set(FLPR_BOARD "xiao_nrf54l15/nrf54l15/cpuflpr")
else()
    message(FATAL_ERROR "Unsupported board for flpr ${BOARD}")
endif()

# register FLPR as a sysbuild image
ExternalZephyrProject_Add(
    APPLICATION app_flpr
    SOURCE_DIR ${APP_DIR}/../app_flpr
    BOARD ${FLPR_BOARD}
)

# compile app_flpr before main app
add_dependencies(${DEFAULT_IMAGE} app_flpr)