#define DT_DRV_COMPAT zmk_conditional_underglow

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/rgb_underglow.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints_types.h>
#if IS_ENABLED(CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY)
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#endif
#endif

LOG_MODULE_REGISTER(conditional_underglow, CONFIG_ZMK_LOG_LEVEL);

#if DT_INST_NODE_HAS_PROP(0, layer_entries)
static const uint32_t layer_entries[] = DT_INST_PROP(0, layer_entries);
#define LAYER_ENTRIES_LEN ARRAY_SIZE(layer_entries)
BUILD_ASSERT(LAYER_ENTRIES_LEN % 5 == 0,
             "layer-entries must contain complete (layer effect H S B) 5-tuples");
#else
static const uint32_t layer_entries[] = {};
#define LAYER_ENTRIES_LEN 0
#endif

#if DT_INST_NODE_HAS_PROP(0, profile_entries)
static const uint32_t profile_entries[] = DT_INST_PROP(0, profile_entries);
#define PROFILE_ENTRIES_LEN ARRAY_SIZE(profile_entries)
BUILD_ASSERT(PROFILE_ENTRIES_LEN % 5 == 0,
             "profile-entries must contain complete (profile effect H S B) 5-tuples");
#else
static const uint32_t profile_entries[] = {};
#define PROFILE_ENTRIES_LEN 0
#endif

// On central: updated by the endpoint_changed listener.
// On peripheral: never set to true — peripheral can't detect USB.
static bool is_usb = false;
static uint8_t active_profile = 0;

static void apply_state(uint32_t effect, uint32_t h, uint32_t s, uint32_t b) {
    zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = h, .s = s, .b = b});
    zmk_rgb_underglow_select_effect(effect);
}

static void apply_default(void) {
    apply_state(CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
                CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
                CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
                CONFIG_ZMK_RGB_UNDERGLOW_BRT_START);
}

static void apply_color(void) {
#if (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) && \
     IS_ENABLED(CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY)
    // Layer check — highest priority, central only.
    // Iterate last-to-first so higher-index entries win on ties.
    for (int i = (int)(LAYER_ENTRIES_LEN / 5 - 1) * 5; i >= 0; i -= 5) {
        if (zmk_keymap_layer_active(layer_entries[i])) {
            LOG_DBG("layer %d active → effect=%d h=%d s=%d b=%d",
                    layer_entries[i], layer_entries[i + 1],
                    layer_entries[i + 2], layer_entries[i + 3], layer_entries[i + 4]);
            apply_state(layer_entries[i + 1], layer_entries[i + 2],
                        layer_entries[i + 3], layer_entries[i + 4]);
            return;
        }
    }
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)
    if (is_usb) {
        apply_default();
        return;
    }
#endif

    // Profile check — central (BLE only) and peripheral.
    for (int i = 0; i < (int)PROFILE_ENTRIES_LEN; i += 5) {
        if (profile_entries[i] == active_profile) {
            LOG_DBG("profile %d → effect=%d h=%d s=%d b=%d",
                    active_profile, profile_entries[i + 1],
                    profile_entries[i + 2], profile_entries[i + 3], profile_entries[i + 4]);
            apply_state(profile_entries[i + 1], profile_entries[i + 2],
                        profile_entries[i + 3], profile_entries[i + 4]);
            return;
        }
    }

    apply_default();
}

// Called by split_peripheral.c when the central writes a new profile index over GATT.
void conditional_ug_set_profile(uint8_t idx) {
    active_profile = idx;
    LOG_DBG("profile set by central: %d", idx);
    apply_color();
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int endpoint_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->endpoint.transport == ZMK_TRANSPORT_USB) {
        is_usb = true;
        LOG_DBG("endpoint: USB");
    } else {
        is_usb = false;
        active_profile = (uint8_t)ev->endpoint.ble.profile_index;
        LOG_DBG("endpoint: BLE profile %d", active_profile);
    }

    apply_color();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cond_ug_endpoint, endpoint_listener);
ZMK_SUBSCRIPTION(cond_ug_endpoint, zmk_endpoint_changed);

#if (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) && \
     IS_ENABLED(CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY)

static int layer_listener(const zmk_event_t *eh) {
    apply_color();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cond_ug_layer, layer_listener);
ZMK_SUBSCRIPTION(cond_ug_layer, zmk_layer_state_changed);

#endif // layer listener
#endif // !split || central

static int conditional_underglow_init(void) {
    apply_color();
    return 0;
}
SYS_INIT(conditional_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
