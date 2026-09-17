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

// multi tap timing
#define MULTI_TAP_WINDOW_MS 450
#define CALIBRATE_TAP_COUNT 5

// iqs231b registers and commands
#define IQS231B_I2C_ADDR 0x44
#define IQS231B_REG_COMMANDS 0x04
#define IQS231B_REG_OTP_BANK_2 0x06
#define IQS231B_REG_TOUCH_THRESHOLD 0x0A
#define IQS231B_REG_PROX_THRESHOLD 0x0B
#define IQS231B_CMD_STANDALONE 0x01
#define IQS231B_TOUCH_THRESHOLD_VAL 0x28
#define IQS231B_PROX_THRESHOLD_VAL 0x08

static const struct gpio_dt_spec touch_pad = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec touch_sda = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), touchsda_gpios);
static struct gpio_callback touch_cb_data;
static struct k_work_delayable tap_timeout_work;
static struct k_work_delayable debounce_work;
static struct k_work_delayable iqs_switch_work;
static struct k_work_delayable iqs_ready_work;
static void touch_pad_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

static bool is_touched = false;
static int64_t press_start_time = 0;
static uint8_t tap_count = 0;
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

static void iqs_ready_work_handler(struct k_work *work)
{
	int ret;

	// pull up d5 for open drain heartbeat
	gpio_pin_configure_dt(&touch_pad, GPIO_INPUT | GPIO_PULL_UP);

	// configure d6 as active low touch input
	ret = gpio_pin_configure(touch_sda.port, touch_sda.pin, GPIO_INPUT | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("Failed to configure touch pin (%d)", ret);
		return;
	}

	gpio_init_callback(&touch_cb_data, touch_pad_isr, BIT(touch_sda.pin));
	ret = gpio_add_callback(touch_sda.port, &touch_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add touch callback (%d)", ret);
		return;
	}

	// configure interrupt on both edges
	ret = gpio_pin_interrupt_configure(touch_sda.port, touch_sda.pin, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure touch interrupt (%d)", ret);
		return;
	}

	int raw_init = gpio_pin_get_raw(touch_sda.port, touch_sda.pin);
	LOG_INF("IQS231B touch sensor ready on D6 raw %d val %d", raw_init, (raw_init == 0) ? 1 : 0);
}

static void iqs_switch_work_handler(struct k_work *work)
{
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
	uint8_t dump[20] = {0};

	for (int attempt = 0; attempt < 5; attempt++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 1)) {
			// first byte is main events
			dump[0] = bb_read_byte(true);
			// read registers 0x00 to 0x12
			for (int r = 0; r < 18; r++) {
				dump[r + 1] = bb_read_byte(r < 17);
			}
			bb_stop();
			if (dump[1] == 0x40) {
				dev_found = true;
				LOG_INF("IQS231B detected prod 0x%02x ver 0x%02x events 0x%02x",
					dump[1], dump[2], dump[0]);
				LOG_INF("IQS231B OTP bank1 0x%02x bank2 0x%02x bank3 0x%02x",
					dump[6], dump[7], dump[8]);
				LOG_INF("IQS231B touch thres 0x%02x prox thres 0x%02x flags 0x%02x",
					dump[11], dump[12], dump[19]);
				break;
			}
		}
		bb_stop();
		k_busy_wait(500);
	}

	if (!dev_found) {
		LOG_INF("IQS231B address 0x44 not responding assuming already standalone");
		// finish setup immediately
		k_work_schedule(&iqs_ready_work, K_NO_WAIT);
		return;
	}

	k_busy_wait(500);

	// set ui to touch with no movement and enable quick release
	uint8_t bank2_val = (dump[7] & ~0x07) | 0x03 | BIT(2);
	for (int retry = 0; retry < 3; retry++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 0)) {
			if (bb_write_byte(IQS231B_REG_OTP_BANK_2)) {
				if (bb_write_byte(bank2_val)) {
					bb_stop();
					LOG_INF("IQS231B set OTP bank 2 to 0x%02x", bank2_val);
					break;
				}
			}
		}
		bb_stop();
		k_busy_wait(500);
	}

	// set prox threshold within datasheet range
	for (int retry = 0; retry < 3; retry++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 0)) {
			if (bb_write_byte(IQS231B_REG_PROX_THRESHOLD)) {
				if (bb_write_byte(IQS231B_PROX_THRESHOLD_VAL)) {
					bb_stop();
					LOG_INF("IQS231B prox threshold set to 0x%02x", IQS231B_PROX_THRESHOLD_VAL);
					break;
				}
			}
		}
		bb_stop();
		k_busy_wait(500);
	}

	// set touch threshold higher to prevent midair hover triggers
	for (int retry = 0; retry < 3; retry++) {
		bb_start();
		if (bb_write_byte((IQS231B_I2C_ADDR << 1) | 0)) {
			if (bb_write_byte(IQS231B_REG_TOUCH_THRESHOLD)) {
				if (bb_write_byte(IQS231B_TOUCH_THRESHOLD_VAL)) {
					bb_stop();
					LOG_INF("IQS231B touch threshold set to 0x%02x", IQS231B_TOUCH_THRESHOLD_VAL);
					break;
				}
			}
		}
		bb_stop();
		k_busy_wait(500);
	}

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
		k_busy_wait(500);
	}

	if (cmd_ok) {
		LOG_INF("IQS231B standalone mode command successful");
	} else {
		LOG_WRN("IQS231B standalone command failed");
	}

	// schedule ready setup after 150ms settling
	k_work_schedule(&iqs_ready_work, K_MSEC(150));
}

