#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include "conditional_underglow_split.h"

LOG_MODULE_REGISTER(cond_ug_peripheral, CONFIG_ZMK_LOG_LEVEL);

static struct bt_uuid_128 svc_uuid  = BT_UUID_INIT_128(COND_UG_SVC_UUID);
static struct bt_uuid_128 prof_uuid = BT_UUID_INIT_128(COND_UG_PROF_UUID);

static ssize_t on_profile_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len,
                                 uint16_t offset, uint8_t flags) {
    if (len != 1 || offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t idx = *(const uint8_t *)buf;
    LOG_DBG("received profile %d from central", idx);
    conditional_ug_set_profile(idx);
    return len;
}

BT_GATT_SERVICE_DEFINE(cond_ug_svc,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),
    BT_GATT_CHARACTERISTIC(&prof_uuid.uuid,
        BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL,
        on_profile_write,
        NULL),
);
