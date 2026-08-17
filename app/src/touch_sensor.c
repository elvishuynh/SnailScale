#include "touch_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(touch_sensor, CONFIG_LOG_DEFAULT_LEVEL);

#include "events.h"
#include "display_manager.h"

// debounce window
#define DEBOUNCE_DELAY_MS 30

// startup blanking window
#define BOOT_BLANKING_MS 1500

// tap duration limits
#define MIN_TAP_DURATION_MS 50
#define MAX_TAP_DURATION_MS 600

// long press hold duration
#define LONG_PRESS_HOLD_MS 3000

static const struct gpio_dt_spec touch_pad = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback touch_cb_data;
static struct k_work_delayable long_press_work;
static struct k_work_delayable debounce_work;

static bool is_touched = false;
static int64_t press_start_time = 0;
static bool calibrate_fired = false;
static int64_t lockout_until = 0;

void touch_sensor_lockout(uint32_t duration_ms)
{
	lockout_until = k_uptime_get() + duration_ms;
}

static void long_press_work_handler(struct k_work *work)
{
	LOG_INF("Touch pad held for 3s. Firing calibrate event.");
	calibrate_fired = true;
	struct calibrate_request_msg msg;
	zbus_chan_pub(&calibrate_request_chan, &msg, K_NO_WAIT);
}

static void debounce_work_handler(struct k_work *work)
{
	int val = gpio_pin_get_dt(&touch_pad);
	int64_t now = k_uptime_get();

	// ignore noise during power up
	if (now < BOOT_BLANKING_MS) {
		return;
	}

	// ignore events when locked
	if (now < lockout_until) {
		is_touched = (val > 0);
		return;
	}

	if (val > 0 && !is_touched) {
		is_touched = true;
		press_start_time = now;
		calibrate_fired = false;
		LOG_INF("touch detected (debounced)");
		// schedule long press timer
		k_work_schedule(&long_press_work, K_MSEC(LONG_PRESS_HOLD_MS));
	} else if (val == 0 && is_touched) {
		is_touched = false;
		LOG_INF("touch released (debounced)");
		// cancel long press timer
		k_work_cancel_delayable(&long_press_work);

		int64_t press_duration = now - press_start_time;

		// validate single tap duration
		if (!calibrate_fired && press_duration >= MIN_TAP_DURATION_MS && press_duration <= MAX_TAP_DURATION_MS) {
			display_manager_register_activity();
			LOG_INF("Tap detected. Firing tare event.");
			struct tare_request_msg msg;
			zbus_chan_pub(&tare_request_chan, &msg, K_NO_WAIT);
		}
	}
}

static void touch_pad_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	// schedule debounce work
	k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_DELAY_MS));
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

	// configure gpio input
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

	// configure interrupt on both edges
	ret = gpio_pin_interrupt_configure_dt(&touch_pad, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure touch interrupt (%d)", ret);
		return ret;
	}

	LOG_INF("AT42QT1010 Touch pad initialized on D5");
	return 0;
}
