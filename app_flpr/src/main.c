#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flpr_main, LOG_LEVEL_INF);

static K_SEM_DEFINE(ep_bound, 0, 1);

static void ep_bound_cb(void *priv) {
    k_sem_give(&ep_bound);
}

static void ep_recv_cb(const void *data, size_t len, void *priv) {
    // flpr doesnt expect inbound data yet
}

static struct ipc_ept_cfg ep_cfg = {
    .name = "heartbeat",
    .cb = {
        .bound    = ep_bound_cb,
        .received = ep_recv_cb,
    },
};

int main(void)
{
    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));
    struct ipc_ept ep;

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

    // wait for cpuapp to bind
    k_sem_take(&ep_bound, K_FOREVER);
    LOG_INF("heartbeat endpoint bound");

    uint8_t counter = 0;
    while (1) {
        int err = ipc_service_send(&ep, &counter, sizeof(counter));
        if (err < 0) {
            LOG_ERR("ipc send failed: %d", err);
        }
        counter++;

#ifdef CONFIG_HEARTBEAT_STRESS_TEST
        k_sleep(K_MSEC(1));
#else
        k_sleep(K_MSEC(1000));
#endif
    }
    return 0;
}
