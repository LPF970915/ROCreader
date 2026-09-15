# Device Model Mapping

This file is the source of truth for device model normalization, screen
resolution selection, and input mapping selection in ROCreader.

Update policy:

- Treat this file as long-lived project memory.
- Do not change model-to-keymap rules casually.
- Only update keymap assignments after the project owner confirms that a
  shipped mapping is wrong or a newly supported device needs to be added.
- New aliases may be added when they normalize to an existing canonical model
  without changing behavior.
- Runtime device detection must consider accepted aliases. Do not require the
  incoming device string to already equal a canonical model name.

## Decision Order

1. Normalize the incoming device/model string to a canonical model name.
2. Resolve the canonical model to a chip family.
3. Resolve the canonical model to:
   - screen resolution
   - input mapping
4. If the model is unknown:
   - default resolution: `640x480`
   - default input mapping: `H700Default`

## Input Mapping IDs

- `H700Default`: generic H700 mapping
- `H70034xxSp`: RG34XXSP dedicated mapping
- `TrimuiBrick`: Trimui Brick dedicated mapping
- `GKD350HUltra`: GKD350H Ultra dedicated mapping

## Chip Families

### H700

Rules:

- Only `rg34xx-sp` uses the dedicated `H70034xxSp` mapping.
- `rgcubexx` uses `720x720`.
- `rg34xx` and `rg34xx-sp` use `720x480`.
- All other known H700 models use `640x480`.
- All H700 models except `rg34xx-sp` use `H700Default`.

| Canonical model | Display name | Accepted aliases | Resolution | Input mapping |
| --- | --- | --- | --- | --- |
| `rgcubexx` | `RG CubeXX` | `rgcubexx`, `cubexx`, `rg cubexx`, `cube xx` | `720x720` | `H700Default` |
| `rg34xx` | `RG34XX` | `rg34xx`, `34xx`, `rg 34xx` | `720x480` | `H700Default` |
| `rg34xx-sp` | `RG34XXSP` | `rg34xxsp`, `34xxsp`, `rg34xx-sp`, `34xx-sp`, `rg 34xxsp` | `720x480` | `H70034xxSp` |
| `rg35xx-sp` | `RG35XXSP` | `rg35xxsp`, `35xxsp`, `rg35xx-sp`, `35xx-sp` | `640x480` | `H700Default` |
| `rg35xx-plus` | `RG35XX Plus` | `rg35xxplus`, `35xxplus`, `rg35xx plus`, `35xx plus` | `640x480` | `H700Default` |
| `rg35xx-2024` | `RG35XX 2024` | `rg35xx2024`, `35xx2024`, `rg35xx 2024`, `35xx 2024`, `rg35xx+`, `35xx+` | `640x480` | `H700Default` |
| `rg40xx-v` | `RG40XXV` | `rg40xxv`, `40xxv`, `rg40xx-v`, `40xx-v` | `640x480` | `H700Default` |
| `rg35xx-h` | `RG35XXH` | `rg35xxh`, `35xxh`, `rg35xx-h`, `35xx-h` | `640x480` | `H700Default` |
| `rg40xx-h` | `RG40XXH` | `rg40xxh`, `40xxh`, `rg40xx-h`, `40xx-h` | `640x480` | `H700Default` |
| `rg28xx` | `RG28XX` | `rg28xx`, `28xx`, `rg 28xx` | `640x480` | `H700Default` |
| `rg35xx-pro` | `RG35XXPRO` | `rg35xxpro`, `35xxpro`, `rg35xx-pro`, `35xx-pro`, `rg35xx pro` | `640x480` | `H700Default` |

### A133P

Rules:

- Only one A133P model is currently supported.
- It always uses the Trimui Brick dedicated mapping.

| Canonical model | Display name | Accepted aliases | Resolution | Input mapping |
| --- | --- | --- | --- | --- |
| `trimui-brick` | `Trimui Brick` | `trimuibrick`, `trimui brick`, `brick`, `tg3040` | `1024x768` | `TrimuiBrick` |

### RK3576S

Rules:

- GKD350H Ultra uses the 1600x1440 landscape Wayland profile.
- The underlying panel is 1440x1600 portrait, rotated by the compositor.

| Canonical model | Display name | Accepted aliases | Resolution | Input mapping |
| --- | --- | --- | --- | --- |
| `gkd350h-ultra` | `GKD350H Ultra` | `gkd350hultra`, `gkd350h`, `gkd350h-ultra`, `gkd ultra`, `gkd_atom`, `gamekiddy gkd atom` | `1600x1440` | `GKD350HUltra` |

### RK3568

Rules:

- RGDS uses two `640x480` panels with the RGDS dual-screen runtime.
- RGDS plus uses two `1024x768` panels with the same RGDS dual-screen runtime and input mapping.

| Canonical model | Display name | Accepted aliases | Resolution | Input mapping |
| --- | --- | --- | --- | --- |
| `rgds` | `RGDS` | `rgds` | `640x480` per panel | `RGDS` |
| `rgds-plus` | `RGDS plus` | `rgdsplus`, `rgds+`, `rgds plus`, `rgds-plus` | `1024x768` per panel | `RGDS` |

## Matching Guidance

- Do not make runtime matching depend on canonical names being present in raw
  device strings.
- Canonical names are internal normalized results, not strict external input
  requirements.
- Search and device classification should match against the accepted aliases
  table first, then normalize to the canonical model.
- Match longer, more specific aliases before shorter ones.
- Required precedence examples:
  - `rg34xx-sp` before `rg34xx`
  - `rg35xx-sp` before generic `rg35xx`
  - `rg35xx-plus` and `rg35xx-2024` before any broad `rg35xx` fallback
- Normalize input by:
  - lowercasing
  - removing spacing and punctuation where helpful
  - matching aliases against a compact canonical comparison form

## Defaults

Use these defaults when detection fails or the device is unknown:

- canonical family assumption: H700-style generic fallback
- resolution: `640x480`
- input mapping: `H700Default`

## Notes

- `RG35XX+` is treated as a historical alias for `rg35xx-2024`.
- If a future device shares a chip family but needs a different resolution or
  mapping, add it as a new canonical model instead of overloading an existing
  one.
