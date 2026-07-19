#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

LOG_MODULE_REGISTER(flpr_main, LOG_LEVEL_INF);

#define TARE_REQUEST 0x01

static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(lsm6ds3tr_c));

#define FIFO_WATERMARK 30 // 30 words = 10 samples X, Y, Z
static int16_t fifo_buf[FIFO_WATERMARK];

#define AMPLITUDE_THRESHOLD 5000
#define ZCR_MIN 3

static bool detect_shake_gesture(int16_t *buffer, size_t num_samples) {
    if (num_samples < 2) return false;

    for (int axis = 0; axis < 3; axis++) {
        int32_t sum = 0;
        int16_t min_val = 32767;
        int16_t max_val = -32768;

        for (size_t i = 0; i < num_samples; i++) {
            int16_t val = buffer[i * 3 + axis];
            sum += val;
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
        }

        int16_t mean = sum / (int32_t)num_samples;
        int16_t p2p = max_val - min_val;

        int zcr = 0;
        bool last_above = (buffer[axis] > mean);
        
        for (size_t i = 1; i < num_samples; i++) {
            bool above = (buffer[i * 3 + axis] > mean);
            if (above != last_above) {
                zcr++;
                last_above = above;
            }
        }

        if (p2p > AMPLITUDE_THRESHOLD && zcr >= ZCR_MIN) {
            LOG_INF("Shake detected on axis %d! (p2p: %d, zcr: %d)", axis, p2p, zcr);
            return true;
        }
    }

    return false;
}

static K_SEM_DEFINE(shake_sem, 0, 1);

static void imu_trigger_handler(const struct device *dev, const struct sensor_trigger *trig) {
    uint8_t status1, status2;
    i2c_reg_read_byte_dt(&imu_i2c, 0x3A, &status1);
    i2c_reg_read_byte_dt(&imu_i2c, 0x3B, &status2);
    
    uint16_t num_words = status1 | ((status2 & 0x07) << 8);
    
    if (num_words >= FIFO_WATERMARK) {
        // burst read from FIFO_DATA_OUT_L (0x3E)
        int ret = i2c_burst_read_dt(&imu_i2c, 0x3E, (uint8_t*)fifo_buf, FIFO_WATERMARK * 2); 
        if (ret == 0) {
            // FIFO_WATERMARK is total words, divide by 3 for # of XYZ samples
            if (detect_shake_gesture(fifo_buf, FIFO_WATERMARK / 3)) {
                k_sem_give(&shake_sem);
            }
        } else {
            LOG_ERR("FIFO burst read failed: %d", ret);
        }
    } else {
        LOG_WRN("Interrupt fired but FIFO only has %u words", num_words);
    }
}

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
