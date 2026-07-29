#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_PATH="$SELF_DIR/$(basename "$0")"

# The ES ports entry and the IUX application share one runtime. Keep the full
# launcher in ports as a fallback for legacy installations without app/.
case "$SELF_DIR" in
  */ports)
    for iux_launcher in \
      /storage/games-external/app/ROCreader/launch.sh \
      "$SELF_DIR/../../app/ROCreader/launch.sh"; do
      if [ -x "$iux_launcher" ]; then
        exec "$iux_launcher" "$@"
      fi
    done
    ;;
esac

APP_DIR="$SELF_DIR/ROCreader"
if [ ! -d "$APP_DIR" ]; then
  APP_DIR="$SELF_DIR"
fi
BIN="$APP_DIR/rocreader_sdl"
LOG_FILE="${ROC_NATIVE_RUNTIME_LOG:-$APP_DIR/ROCreader.log}"
LIB_FULL_DIR="$APP_DIR/lib"
LIB_SYSTEM_SDL_DIR="$APP_DIR/lib_system_sdl"
MANUAL_WEB_TRANSPORT_DIR="$APP_DIR/manual_web_transport"
LIB_DIR="$LIB_FULL_DIR"
UPDATE_STATUS_FILE="$APP_DIR/cache/update_boot_status.txt"
UPDATE_STAGE_DIR="$APP_DIR/cache/update_stage"
INSTALLED_VERSION_FILE="$APP_DIR/version.txt"

export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
export ROCREADER_ROOT="$APP_DIR"
export ROCREADER_CARD1_ROOT="/mnt/mmc"
export ROCREADER_CARD2_ROOT="/mnt/sdcard"
export ROCREADER_SCAN_CARD2="${ROCREADER_SCAN_CARD2:-1}"
export ROCREADER_FULL_INPUT_LOG="${ROCREADER_FULL_INPUT_LOG:-0}"
export ROCREADER_RUNTIME_LOG="${ROCREADER_RUNTIME_LOG:-1}"
export ROCREADER_LOG_MAX_BYTES="${ROCREADER_LOG_MAX_BYTES:-524288}"
export ROCREADER_UPDATE_CONTENTS_URL="${ROCREADER_UPDATE_CONTENTS_URL:-https://github.com/LPF970915/ROCreader/tree/main/H700/Downloads}"
export ROCREADER_ONLINE_SOURCES="${ROCREADER_ONLINE_SOURCES:-$APP_DIR/online_sources.ini}"
if [ -f "$APP_DIR/online_sources.ini" ]; then
  if [ -z "${SSL_CERT_FILE:-}" ] && [ -r /etc/ssl/cert.pem ]; then
    export SSL_CERT_FILE="/etc/ssl/cert.pem"
  fi
  if [ -z "${CURL_CA_BUNDLE:-}" ] && [ -r /etc/ssl/cert.pem ]; then
    export CURL_CA_BUNDLE="/etc/ssl/cert.pem"
  fi
  if [ -x "$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch" ]; then
    export ROCREADER_MANUAL_WEB_FETCH="${ROCREADER_MANUAL_WEB_FETCH:-$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch}"
    if [ -x "$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch_zip" ]; then
      export ROCREADER_MANUAL_WEB_ZIP_FETCH="${ROCREADER_MANUAL_WEB_ZIP_FETCH:-$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch_zip}"
    fi
    if [ -x "$MANUAL_WEB_TRANSPORT_DIR/bin/curl-impersonate" ]; then
      export ROCREADER_MANUAL_WEB_CURL="${ROCREADER_MANUAL_WEB_CURL:-$MANUAL_WEB_TRANSPORT_DIR/bin/curl-impersonate}"
    fi
    export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-1}"
    export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
  else
    export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-0}"
    export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
  fi
else
  export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-0}"
  export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
fi
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/tmp/rocreader-xdg"
fi
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true
cd "$APP_DIR"

set_runtime_libs() {
  lib_dir="$1"
  if [ -d "$lib_dir" ]; then
    LIB_DIR="$lib_dir"
  else
    LIB_DIR="$LIB_FULL_DIR"
  fi
  if [ -d "$MANUAL_WEB_TRANSPORT_DIR/lib" ]; then
    export LD_LIBRARY_PATH="$LIB_DIR:$MANUAL_WEB_TRANSPORT_DIR/lib:$LIB_DIR/pulseaudio:/usr/lib32:/usr/lib:/lib:/mnt/vendor/lib:${LD_LIBRARY_PATH_BASE:-}"
  else
    export LD_LIBRARY_PATH="$LIB_DIR:$LIB_DIR/pulseaudio:/usr/lib32:/usr/lib:/lib:/mnt/vendor/lib:${LD_LIBRARY_PATH_BASE:-}"
  fi
}

