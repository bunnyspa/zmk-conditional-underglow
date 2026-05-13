#define DT_DRV_COMPAT zmk_conditional_underglow

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

/* "Central role" = not split, or split central. Peripheral lacks BLE profile
 * state and cannot broadcast via behavior bindings. */
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define CU_IS_CENTRAL 1
#else
#define CU_IS_CENTRAL 0
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/behavior.h>
#include <dt-bindings/zmk/rgb.h>
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
#include <zephyr/bluetooth/conn.h>
#endif

LOG_MODULE_REGISTER(conditional_underglow, CONFIG_ZMK_LOG_LEVEL);

#define ENTRIES_NODE  DT_INST_CHILD(0, entries)
#define OVERLAYS_NODE DT_INST_CHILD(0, overlays)

/* layers -> bitmask (zero if 'layers' absent) */
#define LAYER_BIT(node, prop, idx) | BIT(DT_PROP_BY_IDX(node, prop, idx))
#define LAYERS_MASK(node) (0                                                \
    COND_CODE_1(DT_NODE_HAS_PROP(node, layers),                             \
        (DT_FOREACH_PROP_ELEM(node, layers, LAYER_BIT)), ()))

/* BT profile state bits. A slot's actual state is exactly one of
 * {UNASSIGNED, DISCONNECTED, CONNECTED} ORed with optional ACTIVE. */
#define CU_STATE_UNASSIGNED   BIT(0)
#define CU_STATE_DISCONNECTED BIT(1)
#define CU_STATE_CONNECTED    BIT(2)
#define CU_STATE_ACTIVE       BIT(3)

#define STATE_BIT(node, prop, idx) \
    | UTIL_CAT(CU_STATE_, DT_STRING_UPPER_TOKEN_BY_IDX(node, prop, idx))
#define STATE_MASK(node) ((uint8_t)(0                                       \
    COND_CODE_1(DT_NODE_HAS_PROP(node, state),                              \
        (DT_FOREACH_PROP_ELEM(node, state, STATE_BIT)), ())))

#define STATE_NEEDS_PROFILE(node)                                           \
    BUILD_ASSERT(!DT_NODE_HAS_PROP(node, state)                             \
                 || DT_NODE_HAS_PROP(node, profile),                        \
                 "'state' requires 'profile' on conditional_underglow child");

/* Output endpoint bits. Exactly one is active at a time at runtime. */
#define CU_ENDPOINT_BLE BIT(0)
#define CU_ENDPOINT_USB BIT(1)

#define ENDPOINT_BIT(node, prop, idx) \
    | UTIL_CAT(CU_ENDPOINT_, DT_STRING_UPPER_TOKEN_BY_IDX(node, prop, idx))
#define ENDPOINT_MASK(node) ((uint8_t)(0                                    \
    COND_CODE_1(DT_NODE_HAS_PROP(node, endpoint),                           \
        (DT_FOREACH_PROP_ELEM(node, endpoint, ENDPOINT_BIT)), ())))

struct entry_desc {
    uint32_t layers_mask;
    bool     has_layers;
    bool     has_profile;
    uint8_t  profile;
    uint8_t  state_mask;
    uint8_t  endpoint_mask;
    uint16_t h;
    uint8_t  s;
    uint8_t  b;
    uint8_t  effect;
};

#define ENTRY_DESC(node) {                                                  \
    .layers_mask   = LAYERS_MASK(node),                                     \
    .has_layers    = DT_NODE_HAS_PROP(node, layers),                        \
    .has_profile   = DT_NODE_HAS_PROP(node, profile),                       \
    .profile       = DT_PROP_OR(node, profile, 0),                          \
    .state_mask    = STATE_MASK(node),                                      \
    .endpoint_mask = ENDPOINT_MASK(node),                                   \
    .h             = DT_PROP_BY_IDX(node, color, 0),                        \
    .s             = DT_PROP_BY_IDX(node, color, 1),                        \
    .b             = DT_PROP_BY_IDX(node, color, 2),                        \
    .effect        = DT_PROP_OR(node, effect, 0),                           \
},

#if DT_NODE_EXISTS(ENTRIES_NODE)
DT_FOREACH_CHILD_STATUS_OKAY(ENTRIES_NODE, STATE_NEEDS_PROFILE)
#endif

