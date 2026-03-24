#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
SYSROOT="${SYSROOT:-$SELF_DIR/sysroot_device}"
DEVICE_HOST="${DEVICE_HOST:-root@192.168.31.141}"
SSH_PORT="${SSH_PORT:-22}"

HAS_LOCAL_RSYNC=0
HAS_REMOTE_RSYNC=0

if command -v rsync >/dev/null 2>&1; then
  HAS_LOCAL_RSYNC=1
fi
if ssh -p "$SSH_PORT" "$DEVICE_HOST" "command -v rsync >/dev/null 2>&1"; then
  HAS_REMOTE_RSYNC=1
fi

if [ "$HAS_LOCAL_RSYNC" -eq 0 ]; then
  echo "[sync_sysroot] ERROR: local rsync not found"
  exit 1
fi

mkdir -p "$SYSROOT"
mkdir -p "$SYSROOT/usr"
mkdir -p "$SYSROOT/lib"

RSYNC_SSH="ssh -p $SSH_PORT"

sync_dir() {
  remote="$1"
  local_dir="$2"
  echo "[sync_sysroot] $remote -> $local_dir"
  if [ "$HAS_REMOTE_RSYNC" -eq 1 ]; then
    mkdir -p "$local_dir"
    rsync -a --delete -e "$RSYNC_SSH" "${DEVICE_HOST}:${remote}/" "$local_dir/"
  else
    echo "[sync_sysroot] remote rsync missing, fallback to tar stream"
    rm -rf "$local_dir"
    mkdir -p "$local_dir"
    ssh -p "$SSH_PORT" "$DEVICE_HOST" "cd '$remote' && tar -cf - ." | tar -xf - -C "$local_dir"
  fi
}

# Runtime libraries used by dynamic linker.
sync_dir "/lib" "$SYSROOT/lib"
sync_dir "/usr/lib" "$SYSROOT/usr/lib"

# Headers + pkg-config metadata for compilation.
sync_dir "/usr/include" "$SYSROOT/usr/include"
if ssh -p "$SSH_PORT" "$DEVICE_HOST" "test -d /usr/share/pkgconfig" >/dev/null 2>&1; then
  sync_dir "/usr/share/pkgconfig" "$SYSROOT/usr/share/pkgconfig"
fi
if ssh -p "$SSH_PORT" "$DEVICE_HOST" "test -d /usr/lib/pkgconfig" >/dev/null 2>&1; then
  sync_dir "/usr/lib/pkgconfig" "$SYSROOT/usr/lib/pkgconfig"
fi

echo "[sync_sysroot] done: $SYSROOT"
