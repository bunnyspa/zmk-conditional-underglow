#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints_types.h>
#include "conditional_underglow_split.h"

LOG_MODULE_REGISTER(cond_ug_central, CONFIG_ZMK_LOG_LEVEL);

#if CONFIG_ZMK_CONDITIONAL_UNDERGLOW_PERIPHERAL_COUNT > 0

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

static const struct bt_uuid_128 svc_uuid  = BT_UUID_INIT_128(COND_UG_SVC_UUID);
static const struct bt_uuid_128 prof_uuid = BT_UUID_INIT_128(COND_UG_PROF_UUID);

struct peripheral_slot {
    struct bt_conn *conn;
    uint16_t profile_handle;
    struct bt_gatt_discover_params disc_params;
    struct k_work_delayable discovery_work;
};

static struct peripheral_slot slots[CONFIG_ZMK_CONDITIONAL_UNDERGLOW_PERIPHERAL_COUNT];

static struct peripheral_slot *slot_for_conn(struct bt_conn *conn) {
    for (int i = 0; i < ARRAY_SIZE(slots); i++) {
        if (slots[i].conn == conn) {
            return &slots[i];
        }
    }
    return NULL;
}

static struct peripheral_slot *free_slot(void) {
    for (int i = 0; i < ARRAY_SIZE(slots); i++) {
        if (!slots[i].conn) {
            return &slots[i];
        }
    }
    return NULL;
}

static uint8_t disc_chrc_func(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               struct bt_gatt_discover_params *params) {
    if (!attr) {
        LOG_DBG("characteristic discovery done");
        return BT_GATT_ITER_STOP;
    }

    struct peripheral_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_chrc *chrc = attr->user_data;
    if (bt_uuid_cmp(chrc->uuid, &prof_uuid.uuid) == 0) {
        slot->profile_handle = chrc->value_handle;
        LOG_DBG("found profile handle 0x%04x", slot->profile_handle);
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t disc_svc_func(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              struct bt_gatt_discover_params *params) {
    if (!attr) {
        LOG_DBG("service not found on this peripheral");
        return BT_GATT_ITER_STOP;
    }

    struct peripheral_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_service_val *svc = attr->user_data;
    slot->disc_params.start_handle = attr->handle + 1;
    slot->disc_params.end_handle   = svc->end_handle;
    slot->disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;
    slot->disc_params.uuid         = NULL;
    slot->disc_params.func         = disc_chrc_func;

    int err = bt_gatt_discover(conn, &slot->disc_params);
    if (err) {
        LOG_ERR("characteristic discovery failed: %d", err);
    }

    return BT_GATT_ITER_STOP;
}

static void start_discovery(struct bt_conn *conn) {
    struct peripheral_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return;
    }

    slot->disc_params.uuid         = &svc_uuid.uuid;
    slot->disc_params.func         = disc_svc_func;
    slot->disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    slot->disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    slot->disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(conn, &slot->disc_params);
    if (err) {
        LOG_ERR("service discovery failed: %d", err);
    }
}

static void discovery_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct peripheral_slot *slot = CONTAINER_OF(dwork, struct peripheral_slot, discovery_work);

    if (slot->conn) {
        start_discovery(slot->conn);
    }
}

static void on_connected(struct bt_conn *conn, uint8_t conn_err) {
    if (conn_err) {
        return;
    }

    // Only discover on connections where we are the BLE central (i.e. to the split peripheral).
    // Connections from HID hosts (PC/phone) have us as the BLE peripheral role.
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    struct peripheral_slot *slot = free_slot();
    if (!slot) {
        LOG_WRN("no free peripheral slot");
        return;
    }

    slot->conn           = bt_conn_ref(conn);
    slot->profile_handle = 0;

    // Delay discovery to avoid competing with ZMK's own split GATT setup
    // which also starts immediately on connection.
    k_work_schedule(&slot->discovery_work, K_SECONDS(2));
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct peripheral_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return;
    }

    k_work_cancel_delayable(&slot->discovery_work);
    bt_conn_unref(slot->conn);
    slot->conn           = NULL;
    slot->profile_handle = 0;
}

BT_CONN_CB_DEFINE(cond_ug_conn_cb) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
};

static void sync_profile(uint8_t idx) {
    for (int i = 0; i < ARRAY_SIZE(slots); i++) {
        if (!slots[i].conn || !slots[i].profile_handle) {
            continue;
        }
        uint8_t buf = idx;
        int err = bt_gatt_write_without_response(slots[i].conn, slots[i].profile_handle,
                                                  &buf, sizeof(buf), false);
        if (err) {
            LOG_WRN("GATT write failed (slot %d): %d", i, err);
        }
    }
}

static int cond_ug_central_init(void) {
    for (int i = 0; i < ARRAY_SIZE(slots); i++) {
        k_work_init_delayable(&slots[i].discovery_work, discovery_work_handler);
    }
    return 0;
}
SYS_INIT(cond_ug_central_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif // CONFIG_ZMK_CONDITIONAL_UNDERGLOW_PERIPHERAL_COUNT > 0

static int endpoint_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (!ev || ev->endpoint.transport != ZMK_TRANSPORT_BLE) {
        return ZMK_EV_EVENT_BUBBLE;
    }

#if CONFIG_ZMK_CONDITIONAL_UNDERGLOW_PERIPHERAL_COUNT > 0
    sync_profile((uint8_t)ev->endpoint.ble.profile_index);
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cond_ug_central, endpoint_listener);
ZMK_SUBSCRIPTION(cond_ug_central, zmk_endpoint_changed);