log_line() {
  if [ "${ROCREADER_FULL_INPUT_LOG:-0}" = "1" ]; then
    printf '%s\n' "$1" >>"$LOG_FILE"
    return 0
  fi
  if [ "${ROCREADER_VERBOSE_LOG:-0}" != "1" ] && [ "${ROCREADER_DEBUG_LOG:-0}" != "1" ]; then
    case "$1" in
      *failed*|*Failed*|*FAILED*|*error*|*Error*|*ERROR*|*missing*|*Missing*|*MISSING*|*crash*|*Crash*|*CRASH*|*fatal*|*Fatal*|*FATAL*) ;;
      *) return 0 ;;
    esac
  fi
  printf '%s\n' "$1" >>"$LOG_FILE"
}

trim_log_if_needed() {
  [ "${ROCREADER_LOG_MAX_BYTES:-0}" -gt 0 ] 2>/dev/null || return 0
  [ -f "$LOG_FILE" ] || return 0
  size="$(wc -c <"$LOG_FILE" 2>/dev/null || printf '0')"
  [ "$size" -le "$ROCREADER_LOG_MAX_BYTES" ] 2>/dev/null || : >"$LOG_FILE"
}

write_update_status() {
  result="$1"
  version="${2:-}"
  mkdir -p "$(dirname "$UPDATE_STATUS_FILE")" 2>/dev/null || true
  {
    printf 'result=%s\n' "$result"
    [ -n "$version" ] && printf 'version=%s\n' "$version"
  } >"$UPDATE_STATUS_FILE"
}

read_installed_version() {
  [ -f "$INSTALLED_VERSION_FILE" ] || return 0
  sed -n '1p' "$INSTALLED_VERSION_FILE"
}

write_installed_version() {
  version="$1"
  [ -n "$version" ] || return 0
  printf '%s\n' "$version" >"$INSTALLED_VERSION_FILE"
}

read_board_ini_model() {
  for path in /oem/board.ini /mnt/vendor/oem/board.ini; do
    [ -r "$path" ] || continue
    tr -d '\000\r' <"$path" | tr 'A-Z' 'a-z' | head -n 1
    return 0
  done
  return 1
}

screen_profile_from_board_ini() {
  model_name="$(read_board_ini_model || true)"
  [ -n "$model_name" ] || return 1
  case "$model_name" in
    *gkd350hultra*|*gkd350h*|*gkd*atom*|*gamekiddy*gkd*atom*)
      printf '1600x1440|%s\n' "$model_name"
      return 0
      ;;
    *trimui*brick*|*brick*|*tg3040*)
      printf '1024x768|%s\n' "$model_name"
      return 0
      ;;
    *rgcubexx*|*cubexx*)
      printf '720x720|%s\n' "$model_name"
      return 0
      ;;
    *rg34xxsp*|*34xxsp*|*rg34xx*|*34xx*)
      printf '720x480|%s\n' "$model_name"
      return 0
      ;;
    *rg28xx*|*28xx*|*rg35xx*|*35xx*|*rg40xx*|*40xx*)
      printf '640x480|%s\n' "$model_name"
      return 0
      ;;
  esac
  printf '720x720|%s\n' "$model_name"
  return 0
}

normalize_screen_override() {
  case "${ROCREADER_SCREEN_PROFILE:-}" in
    1600x1440|gkd350h-ultra|gkd350h|gkd-ultra|gkd_atom|gkd-atom|gamekiddy-gkd-atom)
      export ROCREADER_SCREEN_W=1600
      export ROCREADER_SCREEN_H=1440
      ;;
    1024x768|brick|trimui-brick)
      export ROCREADER_SCREEN_W=1024
      export ROCREADER_SCREEN_H=768
      ;;
    720x720)
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=720
      ;;
    640x480|640)
      export ROCREADER_SCREEN_W=640
      export ROCREADER_SCREEN_H=480
      ;;
    720x480|720)
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=480
      ;;
  esac
}

detect_screen_size_token() {
  for path in \
    /sys/class/graphics/fb0/modes \
    /sys/class/graphics/fb0/mode \
    /sys/class/graphics/fb1/modes \
    /sys/class/graphics/fb1/mode \
    /sys/class/graphics/fb0/virtual_size \
    /sys/class/graphics/fb1/virtual_size; do
    [ -r "$path" ] || continue
    value="$(tr -d '\000\r' <"$path" | head -n 1)"
    case "$value" in
      *1600*x*1440*|*1600*1440*|*1440*x*1600*|*1440*1600*)
        printf '%s\n' "1600x1440"
        return 0
        ;;
      *1024*x*768*|*1024*768*)
        printf '%s\n' "1024x768"
        return 0
        ;;
      *640*)
        printf '%s\n' "640x480"
        return 0
        ;;
      *720*)
        printf '%s\n' "720x480"
        return 0
        ;;
    esac
  done
  return 1
}

