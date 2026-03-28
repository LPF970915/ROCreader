#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

LOG_DIR="${ROC_NATIVE_LOG_DIR:-$SELF_DIR/logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/cross_low_glibc_$(date +%Y%m%d_%H%M%S).log"

SYSROOT="${SYSROOT:-$SELF_DIR/sysroot_device}"
TOOL_PREFIX="${CROSS_TOOL_PREFIX:-aarch64-linux-gnu}"
CXX_CMD="${CROSS_CXX:-${TOOL_PREFIX}-g++}"
READ_ELF="${CROSS_READELF:-${TOOL_PREFIX}-readelf}"
PKG_CMD="${CROSS_PKG_CONFIG:-pkg-config}"
REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"

DIST_ROOT="${DIST_ROOT:-$SELF_DIR/dist_lowglibc}"
APPS_OUT="$DIST_ROOT/APPS"
RUNTIME_DIR="$APPS_OUT/ROCreader"
LAUNCHER="$APPS_OUT/ROCreader.sh"
TARBALL="$DIST_ROOT/ROCreader_APPS_lowglibc.tar.gz"
# Release rule: Downloads only keeps final versioned zip files.
# The zip is assembled from dist_lowglibc/release_stage and contains:
# - Roms/APPS/Imgs/ROCreader.png
# - Roms/APPS/ROCreader.sh
# - Roms/APPS/ROCreader/ (with fonts/sounds/lib/lib_system_sdl plus empty books/book_covers/cache)
DOWNLOADS_ROOT="${DOWNLOADS_ROOT:-$SELF_DIR/Downloads}"
ZIP_STAGE_ROOT="$DIST_ROOT/release_stage"
ZIP_STAGE_APPS="$ZIP_STAGE_ROOT/Roms/APPS"
ZIP_STAGE_RUNTIME="$ZIP_STAGE_APPS/ROCreader"
ZIP_STAGE_IMGS="$ZIP_STAGE_APPS/Imgs"

next_download_zip() {
  python3 - "$DOWNLOADS_ROOT" <<'PY'
import os
import re
import sys

downloads = sys.argv[1]
pattern = re.compile(r"^ROC全能漫画阅读器ver(\d+)\.(\d+)\.zip$")
best = None

if os.path.isdir(downloads):
    for name in os.listdir(downloads):
        match = pattern.match(name)
        if not match:
            continue
        major = int(match.group(1))
        minor = int(match.group(2))
        value = (major, minor)
        if best is None or value > best:
            best = value

if best is None:
    major, minor = 0, 1
else:
    major, minor = best[0], best[1] + 1

print(os.path.join(downloads, f"ROC全能漫画阅读器ver{major}.{minor}.zip"))
PY
}

ZIPFILE="${DOWNLOAD_ZIP_FILE:-$(next_download_zip)}"

if [ ! -d "$SYSROOT" ]; then
  echo "[low_glibc] ERROR: SYSROOT not found: $SYSROOT"
  echo "[low_glibc] Run sync_device_sysroot.sh first."
  exit 1
fi

if [ ! -d "$SYSROOT/usr/include" ] || [ ! -d "$SYSROOT/usr/lib" ]; then
  echo "[low_glibc] ERROR: invalid SYSROOT: missing usr/include or usr/lib"
  exit 1
fi

find_pkg_dirs() {
  for d in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig" \
    "$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig" \
    "$SYSROOT/usr/lib/pkgconfig" \
    "$SYSROOT/lib/aarch64-linux-gnu/pkgconfig" \
    "$SYSROOT/lib/arm-linux-gnueabihf/pkgconfig" \
    "$SYSROOT/lib/pkgconfig" \
    "$SYSROOT/usr/share/pkgconfig"; do
    [ -d "$d" ] && printf "%s:" "$d"
  done
}

PKG_LIBDIR="$(find_pkg_dirs)"
PKG_LIBDIR="${PKG_LIBDIR%:}"

