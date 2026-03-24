#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/preflight_$(date +%Y%m%d_%H%M%S).log"
MODE="${PRECHECK_MODE:-native}" # native | cross
TOOL_PREFIX="${CROSS_TOOL_PREFIX:-arm-linux-gnueabihf}"

if [ "$MODE" = "cross" ]; then
  CC_CMD="${CROSS_CXX:-${TOOL_PREFIX}-g++}"
  PKG_CMD="${CROSS_PKG_CONFIG:-${TOOL_PREFIX}-pkg-config}"
else
  CC_CMD="${CXX:-g++}"
  PKG_CMD="${PKG_CONFIG:-pkg-config}"
fi

{
  echo "===== $(date '+%F %T') ====="
  echo "[preflight] cwd=$SELF_DIR"
  echo "[preflight] mode=$MODE"
  echo "[preflight] cc_cmd=$CC_CMD"
  echo "[preflight] pkg_cmd=$PKG_CMD"

  check_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
      echo "[preflight][OK] command: $1 -> $(command -v "$1")"
    else
      echo "[preflight][MISS] command: $1"
    fi
  }

  check_pkg() {
    PKG_BIN="${PKG_CONFIG:-pkg-config}"
    if ! command -v "$PKG_BIN" >/dev/null 2>&1; then
      echo "[preflight][SKIP] pkg-config missing for: $1"
      return
    fi
    if "$PKG_BIN" --exists "$1" 2>/dev/null; then
      echo "[preflight][OK] pkg: $1"
      echo "[preflight]   cflags: $("$PKG_BIN" --cflags "$1" 2>/dev/null || true)"
      echo "[preflight]   libs:   $("$PKG_BIN" --libs "$1" 2>/dev/null || true)"
    else
      echo "[preflight][MISS] pkg: $1"
    fi
  }

  check_cmd make
  check_cmd "$CC_CMD"
  check_cmd "$PKG_CMD"

  if command -v "$PKG_CMD" >/dev/null 2>&1; then
    PKG_CONFIG="$PKG_CMD" check_pkg sdl2
    PKG_CONFIG="$PKG_CMD" check_pkg SDL2_image
    PKG_CONFIG="$PKG_CMD" check_pkg SDL2_ttf
    PKG_CONFIG="$PKG_CMD" check_pkg poppler-cpp

    if PKG_CONFIG="$PKG_CMD" "$PKG_CMD" --exists mupdf 2>/dev/null; then
      PKG_CONFIG="$PKG_CMD" check_pkg mupdf
    else
      PKG_CONFIG="$PKG_CMD" check_pkg fitz
    fi
  else
    echo "[preflight][SKIP] pkg checks: pkg-config command unavailable"
  fi

echo "[preflight] make print-config REQUIRE_MUPDF=1"
if make print-config REQUIRE_MUPDF=1 CXX="$CC_CMD" PKG_CONFIG="$PKG_CMD" >/tmp/rocreader_preflight_make.out 2>/tmp/rocreader_preflight_make.err; then
    cat /tmp/rocreader_preflight_make.out
    echo "[preflight][OK] make print-config"
  else
    cat /tmp/rocreader_preflight_make.out 2>/dev/null || true
    cat /tmp/rocreader_preflight_make.err 2>/dev/null || true
  echo "[preflight][FAIL] make print-config REQUIRE_MUPDF=1"
  fi
  rm -f /tmp/rocreader_preflight_make.out /tmp/rocreader_preflight_make.err
} | tee "$LOG_FILE"

echo "[preflight] log: $LOG_FILE"
