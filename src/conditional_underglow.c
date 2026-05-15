#define DT_DRV_COMPAT zmk_conditional_underglow

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

/* "Central role" = not split, or split central. */
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define CU_IS_CENTRAL 1
#else
#define CU_IS_CENTRAL 0
#endif

#if CU_IS_CENTRAL
#include <zmk/events/layer_state_changed.h>
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#if CU_IS_CENTRAL && IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/behavior.h>
#endif

LOG_MODULE_REGISTER(conditional_underglow, CONFIG_ZMK_LOG_LEVEL);

#define ENTRIES_NODE  DT_INST_CHILD(0, entries)
#define OVERLAYS_NODE DT_INST_CHILD(0, overlays)

/* layers -> bitmask (zero if 'layers' absent) */
#define LAYER_BIT(node, prop, idx) | BIT(DT_PROP_BY_IDX(node, prop, idx))
#define LAYERS_MASK(node) (0                                                \
    COND_CODE_1(DT_NODE_HAS_PROP(node, layers),                             \
        (DT_FOREACH_PROP_ELEM(node, layers, LAYER_BIT)), ()))

/* BT profile state bits. A slot is exactly one of {UNASSIGNED, DISCONNECTED,
 * CONNECTED} ORed with the optional ACTIVE flag. */
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

/* Output endpoint bits. */
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

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixel_buf[STRIP_NUM_PIXELS];

/* Shared state cache. Central computes; cu_state_sync (locality=GLOBAL on
 * the driver API) forwards the same bytes to peripheral via ZMK's split
 * behavior queue. Both halves read from here.
 *
 * Payload encoding:
 *   param1: bits[ 0..19] = 5 × 4-bit slot states (CU_STATE_*)
 *           bits[20..21] = endpoint mask        (CU_ENDPOINT_*)
 *   param2: 32-bit active-layer mask */
#define CU_NUM_PROFILES 5
uint8_t  cu_profile_states[CU_NUM_PROFILES];
uint8_t  cu_current_endpoint = CU_ENDPOINT_BLE;
uint32_t cu_layer_mask;

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
    return (mask & cu_layer_mask) != 0;
}

static bool selectors_match(bool has_layers, uint32_t layers_mask,
                            bool has_profile, uint8_t profile,
                            uint8_t state_mask, uint8_t endpoint_mask) {
    if (has_layers && !any_layer_active(layers_mask)) return false;
    if (endpoint_mask != 0 && !(endpoint_mask & cu_current_endpoint)) return false;
    if (!has_profile) return true;
    if (profile >= CU_NUM_PROFILES) return false;
    uint8_t state = cu_profile_states[profile];
    if (state_mask == 0) {
        /* Back-compat: profile-only = active BLE profile. */
        if (!(cu_current_endpoint & CU_ENDPOINT_BLE)) return false;
        return (state & CU_STATE_ACTIVE) != 0;
    }
    return (state_mask & state) != 0;
}

#if CU_IS_CENTRAL
static uint32_t compute_layer_mask(void) {
    uint32_t m = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (zmk_keymap_layer_active(i)) m |= BIT(i);
    }
    return m;
}

#if IS_ENABLED(CONFIG_ZMK_BLE)
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
#endif

static uint8_t compute_endpoint(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB)
               ? CU_ENDPOINT_USB
               : CU_ENDPOINT_BLE;
#else
    return CU_ENDPOINT_BLE;
#endif
}

