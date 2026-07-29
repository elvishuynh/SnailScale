#include "bluetooth.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bluetooth, CONFIG_LOG_DEFAULT_LEVEL);

// ble advertisement data flags and device name
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// scan response smp service uuid for nrf connect device manager
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

// log connection events
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed err %d", err);
	} else {
		LOG_INF("Connected");
	}
}

// log disconnection events
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected reason %d", reason);
}

// register callbacks automatically
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int bluetooth_init(void)
{
	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed err %d", err);
		return err;
	}
	LOG_INF("Bluetooth initialized");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start err %d", err);
		return err;
	}
	LOG_INF("Advertising as %s", CONFIG_BT_DEVICE_NAME);

	return 0;
}
