#include "touch_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(touch_sensor, CONFIG_LOG_DEFAULT_LEVEL);

static const struct gpio_dt_spec touch_pad = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback touch_cb_data;
static struct k_work_delayable long_press_work;
static struct k_work_delayable debounce_work;
static void (*long_press_cb)(void) = NULL;

static void long_press_work_handler(struct k_work *work)
{
	LOG_INF("Touch pad held for 2s. Firing callback.");
	if (long_press_cb) {
		long_press_cb();
	}
}

static void debounce_work_handler(struct k_work *work)
{
	int val = gpio_pin_get_dt(&touch_pad);
	if (val > 0) {
		LOG_INF("touch detected (debounced)");
		/* touch detected */
		k_work_schedule(&long_press_work, K_SECONDS(2));
	} else if (val == 0) {
		LOG_INF("touch released (debounced)");
		/* touch released cancel callback */
		k_work_cancel_delayable(&long_press_work);
	}
}

static void touch_pad_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* delay for debounce */
	k_work_reschedule(&debounce_work, K_MSEC(5));
}

int touch_sensor_init(void (*on_long_press)(void))
{
	int ret;

	if (!gpio_is_ready_dt(&touch_pad)) {
		LOG_ERR("Touch pad GPIO not ready");
		return -1;
	}

	long_press_cb = on_long_press;

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
