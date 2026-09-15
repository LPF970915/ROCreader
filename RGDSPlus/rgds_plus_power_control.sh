#!/bin/sh
set -u

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR="${ROCREADER_ROOT:-$SELF_DIR}"
LOG_FILE="${ROCREADER_RGDS_POWER_LOG:-$APP_DIR/rgds_power_control.log}"
STATE_FILE="${ROCREADER_RGDS_POWER_STATE_FILE:-$APP_DIR/rgds_power_state.txt}"
HALL_PATH="${ROCREADER_RGDS_HALL_PATH:-/sys/class/anbernic_misc/hallkey}"
MODE="${1:-auto}"

log() {
  printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOG_FILE" 2>/dev/null || true
}

write_state() {
  printf '%s\n' "$1" > "$STATE_FILE" 2>/dev/null || true
}

suspend_system() {
  # Share the firmware power handler's flock, but never dispatch its configurable
  # shutdown actions. RGdsplus sleep_down writes mem to this same kernel node.
  exec 9>/tmp/.power_key || return 1
  flock -n 9 || return 1
  before=$(cat /sys/power/suspend_stats/success) || return 1
  case "$before" in ''|*[!0-9]*) return 1 ;; esac
  if [ "$MODE" = lid ]; then
    hall=$(cat "$HALL_PATH") || return 1
    case "$hall" in
      0|2) ;;
      *) log "lid reopened or unreadable; cancel suspend"; return 1 ;;
    esac
  fi
  write_state suspending
  log "suspend begin reason=$MODE count=$before"
  if ! printf mem > /sys/power/state; then
    write_state unknown
    log "suspend failed"
    return 1
  fi
  after=$(cat /sys/power/suspend_stats/success) || return 1
  case "$after" in ''|*[!0-9]*) return 1 ;; esac
  if [ "$after" -le "$before" ]; then
    write_state unknown
    log "suspend did not complete count=$after"
    return 1
  fi
  write_state on
  log "hardware resume completed count=$after"
}

screen_on() {
  # Recovery/compatibility only. Normal lid wake is handled by the kernel.
  # In particular, pwr_new.sh on/wake would suspend again, not turn a screen on.
  printf 'compositor:state:on\n' > "${WESTON_DRM_CONFIG:-/tmp/.weston_drm.conf}" || return 1
  for power in /sys/class/backlight/backlight/bl_power /sys/class/backlight/backlight1/bl_power; do
    printf '0\n' > "$power" || return 1
  done
  write_state on
}

case "$MODE" in
  lid|suspend|auto|powerkey|manual|off) suspend_system ;;
  on|wake|resume) screen_on ;;
  *) log "unsupported mode=$MODE"; exit 2 ;;
esac
