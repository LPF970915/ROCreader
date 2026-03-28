# ROCreader Native (SDL2 / C++)

This folder is the standalone native ROCreader project for RG35XX/H700-class handheld systems.
The previous Python project layout is no longer required here.

## Goals

- Replace the Python + pygame display/input layer with native SDL2.
- Keep core business ideas (book scanning/state/input actions).
- Start with a minimal, stable loop that can run on handheld firmware.

## Current status

- SDL2 fullscreen window + render loop
- D-pad / ABXY / L1/L2/R1/R2 / Start / Select / Menu mapping
- Bookshelf scan from card roots:
  - card1 first: `.../ROCreader/books`
  - card2 fallback: `.../ROCreader/books`
  - Windows dev fallback: local `./books`
- Cover roots scan:
  - card1/card2: `.../ROCreader/book_covers`
  - Windows dev fallback: local `./book_covers`
- Cover resolve parity (manual cover + folder internal cover), with graceful fallback:
  - if `SDL2_image` exists: load `png/jpg/jpeg/webp/bmp`
  - otherwise fallback to `SDL_LoadBMP` and rectangle placeholders
- Reader state implemented (`boot/shelf/settings/reader`) with:
  - page/rotation/zoom/scroll persistence (`native_progress.tsv`)
  - long-press smooth scrolling and cross-page continuation
  - rotation-aware short/long key mapping
- Optional `SDL2_ttf` title rendering:
  - unfocused book title: truncated display with `...`
  - focused book title: horizontal marquee loop for full name
- Optional `SDL2_mixer` key SFX (`sounds/move|select|back|change.wav`, controlled by `native_config.ini` `audio=1`)
- Volume keys:
  - Windows preview maps keypad `+/-` to app SFX volume up/down
  - ARM/H700 builds prefer device/system volume via `amixer`, with app SFX volume as fallback
- Cover texture cache capped (LRU-style) for low-memory devices
- Launch-log output to stdout/stderr

## Build (native on device)

```sh
cd /Roms/APPS/ROCreader
chmod +x build_and_run.sh
./build_and_run.sh
```

## Preflight Check (recommended)

```sh
cd /Roms/APPS/ROCreader
chmod +x preflight_check.sh
./preflight_check.sh
```

Cross preflight (for Ubuntu/WSL cross toolchain):

```sh
cd /path/to/ROCreader
PRECHECK_MODE=cross CROSS_TOOL_PREFIX=arm-linux-gnueabihf ./preflight_check.sh
```

Notes:
- `build_and_run.sh` now defaults to `REQUIRE_MUPDF=1`.
- Build fails early if no real PDF backend is available (MuPDF/Fitz or Poppler), to prevent mock rendering from slipping into test builds.
- To allow mock backend explicitly: `REQUIRE_MUPDF=0 ./build_and_run.sh`
- Window mode on desktop testing:
  - default on x86/WSL: windowed
  - force windowed: `ROCREADER_WINDOWED=1 ./build/rocreader_sdl`
  - force fullscreen: `ROCREADER_FULLSCREEN=1 ./build/rocreader_sdl`

## Package to /Roms/APPS

```sh
cd /Roms/APPS/ROCreader
chmod +x package_to_apps.sh
./package_to_apps.sh
```

Outputs:
- `/Roms/APPS/ROCreader.sh`
- `/Roms/APPS/ROCreader/`

## Cross Compile + Package (finalized)

```sh
cd /path/to/ROCreader
chmod +x cross_compile_and_package.sh
CROSS_TOOL_PREFIX=arm-linux-gnueabihf REQUIRE_MUPDF=1 ./cross_compile_and_package.sh
```

Outputs:
- `dist_h700/APPS/ROCreader.sh`
- `dist_h700/APPS/ROCreader/rocreader_sdl`
- `dist_h700/ROCreader_APPS.tar.gz`

