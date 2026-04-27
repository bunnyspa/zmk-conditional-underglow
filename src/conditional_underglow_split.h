#pragma once

#include <zephyr/types.h>

// UUIDs for the peripheral GATT service that receives the active profile index.
// Both split_peripheral.c and split_central.c must use these values.
#define COND_UG_SVC_UUID  BT_UUID_128_ENCODE(0xde97b3a0, 0x8c6f, 0x4f72, 0xb8e6, 0x4e1e5a7d2b90)
#define COND_UG_PROF_UUID BT_UUID_128_ENCODE(0xde97b3a1, 0x8c6f, 0x4f72, 0xb8e6, 0x4e1e5a7d2b90)

// Called by split_peripheral.c when the central writes a new profile index.
void conditional_ug_set_profile(uint8_t idx);