static const struct entry_desc entries[] = {
#if DT_NODE_EXISTS(ENTRIES_NODE)
    DT_FOREACH_CHILD_STATUS_OKAY(ENTRIES_NODE, ENTRY_DESC)
#endif
};

#define MAX_KPS_PER_OVERLAY 32

struct overlay_desc {
    uint32_t layers_mask;
    bool     has_layers;
    bool     has_profile;
    uint8_t  profile;
    uint8_t  state_mask;
    uint8_t  endpoint_mask;
    uint16_t h;
    uint8_t  s;
    uint8_t  b;
    uint8_t  kp_count;
    uint16_t kps[MAX_KPS_PER_OVERLAY];
};

#define KP_ELEM(node, prop, idx) DT_PROP_BY_IDX(node, prop, idx),

#define OVERLAY_DESC(node) {                                                \
    .layers_mask   = LAYERS_MASK(node),                                     \
    .has_layers    = DT_NODE_HAS_PROP(node, layers),                        \
    .has_profile   = DT_NODE_HAS_PROP(node, profile),                       \
    .profile       = DT_PROP_OR(node, profile, 0),                          \
    .state_mask    = STATE_MASK(node),                                      \
    .endpoint_mask = ENDPOINT_MASK(node),                                   \
    .h             = DT_PROP_BY_IDX(node, color, 0),                        \
    .s             = DT_PROP_BY_IDX(node, color, 1),                        \
    .b             = DT_PROP_BY_IDX(node, color, 2),                        \
    .kp_count      = DT_PROP_LEN(node, key_positions),                      \
    .kps           = { DT_FOREACH_PROP_ELEM(node, key_positions, KP_ELEM) },\
},

#define OVERLAY_ASSERTS(node)                                               \
    BUILD_ASSERT(!DT_NODE_HAS_PROP(node, effect),                           \
                 "overlays children cannot set 'effect' (solid-only)");     \
    BUILD_ASSERT(DT_PROP_LEN(node, key_positions) <= MAX_KPS_PER_OVERLAY,   \
                 "overlay key-positions exceeds MAX_KPS_PER_OVERLAY");

#if DT_NODE_EXISTS(OVERLAYS_NODE)
DT_FOREACH_CHILD_STATUS_OKAY(OVERLAYS_NODE, OVERLAY_ASSERTS)
DT_FOREACH_CHILD_STATUS_OKAY(OVERLAYS_NODE, STATE_NEEDS_PROFILE)
#endif

static const struct overlay_desc overlays[] = {
#if DT_NODE_EXISTS(OVERLAYS_NODE)
    DT_FOREACH_CHILD_STATUS_OKAY(OVERLAYS_NODE, OVERLAY_DESC)
#endif
};

#define N_ENTRIES   ARRAY_SIZE(entries)
#define N_OVERLAYS  ARRAY_SIZE(overlays)

#if DT_INST_PROP_HAS_IDX(0, led_map, 0)
static const uint16_t led_map[] = DT_INST_PROP(0, led_map);
#define LED_MAP_LEN ARRAY_SIZE(led_map)
#else
static const uint16_t led_map[] = { 0 };
#define LED_MAP_LEN 0
#endif

#define STRIP_NODE       DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

static const struct device *const strip __maybe_unused = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixel_buf[STRIP_NUM_PIXELS] __maybe_unused;

static bool owns_render = false;

/* HSB -> RGB. H 0-360, S 0-100, B 0-100. Mirrors ZMK upstream. */
static struct led_rgb hsb_to_rgb(uint16_t h, uint8_t s, uint8_t b) {
    float r = 0.0f, g = 0.0f, bl = 0.0f;
    uint8_t i = (h / 60) % 6;
    float v = b / 100.0f;
    float sat = s / 100.0f;
    float f = (h / 60.0f) - (h / 60);
    float p = v * (1.0f - sat);
    float q = v * (1.0f - f * sat);
    float t = v * (1.0f - (1.0f - f) * sat);
    switch (i) {
        case 0: r = v; g = t; bl = p; break;
        case 1: r = q; g = v; bl = p; break;
        case 2: r = p; g = v; bl = t; break;
        case 3: r = p; g = q; bl = v; break;
        case 4: r = t; g = p; bl = v; break;
        case 5: r = v; g = p; bl = q; break;
    }
    return (struct led_rgb){
        .r = (uint8_t)(r * 255),
        .g = (uint8_t)(g * 255),
        .b = (uint8_t)(bl * 255),
    };
}

