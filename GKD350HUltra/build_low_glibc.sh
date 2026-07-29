#!/bin/sh
set -eu

SELF_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SELF_DIR/.." && pwd)"

export SYSROOT="${SYSROOT:-$SELF_DIR/sysroot_device}"
export CROSS_TOOL_PREFIX="${CROSS_TOOL_PREFIX:-aarch64-linux-gnu}"
export DIST_ROOT="${DIST_ROOT:-$SELF_DIR/dist_lowglibc}"
export DOWNLOADS_ROOT="${DOWNLOADS_ROOT:-$SELF_DIR/Downloads}"
export ROC_NATIVE_LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
export ROCREADER_SCREEN_PROFILE="${ROCREADER_SCREEN_PROFILE:-1600x1440}"
export ROCREADER_DEVICE_MODEL="${ROCREADER_DEVICE_MODEL:-gkd350h-ultra}"
export ROCREADER_UPDATE_CONTENTS_URL="${ROCREADER_UPDATE_CONTENTS_URL:-https://github.com/LPF970915/ROCreader/tree/main/GKD350HUltra/Downloads}"
export DOWNLOAD_TARGET_SUFFIX="${DOWNLOAD_TARGET_SUFFIX:-GKD350H Ultra}"
export LEGACY_DOWNLOADS_MIRROR="${LEGACY_DOWNLOADS_MIRROR:-0}"
export ROCREADER_RELEASE_PARENT_DIR="${ROCREADER_RELEASE_PARENT_DIR:-app}"
export ROCREADER_RELEASE_IUX_APP="${ROCREADER_RELEASE_IUX_APP:-1}"
export ROCREADER_RELEASE_INCLUDE_IMGS="${ROCREADER_RELEASE_INCLUDE_IMGS:-0}"
export ROCREADER_PACKAGE_ONLINE_SOURCES="${ROCREADER_PACKAGE_ONLINE_SOURCES:-0}"
export REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"

if [ ! -d "$SYSROOT/usr/include" ] || [ ! -d "$SYSROOT/usr/lib" ]; then
  echo "[gkd_build] ERROR: invalid sysroot: $SYSROOT"
  echo "[gkd_build] Run sync first, for example:"
  echo "[gkd_build]   DEVICE_HOST=root@192.168.31.12 $SELF_DIR/sync_sysroot.sh"
  exit 1
fi

if [ "$REQUIRE_MUPDF" = "1" ] && [ ! -e "$SYSROOT/usr/lib/libpoppler-cpp.so" ]; then
  "$SELF_DIR/prepare_pdf_backend_overlay.sh"
fi

cd "$REPO_ROOT"
exec ./cross_compile_low_glibc.sh
