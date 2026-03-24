#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/build_run_$(date +%Y%m%d_%H%M%S).log"
REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"

echo "[roc_native] log: $LOG_FILE"

{
  echo "===== $(date '+%F %T') ====="
  echo "[roc_native] REQUIRE_MUPDF=$REQUIRE_MUPDF"
  echo "[roc_native] preflight"
  command -v make
  command -v "${CXX:-g++}"
  command -v "${PKG_CONFIG:-pkg-config}" || true
  echo "[roc_native] make print-config"
  make print-config REQUIRE_MUPDF="$REQUIRE_MUPDF"
  echo "[roc_native] make clean"
  make clean REQUIRE_MUPDF="$REQUIRE_MUPDF"
  echo "[roc_native] make"
  make REQUIRE_MUPDF="$REQUIRE_MUPDF"
  echo "[roc_native] run"
  exec ./build/rocreader_sdl
} >>"$LOG_FILE" 2>&1
