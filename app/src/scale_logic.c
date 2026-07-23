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
#include <zephyr/settings/settings.h>

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
static double calibration_factor = 10000.0;

static double cal_raw_0g = 0.0;
static double cal_raw_20g = 200000.0;
static double cal_raw_50g = 500000.0;
static double cal_raw_100g = 1000000.0;
static bool piecewise_cal_active = false;

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
	settings_save_one("scale/cal/tare", &tare_offset, sizeof(tare_offset));

	first_sample = true;
	last_str[0] = '\0';
}

static bool wait_for_weight(const char *weight_str, double baseline_raw, double threshold_jump, int polarity)
{
	display_manager_print(weight_str, 0);
	
	int iterations = 0;
	bool weight_detected = false;
	
	// loop up to 30 seconds (120 * 250ms)
	while (iterations < 120) {
		if (!weight_detected) {
			struct sensor_value val;
			sensor_sample_fetch(nau_dev_ptr);
			sensor_channel_get(nau_dev_ptr, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
			
			double current_raw = sensor_value_to_double(&val);
			bool threshold_met = false;
			
			if (polarity == 0) {
				// polarity unknown, use absolute difference
				threshold_met = (fabs(current_raw - baseline_raw) > threshold_jump);
			} else {
				// polarity known, ensure weight is actually increasing past baseline
				threshold_met = ((current_raw - baseline_raw) * polarity > threshold_jump);
			}
			
			if (threshold_met) {
				weight_detected = true;
				motion_ipc_send_stillness_request();
			} else {
				k_msleep(250);
			}
		} else {
			if (motion_ipc_wait_stillness(250) == 0) {
				// We reached stillness. But we must double check if the weight is ACTUALLY still above the threshold!
				// Otherwise, a momentary press from the user's hand while removing the weight would trigger this.
				struct sensor_value val;
				sensor_sample_fetch(nau_dev_ptr);
				sensor_channel_get(nau_dev_ptr, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
				double current_raw = sensor_value_to_double(&val);
				
				bool still_met = false;
				if (polarity == 0) {
					still_met = (fabs(current_raw - baseline_raw) > threshold_jump);
				} else {
					still_met = ((current_raw - baseline_raw) * polarity > threshold_jump);
				}
				
				if (still_met) {
					return true; // Valid weight is still resting on the scale
				} else {
					// False alarm! They probably just pressed down while removing the previous weight.
					weight_detected = false;
				}
			}
		}
		
		iterations++;
		display_manager_register_activity();
		scale_logic_register_activity();
	}
	return false;
}

static double sample_and_flash(const char *weight_str)
{
	double sum = 0;
	struct sensor_value val;

	for (int i = 0; i < 10; i++) {
		if (i % 2 == 0) {
			display_manager_print(weight_str, 0);
		} else {
			display_manager_clear();
		}
		
		sensor_sample_fetch(nau_dev_ptr);
		sensor_channel_get(nau_dev_ptr, (enum sensor_channel)SENSOR_CHAN_FORCE, &val);
		sum += sensor_value_to_double(&val);
		
		k_msleep(100);
		display_manager_register_activity();
		scale_logic_register_activity();
	}
	
	display_manager_print(weight_str, 0);
	k_msleep(250);

	return sum / 10.0;
}

static void scale_calibrate(void)
{
	display_manager_register_activity();
	scale_logic_register_activity();
	display_manager_clear();

	display_manager_print("CAL", 0);
	k_msleep(1000);

	double temp_cal_raw_0g = 0.0;
	double temp_cal_raw_20g = 0.0;
	double temp_cal_raw_50g = 0.0;
	double temp_cal_raw_100g = 0.0;

	// call original stillness routine for taring
	scale_tare();
	temp_cal_raw_0g = tare_offset;

	// require a 3000 raw unit deviation to trigger next step (about 0.3g usually)
	double threshold_jump_initial = 3000.0;

	// polarity is unknown (0) for the first step
	if (!wait_for_weight("20.0", temp_cal_raw_0g, threshold_jump_initial, 0)) {
		goto dnf;
	}
	temp_cal_raw_20g = sample_and_flash("20.0");

	// now we know the load cell polarity AND its exact sensitivity!
	int polarity = (temp_cal_raw_20g > temp_cal_raw_0g) ? 1 : -1;
	double raw_per_g = fabs(temp_cal_raw_20g - temp_cal_raw_0g) / 20.0;
	
	// require the weight to jump by at least 15g to avoid false triggers from bouncing
	double threshold_jump_dynamic = 15.0 * raw_per_g;

	if (!wait_for_weight("50.0", temp_cal_raw_20g, threshold_jump_dynamic, polarity)) {
		goto dnf;
	}
	temp_cal_raw_50g = sample_and_flash("50.0");

	if (!wait_for_weight("100", temp_cal_raw_50g, threshold_jump_dynamic, polarity)) {
		goto dnf;
	}
	temp_cal_raw_100g = sample_and_flash("100");

	// Validation successful, write to global ratio variables
	cal_raw_0g = temp_cal_raw_0g;
	cal_raw_20g = temp_cal_raw_20g;
	cal_raw_50g = temp_cal_raw_50g;
	cal_raw_100g = temp_cal_raw_100g;
	piecewise_cal_active = true;

	settings_save_one("scale/cal/0g", &cal_raw_0g, sizeof(cal_raw_0g));
	settings_save_one("scale/cal/20g", &cal_raw_20g, sizeof(cal_raw_20g));
	settings_save_one("scale/cal/50g", &cal_raw_50g, sizeof(cal_raw_50g));
	settings_save_one("scale/cal/100g", &cal_raw_100g, sizeof(cal_raw_100g));
	settings_save_one("scale/cal/active", &piecewise_cal_active, sizeof(piecewise_cal_active));

	display_manager_print("END", 0);
	k_msleep(1000);

	first_sample = true;
	last_str[0] = '\0';
	return;

dnf:
	display_manager_print("DNF", 0);
	k_msleep(1000);
	first_sample = true;
	last_str[0] = '\0';
}

static double calculate_absolute_weight(double raw)
{
	if (!piecewise_cal_active) {
		// fallback to standard linear calibration if multi-step wasn't completed
		return raw / calibration_factor;
	}

	int polarity = (cal_raw_20g > cal_raw_0g) ? 1 : -1;
	double diff = (raw - cal_raw_0g) * polarity;
	double diff_20g = (cal_raw_20g - cal_raw_0g) * polarity;
	double diff_50g = (cal_raw_50g - cal_raw_0g) * polarity;

	if (diff <= diff_20g) {
		// 0 to 20g range (or negative)
		double slope = 20.0 / (cal_raw_20g - cal_raw_0g);
		return (raw - cal_raw_0g) * slope;
	} else if (diff <= diff_50g) {
		// 20 to 50g range
		double slope = (50.0 - 20.0) / (cal_raw_50g - cal_raw_20g);
		return 20.0 + (raw - cal_raw_20g) * slope;
	} else {
		// 50 to 100g range (extrapolating beyond 100g)
		double slope = (100.0 - 50.0) / (cal_raw_100g - cal_raw_50g);
		return 50.0 + (raw - cal_raw_50g) * slope;
	}
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

	double raw = sensor_value_to_double(&val);
	double abs_weight = calculate_absolute_weight(raw);
	double tare_weight = calculate_absolute_weight(tare_offset);
	double net_weight = abs_weight - tare_weight;

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

static int scale_cal_settings_set(const char *name, size_t len,
				  settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	int rc;

	if (settings_name_steq(name, "0g", &next) && !next) {
		if (len != sizeof(cal_raw_0g)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cal_raw_0g, sizeof(cal_raw_0g));
		return rc >= 0 ? 0 : rc;
	}

	if (settings_name_steq(name, "20g", &next) && !next) {
		if (len != sizeof(cal_raw_20g)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cal_raw_20g, sizeof(cal_raw_20g));
		return rc >= 0 ? 0 : rc;
	}

	if (settings_name_steq(name, "50g", &next) && !next) {
		if (len != sizeof(cal_raw_50g)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cal_raw_50g, sizeof(cal_raw_50g));
		return rc >= 0 ? 0 : rc;
	}

	if (settings_name_steq(name, "100g", &next) && !next) {
		if (len != sizeof(cal_raw_100g)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cal_raw_100g, sizeof(cal_raw_100g));
		return rc >= 0 ? 0 : rc;
	}

	if (settings_name_steq(name, "active", &next) && !next) {
		if (len != sizeof(piecewise_cal_active)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &piecewise_cal_active, sizeof(piecewise_cal_active));
		return rc >= 0 ? 0 : rc;
	}

	if (settings_name_steq(name, "tare", &next) && !next) {
		if (len != sizeof(tare_offset)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &tare_offset, sizeof(tare_offset));
		return rc >= 0 ? 0 : rc;
	}

	return -ENOENT;
}

static int scale_cal_settings_export(int (*cb)(const char *name,
					       const void *value, size_t val_len))
{
	cb("scale/cal/0g", &cal_raw_0g, sizeof(cal_raw_0g));
	cb("scale/cal/20g", &cal_raw_20g, sizeof(cal_raw_20g));
	cb("scale/cal/50g", &cal_raw_50g, sizeof(cal_raw_50g));
	cb("scale/cal/100g", &cal_raw_100g, sizeof(cal_raw_100g));
	cb("scale/cal/active", &piecewise_cal_active, sizeof(piecewise_cal_active));
	cb("scale/cal/tare", &tare_offset, sizeof(tare_offset));
	return 0;
}

struct settings_handler scale_cal_conf = {
	.name = "scale/cal",
	.h_set = scale_cal_settings_set,
	.h_export = scale_cal_settings_export,
};

static int scale_cal_settings_init(void)
{
	int err = settings_register(&scale_cal_conf);
	if (err) {
		LOG_ERR("settings_register failed (err %d)", err);
	}
	return err;
}
SYS_INIT(scale_cal_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

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

ZBUS_CHAN_DEFINE(calibrate_request_chan,
		 struct calibrate_request_msg,
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
		} else if (chan == &calibrate_request_chan) {
			LOG_INF("Calibration requested via zbus");

			struct sensor_trigger trig = {
				.type = SENSOR_TRIG_DATA_READY,
				.chan = (enum sensor_channel)SENSOR_CHAN_FORCE,
			};

			sensor_trigger_set(nau_dev_ptr, &trig, NULL);
			scale_calibrate();
			sensor_trigger_set(nau_dev_ptr, &trig, nau7802_drdy_handler);
		}
	}
}

K_THREAD_DEFINE(scale_tare_thread_id, 1024, scale_tare_thread, NULL, NULL, NULL, 7, 0, 0);
