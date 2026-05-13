# zmk-conditional-underglow

A [ZMK](https://zmk.dev) module that drives the RGB underglow from the active
keymap layer and/or active Bluetooth profile.

Two kinds of entries are supported:

- **Whole-strip entries** (`entries`) — pick one background color+effect for the
  whole strip based on the current state. Uses ZMK's effect loop.
- **Per-LED overlays** (`overlays`) — paint individual LEDs on top of the
  background by key-position. Solid color only; the module takes over rendering
  while any overlay is active.

> **Breaking change.** The old flat `layer-entries` / `profile-entries`
> properties have been removed. Migrate to the new schema below.

---

## How resolution works

On every layer / BT profile / endpoint change the module runs two passes:

1. **Background pass.** Walk `entries` children in DT order; the **last** child
   whose selectors match wins. If nothing matches, the
   `CONFIG_ZMK_RGB_UNDERGLOW_*_START` values are used (no need to author a
   no-selector default).
2. **Overlay pass.** Walk `overlays` children in DT order; collect **every**
   match.

A child matches when:
- `layers` is absent OR at least one listed layer is active, AND
- `endpoint` is absent OR the active output endpoint is in the listed
  transports (`ble` / `usb`), AND
- `profile` is absent OR — if `state` is absent — the active endpoint is BLE
  on that profile (back-compat default) — OR — if `state` is present — the
  profile is in any listed state.

### Per-profile `state`

`state` is a `string-array` enum that lets you target specific BT slot states.
Requires `profile = <N>`. A slot's state is one of `unassigned` / `disconnected`
/ `connected`, plus the `active` flag if it's the currently selected profile.

| State | Meaning |
|---|---|
| `unassigned` | No peer bonded to this slot. |
| `disconnected` | Peer bonded, link is down. |
| `connected` | Peer bonded, link is up. |
| `active` | This slot is the currently selected profile (orthogonal to the above). |

```dts
// 5 indicator LEDs (kp 1-5), one per profile slot. Each slot shows the
// appropriate color for its current state.
overlays {
    p0_unassigned   { profile = <0>; state = "unassigned";   key-positions = <1>; color = <  0   0  5>; };
    p0_disconnected { profile = <0>; state = "disconnected"; key-positions = <1>; color = < 30 100 25>; };
    p0_connected    { profile = <0>; state = "connected";    key-positions = <1>; color = <120 100 25>; };
    p0_active       { profile = <0>; state = "active";       key-positions = <1>; color = <221 100 25>; };
    // ... and so on for profiles 1-4
};
```

To re-render on background (non-active-slot) connect/disconnect events, the
module registers a Zephyr `bt_conn_cb` via `BT_CONN_CB_DEFINE` and defers the
re-render to the system workqueue.

Then:
- **Overlay set empty** → background color+effect goes through ZMK's normal API,
  the effect loop runs as usual.
- **Overlay set non-empty** → the module stops ZMK's effect loop, fills the
  strip with the background color, then paints each matching overlay's LEDs
  (later overlays overwrite earlier ones on the same pixel), and pushes the
  pixel buffer once.

**Authoring rule:** put broader selectors earlier, more specific ones later —
in `entries` the last match wins; in `overlays` later writes win on contested
pixels.

---

## Split behavior

The module compiles on **both halves** and resolves overlays locally on each
side against its own `led-map`. Layer state is already split-synced by ZMK
(both halves see it); BT profile + endpoint state is central-only, so
profile/endpoint selectors only ever match on central.

| Situation | What central does | What peripheral does |
|---|---|---|
| Whole-strip entry, layer-scoped, `LAYER_CENTRAL_ONLY=y` | Apply locally | Resolves locally and applies (no sync needed) |
| Whole-strip entry, layer-scoped, default | Apply via `&rgb_ug` (syncs) | Receives sync; ALSO resolves layer locally — same color, harmless redundancy |
| Whole-strip entry, profile-scoped | Apply via `&rgb_ug` (syncs) | Receives sync. Cannot resolve profile locally → does nothing on the bg axis |
| Default fallback (`_START`) | Apply via `&rgb_ug` (syncs) | Receives sync |
| Overlay matches on this half's `led-map` | Owned render | Owned render |
| Overlay's kps all map to `0xFFFF` on this half | No overlay on this strip | No overlay on this strip |

Each half declares its own `led-map` so that key-positions on the other half
resolve to `0xFFFF` and are skipped. An overlay targeting `key-positions = <0 7>`
will paint kp 0 on the half whose `led-map` has a real index for kp 0, and
paint kp 7 on the half whose `led-map` has a real index for kp 7.

> **Effect sync caveat:** the `effect` cell on whole-strip entries is applied
> locally on central; peripheral retains its current effect.
>
> **Profile selectors on peripheral:** an overlay/entry with `profile = <N>`
> never matches on peripheral (it has no BT profile state). Use profile
> selectors only for kps on the central half's `led-map`.

---

## Getting started

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

# Optional (split keyboards): set to y to keep layer-scoped whole-strip colors
# on central only
# CONFIG_ZMK_CONDITIONAL_UNDERGLOW_LAYER_CENTRAL_ONLY=n
```

### `<keyboard>.overlay` (central half only on split)

```c
#include <zmk/conditional_underglow.dtsi>

&conditional_underglow {
    // Required when any overlay is defined.
    // Index by key-position (same numbering as ZMK combos);
    // value is the strip-LED index, or 0xFFFF for "no LED here".
    led-map = <
        0  1  2  3  4  5
        6  7  8  9 10 11
       12 13 14 15 16 17
       0xFFFF 0xFFFF 18 19 0xFFFF 0xFFFF
    >;

    entries {
        // Whole-strip per BT profile
        bt0 { profile = <0>; color = <  0   0 25>; };
        bt1 { profile = <1>; color = <120 100 25>; };
        bt2 { profile = <2>; color = <221 100 25>; };

        // Whole-strip per layer (later = higher priority)
        adjust { layers = <4>; color = <221 100 25>; };
        mouse  { layers = <5>; color = <359 100 25>; };
        scroll { layers = <6>; color = <359 100 25>; effect = <0>; };
    };

    overlays {
        // Highlight three thumb LEDs while MOUSE layer is held
        mouse-thumbs {
            layers = <5>;
            key-positions = <0 1 2>;
            color = <359 100 25>;
        };

        // BT-info LED on ADJUST: kp 7 mirrors the active BT profile color
        bt0-on-adjust { layers = <4>; profile = <0>; key-positions = <7>; color = <  0   0 25>; };
        bt1-on-adjust { layers = <4>; profile = <1>; key-positions = <7>; color = <120 100 25>; };
        bt2-on-adjust { layers = <4>; profile = <2>; key-positions = <7>; color = <221 100 25>; };
        bt3-on-adjust { layers = <4>; profile = <3>; key-positions = <7>; color = < 30 100 25>; };
        bt4-on-adjust { layers = <4>; profile = <4>; key-positions = <7>; color = <300 100 25>; };
    };
};
```

You can equivalently extend the predeclared labels:

```c
&cu_entries {
    bt0 { profile = <0>; color = <0 0 25>; };
};
&cu_overlays {
    mouse-thumbs { layers = <5>; key-positions = <0 1 2>; color = <359 100 25>; };
};
```

---

## Properties

### `conditional_underglow` (parent)

| Property | Type | Required | Notes |
|---|---|---|---|
| `led-map` | array of uints | when any overlay is defined | Indexed by key-position; value = strip-LED index or `0xFFFF` for "no LED". |

### `entries` children — whole-strip background

| Property | Type | Required | Notes |
|---|---|---|---|
| `layers` | array | no | Match if any listed layer is active. Omit = any layer. |
| `profile` | int | no | BT profile 0–4. Omit = any profile / USB. |
| `state` | string-array | no | Profile-state filter (requires `profile`). Values: `unassigned`, `disconnected`, `connected`, `active`. See above. |
| `endpoint` | string-array | no | Output-transport filter. Values: `ble`, `usb`. Omit = endpoint-agnostic. |
| `color` | `<H S B>` | **yes** | H 0–360, S 0–100, B 0–100. |
| `effect` | int | no | ZMK effect index. Default 0 (solid). See [ZMK lighting](https://zmk.dev/docs/config/lighting). |

### `overlays` children — per-LED overlay

| Property | Type | Required | Notes |
|---|---|---|---|
| `layers` | array | no | Same semantics as above. |
| `profile` | int | no | Same semantics as above. |
| `state` | string-array | no | Same semantics as above. |
| `endpoint` | string-array | no | Same semantics as above. |
| `key-positions` | array | **yes** | Key-position indices to paint, translated through `led-map`. |
| `color` | `<H S B>` | **yes** | Solid color (overlays are solid-only). |

`effect` is not allowed on overlay children — owned render bypasses ZMK's
effect loop. A `BUILD_ASSERT` flags it at compile time.

---

## Limitations

- Overlays are solid-only — no animations, no per-pixel effects.
- BT profile / endpoint selectors only match on central (peripheral has no
  profile state); use them only for kps that the central `led-map` resolves.
- Maximum 32 `key-positions` per overlay child (`MAX_KPS_PER_OVERLAY`).
