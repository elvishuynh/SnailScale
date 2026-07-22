#include "display_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>

LOG_MODULE_REGISTER(display_manager, CONFIG_LOG_DEFAULT_LEVEL);

static struct k_work_delayable inactivity_work;
K_MSGQ_DEFINE(display_msgq, sizeof(struct display_msg), 16, 4);

static void inactivity_work_handler(struct k_work *work)
{
	display_manager_set_brightness(1);
}

void display_manager_clear(void)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_CLEAR;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_print(const char *str, int align)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_PRINT;
	strncpy(msg.data.print.str, str, sizeof(msg.data.print.str) - 1);
	msg.data.print.str[sizeof(msg.data.print.str) - 1] = '\0';
	msg.data.print.align = align;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_write(const uint8_t *buf, size_t len)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_WRITE;
	if (len > sizeof(msg.data.write.buf)) {
		len = sizeof(msg.data.write.buf);
	}
	memcpy(msg.data.write.buf, buf, len);
	msg.data.write.len = len;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_set_brightness(uint8_t level)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_SET_BRIGHTNESS;
	msg.data.brightness.level = level;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_power_off(void)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_POWER_OFF;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_power_on(void)
{
	struct display_msg msg;
	msg.type = MSG_DISPLAY_POWER_ON;
	k_msgq_put(&display_msgq, &msg, K_NO_WAIT);
}

void display_manager_register_activity(void)
{
	display_manager_set_brightness(2);
	k_work_reschedule(&inactivity_work, K_SECONDS(10));
}

static void display_thread(void)
{
	struct display_msg msg;

	while (1) {
		if (k_msgq_get(&display_msgq, &msg, K_FOREVER) == 0) {
			switch (msg.type) {
				case MSG_DISPLAY_CLEAR:
					pt18_matrix_clear();
					break;
				case MSG_DISPLAY_PRINT:
					pt18_matrix_print(msg.data.print.str, msg.data.print.align);
					break;
				case MSG_DISPLAY_WRITE:
					pt18_matrix_write(msg.data.write.buf, msg.data.write.len);
					break;
				case MSG_DISPLAY_SET_BRIGHTNESS:
					pt18_matrix_set_brightness(msg.data.brightness.level);
					break;
				case MSG_DISPLAY_POWER_OFF:
					pt18_matrix_power_off();
					break;
				case MSG_DISPLAY_POWER_ON:
					pt18_matrix_power_on();
					break;
				default:
					LOG_WRN("Unknown display message type: %d", msg.type);
					break;
			}
		}
	}
}

K_THREAD_DEFINE(display_thread_id, 1024, display_thread, NULL, NULL, NULL, 7, 0, 0);

int display_manager_init(void)
{
	k_work_init_delayable(&inactivity_work, inactivity_work_handler);

	int ret = pt18_matrix_init();
	if (ret != 0) {
		LOG_ERR("pt18_matrix_init failed: %d", ret);
		return ret;
	}
	
	/* Kick off initial timer */
	display_manager_register_activity();
	
	LOG_INF("Display manager initialized");
	return 0;
}
