#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/cross_compile_$(date +%Y%m%d_%H%M%S).log"

TOOL_PREFIX="${CROSS_TOOL_PREFIX:-arm-linux-gnueabihf}"
CXX_CMD="${CROSS_CXX:-${TOOL_PREFIX}-g++}"
PKG_CMD="${CROSS_PKG_CONFIG:-${TOOL_PREFIX}-pkg-config}"
REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"
BUNDLE_SDL2_CORE="${BUNDLE_SDL2_CORE:-0}"
BUNDLE_ALL_DEPS="${BUNDLE_ALL_DEPS:-1}"

DIST_ROOT="${DIST_ROOT:-$SELF_DIR/dist_h700}"
APPS_OUT="$DIST_ROOT/APPS"
RUNTIME_DIR="$APPS_OUT/ROCreader"
LAUNCHER="$APPS_OUT/ROCreader.sh"
TARBALL="$DIST_ROOT/ROCreader_APPS.tar.gz"
DIAG_SCRIPT="$RUNTIME_DIR/diagnose_runtime.sh"

echo "[roc_native] log: $LOG_FILE"

{
  echo "===== $(date '+%F %T') ====="
  echo "[cross] CXX=$CXX_CMD"
  echo "[cross] PKG_CONFIG=$PKG_CMD"
  echo "[cross] REQUIRE_MUPDF=$REQUIRE_MUPDF"
  echo "[cross] BUNDLE_SDL2_CORE=$BUNDLE_SDL2_CORE"
  echo "[cross] BUNDLE_ALL_DEPS=$BUNDLE_ALL_DEPS"
  echo "[cross] DIST_ROOT=$DIST_ROOT"

  command -v make
  command -v "$CXX_CMD"
  command -v "$PKG_CMD"

  echo "[cross] make print-config"
make print-config REQUIRE_MUPDF="$REQUIRE_MUPDF" CXX="$CXX_CMD" PKG_CONFIG="$PKG_CMD"

  echo "[cross] make clean"
make clean REQUIRE_MUPDF="$REQUIRE_MUPDF" CXX="$CXX_CMD" PKG_CONFIG="$PKG_CMD"
  echo "[cross] make"
make REQUIRE_MUPDF="$REQUIRE_MUPDF" CXX="$CXX_CMD" PKG_CONFIG="$PKG_CMD"

  echo "[cross] staging APPS output"
  rm -rf "$APPS_OUT"
  mkdir -p "$APPS_OUT"
  rm -rf "$RUNTIME_DIR"
  mkdir -p "$RUNTIME_DIR"
  cp ./build/rocreader_sdl "$RUNTIME_DIR/"
  if [ -d "$SELF_DIR/ui" ]; then
    command -v python3 >/dev/null 2>&1
    rm -f "$RUNTIME_DIR/ui.pack"
    python3 "$SELF_DIR/scripts/pack_ui_assets.py" "$SELF_DIR/ui" "$RUNTIME_DIR/ui.pack"
  fi
  if [ -d "$SELF_DIR/sounds" ]; then
    rm -rf "$RUNTIME_DIR/sounds"
    cp -a "$SELF_DIR/sounds" "$RUNTIME_DIR/"
  fi
  if [ -f "$SELF_DIR/native_keymap.ini" ]; then
    cp "$SELF_DIR/native_keymap.ini" "$RUNTIME_DIR/"
  fi
  if [ -f "$SELF_DIR/native_config.ini" ]; then
    cp "$SELF_DIR/native_config.ini" "$RUNTIME_DIR/"
  fi
  mkdir -p "$RUNTIME_DIR/lib"
  mkdir -p "$RUNTIME_DIR/lib/pulseaudio"

  echo "[cross] bundle runtime libs"
  is_forbidden_glibc_so() {
    case "$1" in
      libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|ld-linux-*.so.*|librt.so.*|libresolv.so.*)
        return 0
        ;;
      *)
        return 1
        ;;
    esac
  }
  # Bundle critical armhf shared libs for devices that lack them.
  for so in \
    libSDL2_image-2.0.so.0 \
    libSDL2_ttf-2.0.so.0 \
    libmupdf.so \
    libmupdf.so.1 \
    libmupdf-third.so \
    libmupdf-third.so.1 \
    libjbig2dec.so.0 \
    libpoppler-cpp.so.0 \
    libpoppler.so.134 \
    libpoppler.so.126 \
    libtiff.so.6 \
    libtiff.so.5 \
    libjbig.so.0 \
    libdeflate.so.0 \
    liblzma.so.5 \
    libzstd.so.1 \
    libstdc++.so.6 \
    libgcc_s.so.1 \
    libfreetype.so.6 \
    libharfbuzz.so.0 \
    libpng16.so.16 \
    libjpeg.so.8 \
    libwebp.so.7 \
    libwebpdemux.so.2 \
    libwebpmux.so.3 \
    libsharpyuv.so.0 \
    libz.so.1 \
    libc.so.6 \
    libm.so.6 \
    libpthread.so.0 \
    libdl.so.2; do
    if is_forbidden_glibc_so "$so"; then
      continue
    fi
    for d in /usr/lib/arm-linux-gnueabihf /lib/arm-linux-gnueabihf; do
      if [ -f "$d/$so" ]; then
        cp -L "$d/$so" "$RUNTIME_DIR/lib/" || true
        break
      fi
    done
  done

  # Bundle pulse common from pulseaudio private dir when available.
  for so in libpulsecommon-16.1.so libpulsecommon-15.0.so; do
    for d in \
      /usr/lib/arm-linux-gnueabihf/pulseaudio \
      /usr/lib/pulseaudio \
      /usr/lib32/pulseaudio \
      /lib/pulseaudio; do
      if [ -f "$d/$so" ]; then
        cp -L "$d/$so" "$RUNTIME_DIR/lib/pulseaudio/" || true
        break
      fi
    done
  done
  SEARCH_DIRS="