static bool any_layer_active(uint32_t mask) {
    while (mask) {
        uint8_t i = __builtin_ctz(mask);
        if (zmk_keymap_layer_active(i)) return true;
        mask &= ~BIT(i);
    }
    return false;
}

/* Per-slot BT profile state cache + current endpoint. Central computes and
 * broadcasts via the cu_state_sync behavior (slot states in param1, endpoint
 * in param2); peripheral receives and stores. Both halves read from here in
 * selectors_match. */
#define CU_NUM_PROFILES 5
uint8_t cu_profile_states[CU_NUM_PROFILES];
uint8_t cu_current_endpoint = CU_ENDPOINT_BLE;

static bool selectors_match(bool has_layers, uint32_t layers_mask,
                            bool has_profile, uint8_t profile,
                            uint8_t state_mask, uint8_t endpoint_mask) {
    if (has_layers && !any_layer_active(layers_mask)) return false;
    if (endpoint_mask != 0 && !(endpoint_mask & cu_current_endpoint)) return false;
    if (!has_profile) return true;
    if (profile >= CU_NUM_PROFILES) return false;
    uint8_t state = cu_profile_states[profile];
    if (state_mask == 0) {
        /* Back-compat: profile-only = "active BLE profile". Requires BLE
         * endpoint (USB use shouldn't trigger profile entries). */
        if (!(cu_current_endpoint & CU_ENDPOINT_BLE)) return false;
        return (state & CU_STATE_ACTIVE) != 0;
    }
    return (state_mask & state) != 0;
}

static void apply_local(uint8_t eff, uint16_t h, uint8_t s, uint8_t b) {
    zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = h, .s = s, .b = b});
    zmk_rgb_underglow_select_effect(eff);
}

static void apply_global(uint8_t eff, uint16_t h, uint8_t s, uint8_t b) {
#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_SPLIT)
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

static void release_owned_render(void) {
    if (owns_render) {
        zmk_rgb_underglow_on();
        owns_render = false;
    }
}

static void enter_owned_render(void) {
    if (!owns_render) {
        zmk_rgb_underglow_off();
        owns_render = true;
    }
}

static int resolve_and_render(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    const struct entry_desc *bg = NULL;
    for (size_t i = 0; i < N_ENTRIES; i++) {
        if (selectors_match(entries[i].has_layers, entries[i].layers_mask,
                            entries[i].has_profile, entries[i].profile,
                            entries[i].state_mask, entries[i].endpoint_mask)) {
            bg = &entries[i];
        }
    }

    uint16_t bg_h;
    uint8_t  bg_s, bg_b, bg_eff;
    bool     bg_layer_scoped;
    if (bg) {
        bg_h = bg->h; bg_s = bg->s; bg_b = bg->b;
        bg_eff = bg->effect;
        bg_layer_scoped = bg->has_layers;
    } else {
        bg_h   = CONFIG_ZMK_RGB_UNDERGLOW_HUE_START;
        bg_s   = CONFIG_ZMK_RGB_UNDERGLOW_SAT_START;
        bg_b   = CONFIG_ZMK_RGB_UNDERGLOW_BRT_START;
        bg_eff = CONFIG_ZMK_RGB_UNDERGLOW_EFF_START;
        bg_layer_scoped = false;
    }

    size_t matched = 0;
    for (size_t i = 0; i < N_OVERLAYS; i++) {
        if (selectors_match(overlays[i].has_layers, overlays[i].layers_mask,
                            overlays[i].has_profile, overlays[i].profile,
                            overlays[i].state_mask, overlays[i].endpoint_mask)) {
            matched++;
        }
    }

    if (matched == 0) {
        release_owned_render();
#if CU_IS_CENTRAL
#if IS_ENABLED(CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY)
        if (bg_layer_scoped) {
            apply_local(bg_eff, bg_h, bg_s, bg_b);
        } else {
            apply_global(bg_eff, bg_h, bg_s, bg_b);
        }
#else
        apply_global(bg_eff, bg_h, bg_s, bg_b);
#endif
#else
        /* Peripheral: don't touch background. Central drives whole-strip
         * color via &rgb_ug split sync; touching it here would race. */
        ARG_UNUSED(bg_eff);
        ARG_UNUSED(bg_layer_scoped);
#endif
        return ZMK_EV_EVENT_BUBBLE;
    }

    enter_owned_render();
    struct led_rgb bg_rgb = hsb_to_rgb(bg_h, bg_s, bg_b);
    for (size_t i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixel_buf[i] = bg_rgb;
    }
    for (size_t i = 0; i < N_OVERLAYS; i++) {
        const struct overlay_desc *o = &overlays[i];
        if (!selectors_match(o->has_layers, o->layers_mask,
                             o->has_profile, o->profile,
                             o->state_mask, o->endpoint_mask)) continue;
        struct led_rgb c = hsb_to_rgb(o->h, o->s, o->b);
        for (size_t j = 0; j < o->kp_count; j++) {
            uint16_t kp = o->kps[j];
            if (kp >= LED_MAP_LEN) continue;
            uint16_t led = led_map[kp];
            if (led == 0xFFFF) continue;
            if (led >= STRIP_NUM_PIXELS) continue;
            pixel_buf[led] = c;
        }
    }
    led_strip_update_rgb(strip, pixel_buf, STRIP_NUM_PIXELS);
    return ZMK_EV_EVENT_BUBBLE;
}

/* All event paths (BT callbacks, ZMK events) funnel through this work item.
 * On central, the handler first refreshes the state cache from the live BLE
 * API and broadcasts it to peripheral via cu_state_sync, then renders.
 * On peripheral, the handler just renders against the latest cached state
 * that was pushed in by the cu_state_sync behavior handler. */
static struct k_work cu_resolve_work;

void cu_request_render(void) {
    k_work_submit(&cu_resolve_work);
}

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
static uint8_t compute_slot_state(uint8_t i) {
    uint8_t s;
    if (zmk_ble_profile_is_open(i)) {
        s = CU_STATE_UNASSIGNED;
    } else if (zmk_ble_profile_is_connected(i)) {
        s = CU_STATE_CONNECTED;
    } else {
        s = CU_STATE_DISCONNECTED;
    }
    if (i == zmk_ble_active_profile_index()) s |= CU_STATE_ACTIVE;
    return s;
}

static uint8_t compute_endpoint(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB)
               ? CU_ENDPOINT_USB
               : CU_ENDPOINT_BLE;
#else
    return CU_ENDPOINT_BLE;
#endif
}

