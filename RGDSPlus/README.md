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
Like the H700 package, it includes an `Imgs` directory beside the launcher.
`Roms/APPS/Imgs/ROCreader_RGDSPlus.png` uses the same ROC logo as H700, named
to match `ROCreader_RGDSPlus.sh`. SD deployment and online updates install
this image without replacing other applications' images.

## ver2.68 release

- Fix continuous image-page clipping using actual page extents, not the viewport
  height. Pages shorter than the dual-screen canvas now join at their real end;
  all four rotations share the same clipping helper. ZIP, comic EPUB and PDF
  runtimes use this device-independent fix.
- Match ZIP/EPUB decoding to the runtime's 0.05 minimum render scale, avoiding
  larger textures than the dimensions used for scrolling.
- Bound shelf animation steps after idle waits or slow cover loads. Preserve
  the existing animation setting, including an explicit disabled preference.
- Accept CRLF and trailing whitespace in config values so Windows-edited
  boolean settings do not become false on Linux.
- Ship dedicated, LF-normalized release defaults with animations and lid sleep
  both enabled. Packaging no longer copies these defaults from the developer's
  live config. A fresh extraction is ready to use; online updates still preserve
  existing user settings.

History: `9a3ec48` (2026-07-27) fixed ZIP size probing and remains present.
`22ab47d` added a GKD page-end screen-jump workaround, but `886dd88` reverted
it later the same day. This release fixes geometry rather than restoring that
device-specific workaround.

Device audit: the installed ver2.67 binary matched the previous release SHA256,
but `native_config.ini` contained `animations=0`. A runtime/config backup was
created at `/mnt/sdcard/rocreader_flowfix_20260918_OYOChZ/`, and only that setting
was changed to `1`. The lid-sleep setting was also found disabled and restored
to `1` at the user's request. The ver2.67 ZIP was confirmed to contain CRLF
values (`animations=1\r`, `lid_close_screen_off=1\r`): the old Linux parser
compared them literally to `"1"` and would load/save them as disabled.
The firmware hall node and native suspend interface are unchanged. A guarded
12-second RTC wake test returned success and advanced the suspend-success count
from 3 to 4. Physical lid-close/lid-open acceptance remains manual.

Run `make image-flow-regression` for ZIP/EPUB scale and pixel tests across six
viewports (including 1024x1536), four rotations and short/equal/long pages.
The pre-fix implementation failed both the 0.05 scale test and short-page tail
pixel check. Native tests now pass, along with `make shelf-animation-regression`
(including long frames and config round-trips) and `make rgds-plus-regression`.
The shared PDF clipping path is compiled but does not yet have an end-to-end
PDF fixture in this regression.

Release validation: ARM build, ZIP CRC and all eight installer tests using the
final ver2.68 ZIP passed, including enabled defaults and LF-only config bytes.
The six-viewport image tests also passed with RGDS Plus fast prefetch enabled.
Installed on `192.168.31.116` through the pending-update installer; it reported
success and the executable SHA256 matched the package. All 43 book/cover hashes
and six config/progress hashes matched their pre-install values. Startup scanned
55 books and the application subsequently exited with code 0; both requested
switches remained `1` after exit.

## ver2.67 release

- Extend shared TXT font sizes with 28, 30, 32, 34 and 36, retaining the
  existing 18-26 options. Saved settings now restore all ten levels.
- Use the RGDSplus-specific dual-screen mapping title in the key guide.
- Retain the shelf animation, navigation clipping, Plus update address and logo.

Validation: native build and Plus regressions passed, including font-level
persistence, invalid-level boundaries and large-text layout in five profiles.
ARM build, ZIP CRC, AArch64 checks and all eight installer tests using the
actual ver2.67 ZIP passed. Deployed to `192.168.31.116` on 2026-09-17 through
the launcher's pending-update installer; it reported success. Installed
binary/launcher/logo hashes matched the package, all 43 user-file checksums
were preserved, and `animations=1` remained unchanged. Startup completed
with 16 books, two displays and the OpenGL ES 2 spanning renderer.
Physical font-size and key-guide appearance checks remain manual.

The complete ver2.66 backup is on the second card at
`/mnt/sdcard/rocreader_ver267_20260917_sjgDfo/backup_ver266.tar`.

## ver2.66 release

- Clip all shelf cards at the bottom edge of each display profile's navigation
  bar. Covers, shadows, selection frames and titles cannot draw into the
  navigation/status region while rows move.
- Apply the same boundary to local/online shelves, focus overlays and cached
  pages. Nested title marquee clips intersect and restore the outer clip;
  fully clipped titles are skipped.
- Keep ver2.65's accepted animation behavior unchanged. This shared renderer
  fix applies to all model builds, not only RGDS Plus.

Validation: five display profiles passed pixel checks throughout upward/downward
transitions, local/online navigation, cold/warm page caches, caller clipping and
offscreen render targets. Existing animation, Plus and updater regressions,
Windows/ARM builds, all eight installer tests using the actual ver2.66 ZIP,
ZIP CRC and AArch64 checks passed. Deployed ver2.66 to `192.168.31.116` after
connectivity recovered. The installer reported success, installed
binary/launcher/logo hashes matched, and all 43 user-file checksums were
preserved, including `animations=1`. Startup completed with 16 books and
the dual-screen renderer initialized. Physical navigation-clipping acceptance
remains pending.

The complete ver2.65 backup is on the second card at
`/mnt/sdcard/rocreader_ver266_20260917_1QuRzy/backup_ver265.tar`.

## ver2.65 release

- Keep cover focus scaling active during row changes instead of snapping both
  the old and new selections to their final sizes.
- Draw overlapping row windows only once and scroll by the actual row pitch.
  Normalize focus scaling speed to keep comparable timing across display sizes.
- Match ROCgalgame's 0.18-second cubic ease-out row transition timing. This is
  eased scrolling, not a spring/overshoot animation or a full frontend port.
- Keep the navigation clipping/mask unchanged pending physical device testing.
  The existing animation setting must be enabled to see these transitions.
- Retain the Plus-specific online update address, installer and launcher logo.

Run `make shelf-animation-regression` for navigation, row overlap, focus scaling,
disabled-animation and multi-resolution checks.
Set `ROCREADER_TEST_DOWNLOADS` to the release directory and
`ROCREADER_TEST_VERSION=ver2.65` when running the installer tests to check the
actual release ZIP as well.

Validation: ARM and clean Windows builds, five-resolution animation checks at
30/60 fps, existing Plus regressions, version increment tests, all eight
installer tests (including the actual ver2.65 ZIP), ZIP CRC and AArch64 checks
passed. Deployed ver2.65 to `192.168.31.116` after SSH connectivity recovered.
The installer reported success, installed binary/launcher/logo hashes matched,
and all 43 user-file checksums were preserved before enabling animations.
Only `animations=0` was changed to `animations=1`. Startup completed with
16 books and the dual-screen renderer initialized; physical animation and
navigation-overlap acceptance remain pending.

The complete ver2.64 backup is on the second card at
`/mnt/sdcard/rocreader_ver265_20260917_BjjrG1/backup_ver264.tar`.

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
