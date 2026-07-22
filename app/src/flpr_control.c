#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <string.h>

#include <hal/nrf_vpr.h>
#include <hal/nrf_spu.h>
#include <ram_pwrdn.h>
#include "flpr_firmware.h"

#define FLPR_SRAM_GLOBAL_ADDR (DT_REG_ADDR(DT_NODELABEL(cpuflpr_sram_code_data)))

static int flpr_early_ram_init(void) {
    power_up_ram(0x20018000, 0x20018800);
    memset((void *)0x20018000, 0, 0x0800);
    
    power_up_ram(0x20020000, 0x20020800);
    memset((void *)0x20020000, 0, 0x0800);
    
    power_up_ram(FLPR_SRAM_GLOBAL_ADDR, FLPR_SRAM_GLOBAL_ADDR + FLPR_FIRMWARE_SIZE);
    
    return 0;
}
SYS_INIT(flpr_early_ram_init, PRE_KERNEL_1, 0);

static int flpr_boot(void) {
    memcpy((void *)FLPR_SRAM_GLOBAL_ADDR, flpr_firmware, FLPR_FIRMWARE_SIZE);
    
    nrf_spu_periph_perm_secattr_set(NRF_SPU00, nrf_address_slave_get((uint32_t)NRF_VPR00), true);
    nrf_vpr_initpc_set(NRF_VPR00, FLPR_SRAM_GLOBAL_ADDR);
    nrf_vpr_cpurun_set(NRF_VPR00, true);
    
    return 0;
}
SYS_INIT(flpr_boot, POST_KERNEL, 48);
