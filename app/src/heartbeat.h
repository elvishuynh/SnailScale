#ifndef HEARTBEAT_H
#define HEARTBEAT_H

int heartbeat_init(void);
int heartbeat_send_stillness_request(void);
int heartbeat_wait_stillness(int timeout_ms);

#endif
