# GKD350H Ultra packaging

This folder tracks the GKD350H Ultra / ROCKNIX porting workspace.

Observed target facts from the first device dump:

- OS: ROCKNIX 20260613 community
- Kernel: Linux 6.1.118 aarch64
- Reported SoC/device name: RK3576S
- Userland: aarch64 glibc, dynamic loader `/usr/lib/ld-linux-aarch64.so.1`
- RAM: about 2 GB
- Display stack: Wayland/Sway on DRM card `/dev/dri/card0`
- Framebuffer: `/dev/fb0`
- Panel mode: `1440x1600p60`, presented as `1600x1440` landscape by rotation
- ROM/TF mount: `/storage/roms`
- Writable internal storage: `/storage`
- Main joypad: `/dev/input/event3`, `/dev/input/js0`, `gkd_atom_joypad`

## Display Probe

The screen test confirmed:

- Wayland/Sway output name: `DSI-1`
- Wayland logical rect: `1600x1440`
- Sway transform: `270`
- `wlr-randr` transform: `90`
- DRM connector: `DSI-1`, connector id `179`
- DRM mode: `1440x1600@60`
- DRM panel orientation: `Left Side Up`
- Framebuffer: `/dev/fb0`, `rockchipdrmfb`
- Framebuffer virtual size: `1440,1600`
- Framebuffer bpp/stride: `32 bpp`, `5760`

Prefer SDL2 on Wayland for the first ROCreader port. In that path the app
should see the already-rotated `1600x1440` landscape output. Direct fb/DRM
paths must handle the underlying `1440x1600` portrait buffer and rotation.

## Input Probe

Main controller:

```text
/dev/input/event3
/dev/input/js0
name: gkd_atom_joypad
bus/vendor/product/version: 0x19 / 0x1 / 0x2103 / 0x100
```

Observed event mapping and the ROCreader logical mapping used by the
`gkd350h-ultra` input profile:

| Linux control | Linux event code | ROCreader button | Notes |
| --- | --- | --- |
| D-pad up | `BTN_DPAD_UP` / `544` | Up | digital button |
| D-pad down | `BTN_DPAD_DOWN` / `545` | Down | digital button |
| D-pad left | `BTN_DPAD_LEFT` / `546` | Left | digital button |
| D-pad right | `BTN_DPAD_RIGHT` / `547` | Right | digital button |
| South face | `BTN_SOUTH` / `304` | B | SDL joy button `0` |
| East face | `BTN_EAST` / `305` | A | SDL joy button `1` |
| North face | `BTN_NORTH` / `307` | X | SDL joy button `2` |
| West face | `BTN_WEST` / `308` | Y | SDL joy button `3` |
| L1 | `BTN_TL` / `310` | L1 | |
| R1 | `BTN_TR` / `311` | R1 | |
| L2 | `BTN_TL2` / `312` | L2 | |
| R2 | `BTN_TR2` / `313` | R2 | |
| Select | `BTN_SELECT` / `314` | Select | pressed after Start in first probe |
| Start | `BTN_START` / `315` | Start | pressed before Select in first probe |
| Function/Menu | `BTN_MODE` / `316` | Menu | also exported as `DEVICE_FUNC_KEYB_MODIFIER` |
| Left stick click | `BTN_THUMBL` / `317` | unmapped | |
| Extra system key | `BTN_TRIGGER_HAPPY1` / `704` | unmapped | observed once |
| Left analog X | `ABS_X` / `0`, range about `-899..900` | Left/Right | |
| Left analog Y | `ABS_Y` / `1`, range about `-886..899` | Up/Down | |

The first probe exposed only `ABS_X` and `ABS_Y`; no right analog axes were
reported by `evtest` for `gkd_atom_joypad`.

Volume keys are on `gpio-keys`:

```text
/dev/input/event2
KEY_VOLUMEDOWN / 114
KEY_VOLUMEUP / 115
```

