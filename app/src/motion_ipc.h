#ifndef MOTION_IPC_H
#define MOTION_IPC_H

int motion_ipc_init(void);
int motion_ipc_send_stillness_request(void);
int motion_ipc_wait_stillness(int timeout_ms);

#endif
