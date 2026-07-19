#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <hal/nrf_vpr.h>
#include <hal/nrf_spu.h>
#include <ram_pwrdn.h>
#include "flpr_firmware.h"

#include <pt18_matrix/pt18_matrix.h>
#include "scale_logic.h"
#include "touch_sensor.h"
#include "bluetooth.h"
#include "heartbeat.h"


LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);


#define FLPR_SRAM_GLOBAL_ADDR (DT_REG_ADDR(DT_NODELABEL(cpuflpr_sram_code_data)))

// power up, scrub, runs at priority 0
// prepares the silicon before the main core IPC driver wakes up at priority 50
static int flpr_early_ram_init(void) {
    // power up and scrub sram_tx
    power_up_ram(0x20018000, 0x20018800);
    memset((void *)0x20018000, 0, 0x0800);
    
    // power up and scrub sram_rx
    power_up_ram(0x20020000, 0x20020800);
    memset((void *)0x20020000, 0, 0x0800);
    
    // power up FLPR SRAM
    power_up_ram(FLPR_SRAM_GLOBAL_ADDR, FLPR_SRAM_GLOBAL_ADDR + FLPR_FIRMWARE_SIZE);
    
    return 0;
}
SYS_INIT(flpr_early_ram_init, PRE_KERNEL_1, 0);

// boot the coprocessor, priority 48
// starts FLPR after the main core Log Link is active
static int flpr_boot(void) {
    memcpy((void *)FLPR_SRAM_GLOBAL_ADDR, flpr_firmware, FLPR_FIRMWARE_SIZE);
    
    nrf_spu_periph_perm_secattr_set(NRF_SPU00, nrf_address_slave_get((uint32_t)NRF_VPR00), true);
    nrf_vpr_initpc_set(NRF_VPR00, FLPR_SRAM_GLOBAL_ADDR);
    nrf_vpr_cpurun_set(NRF_VPR00, true);
    
    return 0;
}
SYS_INIT(flpr_boot, POST_KERNEL, 48);

int main(void)
{
	// tm1640 led matrix setup
	const struct device *tm_dev = DEVICE_DT_GET(DT_NODELABEL(tm1640));

	if (!device_is_ready(tm_dev)) {
		LOG_ERR("TM1640 device not ready");
		return -1;
	}

	LOG_INF("PT18 matrix scroll demo started");

	pt18_matrix_init(tm_dev);
	pt18_matrix_set_brightness(tm_dev, 1);

	// nau7802 load cell setup
	const struct device *nau_dev = DEVICE_DT_GET(DT_NODELABEL(nau7802));

	if (!device_is_ready(nau_dev)) {
		LOG_ERR("NAU7802 device not ready");
		return -1;
	}

	if (bluetooth_init()) {
		LOG_ERR("BLE OTA DFU unavailable");
		// non fatal scale still functions without ota
	}

	if (touch_sensor_init(scale_tare) != 0) {
		LOG_ERR("Failed to initialize touch sensor");
	}

	if (scale_logic_init(nau_dev, tm_dev) != 0) {
		LOG_ERR("Failed to initialize scale logic subsystem");
		return -1;
	}

	if (heartbeat_init() != 0) {
		LOG_ERR("Failed to initialize heartbeat IPC");
	}

	LOG_INF("NAU7802 DRDY trigger active");

	k_sleep(K_FOREVER);

	return 0;
}
