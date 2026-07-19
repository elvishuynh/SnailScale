#include "heartbeat.h"

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat, CONFIG_LOG_DEFAULT_LEVEL);

static K_SEM_DEFINE(ep_bound, 0, 1);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void ep_bound_cb(void *priv) {
    k_sem_give(&ep_bound);
}

static void ep_recv_cb(const void *data, size_t len, void *priv) {
    if (len < 1) return;
    uint8_t counter = ((const uint8_t *)data)[0];
    
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_toggle_dt(&led);
    }

    // rate limit RTT logging so 1kHz stress test doesn't lock up the console
    static int64_t last_log = 0;
    int64_t now = k_uptime_get();
    if (now - last_log >= 1000) {
        LOG_INF("heartbeat rx %u", counter);
        last_log = now;
    }
}

static struct ipc_ept_cfg ep_cfg = {
    .name = "heartbeat",
    .cb = {
        .bound    = ep_bound_cb,
        .received = ep_recv_cb,
    },
};

int heartbeat_init(void)
{
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));
    static struct ipc_ept ep;

    int ret = ipc_service_open_instance(ipc);
    if (ret < 0 && ret != -EALREADY) {
        LOG_ERR("ipc open failed %d", ret);
        return ret;
    }

    ret = ipc_service_register_endpoint(ipc, &ep, &ep_cfg);
    if (ret < 0) {
        LOG_ERR("ep register failed %d", ret);
        return ret;
    }

    // wait for flpr to bind its end
    k_sem_take(&ep_bound, K_FOREVER);
    LOG_INF("heartbeat endpoint bound on cpuapp");
    return 0;
}