Then copy `dist_h700/APPS` contents to your SD card `/Roms/APPS`.

## Low-GLIBC Cross Build (recommended for 34xxSP)

When runtime shows errors like `GLIBC_2.38 not found`, build against a sysroot
synced from the device itself:

```sh
cd /path/to/ROCreader
chmod +x sync_device_sysroot.sh cross_compile_low_glibc.sh
DEVICE_HOST=root@192.168.31.141 ./sync_device_sysroot.sh
SYSROOT=/path/to/ROCreader/sysroot_device \
CROSS_TOOL_PREFIX=aarch64-linux-gnu \
REQUIRE_MUPDF=1 \
./cross_compile_low_glibc.sh
```

Output packages:

- `dist_lowglibc/ROCreader_APPS_lowglibc.tar.gz`
- `Downloads/ROC全能漫画阅读器verX.Y.zip`

Release zip rules:

- `Downloads` only stores final release `.zip` files
- staging files are generated under `dist_lowglibc/release_stage`
- zip root contains:
  - `Roms/APPS/Imgs/ROCreader.png`
  - `Roms/APPS/ROCreader.sh`
  - `Roms/APPS/ROCreader/`
- `Roms/APPS/ROCreader/` contains the full runtime payload:
  - `rocreader_sdl`
  - `ui.pack`
  - `native_config.ini`
  - `native_keymap.ini`
  - `fonts/`
  - `sounds/`
  - `lib/`
  - `lib_system_sdl/`
  - empty `books/`, `book_covers/`, `cache/` directories
- zip version is auto-incremented from the latest existing `ROC全能漫画阅读器verX.Y.zip`

## Runtime Crash Logs (on device)

Launcher writes runtime diagnostics to:

- `/Roms/APPS/ROCreader.log`

What it logs:
- `ldd` output (if available) to detect missing `.so`
- attempted `SDL_VIDEODRIVER` backends and exit code per backend
- basic environment/path info

Common signs:
- `not found` in `ldd` output: missing dependency library
- all drivers failed: likely SDL video backend mismatch on firmware

## Build (manual)

```sh
cd /Roms/APPS/ROCreader
make print-config REQUIRE_MUPDF=1
make REQUIRE_MUPDF=1
./build/rocreader_sdl
```

## Cross compile notes

Use your own toolchain by overriding variables:

```sh
make CXX=arm-linux-gnueabihf-g++ \
     PKG_CONFIG=arm-linux-gnueabihf-pkg-config
```

Equivalent for finalized script:

```sh
CROSS_CXX=arm-linux-gnueabihf-g++ \
CROSS_PKG_CONFIG=arm-linux-gnueabihf-pkg-config \
./cross_compile_and_package.sh
```

If `pkg-config sdl2` is unavailable, pass flags manually:

```sh
make SDL_CFLAGS="-I/path/to/sdl2/include" SDL_LIBS="-L/path/to/lib -lSDL2"
```

## Controls (default)

- Shelf:
  - D-pad: move focus
  - A: enter folder / open book
  - B: back to root shelf (when in folder)
  - Menu: open sidebar settings
- Reader:
  - Short press: page turn (rotation aware)
  - Long press: directional smooth scroll (rotation aware)
  - L1/R1: zoom out/in
  - L2/R2: rotate -90/+90
  - A: reset to fit in current orientation
  - B: exit reader and save progress
  - Menu: open sidebar settings
- Global:
  - Start + Select: exit app
  - Volume +/-: adjust volume

## Dev model

- Root Python app (`launcher.py`, `core/*`) remains the primary feature playground.
- The native SDL2/C++ code in this root folder is the active H700 project.
- Keep behavior/perf notes in `H700_SDL2_MIGRATION.md` and `PERF_OPTIMIZATION_CHECKLIST.md`.

## Next steps

- Add PDF first-page cover extraction fallback path
- Add on-device perf trace counters (frame time / render spikes) for tuning
