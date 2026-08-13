#include "periph_power.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c22));
static const struct gpio_dt_spec powerswitch_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), powerswitch_gpios);
static const struct gpio_dt_spec drdy_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), drdy_gpios);
static const struct gpio_dt_spec touch_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), touchout_gpios);
static const struct gpio_dt_spec matrix_din_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), matrixdin_gpios);
static const struct gpio_dt_spec matrix_sclk_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), matrixsclk_gpios);

void periph_3v3_off(void)
{
	/* suspend i2c bus */
#ifdef CONFIG_PM_DEVICE
	pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_SUSPEND);
#endif

	/* isolate signal pins */
	gpio_pin_configure_dt(&drdy_gpio, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&touch_gpio, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&matrix_sclk_gpio, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&matrix_din_gpio, GPIO_DISCONNECTED);

	/* cut power */
	gpio_pin_configure_dt(&powerswitch_gpio, GPIO_OUTPUT_INACTIVE);
}

void periph_3v3_on(void)
{
	/* apply power */
	gpio_pin_configure_dt(&powerswitch_gpio, GPIO_OUTPUT_ACTIVE);

	/* settle time */
	k_msleep(15);

	/* resume i2c */
#ifdef CONFIG_PM_DEVICE
	pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_RESUME);
#endif

	/* restore inputs */
	gpio_pin_configure_dt(&drdy_gpio, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&drdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_pin_configure_dt(&touch_gpio, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&touch_gpio, GPIO_INT_EDGE_BOTH);

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

SYS_INIT(periph_power_sys_init, POST_KERNEL, 10);
