#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flpr_main, LOG_LEVEL_INF);

int main(void)
{
	while (1) {
		LOG_INF("Hello World from RISC-V FLPR!");
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