ensure_screen_override_if_needed() {
  normalize_screen_override
  if [ -n "${ROCREADER_SCREEN_W:-}" ] && [ -n "${ROCREADER_SCREEN_H:-}" ]; then
    log_line "[launcher] screen override preset: ${ROCREADER_SCREEN_W}x${ROCREADER_SCREEN_H}"
    return 0
  fi

  model_rule="$(screen_profile_from_board_ini || true)"
  case "$model_rule" in
    1600x1440\|*)
      model_name="${model_rule#*|}"
      export ROCREADER_SCREEN_PROFILE=1600x1440
      export ROCREADER_SCREEN_W=1600
      export ROCREADER_SCREEN_H=1440
      log_line "[launcher] screen override board.ini: $model_name -> 1600x1440"
      return 0
      ;;
    1024x768\|*)
      model_name="${model_rule#*|}"
      export ROCREADER_SCREEN_PROFILE=1024x768
      export ROCREADER_SCREEN_W=1024
      export ROCREADER_SCREEN_H=768
      log_line "[launcher] screen override board.ini: $model_name -> 1024x768"
      return 0
      ;;
    720x720\|*)
      model_name="${model_rule#*|}"
      export ROCREADER_SCREEN_PROFILE=720x720
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=720
      log_line "[launcher] screen override board.ini: $model_name -> 720x720"
      return 0
      ;;
    640x480\|*)
      model_name="${model_rule#*|}"
      export ROCREADER_SCREEN_PROFILE=640x480
      export ROCREADER_SCREEN_W=640
      export ROCREADER_SCREEN_H=480
      log_line "[launcher] screen override board.ini: $model_name -> 640x480"
      return 0
      ;;
    720x480\|*)
      model_name="${model_rule#*|}"
      export ROCREADER_SCREEN_PROFILE=720x480
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=480
      log_line "[launcher] screen override board.ini: $model_name -> 720x480"
      return 0
      ;;
  esac

  detected_profile="$(detect_screen_size_token || true)"
  case "$detected_profile" in
    1600x1440)
      export ROCREADER_SCREEN_PROFILE=1600x1440
      export ROCREADER_SCREEN_W=1600
      export ROCREADER_SCREEN_H=1440
      log_line "[launcher] screen override fb/sysfs -> 1600x1440"
      return 0
      ;;
    1024x768)
      export ROCREADER_SCREEN_PROFILE=1024x768
      export ROCREADER_SCREEN_W=1024
      export ROCREADER_SCREEN_H=768
      log_line "[launcher] screen override fb/sysfs -> 1024x768"
      return 0
      ;;
    720x720)
      export ROCREADER_SCREEN_PROFILE=720x720
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=720
      log_line "[launcher] screen override fb/sysfs -> 720x720"
      return 0
      ;;
    640x480)
      export ROCREADER_SCREEN_PROFILE=640x480
      export ROCREADER_SCREEN_W=640
      export ROCREADER_SCREEN_H=480
      log_line "[launcher] screen override fb/sysfs -> 640x480"
      return 0
      ;;
    720x480)
      export ROCREADER_SCREEN_PROFILE=720x480
      export ROCREADER_SCREEN_W=720
      export ROCREADER_SCREEN_H=480
      log_line "[launcher] screen override fb/sysfs -> 720x480"
      return 0
      ;;
  esac

  export ROCREADER_SCREEN_PROFILE=720x720
  export ROCREADER_SCREEN_W=720
  export ROCREADER_SCREEN_H=720
  log_line "[launcher] screen override fallback: board.ini missing -> 720x720"
}

configure_cache_root_for_profile() {
  if [ -n "${ROCREADER_CACHE_ROOT:-}" ]; then
    return 0
  fi
  case "${ROCREADER_DEVICE_MODEL:-}:${ROCREADER_SCREEN_PROFILE:-}" in
    *gkd350h*:*|*gkd*atom*:*|*gamekiddy*gkd*:*|*:1600x1440)
      export ROCREADER_CACHE_ROOT="$APP_DIR/cache"
      log_line "[launcher] cache root -> $ROCREADER_CACHE_ROOT"
      ;;
  esac
}

