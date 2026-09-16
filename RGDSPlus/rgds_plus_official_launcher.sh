#!/bin/sh
set -u

script_dir() {
  case "$0" in
    */*) dir=${0%/*} ;;
    *) dir=. ;;
  esac
  cd "$dir" 2>/dev/null && pwd -P || pwd
}

SELF_DIR=$(script_dir)
LAUNCHER_PATH="$SELF_DIR/ROCreader_RGDSPlus.sh"
APP_DIR="$SELF_DIR/ROCreader_RGDSPlus"
BIN="$APP_DIR/rocreader_sdl"
LOG_FILE="$SELF_DIR/ROCreader_RGDSPlus_latest.log"
LIB_FULL_DIR="$APP_DIR/lib"
LIB_SYSTEM_SDL_DIR="$APP_DIR/lib_system_sdl"
UPDATE_STATUS_FILE="$APP_DIR/cache/update_boot_status.txt"
UPDATE_STAGE_DIR="$APP_DIR/cache/update_stage"
INSTALLED_VERSION_FILE="$APP_DIR/version.txt"

: > "$LOG_FILE" 2>/dev/null || true

log() {
  printf '%s\n' "$*"
  printf '%s\n' "$*" >> "$LOG_FILE" 2>/dev/null || true
}

write_update_status() {
  result="$1"
  version="${2:-}"
  mkdir -p "$(dirname "$UPDATE_STATUS_FILE")" 2>/dev/null || true
  {
    printf 'result=%s\n' "$result"
    [ -n "$version" ] && printf 'version=%s\n' "$version"
  } > "$UPDATE_STATUS_FILE"
}

read_installed_version() {
  [ -f "$INSTALLED_VERSION_FILE" ] || return 0
  sed -n '1p' "$INSTALLED_VERSION_FILE"
}

extract_marker_value() {
  key="$1"
  marker="$2"
  awk -F= -v wanted="$key" '$1 == wanted { print substr($0, index($0, "=") + 1); exit }' "$marker"
}

extract_version_from_name() {
  file_name="$(basename "$1")"
  printf '%s\n' "$file_name" | sed -n 's/.*\(ver[0-9][0-9.]*\).*for RGDS plus[.]zip$/\1/p'
}

version_sort_key() {
  version="$1"
  printf '%s\n' "$version" | awk '
    BEGIN { count = 0 }
    {
      n = split($0, parts, /[^0-9]+/)
      for (i = 1; i <= n; ++i) {
        if (parts[i] != "") {
          printf "%06d", parts[i]
          count++
        }
      }
    }
    END {
      if (count == 0) printf "000000"
    }'
}

version_is_newer() {
  candidate="$1"
  baseline="$2"
  [ "$(version_sort_key "$candidate")" \> "$(version_sort_key "$baseline")" ]
}

find_pending_marker() {
  for root in /mnt/mmc /mnt/sdcard /mnt/SDCARD "$APP_DIR"; do
    marker="$root/Downloads/ROCreader_update_pending.txt"
    if [ -f "$marker" ]; then
      package_name="$(extract_marker_value filename "$marker")"
      case "$package_name" in */*|*\\*) continue ;; esac
      [ -n "$(extract_version_from_name "$package_name")" ] || continue
      printf '%s' "$marker"
      return 0
    fi
  done
  return 1
}

