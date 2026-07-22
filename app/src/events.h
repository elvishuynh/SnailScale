#ifndef EVENTS_H
#define EVENTS_H

#include <zephyr/zbus/zbus.h>

struct tare_request_msg {
	uint8_t dummy; // prevent empty struct
};

ZBUS_CHAN_DECLARE(tare_request_chan);

#endif
