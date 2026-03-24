#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SELF_DIR/ROCreader"
BIN="$APP_DIR/rocreader_sdl"
LOG_FILE="${ROC_NATIVE_RUNTIME_LOG:-$SELF_DIR/ROCreader.log}"
LIB_FULL_DIR="$APP_DIR/lib"
LIB_SYSTEM_SDL_DIR="$APP_DIR/lib_system_sdl"
LIB_DIR="$LIB_FULL_DIR"

export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/tmp/rocreader-xdg"
fi
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

set_runtime_libs() {
  lib_dir="$1"
  if [ -d "$lib_dir" ]; then
    LIB_DIR="$lib_dir"
  else
    LIB_DIR="$LIB_FULL_DIR"
  fi
  export LD_LIBRARY_PATH="$LIB_DIR:$LIB_DIR/pulseaudio:/usr/lib32:/usr/lib:/lib:/mnt/vendor/lib:${LD_LIBRARY_PATH_BASE:-}"
}

LD_LIBRARY_PATH_BASE="${LD_LIBRARY_PATH:-}"
set_runtime_libs "$LIB_SYSTEM_SDL_DIR"

log_line() {
  printf '%s\n' "$1" >>"$LOG_FILE"
}

log_line "===== $(date '+%F %T %Z') ====="
log_line "[launcher] start"

run_with_driver() {
  drv="$1"
  mode="$2"
  lib_dir="$3"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] try mode=$mode SDL_VIDEODRIVER=$drv lib_dir=$LIB_DIR"
  if SDL_VIDEODRIVER="$drv" "$BIN" >>"$LOG_FILE" 2>&1; then
    return 0
  else
    return $?
  fi
}

run_default() {
  mode="$1"
  lib_dir="$2"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] try mode=$mode SDL_VIDEODRIVER=<default> lib_dir=$LIB_DIR"
  if "$BIN" >>"$LOG_FILE" 2>&1; then
    return 0
  else
    return $?
  fi
}

if [ ! -x "$BIN" ]; then
  log_line "[launcher] ERROR: binary missing or not executable: $BIN"
  exit 4
fi

if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  run_with_driver "$SDL_VIDEODRIVER" "forced" "$LIB_FULL_DIR"
  code=$?
  log_line "[launcher] exit mode=forced driver=$SDL_VIDEODRIVER code=$code"
  exit "$code"
fi

try_mode() {
  mode="$1"
  lib_dir="$2"

  if run_default "$mode" "$lib_dir"; then
    log_line "[launcher] success mode=$mode driver=<default>"
    exit 0
  else
    code=$?
    log_line "[launcher] failed mode=$mode driver=<default> code=$code"
  fi

  for drv in KMSDRM kmsdrm wayland x11; do
    if run_with_driver "$drv" "$mode" "$lib_dir"; then
      log_line "[launcher] success mode=$mode driver=$drv"
      exit 0
    else
      code=$?
      log_line "[launcher] failed mode=$mode driver=$drv code=$code"
    fi
  done
}

try_mode "system_sdl" "$LIB_SYSTEM_SDL_DIR"
try_mode "full" "$LIB_FULL_DIR"
log_line "[launcher] all drivers failed"
exit 5
