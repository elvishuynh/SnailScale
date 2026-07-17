# SnailScale/app/sysbuild.cmake

# register FLPR as a sysbuild image
ExternalZephyrProject_Add(
    APPLICATION app_flpr
    SOURCE_DIR ${APP_DIR}/../app_flpr
    BOARD xiao_nrf54l15/nrf54l15/cpuflpr
)

# compile app_flpr before main app
add_dependencies(app app_flpr)
sysbuild_add_dependencies(FLASH app app_flpr)