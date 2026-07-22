#include "scale_logic.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>
#include <sensor/nau7802_loadcell/nau7802_loadcell.h>

#include "motion_ipc.h"
#include "symbols.h"

LOG_MODULE_REGISTER(scale_logic, CONFIG_LOG_DEFAULT_LEVEL);

static double tare_offset;
static const struct device *tm_dev;
static const struct device *nau_dev_ptr;

void scale_tare(void)
{
	pt18_matrix_clear(tm_dev);

	motion_ipc_send_stillness_request();

	int iterations = 0;
	
	// loop up to 30 seconds (120 * 250ms)
	while (iterations < 120) {
		int frame = iterations % 4;
		if (frame == 0) {
			pt18_matrix_write(tm_dev, sym_movement_a, sizeof(sym_movement_a));
		} else if (frame == 1) {
			pt18_matrix_write(tm_dev, sym_movement_b, sizeof(sym_movement_b));
		} else if (frame == 2) {
			pt18_matrix_write(tm_dev, sym_movement_c, sizeof(sym_movement_c));
		} else {
			pt18_matrix_write(tm_dev, sym_movement_d, sizeof(sym_movement_d));
		}
		
		if (motion_ipc_wait_stillness(250) == 0) {
			break;
		}
		
		iterations++;
	}

	if (iterations >= 120) {
		LOG_WRN("Stillness timeout after 30s, taring anyway");
	}

	pt18_matrix_print(tm_dev, "---", 0);

	double sum = 0;
	struct sensor_value val;

	for (int i = 0; i < 10; i++) {
		sensor_sample_fetch(nau_dev_ptr);
		sensor_channel_get(nau_dev_ptr, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
		sum += sensor_value_to_double(&val);
		k_msleep(100);
	}

	tare_offset = sum / 10.0;
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

	/* apply a temporary calibration factor so the raw ADC counts 
	 * fit on our 3 digit and 17 column display */
	double net_weight = (sensor_value_to_double(&val) - tare_offset) / 10000.0;
	char str[16];

	// hardware decimal requires exactly four chars padded right
	// otherwise the hardwired led column logic misses it and throws into the void
	if (net_weight < -9.9) {
		snprintf(str, sizeof(str), "%4.0f", net_weight);
	} else {
		snprintf(str, sizeof(str), "%4.1f", net_weight);
	}

	pt18_matrix_clear(tm_dev);
	pt18_matrix_print(tm_dev, str, 0);
}

int scale_logic_init(const struct device *nau_dev, const struct device *display_dev)
{
	tm_dev = display_dev;
	nau_dev_ptr = nau_dev;

	while (sensor_sample_fetch(nau_dev_ptr) == -EBUSY) {
		k_msleep(50);
	}

	scale_tare();

	// register DRDY trigger now that the sensor is fully online
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = (enum sensor_channel)SENSOR_CHAN_FORCE,
	};

	if (sensor_trigger_set(nau_dev_ptr, &trig, nau7802_drdy_handler)) {
		LOG_ERR("Failed to set NAU7802 trigger");
		return -1;
	}

	return 0;
}
