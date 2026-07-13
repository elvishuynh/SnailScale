#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <string.h>

#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>
#include <sensor/nau7802_loadcell/nau7802_loadcell.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include <stdio.h>

static double tare_offset;
static const struct device *tm_dev;

static int tare_scale(const struct device *nau_dev, const struct device *display_dev)
{
	pt18_matrix_print(display_dev, "---", 0);

	double sum = 0;
	struct sensor_value val;

	for (int i = 0; i < 10; i++) {
		sensor_sample_fetch(nau_dev);
		sensor_channel_get(nau_dev, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
		sum += sensor_value_to_double(&val);
		k_msleep(100);
	}

	tare_offset = sum / 10.0;
	return 0;
}

// drdy fires from the global workqueue when nau7802 has a new sample
static void nau7802_drdy_handler(const struct device *dev,
				 const struct sensor_trigger *trig)
{
	struct sensor_value val;
	if (sensor_sample_fetch(dev) ||
	    sensor_channel_get(dev, (enum sensor_channel)SENSOR_CHAN_FORCE, &val)) {
		return;
	}

	double net_weight = sensor_value_to_double(&val) - tare_offset;
	char str[16];

	if (net_weight < -9.9) {
		snprintf(str, sizeof(str), "%.0f", net_weight);
	} else {
		snprintf(str, sizeof(str), "%.1f", net_weight);
	}

	pt18_matrix_clear(tm_dev);
	pt18_matrix_print(tm_dev, str, 0);
}

int main(void)
{
	// tm1640 led matrix setup
	tm_dev = DEVICE_DT_GET(DT_NODELABEL(tm1640));

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

	while (sensor_sample_fetch(nau_dev) == -EBUSY) {
		k_msleep(50);
	}

	tare_scale(nau_dev, tm_dev);

	// register DRDY trigger now that the sensor is fully online
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = (enum sensor_channel)SENSOR_CHAN_FORCE,
	};

	if (sensor_trigger_set(nau_dev, &trig, nau7802_drdy_handler)) {
		LOG_ERR("Failed to set NAU7802 trigger");
		return -1;
	}

	LOG_INF("NAU7802 DRDY trigger active");

	k_sleep(K_FOREVER);

	return 0;
}