static void push_state(void) {
    uint32_t e1 = 0;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    for (int i = 0; i < CU_NUM_PROFILES; i++) {
        cu_profile_states[i] = compute_slot_state(i);
        e1 |= ((uint32_t)(cu_profile_states[i] & 0xF)) << (i * 4);
    }
#endif
    cu_current_endpoint = compute_endpoint();
    e1 |= ((uint32_t)(cu_current_endpoint & 0x3)) << 20;
    cu_layer_mask = compute_layer_mask();

    struct zmk_behavior_binding binding = {
        .behavior_dev = "cu_state_sync",
        .param1 = e1,
        .param2 = cu_layer_mask,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}
#endif /* CU_IS_CENTRAL */

/* Pixel render. Runs on ZMK's lowprio workqueue — same queue ZMK's
 * underglow off-handler uses, so our paint is serialized after the
 * one-shot clear at init. */
static void render(void) {
    /* Resolve bg from entries (last match wins). */
    const struct entry_desc *bg = NULL;
    for (size_t i = 0; i < N_ENTRIES; i++) {
        if (selectors_match(entries[i].has_layers, entries[i].layers_mask,
                            entries[i].has_profile, entries[i].profile,
                            entries[i].state_mask, entries[i].endpoint_mask)) {
            bg = &entries[i];
        }
    }

    uint16_t bg_h;
    uint8_t  bg_s, bg_b;
    if (bg) {
        bg_h = bg->h; bg_s = bg->s; bg_b = bg->b;
    } else {
        bg_h = CONFIG_ZMK_RGB_UNDERGLOW_HUE_START;
        bg_s = CONFIG_ZMK_RGB_UNDERGLOW_SAT_START;
        bg_b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_START;
    }

    struct led_rgb bg_rgb = hsb_to_rgb(bg_h, bg_s, bg_b);
    for (size_t i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixel_buf[i] = bg_rgb;
    }

    /* Paint matching overlays. Later DT entries overwrite earlier ones on
     * the same pixel. kps mapped to 0xFFFF (other half / no LED) are
     * silently skipped. */
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
}

static struct k_work cu_render_work;

static void cu_render_work_handler(struct k_work *w) {
    ARG_UNUSED(w);
#if CU_IS_CENTRAL
    push_state();
#endif
    render();
}

void cu_request_render(void) {
    /* Lowprio workqueue: ZMK's `zmk_rgb_underglow_off` queues its strip-clear
     * handler here. Submitting our render to the same queue guarantees it
     * runs AFTER the clear (FIFO), so the clear never wipes our paint. */
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &cu_render_work);
}

#if CU_IS_CENTRAL
static int cu_event_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    cu_request_render();
    return ZMK_EV_EVENT_BUBBLE;
}
#endif

/* settings_load() runs in main() AFTER all SYS_INIT. If the saved underglow
 * state had state.on=true, ZMK's rgb_settings_set() will restart the effect
 * tick timer, undoing our SYS_INIT-time _off(). Schedule a delayed reclaim
 * to call _off() again well after main has had time to load settings. */
static struct k_work_delayable cu_reclaim_work;

static void cu_reclaim_handler(struct k_work *w) {
    ARG_UNUSED(w);
    zmk_rgb_underglow_off();
    cu_request_render();
}

static int conditional_underglow_init(void) {
    k_work_init(&cu_render_work, cu_render_work_handler);
    k_work_init_delayable(&cu_reclaim_work, cu_reclaim_handler);

    /* Take exclusive ownership of the strip. ZMK's effect-tick timer is
     * cancelled and a one-shot clear is queued on the lowprio workqueue;
     * our render (also lowprio) will run after it and paint our pixels. */
    zmk_rgb_underglow_off();

#if CU_IS_CENTRAL
    cu_layer_mask = compute_layer_mask();
    cu_current_endpoint = compute_endpoint();
#endif

    /* Initial paint. On central: render against current state. On
     * peripheral: render against the default-zero cache, which will produce
     * _START bg (no entry matches without a synced layer mask). Peripheral
     * is updated as soon as central pushes its first cu_state_sync. */
    cu_request_render();

    /* 500ms is comfortably past main's settings_subsys_init+settings_load. */
    k_work_schedule(&cu_reclaim_work, K_MSEC(500));
    return 0;
}
SYS_INIT(conditional_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* ZMK events fire only on central — peripheral lacks the keymap/BLE plumbing.
 * Peripheral updates via the cu_state_sync behavior handler, which calls
 * cu_request_render() after decoding the broadcast into the cache. */
#if CU_IS_CENTRAL
ZMK_LISTENER(conditional_underglow, cu_event_listener);
ZMK_SUBSCRIPTION(conditional_underglow, zmk_layer_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(conditional_underglow, zmk_endpoint_changed);
#endif
#endif
