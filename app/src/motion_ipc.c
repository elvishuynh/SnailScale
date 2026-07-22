#include "motion_ipc.h"

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "scale_logic.h"

#define TARE_REQUEST 0x01
#define STILLNESS_REQUEST 0x02
#define STILLNESS_CONFIRMED 0x03

LOG_MODULE_REGISTER(motion_ipc, CONFIG_LOG_DEFAULT_LEVEL);

static struct k_work tare_work;

static void tare_work_handler(struct k_work *work) {
    LOG_INF("Taring scale via shake");
    scale_tare();
}

static K_SEM_DEFINE(ep_bound, 0, 1);
static K_SEM_DEFINE(stillness_sem, 0, 1);

static struct ipc_ept ep;

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
    } else if (msg == STILLNESS_CONFIRMED) {
        LOG_INF("FLPR confirmed stillness");
        k_sem_give(&stillness_sem);
    }
}

static struct ipc_ept_cfg ep_cfg = {
    .name = "motion_ipc",
    .cb = {
        .bound    = ep_bound_cb,
        .received = ep_recv_cb,
    },
};

int motion_ipc_init(void)
{
    k_work_init(&tare_work, tare_work_handler);

    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

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

    // wait for flpr to bind
    k_sem_take(&ep_bound, K_FOREVER);
    LOG_INF("motion ipc endpoint bound on cpuapp");
    return 0;
}

// blocks until stillness is confirmed
int motion_ipc_send_stillness_request(void)
{
    // drain stale signals
    k_sem_reset(&stillness_sem);

    uint8_t msg = STILLNESS_REQUEST;
    int ret = ipc_service_send(&ep, &msg, sizeof(msg));
    if (ret < 0) {
        LOG_ERR("Failed to send STILLNESS_REQUEST: %d", ret);
        return ret;
    }
    return 0;
}

// returns zero if still or error if timeout
int motion_ipc_wait_stillness(int timeout_ms)
{
    return k_sem_take(&stillness_sem, K_MSEC(timeout_ms));
}