static void tap_timeout_work_handler(struct k_work *work)
{
	if (tap_count > 0 && tap_count < CALIBRATE_TAP_COUNT) {
		LOG_INF("Tap sequence ended with %d taps. Firing tare event.", tap_count);
		tap_count = 0;
		display_manager_register_activity();
		struct tare_request_msg msg;
		zbus_chan_pub(&tare_request_chan, &msg, K_NO_WAIT);
	}
}

static void debounce_work_handler(struct k_work *work)
{
	// read d6 touch state low is touched
	int raw_pin = gpio_pin_get_raw(touch_sda.port, touch_sda.pin);
	int val = (raw_pin == 0) ? 1 : 0;
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
		LOG_INF("touch detected (debounced)");
		// notify system to wake up
		struct wake_request_msg wake_msg;
		zbus_chan_pub(&wake_request_chan, &wake_msg, K_NO_WAIT);
	} else if (val == 0 && is_touched) {
		is_touched = false;
		int64_t press_duration = now - press_start_time;
		LOG_INF("touch released (debounced) duration %lld ms", press_duration);

		// validate single tap duration
		if (press_duration >= MIN_TAP_DURATION_MS && press_duration <= MAX_TAP_DURATION_MS) {
			tap_count++;
			LOG_INF("tap %d detected", tap_count);

			if (tap_count >= CALIBRATE_TAP_COUNT) {
				// five taps reached fire calibration
				k_work_cancel_delayable(&tap_timeout_work);
				tap_count = 0;
				display_manager_register_activity();
				LOG_INF("Five rapid taps detected. Firing calibrate event.");
				struct calibrate_request_msg msg;
				zbus_chan_pub(&calibrate_request_chan, &msg, K_NO_WAIT);
			} else {
				// schedule timeout to fire tare if no more taps
				k_work_reschedule(&tap_timeout_work, K_MSEC(MULTI_TAP_WINDOW_MS));
			}
		} else {
			// press duration outside tap limits
			tap_count = 0;
			k_work_cancel_delayable(&tap_timeout_work);
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
	if (!gpio_is_ready_dt(&touch_pad)) {
		LOG_ERR("Touch pad GPIO not ready");
		return -1;
	}

	if (!gpio_is_ready_dt(&touch_sda)) {
		LOG_ERR("Touch SDA GPIO not ready");
		return -1;
	}

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&tap_timeout_work, tap_timeout_work_handler);
	k_work_init_delayable(&iqs_switch_work, iqs_switch_work_handler);
	k_work_init_delayable(&iqs_ready_work, iqs_ready_work_handler);

	// schedule iqs231b standalone switch at 450ms without blocking main
	int64_t uptime = k_uptime_get();
	int32_t delay_ms = (uptime < 450) ? (int32_t)(450 - uptime) : 0;
	k_work_schedule(&iqs_switch_work, K_MSEC(delay_ms));

	LOG_INF("IQS231B background init scheduled in %d ms", delay_ms);
	return 0;
}
