# zmk-conditional-underglow

A [ZMK](https://zmk.dev) module that drives the RGB underglow from current keyboard state.

- Whole-strip and per-LED control
- Layer, Bluetooth profile, and USB/BLE endpoint triggers
- Split keyboard support

## Getting Started

### `config/west.yml`

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    # --- copy from here ---
    - name: bunnyspa
      url-base: https://github.com/bunnyspa
    # --- to here ---
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    # --- copy from here ---
    - name: zmk-conditional-underglow
      remote: bunnyspa
      revision: main
    # --- to here ---
  self:
    path: config
```

### `<keyboard>.conf`

```ini
CONFIG_ZMK_RGB_UNDERGLOW=y
CONFIG_ZMK_CONDITIONAL_UNDERGLOW=y

# Required — the module renders directly instead of ZMK's animation loop.
CONFIG_ZMK_RGB_UNDERGLOW_ON_START=n
# Required — keep the LED strip powered (otherwise colors look dim or wrong).
CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER=n

# Default color when no entry matches.
CONFIG_ZMK_RGB_UNDERGLOW_HUE_START=0
CONFIG_ZMK_RGB_UNDERGLOW_SAT_START=0
CONFIG_ZMK_RGB_UNDERGLOW_BRT_START=25
```

### `<keyboard>.overlay` or `<keyboard>.dtsi`

#### Unibody (e.g. [Keebio BDN9](https://keeb.io/products/bdn9-rev-3-pcb-3x3-9-key-macropad-rotary-encoder-and-rgb) — 3×3, 9 keys)

```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    /* key-position -> strip-LED index. */
    map = <
        0 1 2
        3 4 5
        6 7 8
    >;

    entries { ... };
    overlays { ... };
};
```

#### Split (e.g. [Corne v3-final](https://github.com/foostan/crkbd/tree/v3-final) — 3×6 + 3 thumbs per half, 42 keys)

Each half declares its own `map` in its overlay file. Key-positions belonging to the other half use `__` (alias for "no LED on this half").

Left half (`*_left.overlay`):
```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    map = <
        18 17 12 11  4  3 __ __ __ __ __ __
        19 16 13 10  5  2 __ __ __ __ __ __
        20 15 14  9  6  1 __ __ __ __ __ __
                  8  7  0 __ __ __
    >;
};
```

Right half (`*_right.overlay`):
```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    map = <
        __ __ __ __ __ __  3  4 11 12 17 18
        __ __ __ __ __ __  2  5 10 13 16 19
        __ __ __ __ __ __  1  6  9 14 15 20
                 __ __ __  0  7  8
    >;
};
```

`entries` and `overlays` are shared between halves — put them in a file included by both halves (e.g. a shared `.dtsi`), using the same syntax as the unibody example:

```c
&conditional_underglow {
    // No `map` here — each half defines its own above.

    entries { ... };
    overlays { ... };
};
```

#### Non-key LEDs

Some boards have LEDs on the strip that aren't tied to any key — e.g. the 6 underglow LEDs on a Corne v3. Address them with **extra key-position indices** — entries written past the last real key-position in `map`.

Example: extend the Corne v3 split above with 6 underglow LEDs per half. On Corne v3 the strip starts with the underglow LEDs (indices 0-5), so the per-key portion shifts up by 6:

```c
// left side example
&conditional_underglow {
    map = <
        // real key-positions 0-41 → per-key LEDs at strip indices 6-26
        24 23 18 17 10  9 __ __ __ __ __ __
        25 22 19 16 11  8 __ __ __ __ __ __
        26 21 20 15 12  7 __ __ __ __ __ __
                 14 13  6 __ __ __

        // extra key-positions 42-47 → underglow LEDs 0-5
        2 1 0
        3 4 5
    >;
};
```

Then address them by those indices in any overlay:

```c
overlays {
    // Light all 6 underglow LEDs red on layer 1.
    underglow_layer1 {
        layers = <1>;
        key-positions = <42 43 44 45 46 47>;
        color = <  0 100 25>;
    };
};
```

> [!NOTE]
> Extra key-positions are a local convention — they exist only as keys into your `map`. ZMK's keymap, combos, and other features don't see them.

## Properties

### `map`

Required when any overlay is defined. Flat array indexed by key-position; the value is the strip-LED index for that key, or `__` for "no LED on this half".

The array can be longer than the keymap — extra entries become **extra key-positions** that address LEDs not tied to any real key (e.g. underglow LEDs on a per-key board). See the split example above.

### `entries` (whole-strip background)

- **`color = <H S B>`** *(required)* — H 0–360, S 0–100, B 0–100.
- **`layers`** *(optional)* — match by active layer.
- **`profile`** *(optional)* — match by BT profile.
- **`state`** *(optional)* — match by BT slot state.
- **`endpoint`** *(optional)* — match by USB/BLE output.

See [Selectors](#selectors) below for details.

### `overlays` (per-LED)

- **`key-positions = <…>`** *(required)* — indices to paint, translated through `map`. Up to 32 per overlay.
- **`color = <H S B>`** *(required)* — solid color.
- **`layers`** *(optional)* — match by active layer.
- **`profile`** *(optional)* — match by BT profile.
- **`state`** *(optional)* — match by BT slot state.
- **`endpoint`** *(optional)* — match by USB/BLE output.

See [Selectors](#selectors) below for details.

### Resolution priority

Highest first:

1. `overlays` — every matching entry paints; later writes overwrite earlier ones on the same pixel.
2. `entries` — last matching entry wins as the background.
3. `_START` fallback — `CONFIG_ZMK_RGB_UNDERGLOW_*_START`, used when no entry matches.

Within `entries` and `overlays`, entries lower in the source take priority.

## Selectors

**`layers`** — array of layer indices. Matches if any listed layer is active.

**`profile`** — BT profile 0–4. Matches only the currently active BLE slot unless `state` is also set.

**`state`** — array of slot-state names. Requires `profile`. Each slot is in exactly one of:

| State | Meaning |
|---|---|
| `unassigned` | No peer bonded to this slot. |
| `disconnected` | Peer bonded, link is down. |
| `connected` | Peer bonded, link is up. |
| `active` | This is the currently selected slot (ORs with one of the above). |

`state` is OR-matched — `state = "disconnected", "connected"` matches if either bit is set.

**`endpoint`** — array of `"ble"` / `"usb"`. Matches when the active output is in the listed transports.

## Examples

### Whole-strip background per layer

```c
entries {
    layer1 { layers = <1>; color = <240 100 25>; };  // blue when layer 1 is active
    layer2 { layers = <2>; color = <  0 100 25>; };  // red when layer 2 is active
};
```

### Whole-strip background per BT profile

```c
entries {
    bt0 { profile = <0>; color = <120 100 25>; };  // green when BT 0 is active
    bt1 { profile = <1>; color = <240 100 25>; };  // blue when BT 1 is active
};
```

### Light key-position 0 with the active BT profile's color

```c
overlays {
    bt0 { profile = <0>; key-positions = <0>; color = <  0   0 25>; };  // white
    bt1 { profile = <1>; key-positions = <0>; color = <120 100 25>; };  // green
    bt2 { profile = <2>; key-positions = <0>; color = <240 100 25>; };  // blue
};
```

### Per-slot indicator LEDs

Five LEDs (key-positions 1–5), each showing its slot's bonding state, with the active slot lit green:

```c
overlays {
    p0_un  { profile = <0>; state = "unassigned";   key-positions = <1>; color = <  0   0  5>; };
    p0_dis { profile = <0>; state = "disconnected"; key-positions = <1>; color = <  0 100 25>; };
    p0_con { profile = <0>; state = "connected";    key-positions = <1>; color = < 60 100 25>; };
    p0_act { profile = <0>; state = "active";       key-positions = <1>; color = <120 100 25>; };
    // ... and so on for profiles 1–4
};
```

### USB endpoint indicator

```c
overlays {
    usb_active { endpoint = "usb"; key-positions = <17>; color = <120 100 25>; };
};
```

### Highlight thumb keys on a layer

```c
overlays {
    mouse_thumbs {
        layers = <5>;
        key-positions = <36 37 38>;
        color = <  0 100 25>;
    };
};
```

### Combined: layer background with BT profile indicator

Whole-strip background per layer, plus key-position 0 shows the active BT profile on layer 1:

```c
entries {
    layer1 { layers = <1>; color = <240 100 25>; };  // blue background on layer 1
    layer2 { layers = <2>; color = <  0 100 25>; };  // red background on layer 2
};

overlays {
    bt0_on_layer1 { layers = <1>; profile = <0>; key-positions = <0>; color = <  0   0 25>; };
    bt1_on_layer1 { layers = <1>; profile = <1>; key-positions = <0>; color = <120 100 25>; };
    bt2_on_layer1 { layers = <1>; profile = <2>; key-positions = <0>; color = < 60 100 25>; };
};
```

