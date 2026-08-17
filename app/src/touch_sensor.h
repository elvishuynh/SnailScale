#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <stdint.h>

// initialize touch sensor
int touch_sensor_init(void);

// lockout touch input for duration
void touch_sensor_lockout(uint32_t duration_ms);

#endif

