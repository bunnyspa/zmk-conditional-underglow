# zmk-conditional-underglow

A [ZMK](https://zmk.dev) module that changes RGB underglow color and effect based on the active keymap layer and/or active Bluetooth profile.

**Priority: layer > BT profile > default (`_START` config)**

**On split keyboards, runs on the central half only.** Peripheral sync is controlled by `CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY` (see below).

---

## Behavior by Situation

| Situation | `LAYER_CENTRAL_ONLY=y` central | `LAYER_CENTRAL_ONLY=y` peripheral | `LAYER_CENTRAL_ONLY=n` central | `LAYER_CENTRAL_ONLY=n` peripheral |
|-----------|-------------------------------|-----------------------------------|-------------------------------|-----------------------------------|
| Layer active | Layer color+effect | No change (retains last synced state) | Layer color+effect | Layer color+effect |
| Layer inactive + BT profile match | Profile color+effect | Profile color+effect | Profile color+effect | Profile color+effect |
| Layer inactive + no match | Default (`_START`) | Default (`_START`) | Default (`_START`) | Default (`_START`) |

BT profile changes and the default fallback always sync to **both halves** regardless of the flag.

> **Note on effect sync:** When syncing to peripheral (`LAYER_CENTRAL_ONLY=n`), only the HSB color is forwarded via ZMK's behavior invocation system. The effect index is applied locally on central; the peripheral retains its current effect. For typical use (effect=0 solid), this is not noticeable.

---

## Getting Started

### `config/west.yml`

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: bunnyspa
      url-base: https://github.com/bunnyspa
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-conditional-underglow
      remote: bunnyspa
      revision: main
  self:
    path: config
```

### `<keyboard>.conf`

```ini
CONFIG_ZMK_RGB_UNDERGLOW=y
CONFIG_ZMK_CONDITIONAL_UNDERGLOW=y

# Default state when nothing matches
CONFIG_ZMK_RGB_UNDERGLOW_EFF_START=0
CONFIG_ZMK_RGB_UNDERGLOW_HUE_START=0
CONFIG_ZMK_RGB_UNDERGLOW_SAT_START=0
CONFIG_ZMK_RGB_UNDERGLOW_BRT_START=25

# Optional (split keyboards only): set to y to keep layer colors on central only
# CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY=n
```

### `<keyboard>.overlay` (central half only on split)

```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    layer-entries = <
        // layer  eff    H    S   B
           4      0    221  100  25   // ADJUST: blue
           5      0    359  100  25   // MOUSE:  red
           6      0    359  100  25   // SCROLL: red
    >;
    profile-entries = <
        // profile  eff    H    S   B
           0        0      0    0  25   // BT 0: white
           1        0    120  100  25   // BT 1: green
           2        0    221  100  25   // BT 2: blue
           3        0     30  100  25   // BT 3: orange
           4        0    300  100  25   // BT 4: purple
    >;
};
```

---

## Parameters

**`layer-entries`** — flat array of `(layer effect H S B)` tuples.

**`profile-entries`** — flat array of `(profile effect H S B)` tuples. `profile` is the BT profile index (0–4).

- Entries are checked from last to first; the first match wins — list entries in ascending priority order.
- For effect indices see [ZMK lighting config](https://zmk.dev/docs/config/lighting).
- For spectrum/swirl effects, H/S/B are ignored.
