#include "scale_logic.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/devicetree.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <math.h>
#include <math.h>
#include <zephyr/pm/device.h>

#include "events.h"

#include "display_manager.h"
#include <sensor/nau7802_loadcell/nau7802_loadcell.h>

#include "motion_ipc.h"
#include "symbols.h"

#define SCALE_FILTER_SETTING 2

static double get_ema_alpha(int setting) {
	switch (setting) {
		case 2: return 0.500;
		case 4: return 0.250;
		case 6: return 0.167;
		case 8: return 0.125;
		case 16: return 0.0625;
		case 32: return 0.031;
		case 64: return 0.015;
		case 128: return 0.0078;
		default: return 0.0625;
	}
}

LOG_MODULE_REGISTER(scale_logic, CONFIG_LOG_DEFAULT_LEVEL);

static double tare_offset;
static const struct device *nau_dev_ptr;

static double last_displayed_weight = 0.0;
static double ema_weight = 0.0;
static bool first_sample = true;
static char last_str[16] = {0};

static struct k_work_delayable sleep_work;

static void sleep_work_handler(struct k_work *work)
{
	LOG_INF("Inactivity timeout, entering sleep mode");
	motion_ipc_send_sleep_request();
	display_manager_power_off();
#ifdef CONFIG_PM_DEVICE
	pm_device_action_run(nau_dev_ptr, PM_DEVICE_ACTION_SUSPEND);
#endif
}

void scale_logic_register_activity(void)
{
	k_work_reschedule(&sleep_work, K_SECONDS(15));
}

static void scale_tare(void)
{
	display_manager_register_activity();
	scale_logic_register_activity();
	display_manager_clear();

	motion_ipc_send_stillness_request();

	int iterations = 0;
	
	// loop up to 30 seconds (120 * 250ms)
	while (iterations < 120) {
		int frame = iterations % 4;
		if (frame == 0) {
			display_manager_write(sym_movement_a, sizeof(sym_movement_a));
		} else if (frame == 1) {
			display_manager_write(sym_movement_b, sizeof(sym_movement_b));
		} else if (frame == 2) {
			display_manager_write(sym_movement_c, sizeof(sym_movement_c));
		} else {
			display_manager_write(sym_movement_d, sizeof(sym_movement_d));
		}
		
		if (motion_ipc_wait_stillness(250) == 0) {
			break;
		}
		
		iterations++;
	}

	if (iterations >= 120) {
		LOG_WRN("Stillness timeout after 30s, taring anyway");
	}

	display_manager_print("---", 0);

	double sum = 0;
	struct sensor_value val;

	for (int i = 0; i < 10; i++) {
		sensor_sample_fetch(nau_dev_ptr);
		sensor_channel_get(nau_dev_ptr, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
		sum += sensor_value_to_double(&val);
		k_msleep(100);
	}

	tare_offset = sum / 10.0;

	first_sample = true;
	last_str[0] = '\0';
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

	if (first_sample) {
		ema_weight = net_weight;
		first_sample = false;
	} else {
		double alpha = get_ema_alpha(SCALE_FILTER_SETTING);
		ema_weight = alpha * net_weight + (1.0 - alpha) * ema_weight;
		
		if (fabs(ema_weight - last_displayed_weight) < 0.1) {
			return;
		}
	}

	last_displayed_weight = ema_weight;

	char str[16];

	// hardware decimal requires exactly four chars padded right
	// otherwise the hardwired led column logic misses it and throws into the void
	if (ema_weight < -9.9) {
		snprintf(str, sizeof(str), "%4.0f", ema_weight);
	} else {
		snprintf(str, sizeof(str), "%4.1f", ema_weight);
	}

	if (strcmp(str, last_str) == 0) {
		return;
	}
	strcpy(last_str, str);

	display_manager_register_activity();
	scale_logic_register_activity();
	display_manager_print(str, 0);
}

int scale_logic_init(void)
{
	nau_dev_ptr = DEVICE_DT_GET(DT_NODELABEL(nau7802));
	if (!device_is_ready(nau_dev_ptr)) {
		LOG_ERR("NAU7802 device not ready");
		return -ENODEV;
	}

	while (sensor_sample_fetch(nau_dev_ptr) == -EBUSY) {
		k_msleep(50);
	}

	k_work_init_delayable(&sleep_work, sleep_work_handler);
	scale_logic_register_activity();

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

ZBUS_SUBSCRIBER_DEFINE(scale_tare_sub, 4);

ZBUS_CHAN_DEFINE(tare_request_chan,
		 struct tare_request_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(scale_tare_sub),
		 ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(wake_request_chan,
		 struct wake_request_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(scale_tare_sub),
		 ZBUS_MSG_INIT(0)
);

static void scale_tare_thread(void)
{
	const struct zbus_channel *chan;

	while (!zbus_sub_wait(&scale_tare_sub, &chan, K_FOREVER)) {
		if (chan == &tare_request_chan) {
			LOG_INF("Tare requested via zbus");

			struct sensor_trigger trig = {
				.type = SENSOR_TRIG_DATA_READY,
				.chan = (enum sensor_channel)SENSOR_CHAN_FORCE,
			};

			sensor_trigger_set(nau_dev_ptr, &trig, NULL);
			scale_tare();
			sensor_trigger_set(nau_dev_ptr, &trig, nau7802_drdy_handler);
		} else if (chan == &wake_request_chan) {
			LOG_INF("Wake requested via zbus");
			display_manager_power_on();
			display_manager_register_activity();
			scale_logic_register_activity();
#ifdef CONFIG_PM_DEVICE
			pm_device_action_run(nau_dev_ptr, PM_DEVICE_ACTION_RESUME);
#endif
		}
	}
}

K_THREAD_DEFINE(scale_tare_thread_id, 1024, scale_tare_thread, NULL, NULL, NULL, 7, 0, 0);
