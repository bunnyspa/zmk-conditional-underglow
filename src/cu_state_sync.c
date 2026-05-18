#define DT_DRV_COMPAT zmk_behavior_conditional_underglow_state_sync

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_REGISTER(cu_state_sync, CONFIG_ZMK_LOG_LEVEL);

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

static int behavior_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    LOG_DBG("recv: p1=%08x p2=%08x", binding->param1, binding->param2);
    uint32_t e1 = binding->param1;
    for (int i = 0; i < CU_NUM_PROFILES; i++) {
        cu_profile_states[i] = (e1 >> (i * 4)) & 0xF;
    }
    cu_current_endpoint = (uint8_t)((e1 >> 20) & 0x3);
    cu_layer_mask = binding->param2;
    /* Peripheral needs to re-render against the new cache. On central the
     * work item that just invoked us is about to call resolve_and_render
     * itself; re-submitting from here would race needlessly. */
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
    /* Critical: ZMK reads locality from this struct field (NOT from any DT
     * property). Setting GLOBAL is what makes zmk_behavior_invoke_binding()
     * also enqueue the invocation onto the split RUN_BEHAVIOR GATT char so
     * peripheral receives the same param1/param2. */
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_init, NULL, NULL, NULL,
                       POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                       &driver_api);
