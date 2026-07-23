#include "touch_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(touch_sensor, CONFIG_LOG_DEFAULT_LEVEL);

#include "events.h"
#include "display_manager.h"

static const struct gpio_dt_spec touch_pad = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback touch_cb_data;
static struct k_work_delayable long_press_work;
static struct k_work_delayable debounce_work;

static int tap_count = 0;
static int64_t last_tap_time = 0;
static bool calibrate_fired = false;
static bool is_touched = false;
#define TAP_WINDOW_MS 800

static void long_press_work_handler(struct k_work *work)
{
	LOG_INF("Touch pad held for 3s. Firing calibrate event.");
	calibrate_fired = true;
	struct calibrate_request_msg msg;
	zbus_chan_pub(&calibrate_request_chan, &msg, K_NO_WAIT);
	tap_count = 0;
}

static void debounce_work_handler(struct k_work *work)
{
	display_manager_register_activity();

	int val = gpio_pin_get_dt(&touch_pad);
	int64_t now = k_uptime_get();

	if (val > 0 && !is_touched) {
		is_touched = true;
		LOG_INF("touch detected (debounced)");
		/* touch detected */
		if (tap_count == 1 && (now - last_tap_time) < TAP_WINDOW_MS) {
			tap_count = 2;
			calibrate_fired = false;
			k_work_schedule(&long_press_work, K_SECONDS(3));
		} else {
			tap_count = 1;
			last_tap_time = now;
			k_work_cancel_delayable(&long_press_work);
		}
	} else if (val == 0 && is_touched) {
		is_touched = false;
		LOG_INF("touch released (debounced)");
		/* touch released */
		if (tap_count == 2) {
			k_work_cancel_delayable(&long_press_work);
			if (!calibrate_fired) {
				LOG_INF("Double tap detected. Firing tare event.");
				struct tare_request_msg msg;
				zbus_chan_pub(&tare_request_chan, &msg, K_NO_WAIT);
			}
			tap_count = 0;
		}
	}
}

static void touch_pad_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* delay for debounce */
	k_work_reschedule(&debounce_work, K_MSEC(5));
}

int touch_sensor_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&touch_pad)) {
		LOG_ERR("Touch pad GPIO not ready");
		return -1;
	}

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&long_press_work, long_press_work_handler);

	ret = gpio_pin_configure_dt(&touch_pad, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure touch pin (%d)", ret);
		return ret;
	}

	gpio_init_callback(&touch_cb_data, touch_pad_isr, BIT(touch_pad.pin));
	ret = gpio_add_callback(touch_pad.port, &touch_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add touch callback (%d)", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&touch_pad, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure touch interrupt (%d)", ret);
		return ret;
	}

	LOG_INF("AT42QT1010 Touch pad initialized on D7");
	return 0;
}
