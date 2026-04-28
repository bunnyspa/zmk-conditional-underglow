#define DT_DRV_COMPAT zmk_conditional_underglow

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/behavior.h>
#include <dt-bindings/zmk/rgb.h>
#endif

LOG_MODULE_REGISTER(conditional_underglow, CONFIG_ZMK_LOG_LEVEL);

#if DT_INST_NODE_HAS_PROP(0, layer_entries)
static const uint32_t layer_entries[] = DT_INST_PROP(0, layer_entries);
#define LAYER_ENTRIES_LEN ARRAY_SIZE(layer_entries)
BUILD_ASSERT(LAYER_ENTRIES_LEN % 5 == 0,
             "layer-entries must contain complete (layer effect H S B) tuples");
#else
static const uint32_t layer_entries[] = {};
#define LAYER_ENTRIES_LEN 0
#endif

#if DT_INST_NODE_HAS_PROP(0, profile_entries)
static const uint32_t profile_entries[] = DT_INST_PROP(0, profile_entries);
#define PROFILE_ENTRIES_LEN ARRAY_SIZE(profile_entries)
BUILD_ASSERT(PROFILE_ENTRIES_LEN % 5 == 0,
             "profile-entries must contain complete (profile effect H S B) tuples");
#else
static const uint32_t profile_entries[] = {};
#define PROFILE_ENTRIES_LEN 0
#endif

static void apply_local(uint32_t eff, uint32_t h, uint32_t s, uint32_t b) {
    zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = h, .s = s, .b = b});
    zmk_rgb_underglow_select_effect(eff);
}

/*
 * Invokes &rgb_ug RGB_COLOR_HSB on the behavior system so ZMK's split
 * forwarding propagates the color to the peripheral as well.
 * Effect is applied locally only; peripheral retains its current effect.
 * Falls back to apply_local on non-split builds.
 */
static void apply_global(uint32_t eff, uint32_t h, uint32_t s, uint32_t b) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    struct zmk_behavior_binding binding = {
        .behavior_dev = "rgb_ug",
        .param1 = RGB_COLOR_HSB_CMD,
        .param2 = RGB_COLOR_HSB_VAL(h, s, b),
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
    zmk_rgb_underglow_select_effect(eff);
#else
    apply_local(eff, h, s, b);
#endif
}

static void apply_default(void) {
    apply_global(CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
                 CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
                 CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
                 CONFIG_ZMK_RGB_UNDERGLOW_BRT_START);
}

static int conditional_underglow_listener(const zmk_event_t *eh) {
    for (int i = (LAYER_ENTRIES_LEN / 5 - 1) * 5; i >= 0; i -= 5) {
        if (zmk_keymap_layer_active(layer_entries[i])) {
            LOG_DBG("Layer %d active: eff=%d h=%d s=%d b=%d",
                    layer_entries[i], layer_entries[i + 1],
                    layer_entries[i + 2], layer_entries[i + 3], layer_entries[i + 4]);
#if IS_ENABLED(CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY)
            apply_local(layer_entries[i + 1], layer_entries[i + 2],
                        layer_entries[i + 3], layer_entries[i + 4]);
#else
            apply_global(layer_entries[i + 1], layer_entries[i + 2],
                         layer_entries[i + 3], layer_entries[i + 4]);
#endif
            return ZMK_EV_EVENT_BUBBLE;
        }
    }

#if IS_ENABLED(CONFIG_ZMK_BLE)
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
        apply_default();
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif
    int profile = zmk_ble_active_profile_index();
    for (int i = (PROFILE_ENTRIES_LEN / 5 - 1) * 5; i >= 0; i -= 5) {
        if ((int)profile_entries[i] == profile) {
            LOG_DBG("Profile %d active: eff=%d h=%d s=%d b=%d",
                    profile_entries[i], profile_entries[i + 1],
                    profile_entries[i + 2], profile_entries[i + 3], profile_entries[i + 4]);
            apply_global(profile_entries[i + 1], profile_entries[i + 2],
                         profile_entries[i + 3], profile_entries[i + 4]);
            return ZMK_EV_EVENT_BUBBLE;
        }
    }
#endif

    apply_default();
    return ZMK_EV_EVENT_BUBBLE;
}

static int conditional_underglow_init(void) {
    apply_local(CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
                CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
                CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
                CONFIG_ZMK_RGB_UNDERGLOW_BRT_START);
    return 0;
}
SYS_INIT(conditional_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZMK_LISTENER(conditional_underglow, conditional_underglow_listener);
ZMK_SUBSCRIPTION(conditional_underglow, zmk_layer_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_endpoint_changed);
#endif