find_pending_marker() {
  for downloads_dir in "$APP_DIR/Downloads" /mnt/mmc/Downloads /mnt/sdcard/Downloads /mnt/SDCARD/Downloads; do
    marker="$downloads_dir/ROCreader_update_pending.txt"
    [ -f "$marker" ] && { printf '%s' "$marker"; return 0; }
  done
  return 1
}

extract_marker_value() {
  key="$1"
  marker="$2"
  awk -F= -v wanted="$key" '$1 == wanted { print substr($0, index($0, "=") + 1); exit }' "$marker"
}

extract_version_from_name() {
  file_name="$(basename "$1")"
  printf '%s\n' "$file_name" | sed -n 's/.*\(ver[0-9][0-9.]*\).*\.zip$/\1/p'
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

find_latest_download_zip() {
  best_path=""
  best_version=""
  for downloads_dir in "$APP_DIR/Downloads" /mnt/mmc/Downloads /mnt/sdcard/Downloads /mnt/SDCARD/Downloads; do
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
    unzip -oq "$zip_file" -d "$stage_dir" >>"$LOG_FILE" 2>&1
    return $?
  fi
  if command -v busybox >/dev/null 2>&1; then
    busybox unzip -o "$zip_file" -d "$stage_dir" >>"$LOG_FILE" 2>&1
    return $?
  fi
  return 127
}

find_staged_runtime_dir() {
  stage_dir="$1"
  for candidate in \
    "$stage_dir/app/ROCreader" \
    "$stage_dir/roms/ports/ROCreader" \
    "$stage_dir/Roms/ports/ROCreader" \
    "$stage_dir/Roms/APPS/ROCreader" \
    "$stage_dir/APPS/ROCreader" \
    "$stage_dir/Apps/ROCreader" \
    "$stage_dir/ROCreader"; do
    [ -d "$candidate" ] && { printf '%s' "$candidate"; return 0; }
  done
  return 1
}

find_staged_launcher_file() {
  stage_dir="$1"
  for candidate in \
    "$stage_dir/app/ROCreader/launch.sh" \
    "$stage_dir/roms/ports/ROCreader.sh" \
    "$stage_dir/Roms/ports/ROCreader.sh" \
    "$stage_dir/Roms/APPS/ROCreader.sh" \
    "$stage_dir/APPS/ROCreader.sh"; do
    [ -f "$candidate" ] && { printf '%s' "$candidate"; return 0; }
  done
  return 1
}

install_es_launcher_if_present() {
  staged_es_launcher="$UPDATE_STAGE_DIR/roms/ports/ROCreader.sh"
  [ -f "$staged_es_launcher" ] || return 0
  for ports_dir in /storage/roms/ports "$APP_DIR/../../roms/ports"; do
    [ -d "$ports_dir" ] || continue
    cp "$staged_es_launcher" "$ports_dir/ROCreader.sh.new"
    chmod +x "$ports_dir/ROCreader.sh.new" 2>/dev/null || true
    mv "$ports_dir/ROCreader.sh.new" "$ports_dir/ROCreader.sh"
    return 0
  done
}

replace_runtime_entry() {
  name="$1"
  src="$2/$name"
  dst="$APP_DIR/$name"
  [ -e "$src" ] || return 0
  rm -rf "$dst"
  cp -a "$src" "$APP_DIR/"
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
    if [ -z "$package_version" ]; then
      package_version="$(extract_version_from_name "$package_path")"
    fi
  fi
  [ -n "$package_path" ] || return 0

  log_line "[update] pending marker: $marker"
  log_line "[update] installed version: ${installed_version:-unknown}"
  log_line "[update] latest zip: ${latest_zip:-none}"
  log_line "[update] package: $package_path"
  log_line "[update] package version: ${package_version:-unknown}"

  if [ ! -f "$package_path" ]; then
    log_line "[update] missing package, skip install"
    write_update_status "failed" "$package_version"
    return 0
  fi

  if ! extract_zip_to_stage "$package_path" "$UPDATE_STAGE_DIR"; then
    log_line "[update] extract failed"
    write_update_status "failed" "$package_version"
    return 0
  fi

  staged_runtime="$(find_staged_runtime_dir "$UPDATE_STAGE_DIR" || true)"
  staged_launcher="$(find_staged_launcher_file "$UPDATE_STAGE_DIR" || true)"
  if [ ! -d "$staged_runtime" ]; then
    log_line "[update] staged runtime missing under: $UPDATE_STAGE_DIR"
    write_update_status "failed" "$package_version"
    rm -rf "$UPDATE_STAGE_DIR"
    return 0
  fi
  if [ -f "$staged_runtime/version.txt" ]; then
    package_version="$(sed -n '1p' "$staged_runtime/version.txt")"
  fi

  replace_runtime_entry "rocreader_sdl" "$staged_runtime"
  replace_runtime_entry "ui.pack" "$staged_runtime"
  replace_runtime_entry "native_config.ini" "$staged_runtime"
  replace_runtime_entry "native_keymap.ini" "$staged_runtime"
  # Keep the user's online source config intact during update installs.
  # online_sources.ini must not be overwritten by unpack-and-replace.
  replace_runtime_entry "fonts" "$staged_runtime"
  replace_runtime_entry "sounds" "$staged_runtime"
  replace_runtime_entry "lib" "$staged_runtime"
  replace_runtime_entry "lib_system_sdl" "$staged_runtime"
  replace_runtime_entry "config.json" "$staged_runtime"
  replace_runtime_entry "rocreader.png" "$staged_runtime"
  replace_runtime_entry "icon.png" "$staged_runtime"

  if [ -n "$staged_launcher" ] && [ -f "$staged_launcher" ]; then
    cp "$staged_launcher" "$LAUNCHER_PATH.new"
    mv "$LAUNCHER_PATH.new" "$LAUNCHER_PATH"
  fi
  install_es_launcher_if_present

  chmod +x "$APP_DIR/rocreader_sdl" 2>/dev/null || true
  chmod +x "$LAUNCHER_PATH" 2>/dev/null || true

  write_installed_version "$package_version"
  rm -f "$APP_DIR/Downloads/ROCreader_update_pending.txt" /mnt/mmc/Downloads/ROCreader_update_pending.txt /mnt/sdcard/Downloads/ROCreader_update_pending.txt /mnt/SDCARD/Downloads/ROCreader_update_pending.txt
  rm -rf "$UPDATE_STAGE_DIR"
  write_update_status "success" "$package_version"
  log_line "[update] install success version=${package_version:-unknown}"
}

run_with_driver() {
  drv="$1"
  mode="$2"
  lib_dir="$3"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] run driver=$drv mode=$mode lib_dir=$lib_dir"
  SDL_VIDEODRIVER="$drv" "$BIN" >>"$LOG_FILE" 2>&1
  rc=$?
  log_line "[launcher] exit rc=$rc driver=$drv mode=$mode"
  return "$rc"
}

