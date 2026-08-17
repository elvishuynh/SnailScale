#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/cache.h>
#include <string.h>

#include <hal/nrf_vpr.h>
#include <hal/nrf_spu.h>
#include <ram_pwrdn.h>
#include "flpr_firmware.h"

#define FLPR_SRAM_GLOBAL_ADDR (DT_REG_ADDR(DT_NODELABEL(cpuflpr_sram_code_data)))
#define FLPR_SRAM_GLOBAL_SIZE (DT_REG_SIZE(DT_NODELABEL(cpuflpr_sram_code_data)))
#define IPC_TX_ADDR DT_REG_ADDR(DT_NODELABEL(sram_tx))
#define IPC_TX_SIZE DT_REG_SIZE(DT_NODELABEL(sram_tx))
#define IPC_RX_ADDR DT_REG_ADDR(DT_NODELABEL(sram_rx))
#define IPC_RX_SIZE DT_REG_SIZE(DT_NODELABEL(sram_rx))

static int flpr_early_ram_init(void) {
    power_up_ram(IPC_TX_ADDR, IPC_TX_ADDR + IPC_TX_SIZE);
    memset((void *)IPC_TX_ADDR, 0, IPC_TX_SIZE);

    power_up_ram(IPC_RX_ADDR, IPC_RX_ADDR + IPC_RX_SIZE);
    memset((void *)IPC_RX_ADDR, 0, IPC_RX_SIZE);

    // flush ipc shared ram so flpr sees zeroed buffers
    sys_cache_data_flush_range((void *)IPC_TX_ADDR, IPC_TX_SIZE);
    sys_cache_data_flush_range((void *)IPC_RX_ADDR, IPC_RX_SIZE);

    power_up_ram(FLPR_SRAM_GLOBAL_ADDR, FLPR_SRAM_GLOBAL_ADDR + FLPR_SRAM_GLOBAL_SIZE);

    return 0;
}
SYS_INIT(flpr_early_ram_init, PRE_KERNEL_1, 0);

int flpr_boot(void) {
    memcpy((void *)FLPR_SRAM_GLOBAL_ADDR, flpr_firmware, FLPR_FIRMWARE_SIZE);
    
    // flush firmware payload to sram without config dcache guard
    sys_cache_data_flush_range((void *)FLPR_SRAM_GLOBAL_ADDR, FLPR_FIRMWARE_SIZE);

    nrf_spu_periph_perm_secattr_set(NRF_SPU00, nrf_address_slave_get((uint32_t)NRF_VPR00), true);
    nrf_spu_periph_perm_dmasec_set(NRF_SPU00, nrf_address_slave_get((uint32_t)NRF_VPR00), true);

    nrf_vpr_initpc_set(NRF_VPR00, FLPR_SRAM_GLOBAL_ADDR);
    nrf_vpr_cpurun_set(NRF_VPR00, true);
    
    return 0;
}
