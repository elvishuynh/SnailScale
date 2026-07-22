#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>
#include <stddef.h>

enum display_msg_type {
	MSG_DISPLAY_CLEAR,
	MSG_DISPLAY_PRINT,
	MSG_DISPLAY_WRITE,
	MSG_DISPLAY_SET_BRIGHTNESS,
	MSG_DISPLAY_POWER_OFF,
	MSG_DISPLAY_POWER_ON
};

struct display_msg {
	enum display_msg_type type;
	union {
		struct {
			char str[16];
			int align;
		} print;
		struct {
			uint8_t buf[16];
			size_t len;
		} write;
		struct {
			uint8_t level;
		} brightness;
	} data;
};

int display_manager_init(void);
void display_manager_clear(void);
void display_manager_print(const char *str, int align);
void display_manager_write(const uint8_t *buf, size_t len);
void display_manager_set_brightness(uint8_t level);
void display_manager_power_off(void);
void display_manager_power_on(void);
void display_manager_register_activity(void);

#endif /* DISPLAY_MANAGER_H */