run_default() {
  mode="$1"
  lib_dir="$2"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] run default mode=$mode lib_dir=$lib_dir"
  "$BIN" >>"$LOG_FILE" 2>&1
  rc=$?
  log_line "[launcher] exit rc=$rc default mode=$mode"
  return "$rc"
}

if [ "${1:-}" = "--install-pending-update" ]; then
  perform_pending_update_if_any
  exit $?
fi

LD_LIBRARY_PATH_BASE="${LD_LIBRARY_PATH:-}"
set_runtime_libs "$LIB_SYSTEM_SDL_DIR"

if [ ! -x "$BIN" ]; then
  log_line "[launcher] binary missing: $BIN"
  exit 4
fi

trim_log_if_needed
if [ "${ROCREADER_VERBOSE_LOG:-0}" = "1" ] || [ "${ROCREADER_DEBUG_LOG:-0}" = "1" ]; then
  log_line "===== $(date '+%F %T %Z') ====="
fi
perform_pending_update_if_any
ensure_screen_override_if_needed
configure_cache_root_for_profile
if [ ! -f "$APP_DIR/online_sources.ini" ]; then
  log_line "[launcher] URL sources not installed"
fi

if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  run_with_driver "$SDL_VIDEODRIVER" "forced" "$LIB_FULL_DIR"
  exit $?
fi

should_stop_retry() {
  rc="$1"
  [ "$rc" -eq 0 ] && return 0
  [ "$rc" -ge 128 ] 2>/dev/null && return 0
  return 1
}

try_mode() {
  mode="$1"
  lib_dir="$2"

  if run_default "$mode" "$lib_dir"; then
    exit 0
  fi
  rc=$?
  if should_stop_retry "$rc"; then
    log_line "[launcher] stop retry after default rc=$rc mode=$mode"
    exit "$rc"
  fi

  for drv in KMSDRM kmsdrm wayland x11; do
    if run_with_driver "$drv" "$mode" "$lib_dir"; then
      exit 0
    fi
    rc=$?
    if should_stop_retry "$rc"; then
      log_line "[launcher] stop retry after driver rc=$rc driver=$drv mode=$mode"
      exit "$rc"
    fi
  done
}

try_mode "system_sdl" "$LIB_SYSTEM_SDL_DIR"
try_mode "full" "$LIB_FULL_DIR"
log_line "[launcher] all drivers failed"
exit 5