The `gkd350h-ultra` profile reads `ABS_X/ABS_Y` only from the
`gkd_atom_joypad` evdev device. Other evdev devices such as `gsensor` also
report absolute axes and must not be treated as navigation input. SDL
controller events are ignored for this profile to avoid duplicate
controller/joystick reports turning one physical key into a Start+Select
quit chord.

## UI Layout Notes

The reader/image modes remain fullscreen `1600x1440`. The settings layout
continues to use the 720x720-at-2x placement rules, while the shelf uses native
1600x1440 artwork geometry:

- Shelf covers: `314x471`, starting at `(74, 222)`
- Shelf grid step: `386px` horizontally and `541px` vertically
- Shelf selection/shadow frame: `394x551`
- Navigation content: four `300px` equal slots starting at `x=196`
- URL shelves: the same `1200px` navigation width divided by the active sub-tab count
- Left/right navigation icons: `(48, 104)` and `(1480, 104)`
- Dynamic battery body: `(1249, 26)`, `47x29`, with a `3px` outline and `4x10` terminal
- Dynamic battery percentage starts at `x=1308`; the clock is right-aligned to `x=1468`
- Dynamic avatar badge: `(1512, 8)`, `64x64`

Current 1600x1440 UI asset expectations:

| Asset role | Expected size |
| --- | --- |
| `background_main.png` | `1600x1440` |
| `top_status_bar.png` | `1600x80` |
| `bottom_hint_bar.png` | `1600x80` |
| Shelf cover/title assets | `314x471` cover/title, `394x551` frame |
| Settings preview images | `1120x1440`, paired with a 480px sidebar |

Suggested local workspace layout:

- `gkd350h_ultra_layout.h`: 1600x1440 layout metrics
- `gkd350h_ultra_profile.h`: model/profile constants and aliases
- `sysroot_device/`: synced from the target over SSH
- `dist_lowglibc/`: staged package output
- `Downloads/`: final release zip files
- `logs/`: build logs

The first build route should reuse the repository low-glibc flow with a target
sysroot:

```sh
DEVICE_HOST=root@192.168.31.12 ./GKD350HUltra/sync_sysroot.sh
./GKD350HUltra/prepare_headers_overlay.sh
./GKD350HUltra/build_low_glibc.sh
```

From PowerShell on Windows:

```powershell
.\GKD350HUltra\sync_sysroot.ps1 -DeviceHost root@192.168.31.12
.\GKD350HUltra\prepare_headers_overlay.ps1
.\GKD350HUltra\build_low_glibc.ps1
```

The GKD release ZIP uses the IUX external-app layout:

```text
app/
  ROCreader/
    config.json
    launch.sh
    rocreader.png
    rocreader_sdl
    books/
    book_covers/
roms/
  ports/
    ROCreader.sh  # ES entry forwarding to app/ROCreader
```

Extract it at the external card root. IUX discovers the application at
`/storage/games-external/app/ROCreader`. ES discovers the ports entry and
forwards to the same IUX runtime. The launcher also accepts legacy
`Roms/ports` archives for in-app update compatibility.

`rocreader.png` comes from `ui/common/icon.png`. Package verification ensures
that the selected source is the icon shipped in the ZIP. Its app-specific
filename is required because IUX resolves `icon.png` to the skin's generic
software icon before checking the external application directory.

GKD builds now require a real PDF backend. The observed ROCKNIX sysroot ships
`libpoppler.so.128` but not `libpoppler-cpp`, so the current test sysroot uses
the existing TrimuiBrick aarch64 Poppler-C++ overlay for:

- `usr/include/poppler`
- `libpoppler-cpp.so.0`
- `libpoppler.so.58`
- `libpng12.so.0`
- `libjbig.so.0`
- `libtiff.so.5`

Without that overlay, PDF files fall back to ROCreader's mock backend and render
as gray/white placeholder stripes.

The ROCKNIX image observed on GKD350H Ultra ships runtime libraries but no
`/usr/include`. `prepare_headers_overlay.*` copies the existing H700 aarch64
headers into the GKD sysroot so the first build can link against the GKD
runtime libraries while using compatible SDL/libzip/libcurl/ALSA headers.
