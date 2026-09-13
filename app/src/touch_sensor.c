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

// iqs231b registers and commands
#define IQS231B_I2C_ADDR 0x44
#define IQS231B_REG_COMMANDS 0x04
#define IQS231B_CMD_STANDALONE 0x01

static const struct gpio_dt_spec touch_pad = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec touch_sda = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), touchsda_gpios);
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

// bit bang i2c primitives
static void bb_scl_set(int high)
{
	if (high) {
		gpio_pin_configure(touch_pad.port, touch_pad.pin, GPIO_INPUT | GPIO_PULL_UP);
		// wait for slave to release clock
		int timeout_us = 2000;
		while (gpio_pin_get(touch_pad.port, touch_pad.pin) == 0 && timeout_us > 0) {
			k_busy_wait(10);
			timeout_us -= 10;
		}
	} else {
		gpio_pin_configure(touch_pad.port, touch_pad.pin, GPIO_OUTPUT_LOW);
		gpio_pin_set(touch_pad.port, touch_pad.pin, 0);
	}
	k_busy_wait(20);
}

static void bb_sda_set(int high)
{
	if (high) {
		gpio_pin_configure(touch_sda.port, touch_sda.pin, GPIO_INPUT | GPIO_PULL_UP);
	} else {
		gpio_pin_configure(touch_sda.port, touch_sda.pin, GPIO_OUTPUT_LOW);
		gpio_pin_set(touch_sda.port, touch_sda.pin, 0);
	}
	k_busy_wait(20);
}

static int bb_sda_get(void)
{
	return gpio_pin_get(touch_sda.port, touch_sda.pin);
}

static void bb_start(void)
{
	bb_sda_set(1);
	bb_scl_set(1);
	bb_sda_set(0);
	bb_scl_set(0);
}

static void bb_stop(void)
{
	bb_sda_set(0);
	bb_scl_set(1);
	bb_sda_set(1);
}

static bool bb_write_byte(uint8_t byte)
{
	for (int i = 7; i >= 0; i--) {
		bb_sda_set((byte >> i) & 1);
		bb_scl_set(1);
		bb_scl_set(0);
	}
	bb_sda_set(1);
	bb_scl_set(1);
	int ack = (bb_sda_get() == 0);
	bb_scl_set(0);
	return ack;
}

static void iqs231b_configure_standalone(void)
{
	// check initial bus line levels
	gpio_pin_configure(touch_pad.port, touch_pad.pin, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_configure(touch_sda.port, touch_sda.pin, GPIO_INPUT | GPIO_PULL_UP);
	k_busy_wait(200);
	int scl_raw = gpio_pin_get(touch_pad.port, touch_pad.pin);
	int sda_raw = gpio_pin_get(touch_sda.port, touch_sda.pin);
	LOG_INF("IQS231B lines before start SCL %d SDA %d", scl_raw, sda_raw);

	// send standalone command directly to register 0x04
	// retry address byte across sensing cycles if sleeping
	bool addr_ack = false;
	for (int attempt = 0; attempt < 10; attempt++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 0)) {
			addr_ack = true;
			break;
		}
		bb_stop();
		k_msleep(5);
	}

	if (!addr_ack) {
		LOG_INF("IQS231B address 0x44 not responding maybe already in standalone");
		return;
	}

	// write commands register 0x04
	if (!bb_write_byte(IQS231B_REG_COMMANDS)) {
		LOG_WRN("IQS231B commands register write NACK");
		bb_stop();
		return;
	}

	// write standalone mode command 0x01
	if (!bb_write_byte(IQS231B_CMD_STANDALONE)) {
		LOG_WRN("IQS231B standalone command data NACK");
		bb_stop();
		return;
	}

	bb_stop();
	LOG_INF("IQS231B standalone mode command successful");

	// settle time for standalone transition and ati
	k_msleep(50);
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

	LOG_INF("touch debounce check val %d is_touched %d", val, is_touched);

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
		// notify system to wake up
		struct wake_request_msg wake_msg;
		zbus_chan_pub(&wake_request_chan, &wake_msg, K_NO_WAIT);
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

	if (!gpio_is_ready_dt(&touch_sda)) {
		LOG_ERR("Touch SDA GPIO not ready");
		return -1;
	}

	// configure iqs231b via i2c before releasing pins
	iqs231b_configure_standalone();

	// float d6 for normal sensitivity
	gpio_pin_configure_dt(&touch_sda, GPIO_DISCONNECTED);

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&long_press_work, long_press_work_handler);

	// configure d5 as active low touch input
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

	LOG_INF("IQS231B touch sensor initialized on D5 initial val %d", gpio_pin_get_dt(&touch_pad));
	return 0;
}
