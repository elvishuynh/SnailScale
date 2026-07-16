#ifndef SCALE_LOGIC_H
#define SCALE_LOGIC_H

#include <zephyr/device.h>

void scale_tare(void);

int scale_logic_init(const struct device *nau_dev, const struct device *display_dev);

#endif
