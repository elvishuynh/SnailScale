#include "heartbeat.h"

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "scale_logic.h"

#define TARE_REQUEST 0x01

LOG_MODULE_REGISTER(heartbeat, CONFIG_LOG_DEFAULT_LEVEL);

static struct k_work tare_work;

static void tare_work_handler(struct k_work *work) {
    LOG_INF("Taring scale via shake");
    scale_tare();
}

static K_SEM_DEFINE(ep_bound, 0, 1);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void ep_bound_cb(void *priv) {
    k_sem_give(&ep_bound);
}

static void ep_recv_cb(const void *data, size_t len, void *priv) {
    if (len < 1) return;
    uint8_t msg = ((const uint8_t *)data)[0];
    
    if (msg == TARE_REQUEST) {
        LOG_INF("Received TARE_REQUEST from FLPR");
        if (gpio_is_ready_dt(&led)) {
            gpio_pin_toggle_dt(&led);
        }
        k_work_submit(&tare_work);
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
    k_work_init(&tare_work, tare_work_handler);

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
