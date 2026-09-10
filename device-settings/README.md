# DeviceSettings (astonc)

Privileged `system_ext` extra-settings APK (`org.lineageos.device.settings`).
Shortcut from the Settings homepage. Not a rewrite of `packages/apps/Settings`.
Game Bar is an overlay, not a settings screen, and does not use this banner.

Compose UI lives under `src/org/lineageos/device/settings/ui/`. Screens share
`DeviceSettingsScaffold`. Tile-launched activities keep their class names.

## Banner

The collapsing header is a **device home**: rich art, no hero object, inventory
on a frosted plate. The 1+ mark is a watermark in the floor.

| Asset | Size | Role |
|---|---|---|
| `res/drawable/oplus_banner.png` | 1080×662 | Expanded art |
| `res/drawable/oplus_mask.png` | 1080×182 | Collapsed stamp (top strip) |
| `res/drawable/oplus_logo_glass.png` | 1254×1254 | Unused extra; do not wire |

After a recut, measure the PNGs and set `ART_W` / `ART_H` / `MASK_W` / `MASK_H`
(and `CORNER`, ~20 px on the 1080-wide cuts) in `DeviceSettingsScaffold.kt`.
Wrong constants squash `FillBounds` or kill collapse travel (`limit = 0`).
Write **nothing** on the collapsed strip.

Collapse top-aligns the expanded art under the status bar and clips from the
bottom. Status strip stays opaque `surfaceContainer`.

### Plate

Bottom-right. Left `0.345` of width, top `0.20` of height, **12 dp** right and
bottom. Black scrim `alpha = 0.2`, 16 dp blur, 10 dp inset.

Frost is a **full-size** copy of the same art (`FillBounds` on the expanded
banner, `FillWidth` on the mask strip), then clipped. Do not blur a crop that
has been scaled to the plate — that is not what is behind, and it looks off.

### Inventory

`LiveBanner` list: Device, RAM, Storage, Battery, Health. No bars, no
typewriter, no CPU/FPS/SoC temp (Game Bar owns live capture).

Expand (`expandedAlpha > 0.01`) rereads RAM (`/proc/meminfo`), `/data` storage,
and sticky `ACTION_BATTERY_CHANGED` (level + pack temp). Health is one ColorOS
SOH read (`ui_soh`, then `battery_soh`, then `charge_full / charge_full_design`).

Device identity: SKU from `ro.product.{odm,system,vendor}.device` (OP\*/CPH\*),
not `ro.product.device` aston/astonc. **OP5CF9L1 → Ace 3, anything else → 12R**.
SKU sits as the extra (left of the trailing value), same as battery temp.

### Back

40 dp circle, 24 dp arrow, 6 dp into the art (22 dp from the screen), 10 dp
from the top of the art. Same frost + scrim as the plate. Gesture / navbar
back still pops the nav host and finishes.

## Layout

Do not add a third local seekbar or switch inner screens to `SliderPreference`;
they already use `CustomSeekBar`. Do not edit `ax_compose` unless a shared
control is actually broken.
