#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <string.h>

#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>
#include <sensor/nau7802_loadcell/nau7802_loadcell.h>

LOG_MODULE_REGISTER(snailscale, LOG_LEVEL_DBG);

#define SCROLL_DELAY_MS 80
#define SCROLL_MSG      "Hello World"

// total pixel width is strlen times stride so start offscreen right end offscreen left
#define SCROLL_START    PT18_MATRIX_COLUMNS
#define SCROLL_END      (-(int)(sizeof(SCROLL_MSG) - 1) * 6)

// drdy fires from the global workqueue when nau7802 has a new sample
static void nau7802_drdy_handler(const struct device *dev,
				 const struct sensor_trigger *trig)
{
	struct sensor_value val;

	if (sensor_sample_fetch(dev)) {
		LOG_ERR("nau7802 sample fetch failed");
		return;
	}

	if (sensor_channel_get(dev, (enum sensor_channel)SENSOR_CHAN_FORCE, &val)) {
		LOG_ERR("nau7802 channel get failed");
		return;
	}

	LOG_INF("force: %d.%06d", val.val1, val.val2);
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

	LOG_INF("NAU7802 load cell ready");

	// set up data ready trigger so readings fire async on the workqueue
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = (enum sensor_channel)SENSOR_CHAN_FORCE,
	};

	if (sensor_trigger_set(nau_dev, &trig, nau7802_drdy_handler)) {
		LOG_ERR("failed to set nau7802 trigger");
		return -1;
	}

	LOG_INF("NAU7802 DRDY trigger active");

	// led scroll runs on main thread while nau7802 reads happen in background
	while (1) {
		for (int off = SCROLL_START; off >= SCROLL_END; off--) {
			pt18_matrix_print(tm_dev, SCROLL_MSG, off);
			k_msleep(SCROLL_DELAY_MS);
		}
	}

	return 0;
}
