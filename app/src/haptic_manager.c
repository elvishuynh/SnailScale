#include "haptic_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/haptics.h>
#include <zephyr/drivers/haptics/drv2605.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(haptic_manager, CONFIG_LOG_DEFAULT_LEVEL);

// drv2605 register addresses
#define DRV2605_REG_STATUS 0x00
#define DRV2605_REG_MODE 0x01
#define DRV2605_REG_GO 0x0C
#define DRV2605_REG_RATED_VOLTAGE 0x16
#define DRV2605_REG_OVERDRIVE_CLAMP_VOLTAGE 0x17
#define DRV2605_REG_FEEDBACK_CONTROL 0x1A
#define DRV2605_REG_CONTROL3 0x1D

// calculated rated voltage 1800mv
#define DRV2605_VAL_RATED_1800MV 81

// calculated clamp voltage 2000mv
#define DRV2605_VAL_CLAMP_2000MV 91

// feedback control lra 3x brake high gain
#define DRV2605_VAL_FEEDBACK_LRA 0xB6

// lra open loop
#define DRV2605_VAL_CONTROL3_LRA_OPEN 0x01

#if DT_NODE_EXISTS(DT_NODELABEL(drv2605))
// drv2605 devicetree spec
static const struct device *haptic_dev = DEVICE_DT_GET(DT_NODELABEL(drv2605));
static const struct i2c_dt_spec haptic_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(drv2605));
#endif

int haptic_manager_reinit(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(drv2605))
	if (!device_is_ready(haptic_dev) || !i2c_is_ready_dt(&haptic_i2c)) {
		return -ENODEV;
	}

	// wake up chip
	i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_MODE, 0x00);

	// set lra feedback
	i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_FEEDBACK_CONTROL, DRV2605_VAL_FEEDBACK_LRA);

	// set rated voltage
	i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_RATED_VOLTAGE, DRV2605_VAL_RATED_1800MV);

	// set clamp voltage
	i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_OVERDRIVE_CLAMP_VOLTAGE, DRV2605_VAL_CLAMP_2000MV);

	// enable lra open loop
	i2c_reg_update_byte_dt(&haptic_i2c, DRV2605_REG_CONTROL3, DRV2605_VAL_CONTROL3_LRA_OPEN, DRV2605_VAL_CONTROL3_LRA_OPEN);

	return 0;
#else
	return 0;
#endif
}

static int play_effect(uint8_t effect_id)
{
#if DT_NODE_EXISTS(DT_NODELABEL(drv2605))
	if (!device_is_ready(haptic_dev) || !i2c_is_ready_dt(&haptic_i2c)) {
		return -ENODEV;
	}

	uint8_t pre_status = 0;
	uint8_t pre_go = 0;
	uint8_t post_status = 0;
	uint8_t post_go = 0;

	// read status to clear latched faults and get state
	i2c_reg_read_byte_dt(&haptic_i2c, DRV2605_REG_STATUS, &pre_status);

	// read go bit before starting
	i2c_reg_read_byte_dt(&haptic_i2c, DRV2605_REG_GO, &pre_go);

	// clear go bit to guarantee low state
	i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_GO, 0x00);

	// wake from standby and set internal trigger
	int ret = i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_MODE, 0x00);
	if (ret < 0) {
		LOG_ERR("wake chip failed %d", ret);
		return ret;
	}

	// wait for analog wakeup
	k_usleep(250);

	struct drv2605_rom_data rom = {
		.library = DRV2605_LIBRARY_LRA,
		.trigger = DRV2605_MODE_INTERNAL_TRIGGER,
		.seq_regs[0] = effect_id,
		.seq_regs[1] = 0,
		.overdrive_time = 0,
		.sustain_pos_time = 0,
		.sustain_neg_time = 0,
		.brake_time = 0,
	};

	// configure rom playback
	union drv2605_config_data cfg = { .rom_data = &rom };
	ret = drv2605_haptic_config(haptic_dev, DRV2605_HAPTICS_SOURCE_ROM, &cfg);
	if (ret < 0) {
		LOG_ERR("config rom failed %d", ret);
		return ret;
	}

	// trigger go bit directly to ensure rising edge
	ret = i2c_reg_write_byte_dt(&haptic_i2c, DRV2605_REG_GO, 0x01);

	// read post trigger registers for diagnostics
	i2c_reg_read_byte_dt(&haptic_i2c, DRV2605_REG_STATUS, &post_status);
	i2c_reg_read_byte_dt(&haptic_i2c, DRV2605_REG_GO, &post_go);

	LOG_INF("play effect %d pre status 0x%02x go 0x%02x post status 0x%02x go 0x%02x ret %d",
		effect_id, pre_status, pre_go, post_status, post_go, ret);

	return ret;
#else
	return 0;
#endif
}

int haptic_manager_init(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(drv2605))
	// check device ready
	if (!device_is_ready(haptic_dev)) {
		LOG_ERR("drv2605 device not ready");
		return -ENODEV;
	}

	// restore registers
	haptic_manager_reinit();

	LOG_INF("haptic manager initialized with lra");

	// boot test click
	haptic_play_subtle_click();

	return 0;
#else
	LOG_INF("haptic driver not present in devicetree");
	return 0;
#endif
}

int haptic_play_click(void)
{
	// strong click 60 percent to match boot and wake
	return play_effect(2);
}

int haptic_play_subtle_click(void)
{
	// subtle click
	return play_effect(2);
}

int haptic_play_arm_cal(void)
{
	// soft bump 60 percent
	return play_effect(8);
}

int haptic_play_cal_confirm(void)
{
	// double click 60 percent
	return play_effect(11);
}
