#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <hal/nrf_vpr.h>
#include <ram_pwrdn.h>
#include "flpr_firmware.h"

#include <pt18_matrix/pt18_matrix.h>
#include "scale_logic.h"
#include "touch_sensor.h"
#include "bluetooth.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define FLPR_SRAM_GLOBAL_ADDR (DT_REG_ADDR(DT_NODELABEL(cpuflpr_sram_code_data)))

static void flpr_start(void) {
	// power up ram
	power_up_ram(FLPR_SRAM_GLOBAL_ADDR, FLPR_SRAM_GLOBAL_ADDR + FLPR_FIRMWARE_SIZE);
	
	// copy firmware
	memcpy((void *)FLPR_SRAM_GLOBAL_ADDR, flpr_firmware, FLPR_FIRMWARE_SIZE);
	
	// set pc
	nrf_vpr_initpc_set(NRF_VPR00, FLPR_SRAM_GLOBAL_ADDR);
	
	// start core
	nrf_vpr_cpurun_set(NRF_VPR00, true);
}

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

	flpr_start();

	LOG_INF("NAU7802 DRDY trigger active");

	k_sleep(K_FOREVER);

	return 0;
}
