# ROCreader RGDS plus

RGDS plus is the RK3568 dual-screen variant of RGDS with two 1024x768 4:3
panels. It reuses the RGDS Weston/Wayland spanning runtime and RGDS input map.

The active layout is the existing `1024x768` profile used by Trimui Brick:

- each panel: `1024x768`
- shared Weston window: `2048x768`
- normal reader canvas: `1024x1536`
- rotated image spread: `2048x768`

Build the package with:

```powershell
powershell -ExecutionPolicy Bypass -File RGDSPlus\build_rgds_plus_official.ps1
```

The generated package is written to `RGDSPlus\Downloads\` and installs as
`ROCreader_RGDSPlus` under `Roms/APPS`.

## ver2.64 release

- Rebuild the RGDS plus package as ver2.64, retaining the display, TXT, audio
  and lid suspend fixes below.
- Automatic versioning starts at ver2.64 when no newer package exists. With
  ver2.64 in `Downloads`, the next build is ver2.65, then ver2.66.
- Docker failures stop packaging immediately instead of accepting a stale ZIP.
- Run `powershell -ExecutionPolicy Bypass -File tools\test_rgds_plus_release_version.ps1`
  to check version increments without building or deploying.

Validation: ARM rebuild, clean Windows build, lid/status/TXT regression tests,
version increment tests, ZIP CRC and AArch64 executable checks passed.
This release was not redeployed to the device for new physical acceptance tests.

### Online update revision

- The ver2.64 package uses its own update directory:
  `https://github.com/LPF970915/ROCreader/tree/main/RGDSPlus/Downloads`.
  The launcher sets this address and the updater also has a Plus-specific
  default. Only packages ending in `for RGDS plus.zip` are selected.
- Downloaded newer packages install on exit or the next start, retaining books,
  covers, settings, keymaps, sources and reading progress. Foreign pending
  packages are ignored; invalid package versions and installation failures
  are not reported as successful updates.
- This revision still reports ver2.64. An existing ver2.64 installation needs
  one manual replacement with this revised package; subsequent ver2.65 and
  newer packages can be installed online. Equal versions are not reinstalled.
- Test selection with `make rgds-plus-update-regression`. On an isolated Linux
  environment with `unzip`, run `python3 tools/test_rgds_plus_updater.py -v`
  to test installation, restart, data preservation and rejection paths.
  Set `ROCREADER_TEST_DOWNLOADS` to the release directory to include the
  actual ver2.64 ZIP installation test.

Validation: ARM and clean Windows builds, updater and existing regression tests,
all eight installer tests (including the actual ZIP), live GitHub directory
selection and a published-package download passed. No new on-device update
test was performed.

## ver2.01 fixes

- Use Brick's status-bar positions with the RGDS input map so battery,
  percentage, clock and avatar do not overlap.
- Keep TXT content margins during dual-screen canvas updates, including
  incremental loading. Old TXT layout/resume caches are rebuilt using the
  saved source position.

Run `make rgds-plus-regression` in the native SDL2/SDL2_ttf build environment.
It checks status-bar overlap, progressive TXT wrapping, cache replacement and
reading-position restoration, and writes BMP previews under `build/`.

## ver2.02 fixes

- Use the firmware ALSA and PulseAudio libraries with firmware SDL. The bundled
  H700 audio libraries use different ALSA plugin paths and PulseAudio versions,
  which prevented key sounds from opening the default audio device.
- Keep the firmware default output routing; no sound-server or global ALSA
  configuration changes are needed.

## ver2.03 fixes

- Poll the Plus firmware hall sensor even when the reader is idle. As in the
  installed dmenu, bit 0 clear means closed; wait 3 seconds before suspending.
- Use the firmware's RGdsplus kernel `mem` suspend path and shared power lock.
  Opening the lid wakes the hardware; the blocking call then returns and the
  reader restores input and rendering without waiting for another power press.
- Save active reading progress and pending settings before sleep. Failed sleep
  requests are retried with a delay; a wake while still closed is not a new close.
- The lid/idle setting must be enabled. Manual power-key sleep also uses this
  path, without invoking the firmware script's configurable shutdown actions.
- Original RGDS keeps its existing helper. The Plus package installs its own
  helper under the compatible `rgds_power_control.sh` runtime filename.

Firmware audit: `/mnt/vendor/bin/dmenu.bin` reads `hallkey` at 0x15670, masks
bit 0 at 0x15738, and schedules a 3000 ms close check at 0x2d244.
`/mnt/mod/ctrl/pwr_new.sh` implements `sleep_down_RGdsplus` with `/sys/power/state`.
The original helper incorrectly attempted writes to read-only DRM `dpms` nodes.

Validation: ARM package build, clean Windows build and `make rgds-plus-regression`
pass. Device checks covered native suspend with an RTC recovery alarm, an idle
reader receiving a simulated hall close, and an injected power-key event. Both
reader paths returned from suspend, resynced input and restored both DSI outputs
to `On`; the simulated closed value did not cause an immediate second suspend.
Physical lid-open wake and speaker audibility remain manual acceptance checks.
