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

// bit bang i2c open drain primitives
static void bb_scl_set(int high)
{
	if (high) {
		gpio_pin_set_raw(touch_pad.port, touch_pad.pin, 1);
		// wait for clock stretch
		int timeout_us = 10000;
		while (gpio_pin_get_raw(touch_pad.port, touch_pad.pin) == 0 && timeout_us > 0) {
			k_busy_wait(10);
			timeout_us -= 10;
		}
	} else {
		gpio_pin_set_raw(touch_pad.port, touch_pad.pin, 0);
	}
	k_busy_wait(10);
}

static void bb_sda_set(int high)
{
	gpio_pin_set_raw(touch_sda.port, touch_sda.pin, high ? 1 : 0);
	k_busy_wait(10);
}

static int bb_sda_get(void)
{
	return gpio_pin_get_raw(touch_sda.port, touch_sda.pin);
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
	k_busy_wait(20);
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

static uint8_t bb_read_byte(bool ack)
{
	uint8_t byte = 0;
	bb_sda_set(1);
	for (int i = 7; i >= 0; i--) {
		bb_scl_set(1);
		if (bb_sda_get() > 0) {
			byte |= (1 << i);
		}
		bb_scl_set(0);
	}
	bb_sda_set(ack ? 0 : 1);
	bb_scl_set(1);
	bb_scl_set(0);
	bb_sda_set(1);
	return byte;
}

static void iqs231b_configure_standalone(void)
{
	// wait for test mode window of 340ms to finish
	int64_t uptime = k_uptime_get();
	if (uptime < 450) {
		k_msleep(450 - uptime);
	}

	// configure pins as open drain with pull up
	gpio_pin_configure(touch_pad.port, touch_pad.pin,
			   GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN | GPIO_PULL_UP | GPIO_INPUT);
	gpio_pin_configure(touch_sda.port, touch_sda.pin,
			   GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN | GPIO_PULL_UP | GPIO_INPUT);
	k_busy_wait(100);

	int scl_raw = gpio_pin_get_raw(touch_pad.port, touch_pad.pin);
	int sda_raw = gpio_pin_get_raw(touch_sda.port, touch_sda.pin);
	LOG_INF("IQS231B lines before start SCL %d SDA %d", scl_raw, sda_raw);

	// probe device and read registers if responding
	bool dev_found = false;
	uint8_t events = 0;
	uint8_t prod_id = 0;
	uint8_t sw_ver = 0;

	for (int attempt = 0; attempt < 5; attempt++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 1)) {
			events = bb_read_byte(true);
			prod_id = bb_read_byte(true);
			sw_ver = bb_read_byte(false);
			bb_stop();
			dev_found = true;
			LOG_INF("IQS231B detected prod 0x%02x ver 0x%02x events 0x%02x",
				prod_id, sw_ver, events);
			break;
		}
		bb_stop();
		k_msleep(10);
	}

	if (!dev_found) {
		LOG_INF("IQS231B address 0x44 not responding assuming already standalone");
		return;
	}

	k_msleep(5);

	// write commands register 4 with standalone command 1
	bool cmd_ok = false;
	for (int retry = 0; retry < 3; retry++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 0)) {
			if (bb_write_byte(IQS231B_REG_COMMANDS)) {
				if (bb_write_byte(IQS231B_CMD_STANDALONE)) {
					cmd_ok = true;
					bb_stop();
					break;
				}
			}
		}
		bb_stop();
		k_msleep(10);
	}

	if (cmd_ok) {
		LOG_INF("IQS231B standalone mode command successful");
	} else {
		LOG_WRN("IQS231B standalone command failed");
	}

	// settle time for standalone transition and ati
	k_msleep(150);
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
