#include "motion_ipc.h"

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zephyr/zbus/zbus.h>
#include "events.h"
#define TARE_REQUEST 0x01
#define STILLNESS_REQUEST 0x02
#define STILLNESS_CONFIRMED 0x03
#define SLEEP_REQUEST 0x04
#define WAKE_REQUEST 0x05

LOG_MODULE_REGISTER(motion_ipc, CONFIG_LOG_DEFAULT_LEVEL);


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
        struct tare_request_msg tare_msg;
        zbus_chan_pub(&tare_request_chan, &tare_msg, K_NO_WAIT);
    } else if (msg == STILLNESS_CONFIRMED) {
        LOG_INF("FLPR confirmed stillness");
        k_sem_give(&stillness_sem);
    } else if (msg == WAKE_REQUEST) {
        LOG_INF("Received WAKE_REQUEST from FLPR");
        struct wake_request_msg wake_msg;
        zbus_chan_pub(&wake_request_chan, &wake_msg, K_NO_WAIT);
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

int motion_ipc_send_sleep_request(void)
{
    uint8_t msg = SLEEP_REQUEST;
    int ret = ipc_service_send(&ep, &msg, sizeof(msg));
    if (ret < 0) {
        LOG_ERR("Failed to send SLEEP_REQUEST: %d", ret);
        return ret;
    }
    LOG_INF("Sent SLEEP_REQUEST to FLPR");
    return 0;
}