find_libdir() {
  for d in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu" \
    "$SYSROOT/lib/aarch64-linux-gnu" \
    "$SYSROOT/usr/lib/arm-linux-gnueabihf" \
    "$SYSROOT/lib/arm-linux-gnueabihf" \
    "$SYSROOT/usr/lib" \
    "$SYSROOT/lib"; do
    [ -d "$d" ] && { printf "%s" "$d"; return 0; }
  done
  return 1
}

LIBDIR="$(find_libdir || true)"
if [ -z "$LIBDIR" ]; then
  echo "[low_glibc] ERROR: cannot find library directory in SYSROOT"
  exit 1
fi

has_so_in_sysroot() {
  name="$1"
  for d in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu" \
    "$SYSROOT/lib/aarch64-linux-gnu" \
    "$SYSROOT/usr/lib/arm-linux-gnueabihf" \
    "$SYSROOT/lib/arm-linux-gnueabihf" \
    "$SYSROOT/usr/lib" \
    "$SYSROOT/lib"; do
    if [ -f "$d/lib${name}.so" ] || ls "$d/lib${name}.so."* >/dev/null 2>&1; then
      return 0
    fi
  done
  return 1
}

find_so_in_sysroot() {
  name="$1"
  for d in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu" \
    "$SYSROOT/lib/aarch64-linux-gnu" \
    "$SYSROOT/usr/lib/arm-linux-gnueabihf" \
    "$SYSROOT/lib/arm-linux-gnueabihf" \
    "$SYSROOT/usr/lib" \
    "$SYSROOT/lib"; do
    if [ -f "$d/lib${name}.so" ]; then
      printf "%s/lib%s.so" "$d" "$name"
      return 0
    fi
    so_ver="$(ls "$d/lib${name}.so."* 2>/dev/null | head -n 1 || true)"
    if [ -n "$so_ver" ]; then
      printf "%s" "$so_ver"
      return 0
    fi
  done
  return 1
}

find_so_in_libdir() {
  name="$1"
  if [ -f "$LIBDIR/lib${name}.so" ]; then
    printf "%s/lib%s.so" "$LIBDIR" "$name"
    return 0
  fi
  so_ver="$(ls "$LIBDIR/lib${name}.so."* 2>/dev/null | head -n 1 || true)"
  if [ -n "$so_ver" ]; then
    printf "%s" "$so_ver"
    return 0
  fi
  return 1
}

