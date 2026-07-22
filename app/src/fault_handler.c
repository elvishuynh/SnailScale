#include "fault_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <pt18_matrix/pt18_matrix.h>
#include <pt18_matrix/pt18_matrix_text.h>

LOG_MODULE_REGISTER(fault_handler, CONFIG_LOG_DEFAULT_LEVEL);

void system_fault_handler(const char *reason)
{
	LOG_ERR("SYSTEM FAULT: %s", reason);
	LOG_ERR("Rebooting in 3 seconds...");

	pt18_matrix_clear();
	pt18_matrix_print("ERR", 0);

	k_sleep(K_SECONDS(3));
	sys_reboot(SYS_REBOOT_COLD);
}
