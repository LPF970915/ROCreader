#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

APPS_DIR="${APPS_DIR:-/Roms/APPS}"
OUT_DIR="$APPS_DIR/ROCreader"
LAUNCHER="$APPS_DIR/ROCreader.sh"
LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/package_$(date +%Y%m%d_%H%M%S).log"
REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"

echo "[roc_native] log: $LOG_FILE"

{
  echo "===== $(date '+%F %T') ====="
  echo "[roc_native] REQUIRE_MUPDF=$REQUIRE_MUPDF"
  echo "[roc_native] make print-config"
make print-config REQUIRE_MUPDF="$REQUIRE_MUPDF"

  if [ ! -x "./build/rocreader_sdl" ]; then
    echo "[roc_native] build binary missing, running make..."
make clean REQUIRE_MUPDF="$REQUIRE_MUPDF"
make REQUIRE_MUPDF="$REQUIRE_MUPDF"
  fi

  rm -rf "$OUT_DIR" "$APPS_DIR/ROCreader_native" "$APPS_DIR/ROCreader_native.sh"
  mkdir -p "$OUT_DIR"
  cp ./build/rocreader_sdl "$OUT_DIR/"
  if [ -d "$SELF_DIR/ui" ]; then
    command -v python3 >/dev/null 2>&1
    rm -f "$OUT_DIR/ui.pack"
    python3 "$SELF_DIR/scripts/pack_ui_assets.py" "$SELF_DIR/ui" "$OUT_DIR/ui.pack"
  fi
  if [ -d "$SELF_DIR/sounds" ]; then
    rm -rf "$OUT_DIR/sounds"
    cp -a "$SELF_DIR/sounds" "$OUT_DIR/"
  fi
  if [ -f "$SELF_DIR/native_keymap.ini" ]; then
    cp "$SELF_DIR/native_keymap.ini" "$OUT_DIR/"
  fi
  if [ -f "$SELF_DIR/native_config.ini" ]; then
    cp "$SELF_DIR/native_config.ini" "$OUT_DIR/"
  fi

  cat > "$LAUNCHER" <<'EOF'
#!/bin/sh
set -eu
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SELF_DIR/ROCreader"
BIN="$APP_DIR/rocreader_sdl"
LOG_FILE="${ROC_NATIVE_RUNTIME_LOG:-$SELF_DIR/ROCreader.log}"

{
  echo "===== $(date '+%F %T') ====="
  echo "[launcher] start"
  if command -v ldd >/dev/null 2>&1; then
    ldd "$BIN" || true
  fi
} >>"$LOG_FILE" 2>&1

export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/tmp/rocreader-xdg"
fi
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true

run_with_driver() {
  drv="$1"
  echo "[launcher] try driver=$drv" >>"$LOG_FILE"
  SDL_VIDEODRIVER="$drv" "$BIN" >>"$LOG_FILE" 2>&1
}

if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  run_with_driver "$SDL_VIDEODRIVER"
  exit $?
fi

for drv in wayland x11 kmsdrm fbcon directfb; do
  if run_with_driver "$drv"; then
    echo "[launcher] success driver=$drv" >>"$LOG_FILE"
    exit 0
  fi
  code=$?
  echo "[launcher] failed driver=$drv code=$code" >>"$LOG_FILE"
done

echo "[launcher] all drivers failed" >>"$LOG_FILE"
exit 1
EOF

  chmod +x "$OUT_DIR/rocreader_sdl"
  chmod +x "$LAUNCHER"

  echo "[roc_native] packaged:"
  echo "  $LAUNCHER"
  echo "  $OUT_DIR/"
} >>"$LOG_FILE" 2>&1