find_latest_download_zip() {
  best_path=""
  best_version=""
  for root in /mnt/mmc /mnt/sdcard /mnt/SDCARD "$APP_DIR"; do
    downloads_dir="$root/Downloads"
    [ -d "$downloads_dir" ] || continue
    for zip_file in "$downloads_dir"/*.zip; do
      [ -f "$zip_file" ] || continue
      zip_version="$(extract_version_from_name "$zip_file")"
      [ -n "$zip_version" ] || continue
      if [ -z "$best_path" ] || version_is_newer "$zip_version" "$best_version"; then
        best_path="$zip_file"
        best_version="$zip_version"
      fi
    done
  done
  [ -n "$best_path" ] && printf '%s\n' "$best_path"
}

extract_zip_to_stage() {
  zip_file="$1"
  stage_dir="$2"
  rm -rf "$stage_dir"
  mkdir -p "$stage_dir"
  if command -v unzip >/dev/null 2>&1; then
    unzip -oq "$zip_file" -d "$stage_dir" >> "$LOG_FILE" 2>&1
    return $?
  fi
  if command -v busybox >/dev/null 2>&1; then
    busybox unzip -o "$zip_file" -d "$stage_dir" >> "$LOG_FILE" 2>&1
    return $?
  fi
  return 127
}

replace_runtime_entry() {
  name="$1"
  src="$2/$name"
  dst="$APP_DIR/$name"
  [ -e "$src" ] || return 0
  incoming="$dst.update-new"
  previous="$dst.update-old"
  rm -rf "$incoming" || return 1
  if ! cp -a "$src" "$incoming"; then
    rm -rf "$incoming"
    return 1
  fi
  rm -rf "$previous" || return 1
  if [ -e "$dst" ] && ! mv "$dst" "$previous"; then
    return 1
  fi
  if ! mv "$incoming" "$dst"; then
    [ ! -e "$previous" ] || mv "$previous" "$dst"
    return 1
  fi
  rm -rf "$previous"
}

find_staged_runtime_dir() {
  stage_dir="$1"
  for candidate in \
    "$stage_dir/Roms/APPS/ROCreader_RGDSPlus" \
    "$stage_dir/APPS/ROCreader_RGDSPlus" \
    "$stage_dir/ROCreader_RGDSPlus"; do
    [ -d "$candidate" ] && { printf '%s' "$candidate"; return 0; }
  done
  return 1
}

clear_pending_update_markers() {
  for root in /mnt/mmc /mnt/sdcard /mnt/SDCARD "$APP_DIR"; do
    marker_file="$root/Downloads/ROCreader_update_pending.txt"
    [ -f "$marker_file" ] || continue
    marker_name="$(extract_marker_value filename "$marker_file")"
    [ -n "$(extract_version_from_name "$marker_name")" ] || continue
    rm -f "$marker_file" 2>/dev/null || true
  done
}

perform_pending_update_if_any() {
  marker="$(find_pending_marker || true)"
  installed_version="$(read_installed_version || true)"
  package_path=""
  package_version=""
  latest_zip="$(find_latest_download_zip || true)"
  latest_version=""
  if [ -n "$latest_zip" ]; then
    latest_version="$(extract_version_from_name "$latest_zip")"
  fi
  if [ -n "$latest_zip" ] && [ -n "$latest_version" ]; then
    if [ -z "$installed_version" ] || version_is_newer "$latest_version" "$installed_version"; then
      package_path="$latest_zip"
      package_version="$latest_version"
    fi
  fi
  if [ -z "$package_path" ] && [ -n "$marker" ]; then
    package_dir="$(dirname "$marker")"
    package_name="$(extract_marker_value filename "$marker")"
    package_version="$(extract_marker_value version "$marker")"
    package_path="$package_dir/$package_name"
    [ -n "$package_version" ] || package_version="$(extract_version_from_name "$package_path")"
    if [ -n "$package_version" ] && [ -n "$installed_version" ] && ! version_is_newer "$package_version" "$installed_version"; then
      log "[update] stale pending marker version=$package_version installed=$installed_version; clearing"
      clear_pending_update_markers
      return 0
    fi
  fi
  [ -n "$package_path" ] || return 0

  log "[update] pending marker: ${marker:-none}"
  log "[update] installed version: ${installed_version:-unknown}"
  log "[update] latest zip: ${latest_zip:-none}"
  log "[update] package: $package_path"
  log "[update] package version: ${package_version:-unknown}"

  if [ ! -f "$package_path" ]; then
    log "[update] missing package, skip install"
    write_update_status "failed" "$package_version"
    return 1
  fi
  if ! extract_zip_to_stage "$package_path" "$UPDATE_STAGE_DIR"; then
    log "[update] extract failed"
    write_update_status "failed" "$package_version"
    return 1
  fi
  staged_runtime="$(find_staged_runtime_dir "$UPDATE_STAGE_DIR" || true)"
  if [ ! -f "$staged_runtime/rocreader_sdl" ]; then
    log "[update] staged runtime missing under: $UPDATE_STAGE_DIR"
    write_update_status "failed" "$package_version"
    rm -rf "$UPDATE_STAGE_DIR"
    return 1
  fi
  staged_version="$(sed -n '1p' "$staged_runtime/version.txt" 2>/dev/null || true)"
  if [ -z "$staged_version" ] || [ "$staged_version" != "$package_version" ]; then
    log "[update] package version mismatch"
    write_update_status "failed" "$package_version"
    return 1
  fi
  for entry in rocreader_sdl ui.pack fonts sounds lib lib_system_sdl rgds_power_control.sh; do
    if ! replace_runtime_entry "$entry" "$staged_runtime"; then
      log "[update] failed to install $entry"
      write_update_status "failed" "$package_version"
      return 1
    fi
  done

  staged_launcher="$UPDATE_STAGE_DIR/Roms/APPS/ROCreader_RGDSPlus.sh"
  if [ -f "$staged_launcher" ]; then
    if ! cp "$staged_launcher" "$LAUNCHER_PATH.new" ||
       ! mv "$LAUNCHER_PATH.new" "$LAUNCHER_PATH"; then
      write_update_status "failed" "$package_version"
      return 1
    fi
  fi

  if ! printf '%s\n' "$package_version" > "$APP_DIR/version.txt"; then
    write_update_status "failed" "$package_version"
    return 1
  fi
  chmod +x "$APP_DIR/rocreader_sdl" "$APP_DIR/rgds_power_control.sh" "$LAUNCHER_PATH" 2>/dev/null || true
  clear_pending_update_markers
  rm -f "$package_path" 2>/dev/null || true
  rm -rf "$UPDATE_STAGE_DIR"
  write_update_status "success" "$package_version"
  log "[update] install success version=${package_version:-unknown}"
  return 0
}

pending_update_available() {
  marker="$(find_pending_marker || true)"
  installed_version="$(read_installed_version || true)"
  latest_zip="$(find_latest_download_zip || true)"
  latest_version=""
  if [ -n "$latest_zip" ]; then
    latest_version="$(extract_version_from_name "$latest_zip")"
  fi
  if [ -n "$latest_zip" ] && [ -n "$latest_version" ]; then
    if [ -z "$installed_version" ] || version_is_newer "$latest_version" "$installed_version"; then
      return 0
    fi
  fi
  if [ -n "$marker" ]; then
    package_version="$(extract_marker_value version "$marker")"
    [ -n "$package_version" ] || package_version="$(extract_version_from_name "$(dirname "$marker")/$(extract_marker_value filename "$marker")")"
    if [ -z "$package_version" ] || [ -z "$installed_version" ] || version_is_newer "$package_version" "$installed_version"; then
      return 0
    fi
    clear_pending_update_markers
  fi
  return 1
}

if [ "${1:-}" = "--install-pending-update" ]; then
  perform_pending_update_if_any
  exit $?
fi

log "[rgds_plus_official] started: $(date 2>/dev/null || echo unknown)"
log "[rgds_plus_official] app=$APP_DIR"
log "[rgds_plus_official] bin=$BIN"
log "[rgds_plus_official] weston_pids=$(pidof weston 2>/dev/null || true)"

if [ ! -x "$BIN" ]; then
  chmod +x "$BIN" 2>/dev/null || true
fi
if [ ! -f "$BIN" ]; then
  log "[rgds_plus_official] missing binary"
  exit 127
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
export ROCREADER_DEVICE_MODEL="${ROCREADER_DEVICE_MODEL:-rgds-plus}"
export ROCREADER_SCREEN_PROFILE="${ROCREADER_SCREEN_PROFILE:-1024x768}"
export ROCREADER_SCREEN_W="${ROCREADER_SCREEN_W:-1024}"
export ROCREADER_SCREEN_H="${ROCREADER_SCREEN_H:-768}"
export ROCREADER_ROOT="$APP_DIR"
export ROCREADER_UPDATE_CONTENTS_URL="${ROCREADER_UPDATE_CONTENTS_URL:-https://github.com/LPF970915/ROCreader/tree/main/RGDSPlus/Downloads}"
export ROCREADER_CARD1_ROOT="${ROCREADER_CARD1_ROOT:-/mnt/mmc}"
export ROCREADER_CARD2_ROOT="${ROCREADER_CARD2_ROOT:-/mnt/sdcard}"
export ROCREADER_SCAN_CARD2="${ROCREADER_SCAN_CARD2:-1}"
export ROCREADER_RUNTIME_LOG="${ROCREADER_RUNTIME_LOG:-1}"
export ROCREADER_VERBOSE_LOG="${ROCREADER_VERBOSE_LOG:-1}"
export ROCREADER_FULL_INPUT_LOG="${ROCREADER_FULL_INPUT_LOG:-0}"
export ROCREADER_LOG_MAX_BYTES="${ROCREADER_LOG_MAX_BYTES:-524288}"
export ROCREADER_IMAGE_FAST_MODE="${ROCREADER_IMAGE_FAST_MODE:-1}"
export ROCREADER_IMAGE_RENDER_THREAD_PRIORITY="${ROCREADER_IMAGE_RENDER_THREAD_PRIORITY:-normal}"
export ROCREADER_IMAGE_TEXTURE_CACHE="${ROCREADER_IMAGE_TEXTURE_CACHE:-8}"
export ROCREADER_IMAGE_PREFETCH_SCREENS="${ROCREADER_IMAGE_PREFETCH_SCREENS:-4}"
export ROCREADER_IMAGE_PREFETCH_AHEAD="${ROCREADER_IMAGE_PREFETCH_AHEAD:-4}"
export ROCREADER_IMAGE_PREFETCH_BIDIRECTIONAL="${ROCREADER_IMAGE_PREFETCH_BIDIRECTIONAL:-1}"
export ROCREADER_IMAGE_PREFETCH_DEDICATED_THREAD="${ROCREADER_IMAGE_PREFETCH_DEDICATED_THREAD:-1}"
export ROCREADER_IMAGE_IDLE_PREFETCH_MS="${ROCREADER_IMAGE_IDLE_PREFETCH_MS:-0}"
export ROCREADER_IMAGE_VISUAL_THROTTLE_MS="${ROCREADER_IMAGE_VISUAL_THROTTLE_MS:-0}"
export ROCREADER_RGDS_DUALSCREEN=1
export ROCREADER_RGDS_LAYOUT=spanning
export ROCREADER_PWR_SCRIPT="${ROCREADER_PWR_SCRIPT:-$APP_DIR/rgds_power_control.sh}"
export ROCREADER_BOOKS_ROOT="${ROCREADER_BOOKS_ROOT:-$APP_DIR/books}"
export ROCREADER_COVER_ROOT="${ROCREADER_COVER_ROOT:-$APP_DIR/book_covers}"

if [ -d "$LIB_SYSTEM_SDL_DIR" ]; then
  LIB_DIR="$LIB_SYSTEM_SDL_DIR"
else
  LIB_DIR="$LIB_FULL_DIR"
fi
export LD_LIBRARY_PATH="$LIB_DIR:$LIB_DIR/pulseaudio:/usr/lib:/usr/lib/pulseaudio:/lib:/mnt/vendor/lib:${LD_LIBRARY_PATH:-}"

# Firmware SDL uses the firmware ALSA plugins and PulseAudio server. Keep their
# libraries together instead of mixing them with the H700 sysroot audio libraries.
if [ "$LIB_DIR" = "$LIB_SYSTEM_SDL_DIR" ] &&
   [ -f /usr/lib/libasound.so.2 ] && [ -f /usr/lib/libpulse.so.0 ]; then
  export LD_PRELOAD="/usr/lib/libasound.so.2:/usr/lib/libpulse.so.0${LD_PRELOAD:+:$LD_PRELOAD}"
  log "[rgds_plus_official] audio libraries: firmware ALSA/PulseAudio"
fi

mkdir -p "$XDG_RUNTIME_DIR" "$APP_DIR/cache" 2>/dev/null || true
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

cd "$APP_DIR" || exit 126
log "[rgds_plus_official] SDL_VIDEODRIVER=$SDL_VIDEODRIVER WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
log "[rgds_plus_official] ROCREADER_RGDS_LAYOUT=$ROCREADER_RGDS_LAYOUT"
log "[rgds_plus_official] image_fast_mode=$ROCREADER_IMAGE_FAST_MODE texture_cache=$ROCREADER_IMAGE_TEXTURE_CACHE prefetch_screens=$ROCREADER_IMAGE_PREFETCH_SCREENS prefetch_ahead=$ROCREADER_IMAGE_PREFETCH_AHEAD bidirectional=$ROCREADER_IMAGE_PREFETCH_BIDIRECTIONAL dedicated_prefetch=$ROCREADER_IMAGE_PREFETCH_DEDICATED_THREAD idle_prefetch_ms=$ROCREADER_IMAGE_IDLE_PREFETCH_MS visual_throttle_ms=$ROCREADER_IMAGE_VISUAL_THROTTLE_MS render_priority=$ROCREADER_IMAGE_RENDER_THREAD_PRIORITY"
log "[rgds_plus_official] ROCREADER_PWR_SCRIPT=$ROCREADER_PWR_SCRIPT"
log "[rgds_plus_official] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
log "[rgds_plus_official] books_root=$ROCREADER_BOOKS_ROOT"
log "[rgds_plus_official] cover_root=$ROCREADER_COVER_ROOT"
uname -a >> "$LOG_FILE" 2>&1 || true
ls -la "$APP_DIR" >> "$LOG_FILE" 2>&1 || true

if pending_update_available; then
  export ROCREADER_BOOT_INSTALL_PENDING_UPDATE="${ROCREADER_BOOT_INSTALL_PENDING_UPDATE:-1}"
  export ROCREADER_UPDATE_INSTALL_COMMAND="${ROCREADER_UPDATE_INSTALL_COMMAND:-\"$LAUNCHER_PATH\" --install-pending-update}"
fi

"$BIN" >> "$LOG_FILE" 2>&1
rc=$?
log "[rgds_plus_official] exit code=$rc"
if [ "$rc" -eq 23 ]; then
  log "[update] installed during boot; restarting app"
  unset ROCREADER_BOOT_INSTALL_PENDING_UPDATE
  unset ROCREADER_UPDATE_INSTALL_COMMAND
  "$LAUNCHER_PATH" >> "$LOG_FILE" 2>&1
  exit $?
fi
if [ "$rc" -eq 0 ] && pending_update_available; then
  log "[update] pending package found after app exit; installing"
  perform_pending_update_if_any
fi
exit "$rc"
