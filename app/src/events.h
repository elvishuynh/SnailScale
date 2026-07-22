#ifndef EVENTS_H
#define EVENTS_H

#include <zephyr/zbus/zbus.h>

struct tare_request_msg {
	uint8_t dummy; // prevent empty struct
};

struct wake_request_msg {
	uint8_t dummy;
};

ZBUS_CHAN_DECLARE(tare_request_chan);
ZBUS_CHAN_DECLARE(wake_request_chan);

#endif
