#define DT_DRV_COMPAT zmk_behavior_conditional_underglow_state_sync

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define CU_IS_CENTRAL 1
#else
#define CU_IS_CENTRAL 0
#endif

#define CU_NUM_PROFILES 5

extern uint8_t  cu_profile_states[CU_NUM_PROFILES];
extern uint8_t  cu_current_endpoint;
extern uint32_t cu_layer_mask;
extern void cu_request_render(void);

static int behavior_cu_state_sync_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    uint32_t e1 = binding->param1;
    for (int i = 0; i < CU_NUM_PROFILES; i++) {
        cu_profile_states[i] = (e1 >> (i * 4)) & 0xF;
    }
    cu_current_endpoint = (uint8_t)((e1 >> 20) & 0x3);
    cu_layer_mask = binding->param2;
    /* Central already triggered its own render in the work handler that
     * invoked us, and re-submitting from here would cause work loops.
     * Peripheral receives this binding via split forwarding and is the
     * one that needs a render kick. */
#if !CU_IS_CENTRAL
    cu_request_render();
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    /* Forward to peripheral via split RUN_BEHAVIOR GATT char. ZMK reads
     * locality from this struct field, NOT from the DT `locality` property. */
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_cu_state_sync_init, NULL, NULL, NULL,
                       POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                       &driver_api);
