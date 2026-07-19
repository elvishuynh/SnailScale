#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

LOG_MODULE_REGISTER(flpr_main, LOG_LEVEL_INF);

#define TARE_REQUEST 0x01
#define STILLNESS_REQUEST 0x02
#define STILLNESS_CONFIRMED 0x03

static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(lsm6ds3tr_c));

#define FIFO_WATERMARK 30 // 30 words = 10 samples X, Y, Z
static int16_t fifo_buf[FIFO_WATERMARK];

#define SHAKE_WINDOW_SAMPLES 20
#define VARIANCE_THRESHOLD 4000000
#define ZCR_MIN 4
#define ZCR_MARGIN 1500

// variance below this means the scale is not moving
#define STILLNESS_THRESHOLD 500000
#define STILLNESS_REQUIRED_READS 1

struct vec3 {
    int16_t x, y, z;
};
static struct vec3 history[SHAKE_WINDOW_SAMPLES];
static size_t history_idx = 0;
static size_t history_count = 0;

// stillness detection state
static volatile bool awaiting_stillness;
static int still_count;
static struct ipc_ept ep;

static bool detect_shake_gesture(int16_t *buffer, size_t num_samples) {
    bool triggered = false;

    for (size_t i = 0; i < num_samples; i++) {
        history[history_idx].x = buffer[i * 3 + 0];
        history[history_idx].y = buffer[i * 3 + 1];
        history[history_idx].z = buffer[i * 3 + 2];
        
        history_idx = (history_idx + 1) % SHAKE_WINDOW_SAMPLES;
        if (history_count < SHAKE_WINDOW_SAMPLES) {
            history_count++;
        }
    }

    if (history_count < SHAKE_WINDOW_SAMPLES) {
        return false;
    }

    int64_t sum_x = 0, sum_y = 0, sum_z = 0;
    for (size_t i = 0; i < SHAKE_WINDOW_SAMPLES; i++) {
        sum_x += history[i].x;
        sum_y += history[i].y;
        sum_z += history[i].z;
    }
    int32_t mean_x = (int32_t)(sum_x / SHAKE_WINDOW_SAMPLES);
    int32_t mean_y = (int32_t)(sum_y / SHAKE_WINDOW_SAMPLES);
    int32_t mean_z = (int32_t)(sum_z / SHAKE_WINDOW_SAMPLES);

    int64_t var_sum = 0;
    for (size_t i = 0; i < SHAKE_WINDOW_SAMPLES; i++) {
        int32_t dx = history[i].x - mean_x;
        int32_t dy = history[i].y - mean_y;
        int32_t dz = history[i].z - mean_z;
        var_sum += (int64_t)(dx * dx) + (int64_t)(dy * dy) + (int64_t)(dz * dz);
    }
    int32_t variance = (int32_t)(var_sum / SHAKE_WINDOW_SAMPLES);

    int max_zcr = 0;
    int32_t means[3] = {mean_x, mean_y, mean_z};
    
    for (int axis = 0; axis < 3; axis++) {
        int zcr = 0;
        int16_t initial_val = (axis == 0) ? history[history_idx].x : ((axis == 1) ? history[history_idx].y : history[history_idx].z);
        bool is_high = (initial_val > means[axis]);
        
        for (size_t i = 1; i < SHAKE_WINDOW_SAMPLES; i++) {
            size_t idx = (history_idx + i) % SHAKE_WINDOW_SAMPLES;
            int16_t val = (axis == 0) ? history[idx].x : ((axis == 1) ? history[idx].y : history[idx].z);
            
            if (is_high && val < means[axis] - ZCR_MARGIN) {
                zcr++;
                is_high = false;
            } else if (!is_high && val > means[axis] + ZCR_MARGIN) {
                zcr++;
                is_high = true;
            }
        }
        if (zcr > max_zcr) max_zcr = zcr;
    }

    if (variance > VARIANCE_THRESHOLD && max_zcr >= ZCR_MIN) {
        LOG_INF("Shake detected! (var: %d, max_zcr: %d)", variance, max_zcr);
        history_count = 0; // reset to avoid double trigger
        triggered = true;
    }

    return triggered;
}

static K_SEM_DEFINE(shake_sem, 0, 1);

