# zmk-conditional-underglow

A [ZMK](https://zmk.dev) module that drives the RGB underglow from current keyboard state.

- Whole-strip and per-LED control
- Layer, bluetooth profile, and USB/BLE endpoint triggers
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

# Required — the module owns the strip and must not race ZMK's effect tick.
CONFIG_ZMK_RGB_UNDERGLOW_ON_START=n
# Required — keep strip VCC up so colors don't brown out.
CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER=n

# Default color when no entry matches.
CONFIG_ZMK_RGB_UNDERGLOW_HUE_START=0
CONFIG_ZMK_RGB_UNDERGLOW_SAT_START=0
CONFIG_ZMK_RGB_UNDERGLOW_BRT_START=25
```

### `<keyboard>.overlay` or `<keyboard>.dtsi`

#### Unibody (e.g. Planck — 4×12, 48 keys)

```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    /* key-position -> strip-LED index. */
    map = <
         0  1  2  3  4  5  6  7  8  9 10 11
        12 13 14 15 16 17 18 19 20 21 22 23
        24 25 26 27 28 29 30 31 32 33 34 35
        36 37 38 39 40 41 42 43 44 45 46 47
    >;

    entries {
        // Whole-strip background per layer.
        layer1 { layers = <1>; color = <221 100 25>; };  // blue when layer 1 is active
        layer2 { layers = <2>; color = <  0 100 25>; };  // red when layer 2 is active
    };

    overlays {
        // Light kp 0 with the active BT profile's color.
        bt0 { profile = <0>; key-positions = <0>; color = <  0   0 25>; };
        bt1 { profile = <1>; key-positions = <0>; color = <120 100 25>; };
    };
};
```

#### Split (e.g. Corne — 3×6 + 3 thumbs per half, 42 keys)

Each half declares its own `map` in its own overlay file. Key-positions belonging to the other half use `__` (alias for "no LED on this half"), so they're silently skipped on this side.

Left half (`*_left.overlay`):
```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    map = <
         0  1  2  3  4  5 __ __ __ __ __ __
         6  7  8  9 10 11 __ __ __ __ __ __
        12 13 14 15 16 17 __ __ __ __ __ __
                 18 19 20 __ __ __
    >;
};
```

Right half (`*_right.overlay`):
```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    map = <
        __ __ __ __ __ __  0  1  2  3  4  5
        __ __ __ __ __ __  6  7  8  9 10 11
        __ __ __ __ __ __ 12 13 14 15 16 17
                 __ __ __ 18 19 20
    >;
};
```

`entries` and `overlays` are shared between halves — put them in a file included by both halves (e.g. a shared `.dtsi`), using the same syntax as the unibody example:

```c
&conditional_underglow {
    // No `map` here — each half defines its own above.

    entries {
        layer1 { layers = <1>; color = <221 100 25>; };
        layer2 { layers = <2>; color = <  0 100 25>; };
    };

    overlays {
        bt0 { profile = <0>; key-positions = <0>; color = <  0   0 25>; };
        bt1 { profile = <1>; key-positions = <0>; color = <120 100 25>; };
    };
};
```

## How it resolves

Two passes run on every layer / BT-profile / endpoint change:

1. **Background** — walk `entries` in DT order. The **last** matching child wins. If nothing matches, the strip falls back to `CONFIG_ZMK_RGB_UNDERGLOW_*_START`.
2. **Overlays** — walk `overlays` in DT order. **Every** match is painted on top of the background. Later writes overwrite earlier writes on the same pixel.

If no overlay matches, the whole strip is colored via ZMK's normal API and the effect loop runs. As soon as any overlay matches, the module takes over rendering directly (solid colors only — no animation).

**Authoring rule:** put broader selectors first, more specific ones later. In `entries` the last match wins; in `overlays` the last write wins on contested pixels.

## Selectors

All four selectors are optional; an omitted selector matches anything.

**`layers`** — array of layer indices. Matches if any listed layer is active.

**`profile`** — BT profile 0–4. Without an explicit `state`, this only matches when the keyboard is currently outputting via BLE to that slot. To target other slot states, add `state`.

**`state`** — string-array of slot-state names. Requires `profile`. Each slot is in exactly one of:

| State | Meaning |
|---|---|
| `unassigned` | No peer bonded to this slot. |
| `disconnected` | Peer bonded, link is down. |
| `connected` | Peer bonded, link is up. |
| `active` | This is the currently selected slot (ORs with one of the above). |

`state` is OR-matched — `state = "disconnected", "connected"` matches if either bit is set.

**`endpoint`** — string-array of `"ble"` / `"usb"`. Matches when the active output is in the listed transports.

## Properties

### `entries` (whole-strip background)

**`color = <H S B>`** *(required)* — H 0–360, S 0–100, B 0–100.

**`effect`** *(optional)* — ZMK effect index, default 0 (solid). See [ZMK lighting](https://zmk.dev/docs/config/lighting).

Plus any of the four selectors above.

### `overlays` (per-LED)

**`key-positions = <…>`** *(required)* — indices to paint, translated through the parent `map`. Up to 32 per overlay.

**`color = <H S B>`** *(required)* — solid color. `effect` is not allowed on overlays — the owned-render path bypasses ZMK's effect loop.

Plus any of the four selectors above.

### Parent `map`

Required when any overlay is defined. Flat array indexed by key-position; the value is the strip-LED index for that key, or `__` for "no LED on this half".

The array can be longer than the keymap — extra entries become synthetic kps that address LEDs not tied to any real key (e.g. underglow LEDs at the bottom of a per-key board). See [Addressing non-key LEDs](#addressing-non-key-leds) below.

## Split keyboards

The module runs on both halves. Central computes state (active layer, BT profile, endpoint) and broadcasts it to peripheral via the standard split RUN_BEHAVIOR channel. Both halves resolve overlays locally against the same state cache.

Each half declares its own `map`, so an overlay with `key-positions = <0 7>` paints kp 0 on whichever half has it, and kp 7 on whichever half has it.

> [!NOTE]
> The `effect` cell on whole-strip entries is applied locally on central; peripheral keeps its current effect.

## Examples

Per-slot indicator LEDs — five LEDs (kp 1–5), each shows its slot's bonding state, with the active slot lit green:

```c
overlays {
    p0_un  { profile = <0>; state = "unassigned";   key-positions = <1>; color = <  0   0  5>; };
    p0_dis { profile = <0>; state = "disconnected"; key-positions = <1>; color = <  0 100 25>; };
    p0_con { profile = <0>; state = "connected";    key-positions = <1>; color = < 60 100 25>; };
    p0_act { profile = <0>; state = "active";       key-positions = <1>; color = <120 100 25>; };
    // ... and so on for profiles 1–4
};
```

USB endpoint indicator:

```c
overlays {
    usb_active { endpoint = "usb"; key-positions = <17>; color = <120 100 25>; };
};
```

### Addressing non-key LEDs

Some boards have LEDs on the strip that aren't tied to any key — e.g. a per-key Corne with 21 per-key LEDs *plus* 6 underglow LEDs at the bottom edge of the PCB. Those underglow LEDs have no `&kp` position to reference.

Solution: extend the `map` past the last real kp into synthetic kp indices. The module just uses the array as a lookup table — it doesn't care whether an index corresponds to a real keymap position.

```c
&conditional_underglow {
    map = <
        // real key-positions (kp 0-41 on per-key Corne, 1 LED each)
         0  1  2  3  4  5 __ __ __ __ __ __
         6  7  8  9 10 11 __ __ __ __ __ __
        12 13 14 15 16 17 __ __ __ __ __ __
                 18 19 20 __ __ __

        // synthetic kps 42-47 → underglow strip LEDs 21-26
        21 22 23 24 25 26
    >;

    overlays {
        // Light all 6 underglow LEDs red on the MOUSE layer.
        underglow_mouse {
            layers = <5>;
            key-positions = <42 43 44 45 46 47>;
            color = <  0 100 25>;
        };
    };
};
```

> [!NOTE]
> Synthetic kps are a local convention — they exist only as keys into your `map`. ZMK's keymap, combos, and other features don't see them.

## Notes

- Overlays are solid-only — no animations, no per-pixel effects.
- BT profile / endpoint state is central-only on split keyboards; profile/endpoint selectors only fire for kps that the central half's `map` resolves.