/usr/lib/arm-linux-gnueabihf
/lib/arm-linux-gnueabihf
/usr/lib/arm-linux-gnueabihf/pulseaudio
/usr/lib/pulseaudio
/usr/lib32/pulseaudio
/lib/pulseaudio
"
  resolve_and_copy_so() {
    name="$1"
    if is_forbidden_glibc_so "$name"; then
      return 1
    fi
    for d in $SEARCH_DIRS; do
      if [ -f "$d/$name" ]; then
        cp -L "$d/$name" "$RUNTIME_DIR/lib/" || true
        # Keep a second copy for pulseaudio-private libs for compatibility.
        case "$d" in
          */pulseaudio)
            cp -L "$d/$name" "$RUNTIME_DIR/lib/pulseaudio/" || true
            ;;
        esac
        return 0
      fi
    done
    # Fallback: search recursively in common roots.
    for root in /usr/lib/arm-linux-gnueabihf /lib/arm-linux-gnueabihf /usr/lib /lib; do
      found="$(find "$root" -type f -name "$name" 2>/dev/null | head -n 1 || true)"
      if [ -n "$found" ] && [ -f "$found" ]; then
        cp -L "$found" "$RUNTIME_DIR/lib/" || true
        case "$found" in
          */pulseaudio/*)
            cp -L "$found" "$RUNTIME_DIR/lib/pulseaudio/" || true
            ;;
        esac
        return 0
      fi
    done
    return 1
  }
  collect_needed_once() {
    src="$1"
    if command -v arm-linux-gnueabihf-readelf >/dev/null 2>&1; then
      arm-linux-gnueabihf-readelf -d "$src" 2>/dev/null | grep NEEDED | sed -n 's/.*\[\(.*\)\].*/\1/p'
      return 0
    fi
    if command -v readelf >/dev/null 2>&1; then
      readelf -d "$src" 2>/dev/null | grep NEEDED | sed -n 's/.*\[\(.*\)\].*/\1/p'
      return 0
    fi
    return 1
  }
  if [ "$BUNDLE_ALL_DEPS" = "1" ]; then
    # Prime likely runtime deps so closure can continue from them too.
    for so in libpulse.so.0 libpulse-simple.so.0 libasound.so.2 libdbus-1.so.3 libsndfile.so.1; do
      resolve_and_copy_so "$so" || true
    done
  fi

  echo "[cross] recursive dependency closure"
  changed=1
  pass=0
  while [ "$changed" -eq 1 ] && [ "$pass" -lt 24 ]; do
    changed=0
    pass=$((pass + 1))
    for src in "$RUNTIME_DIR/rocreader_sdl" "$RUNTIME_DIR"/lib/*.so*; do
      [ -f "$src" ] || continue
      for need in $(collect_needed_once "$src" || true); do
        [ -z "$need" ] && continue
        [ -f "$RUNTIME_DIR/lib/$need" ] && continue
        if resolve_and_copy_so "$need"; then
          changed=1
        fi
      done
    done
  done
  echo "[cross] closure passes=$pass"
  if [ "$BUNDLE_SDL2_CORE" = "1" ]; then
    for d in /usr/lib/arm-linux-gnueabihf /lib/arm-linux-gnueabihf; do
      if [ -f "$d/libSDL2-2.0.so.0" ]; then
        cp -L "$d/libSDL2-2.0.so.0" "$RUNTIME_DIR/lib/" || true
        break
      fi
    done
  else
    echo "[cross] skip bundling libSDL2-2.0.so.0 (use system SDL2 to avoid libpulse mismatch)"
  fi
  # Never ship glibc core libs; they must come from the target system.
  rm -f \
    "$RUNTIME_DIR/lib/libc.so."* \
    "$RUNTIME_DIR/lib/libm.so."* \
    "$RUNTIME_DIR/lib/libpthread.so."* \
    "$RUNTIME_DIR/lib/libdl.so."* \
    "$RUNTIME_DIR/lib/ld-linux-"*.so.* \
    "$RUNTIME_DIR/lib/librt.so."* \
    "$RUNTIME_DIR/lib/libresolv.so."* 2>/dev/null || true
  ls -1 "$RUNTIME_DIR/lib" || true

  cat > "$LAUNCHER" <<'EOF'
#!/bin/sh
set -eu
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SELF_DIR/ROCreader"
BIN="$APP_DIR/rocreader_sdl"
LOG_FILE="${ROC_NATIVE_RUNTIME_LOG:-$SELF_DIR/ROCreader.log}"
export LD_LIBRARY_PATH="/usr/lib32:/usr/lib:/lib:/mnt/vendor/lib:$APP_DIR/lib:$APP_DIR/lib/pulseaudio:/usr/lib32/pulseaudio:/usr/lib/pulseaudio:/usr/lib/arm-linux-gnueabihf/pulseaudio:${LD_LIBRARY_PATH:-}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/tmp/rocreader-xdg"
fi
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true

{
  echo "===== $(date '+%F %T') ====="
  echo "[launcher] start"
  echo "[launcher] app=$APP_DIR"
  echo "[launcher] bin=$BIN"
  echo "[launcher] pwd=$(pwd)"
  echo "[launcher] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
  uname -a 2>/dev/null || true
  if command -v ldd >/dev/null 2>&1; then
    echo "[launcher] ldd begin"
    ldd "$BIN" || true
    echo "[launcher] ldd end"
  else
    echo "[launcher] ldd unavailable"
  fi
} >>"$LOG_FILE" 2>&1

if [ -f "$APP_DIR/diagnose_runtime.sh" ]; then
  sh "$APP_DIR/diagnose_runtime.sh" >>"$LOG_FILE" 2>&1 || true
fi

run_with_driver() {
  drv="$1"
  echo "[launcher] try driver=$drv" >>"$LOG_FILE"
  SDL_VIDEODRIVER="$drv" "$BIN" >>"$LOG_FILE" 2>&1
}

if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  run_with_driver "$SDL_VIDEODRIVER"
  code=$?
  echo "[launcher] exit_code=$code driver=$SDL_VIDEODRIVER" >>"$LOG_FILE"
  exit "$code"
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

  cat > "$DIAG_SCRIPT" <<'EOF'
#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SELF_DIR/rocreader_sdl"

echo "[diag] begin"
if command -v readelf >/dev/null 2>&1; then
  echo "[diag] needed libs (readelf)"
  readelf -d "$BIN" 2>/dev/null | grep NEEDED || true
fi

echo "[diag] library probe"
for f in \
  /usr/lib/libSDL2-2.0.so.0 \
  /usr/lib/libSDL2_image-2.0.so.0 \
  /usr/lib/libSDL2_ttf-2.0.so.0 \
  /usr/lib/libmupdf.so \
  /usr/lib/libmupdf.so.1 \
  /usr/lib/libmupdf-third.so \
  /usr/lib/libmupdf-third.so.1 \
  /usr/lib/libjbig2dec.so.0 \
  /usr/lib/libpoppler-cpp.so.0 \
  /usr/lib/libpoppler.so.134 \
  /usr/lib/libpoppler.so.126 \
  /usr/lib/pulseaudio/libpulsecommon-16.1.so \
  /usr/lib/libtiff.so.6 \
  /usr/lib/libtiff.so.5 \
  /usr/lib/libwebpdemux.so.2 \
  /usr/lib/libwebpmux.so.3 \
  /usr/lib32/libSDL2-2.0.so.0 \
  /usr/lib32/libSDL2_image-2.0.so.0 \
  /usr/lib32/libSDL2_ttf-2.0.so.0 \
  /usr/lib32/libmupdf.so \
  /usr/lib32/libmupdf.so.1 \
  /usr/lib32/libmupdf-third.so \
  /usr/lib32/libmupdf-third.so.1 \
  /usr/lib32/libjbig2dec.so.0 \
  /usr/lib32/libpoppler-cpp.so.0 \
  /usr/lib32/libpoppler.so.134 \
  /usr/lib32/libpoppler.so.126 \
  /usr/lib32/pulseaudio/libpulsecommon-16.1.so \
  /usr/lib32/libtiff.so.6 \
  /usr/lib32/libtiff.so.5 \
  /usr/lib32/libwebpdemux.so.2 \
  /usr/lib32/libwebpmux.so.3 \
  /lib/libSDL2-2.0.so.0 \
  /lib/libSDL2_image-2.0.so.0 \
  /lib/libSDL2_ttf-2.0.so.0 \
  /lib/libmupdf.so \
  /lib/libmupdf.so.1 \
  /lib/libmupdf-third.so \
  /lib/libmupdf-third.so.1 \
  /lib/libjbig2dec.so.0 \
  /lib/libpoppler-cpp.so.0 \
  /lib/libpoppler.so.134 \
  /lib/libpoppler.so.126 \
  /lib/pulseaudio/libpulsecommon-16.1.so \
  /lib/libtiff.so.6 \
  /lib/libtiff.so.5 \
  /lib/libwebpdemux.so.2 \
  /lib/libwebpmux.so.3; do
  if [ -f "$f" ]; then
    echo "[diag] found $f"
  fi
done
echo "[diag] end"
EOF
  chmod +x "$RUNTIME_DIR/rocreader_sdl"
  chmod +x "$DIAG_SCRIPT"
  chmod +x "$LAUNCHER"

  echo "[cross] tarball"
  mkdir -p "$DIST_ROOT"
  rm -f "$TARBALL"
  tar -C "$DIST_ROOT" -czf "$TARBALL" APPS

  echo "[cross] done"
  echo "[cross] runtime: $RUNTIME_DIR"
  echo "[cross] launcher: $LAUNCHER"
  echo "[cross] tarball: $TARBALL"
} >>"$LOG_FILE" 2>&1
