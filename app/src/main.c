#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <pt18_matrix/pt18_matrix.h>
#include "scale_logic.h"
#include "touch_sensor.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

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

	if (touch_sensor_init(scale_tare) != 0) {
		LOG_ERR("Failed to initialize touch sensor");
	}

	if (scale_logic_init(nau_dev, tm_dev) != 0) {
		LOG_ERR("Failed to initialize scale logic subsystem");
		return -1;
	}

	LOG_INF("NAU7802 DRDY trigger active");

	k_sleep(K_FOREVER);

	return 0;
}
