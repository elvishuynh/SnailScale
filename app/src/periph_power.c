#include "periph_power.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(periph_power, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c22));
static const struct gpio_dt_spec powerswitch_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), powerswitch_gpios);
static const struct gpio_dt_spec drdy_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), drdy_gpios);
static const struct gpio_dt_spec matrix_din_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), matrixdin_gpios);
static const struct gpio_dt_spec matrix_sclk_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), matrixsclk_gpios);

void periph_3v3_off(void)
{
	/* suspend i2c bus */
#ifdef CONFIG_PM_DEVICE
	if (device_is_ready(i2c_dev)) {
		pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_SUSPEND);
	}
#endif

	/* isolate signal pins */
	gpio_pin_configure_dt(&drdy_gpio, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&matrix_sclk_gpio, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&matrix_din_gpio, GPIO_DISCONNECTED);

	/* cut power */
	gpio_pin_configure_dt(&powerswitch_gpio, GPIO_OUTPUT_INACTIVE);
	gpio_pin_set_dt(&powerswitch_gpio, 0);
	LOG_INF("Peripheral power OFF");
}

void periph_3v3_on(void)
{
	if (!gpio_is_ready_dt(&powerswitch_gpio)) {
		LOG_ERR("powerswitch gpio not ready");
		return;
	}

	gpio_pin_configure_dt(&powerswitch_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&powerswitch_gpio, 1);

	/* Settle time for TPS22917 rail with CT capacitor */
	k_msleep(15);
	LOG_INF("Peripheral power ON: pin %d", powerswitch_gpio.pin);

	/* resume i2c if already initialized (for runtime sleep/wake cycles) */
#ifdef CONFIG_PM_DEVICE
	if (device_is_ready(i2c_dev)) {
		pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_RESUME);
	}
#endif

	/* restore inputs */
	gpio_pin_configure_dt(&drdy_gpio, GPIO_INPUT);

	/* restore outputs */
	gpio_pin_configure_dt(&matrix_din_gpio, GPIO_OUTPUT_LOW);
	gpio_pin_configure_dt(&matrix_sclk_gpio, GPIO_OUTPUT_LOW);
}

static int periph_power_sys_init(void)
{
	/* turn on power before sensor drivers init */
	periph_3v3_on();
	return 0;
}

SYS_INIT(periph_power_sys_init, POST_KERNEL, 41);

