#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <stdint.h>

// initialize touch sensor
int touch_sensor_init(void);

// lock touch sensor while busy
void touch_sensor_lock(void);

// unlock touch sensor with post settle cooldown
void touch_sensor_unlock(uint32_t settle_ms);

#endif

