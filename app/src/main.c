#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/settings/settings.h>

#include "scale_logic.h"
#include "touch_sensor.h"
#include "bluetooth.h"
#include "motion_ipc.h"
#include "fault_handler.h"
#include "display_manager.h"

#include <zephyr/dfu/mcuboot.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);
int main(void)
{
	LOG_INF("PT18 matrix scroll demo started");

	if (settings_subsys_init() != 0) {
		system_fault_handler("Settings subsys init failed");
	}

	if (display_manager_init() != 0) {
		system_fault_handler("Display manager init failed");
	}


	if (bluetooth_init()) {
		// reboot if bluetooth fails so mcuboot can revert to working firmware
		system_fault_handler("Bluetooth init failed");
	}

	if (touch_sensor_init() != 0) {
		system_fault_handler("Touch sensor init failed");
	}

	// ipc must be up before scale logic starts
	// flpr is already booted
	if (motion_ipc_init() != 0) {
		system_fault_handler("Motion IPC init failed");
	}


	if (scale_logic_init() != 0) {
		system_fault_handler("Scale logic init failed");
	}

	settings_load();

	LOG_INF("NAU7802 DRDY trigger active");

	// confirm image to prevent rollback
	boot_write_img_confirmed();

	k_sleep(K_FOREVER);

	return 0;
}