static void push_state_to_peripheral(void) {
    uint32_t states_encoded = 0;
    for (int i = 0; i < CU_NUM_PROFILES; i++) {
        cu_profile_states[i] = compute_slot_state(i);
        states_encoded |= ((uint32_t)(cu_profile_states[i] & 0xF)) << (i * 4);
    }
    cu_current_endpoint = compute_endpoint();
    struct zmk_behavior_binding binding = {
        .behavior_dev = "cu_state_sync",
        .param1 = states_encoded,
        .param2 = cu_current_endpoint,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}
#endif

static void cu_resolve_work_handler(struct k_work *w) {
    ARG_UNUSED(w);
#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
    push_state_to_peripheral();
#endif
    resolve_and_render(NULL);
}

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
/* bt_conn_cb runs in the BT thread; defer to the system workqueue. */
static void cu_bt_connected(struct bt_conn *conn, uint8_t err) {
    ARG_UNUSED(conn);
    ARG_UNUSED(err);
    cu_request_render();
}

static void cu_bt_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    cu_request_render();
}

BT_CONN_CB_DEFINE(cu_bt_cb) = {
    .connected = cu_bt_connected,
    .disconnected = cu_bt_disconnected,
};
#endif

static int cu_event_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    cu_request_render();
    return ZMK_EV_EVENT_BUBBLE;
}

static int conditional_underglow_init(void) {
    k_work_init(&cu_resolve_work, cu_resolve_work_handler);
#if CU_IS_CENTRAL
    apply_local(CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
                CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
                CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
                CONFIG_ZMK_RGB_UNDERGLOW_BRT_START);
#if IS_ENABLED(CONFIG_ZMK_BLE)
    cu_current_endpoint = compute_endpoint();
#endif
    /* Initial profile-state push is deferred to the first
     * ble_active_profile_changed / endpoint_changed event — BLE may not be
     * fully up yet at SYS_INIT time. */
#endif
    return 0;
}
SYS_INIT(conditional_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZMK_LISTENER(conditional_underglow, cu_event_listener);
ZMK_SUBSCRIPTION(conditional_underglow, zmk_layer_state_changed);
#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_ble_active_profile_changed);
#endif
#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_endpoint_changed);
#endif