static void imu_trigger_handler(const struct device *dev, const struct sensor_trigger *trig) {
    uint8_t status1, status2;
    i2c_reg_read_byte_dt(&imu_i2c, 0x3A, &status1);
    i2c_reg_read_byte_dt(&imu_i2c, 0x3B, &status2);
    
    uint16_t num_words = status1 | ((status2 & 0x07) << 8);
    
    if (num_words < FIFO_WATERMARK) {
        LOG_WRN("Interrupt fired but FIFO only has %u words", num_words);
        return;
    }

    // burst read from FIFO_DATA_OUT_L (0x3E)
    int ret = i2c_burst_read_dt(&imu_i2c, 0x3E, (uint8_t*)fifo_buf, FIFO_WATERMARK * 2);
    if (ret != 0) {
        LOG_ERR("FIFO burst read failed: %d", ret);
        return;
    }

    size_t num_samples = FIFO_WATERMARK / 3;

    if (awaiting_stillness) {
        // calculate per batch variance to check if scale is still
        int64_t sx = 0, sy = 0, sz = 0;
        for (size_t i = 0; i < num_samples; i++) {
            sx += fifo_buf[i * 3 + 0];
            sy += fifo_buf[i * 3 + 1];
            sz += fifo_buf[i * 3 + 2];
        }
        int32_t mx = (int32_t)(sx / num_samples);
        int32_t my = (int32_t)(sy / num_samples);
        int32_t mz = (int32_t)(sz / num_samples);

        int64_t var = 0;
        for (size_t i = 0; i < num_samples; i++) {
            int32_t dx = fifo_buf[i * 3 + 0] - mx;
            int32_t dy = fifo_buf[i * 3 + 1] - my;
            int32_t dz = fifo_buf[i * 3 + 2] - mz;
            var += (int64_t)(dx * dx) + (int64_t)(dy * dy) + (int64_t)(dz * dz);
        }
        var /= num_samples;

        if (var < STILLNESS_THRESHOLD) {
            still_count++;
            LOG_INF("Still read %d/%d (var: %lld)", still_count, STILLNESS_REQUIRED_READS, var);
            if (still_count >= STILLNESS_REQUIRED_READS) {
                LOG_INF("Stillness confirmed");
                awaiting_stillness = false;
                still_count = 0;
                uint8_t msg = STILLNESS_CONFIRMED;
                ipc_service_send(&ep, &msg, sizeof(msg));
            }
        } else {
            // reset if movement detected again
            still_count = 0;
        }
    } else {
        // normal shake detection
        if (detect_shake_gesture(fifo_buf, num_samples)) {
            k_sem_give(&shake_sem);
        }
    }
}

static K_SEM_DEFINE(ep_bound, 0, 1);

static void ep_bound_cb(void *priv) {
    k_sem_give(&ep_bound);
}

static void ep_recv_cb(const void *data, size_t len, void *priv) {
    if (len < 1) return;
    uint8_t msg = ((const uint8_t *)data)[0];

    if (msg == STILLNESS_REQUEST) {
        LOG_INF("Stillness check requested by cpuapp");
        still_count = 0;
        awaiting_stillness = true;
    }
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

    if (!i2c_is_ready_dt(&imu_i2c)) {
        LOG_ERR("imu I2C bus not ready");
    }

    const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));
    if (!device_is_ready(imu_dev)) {
        LOG_ERR("imu not ready");
    } else {
        struct sensor_trigger trig = {
            .type = SENSOR_TRIG_DATA_READY,
            .chan = SENSOR_CHAN_ACCEL_XYZ,
        };
        
        // wake up the imu and set odr to 12.5hz so it generates interrupts
        struct sensor_value odr = { .val1 = 12, .val2 = 500000 };
        if (sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) < 0) {
            LOG_ERR("failed to set imu odr");
        }

        if (sensor_trigger_set(imu_dev, &trig, imu_trigger_handler) < 0) {
            LOG_ERR("failed to set imu trigger");
        } else {
            LOG_INF("imu trigger configured");
            
            // reconfigure FIFO manually via I2C for Zephyr default override
            i2c_reg_write_byte_dt(&imu_i2c, 0x06, FIFO_WATERMARK); // FIFO_CTRL1
            i2c_reg_write_byte_dt(&imu_i2c, 0x07, 0x00);          // FIFO_CTRL2
            i2c_reg_write_byte_dt(&imu_i2c, 0x08, 0x01);          // FIFO_CTRL3 (no XL decimation)
            i2c_reg_write_byte_dt(&imu_i2c, 0x09, 0x00);          // FIFO_CTRL4
            i2c_reg_write_byte_dt(&imu_i2c, 0x0A, 0x0E);          // FIFO_CTRL5 (12.5Hz, Continuous mode)
            i2c_reg_write_byte_dt(&imu_i2c, 0x0D, 0x08);          // INT1_CTRL (Enable FTH, disable DRDY)
            LOG_INF("imu FIFO activated");
        }
    }

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
    LOG_INF("tare endpoint bound");

    while (1) {
        k_sem_take(&shake_sem, K_FOREVER);
        
        uint8_t msg = TARE_REQUEST;
        int err = ipc_service_send(&ep, &msg, sizeof(msg));
        if (err < 0) {
            LOG_ERR("ipc send failed: %d", err);
        } else {
            LOG_INF("Sent TARE_REQUEST to CPUAPP");
        }
    }
    return 0;
}