{
  echo "===== $(date '+%F %T') ====="
  echo "[low_glibc] SYSROOT=$SYSROOT"
  echo "[low_glibc] CXX=$CXX_CMD"
  echo "[low_glibc] READELF=$READ_ELF"
  echo "[low_glibc] PKG_CONFIG=$PKG_CMD"
  echo "[low_glibc] REQUIRE_MUPDF=$REQUIRE_MUPDF"
  echo "[low_glibc] DIST_ROOT=$DIST_ROOT"

  command -v "$CXX_CMD"
  command -v "$PKG_CMD"
  command -v "$READ_ELF" || true

  export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
  export PKG_CONFIG_LIBDIR="$PKG_LIBDIR"
  export PKG_CONFIG_PATH=""

  echo "[low_glibc] PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR"
  "$PKG_CMD" --exists sdl2 && echo "[low_glibc] pkg OK: sdl2" || echo "[low_glibc] pkg MISS: sdl2"
  "$PKG_CMD" --exists SDL2_image && echo "[low_glibc] pkg OK: SDL2_image" || echo "[low_glibc] pkg MISS: SDL2_image"
  "$PKG_CMD" --exists SDL2_ttf && echo "[low_glibc] pkg OK: SDL2_ttf" || echo "[low_glibc] pkg MISS: SDL2_ttf"
  "$PKG_CMD" --exists SDL2_mixer && echo "[low_glibc] pkg OK: SDL2_mixer" || echo "[low_glibc] pkg MISS: SDL2_mixer"
  "$PKG_CMD" --exists poppler-cpp && echo "[low_glibc] pkg OK: poppler-cpp" || echo "[low_glibc] pkg MISS: poppler-cpp"
  "$PKG_CMD" --exists libzip && echo "[low_glibc] pkg OK: libzip" || echo "[low_glibc] pkg MISS: libzip"
  "$PKG_CMD" --exists mupdf && echo "[low_glibc] pkg OK: mupdf" || echo "[low_glibc] pkg MISS: mupdf"
  "$PKG_CMD" --exists fitz && echo "[low_glibc] pkg OK: fitz" || echo "[low_glibc] pkg MISS: fitz"
  if [ -f "$SYSROOT/usr/include/zip.h" ]; then
    echo "[low_glibc] header OK: zip.h"
  else
    echo "[low_glibc] header MISS: zip.h"
  fi
  TARGET_LIBZIP_SO="$(find_so_in_libdir zip || true)"
  if [ -n "$TARGET_LIBZIP_SO" ]; then
    echo "[low_glibc] target lib OK: $TARGET_LIBZIP_SO"
  else
    echo "[low_glibc] target lib MISS: libzip for $(basename "$LIBDIR")"
  fi

  # Fallback: force-enable feature libs from sysroot when pkg-config metadata
  # is incomplete/mismatched but headers + shared libs are present.
  FALLBACK_IMG_CFLAGS=""
  FALLBACK_IMG_LIBS=""
  FALLBACK_TTF_CFLAGS=""
  FALLBACK_TTF_LIBS=""
  FALLBACK_MIX_CFLAGS=""
  FALLBACK_MIX_LIBS=""
  FALLBACK_POPPLER_CFLAGS=""
  FALLBACK_POPPLER_LIBS=""
  FALLBACK_LIBZIP_CFLAGS=""
  FALLBACK_LIBZIP_LIBS=""
  FALLBACK_MUPDF_CFLAGS=""
  FALLBACK_MUPDF_LIBS=""

  if [ -f "$SYSROOT/usr/include/SDL2/SDL_image.h" ] && \
     { [ -f "$LIBDIR/libSDL2_image-2.0.so" ] || [ -f "$LIBDIR/libSDL2_image-2.0.so.0" ]; }; then
    FALLBACK_IMG_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_IMG_LIBS="-L$LIBDIR -lSDL2_image"
    echo "[low_glibc] fallback enable: SDL2_image"
  fi

  if [ -f "$SYSROOT/usr/include/SDL2/SDL_ttf.h" ] && \
     { [ -f "$LIBDIR/libSDL2_ttf-2.0.so" ] || [ -f "$LIBDIR/libSDL2_ttf-2.0.so.0" ]; }; then
    FALLBACK_TTF_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_TTF_LIBS="-L$LIBDIR -lSDL2_ttf"
    echo "[low_glibc] fallback enable: SDL2_ttf"
  fi

  if [ -f "$SYSROOT/usr/include/SDL2/SDL_mixer.h" ] && \
     { [ -f "$LIBDIR/libSDL2_mixer-2.0.so" ] || [ -f "$LIBDIR/libSDL2_mixer-2.0.so.0" ]; }; then
    FALLBACK_MIX_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_MIX_LIBS="-L$LIBDIR -lSDL2_mixer"
    echo "[low_glibc] fallback enable: SDL2_mixer"
  fi

  if [ -f "$SYSROOT/usr/include/poppler/cpp/poppler-document.h" ] && \
     { [ -f "$LIBDIR/libpoppler-cpp.so" ] || [ -f "$LIBDIR/libpoppler-cpp.so.0" ]; }; then
    FALLBACK_POPPLER_CFLAGS="-I$SYSROOT/usr/include/poppler"
    FALLBACK_POPPLER_LIBS="-L$LIBDIR -lpoppler-cpp -lpoppler"
    echo "[low_glibc] fallback enable: poppler-cpp"
  fi

  if [ -f "$SYSROOT/usr/include/zip.h" ] && [ -n "${TARGET_LIBZIP_SO:-}" ]; then
    FALLBACK_LIBZIP_CFLAGS="-I$SYSROOT/usr/include"
    FALLBACK_LIBZIP_LIBS="-L$LIBDIR -lzip"
    echo "[low_glibc] fallback enable: libzip"
  fi

  if [ -f "$SYSROOT/usr/include/mupdf/fitz.h" ] && \
     { [ -f "$LIBDIR/libmupdf.so" ] || [ -f "$LIBDIR/libmupdf.so.1" ]; }; then
    FALLBACK_MUPDF_CFLAGS="-I$SYSROOT/usr/include"
    # Use explicit .so paths when available to avoid missing unversioned symlinks.
    MUPDF_SO="$(ls "$LIBDIR"/libmupdf.so* 2>/dev/null | head -n 1 || true)"
    MUPDF_THIRD_SO="$(ls "$LIBDIR"/libmupdf-third.so* 2>/dev/null | head -n 1 || true)"
    JBIG2_SO="$(ls "$LIBDIR"/libjbig2dec.so* 2>/dev/null | head -n 1 || true)"
    FALLBACK_MUPDF_LIBS=""
    [ -n "$MUPDF_SO" ] && FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS $MUPDF_SO"
    [ -n "$MUPDF_THIRD_SO" ] && FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS $MUPDF_THIRD_SO"
    [ -n "$JBIG2_SO" ] && FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS $JBIG2_SO"
    # Keep -L for dependency lookup, but avoid forcing -lmupdf which may fail
    # when only versioned SONAME files exist in sysroot.
    FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS -L$LIBDIR"
    if [ -n "$MUPDF_THIRD_SO" ] || [ -f "$LIBDIR/libmupdf-third.so" ]; then
      FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS -lmupdf-third"
    fi
    if [ -n "$JBIG2_SO" ] || [ -f "$LIBDIR/libjbig2dec.so" ] || [ -f "$LIBDIR/libjbig2dec.so.0" ]; then
      FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS -ljbig2dec"
    fi
    echo "[low_glibc] fallback enable: mupdf"
    echo "[low_glibc] fallback mupdf libs: $FALLBACK_MUPDF_LIBS"
  fi

  # If fallback probing misses, do not force-empty MUPDF vars into make.
  # Use pkg-config result first, then fallback values.
  MUPDF_CFLAGS_FINAL="$FALLBACK_MUPDF_CFLAGS"
  MUPDF_LIBS_FINAL="$FALLBACK_MUPDF_LIBS"
  if [ -z "$MUPDF_CFLAGS_FINAL" ]; then
    MUPDF_CFLAGS_FINAL="$("$PKG_CMD" --cflags mupdf 2>/dev/null || "$PKG_CMD" --cflags fitz 2>/dev/null || true)"
  fi
  if [ -z "$MUPDF_LIBS_FINAL" ]; then
    MUPDF_LIBS_FINAL="$("$PKG_CMD" --libs mupdf 2>/dev/null || "$PKG_CMD" --libs fitz 2>/dev/null || true)"
  fi
  # If pkg-config returns -lmupdf style flags, strip them unconditionally.
  # We will append absolute SONAME paths from sysroot below.
  MUPDF_LIBS_FILTERED=""
  for tok in $MUPDF_LIBS_FINAL; do
    case "$tok" in
      -lmupdf|-lmupdf-third|-ljbig2dec) ;;
      *) MUPDF_LIBS_FILTERED="$MUPDF_LIBS_FILTERED $tok" ;;
    esac
  done
  MUPDF_LIBS_FINAL="$MUPDF_LIBS_FILTERED"
  MUPDF_SO_ABS="$(find_so_in_sysroot mupdf || true)"
  MUPDF_THIRD_SO_ABS="$(find_so_in_sysroot mupdf-third || true)"
  JBIG2_SO_ABS="$(find_so_in_sysroot jbig2dec || true)"
  [ -n "$MUPDF_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $MUPDF_SO_ABS"
  [ -n "$MUPDF_THIRD_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $MUPDF_THIRD_SO_ABS"
  [ -n "$JBIG2_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $JBIG2_SO_ABS"
  # Some distro sysroots ship a mupdf.pc file without the actual libmupdf shared
  # object. In that case, disable MuPDF entirely and let Poppler provide the real
  # renderer instead of failing the final link step on unresolved fz_* symbols.
  if [ -z "$MUPDF_SO_ABS" ]; then
    MUPDF_CFLAGS_FINAL=""
    MUPDF_LIBS_FINAL=""
    echo "[low_glibc] disable mupdf: libmupdf shared library not found in sysroot"
  fi
  # Some sysroot MuPDF packages don't expose full transitive link deps in .pc.
  # Add common runtime deps explicitly when present to avoid "DSO missing".
  if [ -n "$MUPDF_LIBS_FINAL" ]; then
    for dep in jpeg openjp2 png16 z bz2 harfbuzz freetype gumbo mujs jbig2dec; do
      if has_so_in_sysroot "$dep"; then
        dep_so="$(find_so_in_sysroot "$dep" || true)"
        if [ -n "$dep_so" ]; then
          case " $MUPDF_LIBS_FINAL " in
            *" $dep_so "*) : ;;
            *) MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $dep_so" ;;
          esac
        else
          case " $MUPDF_LIBS_FINAL " in
            *" -l$dep "*) : ;;
            *) MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL -l$dep" ;;
          esac
        fi
      fi
    done
  fi
  echo "[low_glibc] final mupdf cflags: $MUPDF_CFLAGS_FINAL"
  echo "[low_glibc] final mupdf libs: $MUPDF_LIBS_FINAL"
  LIBZIP_CFLAGS_FINAL="$FALLBACK_LIBZIP_CFLAGS"
  LIBZIP_LIBS_FINAL="$FALLBACK_LIBZIP_LIBS"
  if [ -z "$LIBZIP_CFLAGS_FINAL" ]; then
    LIBZIP_CFLAGS_FINAL="$("$PKG_CMD" --cflags libzip 2>/dev/null || true)"
  fi
  if [ -z "$LIBZIP_LIBS_FINAL" ]; then
    LIBZIP_LIBS_FINAL="$("$PKG_CMD" --libs libzip 2>/dev/null || true)"
  fi
  if [ -z "$LIBZIP_CFLAGS_FINAL" ] || [ -z "$LIBZIP_LIBS_FINAL" ]; then
    echo "[low_glibc] libzip not enabled for target build"
  fi
  echo "[low_glibc] LIBZIP_CFLAGS=$LIBZIP_CFLAGS_FINAL"
  echo "[low_glibc] LIBZIP_LIBS=$LIBZIP_LIBS_FINAL"

  echo "[low_glibc] make clean"
  make clean
  echo "[low_glibc] make"
  make \
    CXX="$CXX_CMD" \
    PKG_CONFIG="$PKG_CMD" \
    REQUIRE_MUPDF="$REQUIRE_MUPDF" \
    SDL_CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include/SDL2 -I$SYSROOT/usr/include -D_REENTRANT" \
    SDL_LIBS="-L$LIBDIR -lSDL2" \
    IMG_CFLAGS="$FALLBACK_IMG_CFLAGS" \
    IMG_LIBS="$FALLBACK_IMG_LIBS" \
    TTF_CFLAGS="$FALLBACK_TTF_CFLAGS" \
    TTF_LIBS="$FALLBACK_TTF_LIBS" \
    MIX_CFLAGS="$FALLBACK_MIX_CFLAGS" \
    MIX_LIBS="$FALLBACK_MIX_LIBS" \
    POPPLER_CFLAGS="$FALLBACK_POPPLER_CFLAGS" \
    POPPLER_LIBS="$FALLBACK_POPPLER_LIBS" \
    LIBZIP_CFLAGS="$LIBZIP_CFLAGS_FINAL" \
    LIBZIP_LIBS="$LIBZIP_LIBS_FINAL" \
    MUPDF_CFLAGS="$MUPDF_CFLAGS_FINAL" \
    MUPDF_LIBS="$MUPDF_LIBS_FINAL" \
    EXTRA_CXXFLAGS="--sysroot=$SYSROOT" \
    EXTRA_LDFLAGS="--sysroot=$SYSROOT"

  rm -rf "$APPS_OUT"
  mkdir -p "$APPS_OUT"
  rm -rf "$RUNTIME_DIR"
  mkdir -p "$RUNTIME_DIR/lib"
  mkdir -p "$RUNTIME_DIR/lib_system_sdl"
  mkdir -p "$RUNTIME_DIR/lib/pulseaudio"
  mkdir -p "$RUNTIME_DIR/lib_system_sdl/pulseaudio"
  mkdir -p "$RUNTIME_DIR/books"
  mkdir -p "$RUNTIME_DIR/book_covers"
  mkdir -p "$RUNTIME_DIR/cache"
  cp ./build/rocreader_sdl "$RUNTIME_DIR/"
  if [ -d "$SELF_DIR/ui" ]; then
    command -v python3 >/dev/null 2>&1
    rm -f "$RUNTIME_DIR/ui.pack"
    python3 "$SELF_DIR/scripts/pack_ui_assets.py" "$SELF_DIR/ui" "$RUNTIME_DIR/ui.pack"
  fi
  if [ -d "$SELF_DIR/fonts" ]; then
    rm -rf "$RUNTIME_DIR/fonts"
    cp -a "$SELF_DIR/fonts" "$RUNTIME_DIR/"
  fi
  if [ ! -f "$RUNTIME_DIR/fonts/ui_font.ttf" ]; then
    echo "[low_glibc] ERROR: packaged font missing in runtime dir: ui_font.ttf"
    exit 1
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

  collect_needed() {
    src="$1"
    if command -v "$READ_ELF" >/dev/null 2>&1; then
      "$READ_ELF" -d "$src" 2>/dev/null | grep NEEDED | sed -n 's/.*\[\(.*\)\].*/\1/p'
      return 0
    fi
    readelf -d "$src" 2>/dev/null | grep NEEDED | sed -n 's/.*\[\(.*\)\].*/\1/p'
  }

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

  resolve_and_copy_so() {
    name="$1"
    is_forbidden_glibc_so "$name" && return 1
    for d in \
      "$SYSROOT/usr/lib/aarch64-linux-gnu" \
      "$SYSROOT/lib/aarch64-linux-gnu" \
      "$SYSROOT/usr/lib/arm-linux-gnueabihf" \
      "$SYSROOT/lib/arm-linux-gnueabihf" \
      "$SYSROOT/usr/lib" \
      "$SYSROOT/lib" \
      "$SYSROOT/usr/lib/pulseaudio"; do
      if [ -f "$d/$name" ]; then
        cp -L "$d/$name" "$RUNTIME_DIR/lib/" || true
        case "$d" in
          */pulseaudio) cp -L "$d/$name" "$RUNTIME_DIR/lib/pulseaudio/" || true ;;
        esac
        return 0
      fi
    done
    return 1
  }

  echo "[low_glibc] dependency closure"
  changed=1
  pass=0
  while [ "$changed" -eq 1 ] && [ "$pass" -lt 24 ]; do
    changed=0
    pass=$((pass + 1))
    for src in "$RUNTIME_DIR/rocreader_sdl" "$RUNTIME_DIR"/lib/*.so*; do
      [ -f "$src" ] || continue
      for need in $(collect_needed "$src" || true); do
        [ -z "$need" ] && continue
        [ -f "$RUNTIME_DIR/lib/$need" ] && continue
        if resolve_and_copy_so "$need"; then
          changed=1
        fi
      done
    done
  done
  echo "[low_glibc] closure passes=$pass"

  rm -f \
    "$RUNTIME_DIR/lib/libc.so."* \
    "$RUNTIME_DIR/lib/libm.so."* \
    "$RUNTIME_DIR/lib/libpthread.so."* \
    "$RUNTIME_DIR/lib/libdl.so."* \
    "$RUNTIME_DIR/lib/ld-linux-"*.so.* \
    "$RUNTIME_DIR/lib/librt.so."* \
    "$RUNTIME_DIR/lib/libresolv.so."* 2>/dev/null || true

  LIBZIP_SO_ABS="${TARGET_LIBZIP_SO:-}"
  if [ -n "$LIBZIP_SO_ABS" ] && [ ! -f "$RUNTIME_DIR/lib/$(basename "$LIBZIP_SO_ABS")" ]; then
    cp -L "$LIBZIP_SO_ABS" "$RUNTIME_DIR/lib/" || true
  fi

  cp -a "$RUNTIME_DIR/lib/." "$RUNTIME_DIR/lib_system_sdl/"
  rm -f \
    "$RUNTIME_DIR/lib_system_sdl/libdrm.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libgbm.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libwayland-client.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libwayland-cursor.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libwayland-egl.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libwayland-server.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libX11.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXau.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libxcb.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXcursor.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXdmcp.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXext.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXfixes.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXi.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXinerama.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libxkbcommon.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXrandr.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXrender.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXss.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libXxf86vm.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libdecor-0.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libdbus-1.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libsystemd.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libffi.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libSDL2-2.0.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libSDL2_image-2.0.so."* \
    "$RUNTIME_DIR/lib_system_sdl/libSDL2_ttf-2.0.so."* 2>/dev/null || true

  cat > "$LAUNCHER" <<'EOF'
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
EOF

  chmod +x "$RUNTIME_DIR/rocreader_sdl"
  chmod +x "$LAUNCHER"

  mkdir -p "$DIST_ROOT"
  rm -f "$TARBALL"
  tar -C "$DIST_ROOT" -czf "$TARBALL" APPS

  mkdir -p "$DOWNLOADS_ROOT"
  rm -rf "$ZIP_STAGE_ROOT"
  mkdir -p "$ZIP_STAGE_RUNTIME"
  mkdir -p "$ZIP_STAGE_IMGS"
  cp -a "$RUNTIME_DIR/." "$ZIP_STAGE_RUNTIME/"
  mkdir -p "$ZIP_STAGE_RUNTIME/books" "$ZIP_STAGE_RUNTIME/book_covers" "$ZIP_STAGE_RUNTIME/cache"
  find "$ZIP_STAGE_RUNTIME/books" -mindepth 1 -delete 2>/dev/null || true
  find "$ZIP_STAGE_RUNTIME/book_covers" -mindepth 1 -delete 2>/dev/null || true
  find "$ZIP_STAGE_RUNTIME/cache" -mindepth 1 -delete 2>/dev/null || true
  if [ ! -f "$ZIP_STAGE_RUNTIME/fonts/ui_font.ttf" ]; then
    echo "[low_glibc] ERROR: packaged font missing in release stage: ui_font.ttf"
    exit 1
  fi
  cp "$LAUNCHER" "$ZIP_STAGE_APPS/ROCreader.sh"
  if [ -f "$SELF_DIR/ui/ROCreader.png" ]; then
    cp "$SELF_DIR/ui/ROCreader.png" "$ZIP_STAGE_IMGS/ROCreader.png"
  fi
  rm -f "$ZIPFILE"
  ZIP_SRC="$ZIP_STAGE_ROOT" ZIP_DST="$ZIPFILE" python3 - <<'PY'
import os
import zipfile

src = os.environ["ZIP_SRC"]
dst = os.environ["ZIP_DST"]

with zipfile.ZipFile(dst, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        files.sort()
        rel_root = os.path.relpath(root, src)
        if rel_root != ".":
            zf.write(root, rel_root.replace("\\", "/") + "/")
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src).replace("\\", "/")
            zf.write(full, rel)
PY

  echo "[low_glibc] done"
  echo "[low_glibc] output: $TARBALL"
  echo "[low_glibc] download zip: $ZIPFILE"
} >>"$LOG_FILE" 2>&1

echo "[low_glibc] log: $LOG_FILE"
