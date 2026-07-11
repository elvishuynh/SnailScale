#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>

LOG_MODULE_REGISTER(pt18_demo, LOG_LEVEL_DBG);

#define SCROLL_DELAY_MS 80
#define SCROLL_MSG      "Hello World"

/* strlen * stride gives total pixel width. start offscreen right, end offscreen left */
#define SCROLL_START    PT18_MATRIX_COLUMNS
#define SCROLL_END      (-(int)(sizeof(SCROLL_MSG) - 1) * 6)

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(tm1640));

	if (!device_is_ready(dev)) {
		LOG_ERR("TM1640 device not ready");
		return -1;
	}

	LOG_INF("PT18 matrix scroll demo started");

	pt18_matrix_init(dev);
	pt18_matrix_set_brightness(dev, 1);

	while (1) {
		for (int off = SCROLL_START; off >= SCROLL_END; off--) {
			pt18_matrix_print(dev, SCROLL_MSG, off);
			k_msleep(SCROLL_DELAY_MS);
		}
	}

	return 0;
}
