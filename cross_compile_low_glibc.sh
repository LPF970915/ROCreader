#!/bin/sh
set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

REQUIRE_MUPDF="${REQUIRE_MUPDF:-1}"
TRIMUI_BRICK_LAYOUT="${TRIMUI_BRICK_LAYOUT:-0}"
H700_ROOT="${H700_ROOT:-$SELF_DIR/H700}"

if [ "$TRIMUI_BRICK_LAYOUT" = "1" ] || [ ! -d "$H700_ROOT" ]; then
  DEFAULT_LOG_DIR="$SELF_DIR/logs"
  DEFAULT_SYSROOT="$SELF_DIR/sysroot_device"
  DEFAULT_DIST_ROOT="$SELF_DIR/dist_lowglibc"
  DEFAULT_DOWNLOADS_ROOT="$SELF_DIR/Downloads"
else
  DEFAULT_LOG_DIR="$H700_ROOT/logs"
  DEFAULT_SYSROOT="$H700_ROOT/sysroot_device"
  DEFAULT_DIST_ROOT="$H700_ROOT/dist_lowglibc"
  DEFAULT_DOWNLOADS_ROOT="$H700_ROOT/Downloads"
fi

LOG_DIR="${ROC_NATIVE_LOG_DIR:-$DEFAULT_LOG_DIR}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/cross_low_glibc_$(date +%Y%m%d_%H%M%S).log"

SYSROOT="${SYSROOT:-$DEFAULT_SYSROOT}"
TOOL_PREFIX="${CROSS_TOOL_PREFIX:-aarch64-linux-gnu}"
CXX_CMD="${CROSS_CXX:-${TOOL_PREFIX}-g++}"
READ_ELF="${CROSS_READELF:-${TOOL_PREFIX}-readelf}"
PKG_CMD="${CROSS_PKG_CONFIG:-pkg-config}"

DIST_ROOT="${DIST_ROOT:-$DEFAULT_DIST_ROOT}"
if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
  APPS_OUT="$DIST_ROOT/Apps"
  RUNTIME_DIR="$APPS_OUT/ROCreader"
  LAUNCHER="$RUNTIME_DIR/launch.sh"
else
  APPS_OUT="$DIST_ROOT/APPS"
  RUNTIME_DIR="$APPS_OUT/ROCreader"
  LAUNCHER="$APPS_OUT/ROCreader.sh"
fi
TARBALL="$DIST_ROOT/ROCreader_APPS_lowglibc.tar.gz"
# Release rule: Downloads only keeps final versioned zip files.
# The zip is assembled from dist_lowglibc/release_stage and contains:
# - Roms/APPS/ROCreader.sh by default, or the configured release parent path.
# - Roms/APPS/ROCreader/ by default, or the configured release parent path.
DOWNLOADS_ROOT="${DOWNLOADS_ROOT:-$DEFAULT_DOWNLOADS_ROOT}"
RELEASE_PARENT_DIR="${ROCREADER_RELEASE_PARENT_DIR:-Roms/APPS}"
RELEASE_IUX_APP="${ROCREADER_RELEASE_IUX_APP:-0}"
RELEASE_INCLUDE_IMGS="${ROCREADER_RELEASE_INCLUDE_IMGS:-1}"
PACKAGE_ONLINE_SOURCES="${ROCREADER_PACKAGE_ONLINE_SOURCES:-1}"
ZIP_STAGE_ROOT="$DIST_ROOT/release_stage"
if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
  ZIP_STAGE_APPS="$ZIP_STAGE_ROOT/Apps"
  ZIP_STAGE_RUNTIME="$ZIP_STAGE_APPS/ROCreader"
  ZIP_STAGE_IMGS="$ZIP_STAGE_RUNTIME"
elif [ "$RELEASE_IUX_APP" = "1" ]; then
  ZIP_STAGE_APPS="$ZIP_STAGE_ROOT/$RELEASE_PARENT_DIR"
  ZIP_STAGE_RUNTIME="$ZIP_STAGE_APPS/ROCreader"
  ZIP_STAGE_IMGS="$ZIP_STAGE_RUNTIME"
else
  ZIP_STAGE_APPS="$ZIP_STAGE_ROOT/$RELEASE_PARENT_DIR"
  ZIP_STAGE_RUNTIME="$ZIP_STAGE_APPS/ROCreader"
  ZIP_STAGE_IMGS="$ZIP_STAGE_APPS/Imgs"
fi

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

next_download_zip_utf8() {
  python3 - "$DOWNLOADS_ROOT" "$TRIMUI_BRICK_LAYOUT" "${DOWNLOAD_RELEASE_VERSION:-}" "${DOWNLOAD_TARGET_SUFFIX:-}" <<'PY'
import os
import re
import sys

downloads = sys.argv[1]
trimui_brick_layout = sys.argv[2] == "1"
release_version_override = sys.argv[3]
target_suffix_override = sys.argv[4]
prefix = "ROC全能漫画阅读器ver"
suffix = f" for {target_suffix_override}.zip" if target_suffix_override else (
    " for Trimui Brick.zip" if trimui_brick_layout else " for H700.zip"
)
pattern = re.compile(r"^" + re.escape(prefix) + r"(\d+)\.(\d+)" + re.escape(suffix) + r"$")
best = None

if release_version_override:
    if not re.fullmatch(r"\d+(?:\.\d+)*", release_version_override):
        raise SystemExit(f"invalid DOWNLOAD_RELEASE_VERSION: {release_version_override}")
    print(os.path.join(downloads, f"{prefix}{release_version_override}{suffix}"))
    raise SystemExit(0)

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
    major, minor = (0, 1) if trimui_brick_layout else (1, 2)
else:
    major, minor = best[0], best[1] + 1

version = f"{major}.{minor:02d}"
print(os.path.join(downloads, f"{prefix}{version}{suffix}"))
PY
}

ZIPFILE="${DOWNLOAD_ZIP_FILE:-$(next_download_zip_utf8)}"
RELEASE_VERSION="$(basename "$ZIPFILE" | sed -n 's/.*\(ver[0-9][0-9.]*\).*[.]zip$/\1/p')"

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

PKG_LIBDIR="$(find_pkg_dirs || true)"
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
    for so_ver in "$d/lib${name}.so."*; do
      [ -f "$so_ver" ] || continue
      printf "%s" "$so_ver"
      return 0
    done
  done
  return 1
}

find_a_in_sysroot() {
  name="$1"
  for d in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu" \
    "$SYSROOT/lib/aarch64-linux-gnu" \
    "$SYSROOT/usr/lib/arm-linux-gnueabihf" \
    "$SYSROOT/lib/arm-linux-gnueabihf" \
    "$SYSROOT/usr/lib" \
    "$SYSROOT/lib"; do
    if [ -f "$d/lib${name}.a" ]; then
      printf "%s/lib%s.a" "$d" "$name"
      return 0
    fi
  done
  return 1
}

find_so_dir_in_sysroot() {
  name="$1"
  so_path="$(find_so_in_sysroot "$name" || true)"
  [ -n "$so_path" ] || return 1
  dirname "$so_path"
}

find_so_in_libdir() {
  name="$1"
  if [ -f "$LIBDIR/lib${name}.so" ]; then
    printf "%s/lib%s.so" "$LIBDIR" "$name"
    return 0
  fi
  for so_ver in "$LIBDIR/lib${name}.so."*; do
    [ -f "$so_ver" ] || continue
    printf "%s" "$so_ver"
    return 0
  done
  return 1
}

{
  echo "===== $(date '+%F %T') ====="
  echo "[low_glibc] SYSROOT=$SYSROOT"
  echo "[low_glibc] CXX=$CXX_CMD"
  echo "[low_glibc] READELF=$READ_ELF"
  echo "[low_glibc] PKG_CONFIG=$PKG_CMD"
  echo "[low_glibc] REQUIRE_MUPDF=$REQUIRE_MUPDF"
  echo "[low_glibc] TRIMUI_BRICK_LAYOUT=$TRIMUI_BRICK_LAYOUT"
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
  "$PKG_CMD" --exists alsa && echo "[low_glibc] pkg OK: alsa" || echo "[low_glibc] pkg MISS: alsa"
  "$PKG_CMD" --exists poppler-cpp && echo "[low_glibc] pkg OK: poppler-cpp" || echo "[low_glibc] pkg MISS: poppler-cpp"
  "$PKG_CMD" --exists libzip && echo "[low_glibc] pkg OK: libzip" || echo "[low_glibc] pkg MISS: libzip"
  "$PKG_CMD" --exists libcurl && echo "[low_glibc] pkg OK: libcurl" || echo "[low_glibc] pkg MISS: libcurl"
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
  FALLBACK_ALSA_CFLAGS=""
  FALLBACK_ALSA_LIBS=""
  FALLBACK_POPPLER_CFLAGS=""
  FALLBACK_POPPLER_LIBS=""
  FALLBACK_LIBZIP_CFLAGS=""
  FALLBACK_LIBZIP_LIBS=""
  FALLBACK_CURL_CFLAGS=""
  FALLBACK_CURL_LIBS=""
  FALLBACK_MUPDF_CFLAGS=""
  FALLBACK_MUPDF_LIBS=""

  SDL_IMAGE_LIBDIR="$(find_so_dir_in_sysroot SDL2_image-2.0 || true)"
  if [ -f "$SYSROOT/usr/include/SDL2/SDL_image.h" ] && [ -n "$SDL_IMAGE_LIBDIR" ]; then
    FALLBACK_IMG_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_IMG_LIBS="-L$SDL_IMAGE_LIBDIR -lSDL2_image"
    echo "[low_glibc] fallback enable: SDL2_image"
  fi

  SDL_TTF_LIBDIR="$(find_so_dir_in_sysroot SDL2_ttf-2.0 || true)"
  if [ -f "$SYSROOT/usr/include/SDL2/SDL_ttf.h" ] && [ -n "$SDL_TTF_LIBDIR" ]; then
    FALLBACK_TTF_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_TTF_LIBS="-L$SDL_TTF_LIBDIR -lSDL2_ttf"
    echo "[low_glibc] fallback enable: SDL2_ttf"
  fi

  SDL_MIXER_LIBDIR="$(find_so_dir_in_sysroot SDL2_mixer-2.0 || true)"
  if [ -f "$SYSROOT/usr/include/SDL2/SDL_mixer.h" ] && [ -n "$SDL_MIXER_LIBDIR" ]; then
    FALLBACK_MIX_CFLAGS="-I$SYSROOT/usr/include/SDL2"
    FALLBACK_MIX_LIBS="-L$SDL_MIXER_LIBDIR -lSDL2_mixer"
    echo "[low_glibc] fallback enable: SDL2_mixer"
  fi

  ALSA_LIBDIR="$(find_so_dir_in_sysroot asound || true)"
  if [ -f "$SYSROOT/usr/include/alsa/asoundlib.h" ] && [ -n "$ALSA_LIBDIR" ]; then
    FALLBACK_ALSA_CFLAGS="-I$SYSROOT/usr/include"
    FALLBACK_ALSA_LIBS="-L$ALSA_LIBDIR -lasound"
    echo "[low_glibc] fallback enable: alsa"
  fi

  if [ -f "$SYSROOT/usr/include/poppler/cpp/poppler-document.h" ] && \
     { [ -f "$LIBDIR/libpoppler-cpp.so" ] || [ -f "$LIBDIR/libpoppler-cpp.so.0" ]; }; then
    FALLBACK_POPPLER_CFLAGS="-I$SYSROOT/usr/include/poppler"
    FALLBACK_POPPLER_LIBS="-L$LIBDIR -L$SYSROOT/usr/lib/aarch64-linux-gnu -L$SYSROOT/lib/aarch64-linux-gnu -lpoppler-cpp -lpoppler"
    for dep in lcms2 tiff jpeg openjp2 fontconfig freetype png12 z expat; do
      if has_so_in_sysroot "$dep"; then
        dep_so="$(find_so_in_sysroot "$dep" || true)"
        if [ -n "$dep_so" ]; then
          FALLBACK_POPPLER_LIBS="$FALLBACK_POPPLER_LIBS $dep_so"
        else
          FALLBACK_POPPLER_LIBS="$FALLBACK_POPPLER_LIBS -l$dep"
        fi
      fi
    done
    echo "[low_glibc] fallback enable: poppler-cpp"
  fi

  if [ -f "$SYSROOT/usr/include/zip.h" ] && [ -n "${TARGET_LIBZIP_SO:-}" ]; then
    FALLBACK_LIBZIP_CFLAGS="-I$SYSROOT/usr/include -I$SYSROOT/usr/include/aarch64-linux-gnu"
    FALLBACK_LIBZIP_LIBS="-L$LIBDIR -lzip"
    echo "[low_glibc] fallback enable: libzip"
  fi

  TARGET_CURL_SO="$(find_so_in_libdir curl || true)"
  CURL_HEADER_DIR=""
  for d in \
    "$SYSROOT/usr/include" \
    "$SYSROOT/usr/include/aarch64-linux-gnu" \
    "$SYSROOT/usr/include/arm-linux-gnueabihf"; do
    if [ -f "$d/curl/curl.h" ]; then
      CURL_HEADER_DIR="$d"
      break
    fi
  done
  if [ -n "$CURL_HEADER_DIR" ] && [ -n "$TARGET_CURL_SO" ]; then
    FALLBACK_CURL_CFLAGS="-I$CURL_HEADER_DIR"
    FALLBACK_CURL_LIBS="-L$LIBDIR -lcurl"
    for dep in ssl crypto z nghttp2 idn2 psl brotlidec zstd mbedtls mbedx509 mbedcrypto; do
      if has_so_in_sysroot "$dep"; then
        dep_so="$(find_so_in_sysroot "$dep" || true)"
        if [ -n "$dep_so" ]; then
          FALLBACK_CURL_LIBS="$FALLBACK_CURL_LIBS $dep_so"
        else
          FALLBACK_CURL_LIBS="$FALLBACK_CURL_LIBS -l$dep"
        fi
      fi
    done
    echo "[low_glibc] fallback enable: libcurl"
  fi

  MUPDF_STATIC_ABS="$(find_a_in_sysroot mupdf || true)"
  if [ -f "$SYSROOT/usr/include/mupdf/fitz.h" ] && \
     { [ -f "$LIBDIR/libmupdf.so" ] || [ -f "$LIBDIR/libmupdf.so.1" ] || [ -n "$MUPDF_STATIC_ABS" ]; }; then
    FALLBACK_MUPDF_CFLAGS="-I$SYSROOT/usr/include"
    # Use explicit .so paths when available to avoid missing unversioned symlinks.
    MUPDF_SO="$(ls "$LIBDIR"/libmupdf.so* 2>/dev/null | head -n 1 || true)"
    MUPDF_THIRD_SO="$(ls "$LIBDIR"/libmupdf-third.so* 2>/dev/null | head -n 1 || true)"
    JBIG2_SO="$(ls "$LIBDIR"/libjbig2dec.so* 2>/dev/null | head -n 1 || true)"
    FALLBACK_MUPDF_LIBS=""
    [ -n "$MUPDF_SO" ] && FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS $MUPDF_SO"
    [ -z "$MUPDF_SO" ] && [ -n "$MUPDF_STATIC_ABS" ] && FALLBACK_MUPDF_LIBS="$FALLBACK_MUPDF_LIBS $MUPDF_STATIC_ABS"
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
  MUPDF_A_ABS="$(find_a_in_sysroot mupdf || true)"
  MUPDF_THIRD_SO_ABS="$(find_so_in_sysroot mupdf-third || true)"
  JBIG2_SO_ABS="$(find_so_in_sysroot jbig2dec || true)"
  [ -n "$MUPDF_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $MUPDF_SO_ABS"
  [ -z "$MUPDF_SO_ABS" ] && [ -n "$MUPDF_A_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $MUPDF_A_ABS"
  [ -n "$MUPDF_THIRD_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $MUPDF_THIRD_SO_ABS"
  [ -n "$JBIG2_SO_ABS" ] && MUPDF_LIBS_FINAL="$MUPDF_LIBS_FINAL $JBIG2_SO_ABS"
  # Some distro sysroots ship a mupdf.pc file without the actual libmupdf shared
  # object. In that case, disable MuPDF entirely and let Poppler provide the real
  # renderer instead of failing the final link step on unresolved fz_* symbols.
  if [ -z "$MUPDF_SO_ABS" ] && [ -z "$MUPDF_A_ABS" ]; then
    MUPDF_CFLAGS_FINAL=""
    MUPDF_LIBS_FINAL=""
    echo "[low_glibc] disable mupdf: libmupdf library not found in sysroot"
  fi
  # Some sysroot MuPDF packages don't expose full transitive link deps in .pc.
  # Add common runtime deps explicitly when present to avoid "DSO missing".
  if [ -n "$MUPDF_LIBS_FINAL" ]; then
    for dep in jbig2dec jpeg openjp2 png12 freetype z m; do
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

  CURL_CFLAGS_FINAL="$FALLBACK_CURL_CFLAGS"
  CURL_LIBS_FINAL="$FALLBACK_CURL_LIBS"
  if [ -z "$CURL_CFLAGS_FINAL" ]; then
    CURL_CFLAGS_FINAL="$("$PKG_CMD" --cflags libcurl 2>/dev/null || true)"
  fi
  if [ -z "$CURL_LIBS_FINAL" ]; then
    CURL_LIBS_FINAL="$("$PKG_CMD" --libs libcurl 2>/dev/null || true)"
  fi
  if [ -z "$CURL_CFLAGS_FINAL" ] || [ -z "$CURL_LIBS_FINAL" ]; then
    echo "[low_glibc] libcurl not enabled for target build; online transport will fall back to curl/wget commands"
  fi
  echo "[low_glibc] CURL_CFLAGS=$CURL_CFLAGS_FINAL"
  echo "[low_glibc] CURL_LIBS=$CURL_LIBS_FINAL"

  ALSA_CFLAGS_FINAL="$FALLBACK_ALSA_CFLAGS"
  ALSA_LIBS_FINAL="$FALLBACK_ALSA_LIBS"
  if [ -z "$ALSA_CFLAGS_FINAL" ]; then
    ALSA_CFLAGS_FINAL="$("$PKG_CMD" --cflags alsa 2>/dev/null || true)"
  fi
  if [ -z "$ALSA_LIBS_FINAL" ]; then
    ALSA_LIBS_FINAL="$("$PKG_CMD" --libs alsa 2>/dev/null || true)"
  fi
  echo "[low_glibc] ALSA_CFLAGS=$ALSA_CFLAGS_FINAL"
  echo "[low_glibc] ALSA_LIBS=$ALSA_LIBS_FINAL"

  JPEG_CFLAGS_FINAL=""
  JPEG_LIBS_FINAL=""
  if [ -f "$SYSROOT/usr/include/jpeglib.h" ]; then
    JPEG_CFLAGS_FINAL="-I$SYSROOT/usr/include"
  fi
  for jpeg_lib in \
    "$SYSROOT/usr/lib/aarch64-linux-gnu/libjpeg.a" \
    "$SYSROOT/lib/aarch64-linux-gnu/libjpeg.a" \
    "$SYSROOT/usr/lib/libjpeg.a" \
    "$SYSROOT/lib/libjpeg.a"; do
    if [ -f "$jpeg_lib" ]; then
      JPEG_LIBS_FINAL="$jpeg_lib"
      break
    fi
  done
  if [ -z "$JPEG_LIBS_FINAL" ]; then
    JPEG_CFLAGS_FINAL=""
  fi
  echo "[low_glibc] JPEG_CFLAGS=$JPEG_CFLAGS_FINAL"
  echo "[low_glibc] JPEG_LIBS=$JPEG_LIBS_FINAL"

  WEBP_CFLAGS_FINAL="${WEBP_CFLAGS:-}"
  WEBP_LIBS_FINAL="${WEBP_LIBS:-}"
  if [ -z "$WEBP_CFLAGS_FINAL" ] && [ -f "$SYSROOT/usr/include/webp/decode.h" ]; then
    WEBP_CFLAGS_FINAL="-I$SYSROOT/usr/include"
  fi
  if [ -z "$WEBP_LIBS_FINAL" ]; then
    for webp_lib in \
      "$SYSROOT/usr/lib/aarch64-linux-gnu/libwebp.a" \
      "$SYSROOT/lib/aarch64-linux-gnu/libwebp.a" \
      "$SYSROOT/usr/lib/libwebp.a" \
      "$SYSROOT/lib/libwebp.a"; do
      if [ -f "$webp_lib" ]; then
        WEBP_LIBS_FINAL="$webp_lib"
        break
      fi
    done
  fi
  if [ -z "$WEBP_LIBS_FINAL" ]; then
    WEBP_CFLAGS_FINAL=""
  fi
  echo "[low_glibc] WEBP_CFLAGS=$WEBP_CFLAGS_FINAL"
  echo "[low_glibc] WEBP_LIBS=$WEBP_LIBS_FINAL"

  echo "[low_glibc] make clean"
  make clean REQUIRE_MUPDF=0
  MAKE_JOBS="${MAKE_JOBS:-$(nproc 2>/dev/null || echo 1)}"
  echo "[low_glibc] make -j$MAKE_JOBS"
  make -j"$MAKE_JOBS" \
    CXX="$CXX_CMD" \
    PKG_CONFIG="$PKG_CMD" \
    REQUIRE_MUPDF="$REQUIRE_MUPDF" \
    SDL_CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include/SDL2 -I$SYSROOT/usr/include -D_REENTRANT" \
    SDL_LIBS="-L$LIBDIR -L$SYSROOT/usr/lib -lSDL2" \
    IMG_CFLAGS="$FALLBACK_IMG_CFLAGS" \
    IMG_LIBS="$FALLBACK_IMG_LIBS" \
    TTF_CFLAGS="$FALLBACK_TTF_CFLAGS" \
    TTF_LIBS="$FALLBACK_TTF_LIBS" \
    MIX_CFLAGS="$FALLBACK_MIX_CFLAGS" \
    MIX_LIBS="$FALLBACK_MIX_LIBS" \
    ALSA_CFLAGS="$ALSA_CFLAGS_FINAL" \
    ALSA_LIBS="$ALSA_LIBS_FINAL" \
    POPPLER_CFLAGS="$FALLBACK_POPPLER_CFLAGS" \
    POPPLER_LIBS="$FALLBACK_POPPLER_LIBS" \
    LIBZIP_CFLAGS="$LIBZIP_CFLAGS_FINAL" \
    LIBZIP_LIBS="$LIBZIP_LIBS_FINAL" \
    CURL_CFLAGS="$CURL_CFLAGS_FINAL" \
    CURL_LIBS="$CURL_LIBS_FINAL" \
    WEBP_CFLAGS="$WEBP_CFLAGS_FINAL" \
    WEBP_LIBS="$WEBP_LIBS_FINAL" \
    JPEG_CFLAGS="$JPEG_CFLAGS_FINAL" \
    JPEG_LIBS="$JPEG_LIBS_FINAL" \
    MUPDF_CFLAGS="$MUPDF_CFLAGS_FINAL" \
    MUPDF_LIBS="$MUPDF_LIBS_FINAL" \
    FS_LIBS="-lstdc++fs" \
    EXTRA_CXXFLAGS="--sysroot=$SYSROOT" \
    EXTRA_LDFLAGS="--sysroot=$SYSROOT -Wl,-rpath-link,$SYSROOT/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$SYSROOT/lib/aarch64-linux-gnu -Wl,-rpath-link,$SYSROOT/usr/lib -Wl,-rpath-link,$SYSROOT/lib -Wl,--allow-shlib-undefined"

  rm -rf "$APPS_OUT"
  mkdir -p "$APPS_OUT"
  rm -rf "$RUNTIME_DIR"
  mkdir -p "$RUNTIME_DIR/lib"
  mkdir -p "$RUNTIME_DIR/lib/pulseaudio"
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    mkdir -p "$RUNTIME_DIR/resources/lib_system_sdl/pulseaudio"
    mkdir -p "$RUNTIME_DIR/resources/fonts"
    mkdir -p "$RUNTIME_DIR/resources/sounds"
    mkdir -p "$RUNTIME_DIR/books"
  else
    mkdir -p "$RUNTIME_DIR/lib_system_sdl"
    mkdir -p "$RUNTIME_DIR/lib_system_sdl/pulseaudio"
    mkdir -p "$RUNTIME_DIR/books"
  fi
  mkdir -p "$RUNTIME_DIR/book_covers"
  mkdir -p "$RUNTIME_DIR/cache"
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    cp ./build/rocreader_sdl "$RUNTIME_DIR/reader"
  else
    cp ./build/rocreader_sdl "$RUNTIME_DIR/"
  fi
  if [ -d "$SELF_DIR/ui" ]; then
    command -v python3 >/dev/null 2>&1
    if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
      rm -f "$RUNTIME_DIR/resources/ui.pack"
      python3 "$SELF_DIR/scripts/pack_ui_assets.py" "$SELF_DIR/ui" "$RUNTIME_DIR/resources/ui.pack"
    else
      rm -f "$RUNTIME_DIR/ui.pack"
      python3 "$SELF_DIR/scripts/pack_ui_assets.py" "$SELF_DIR/ui" "$RUNTIME_DIR/ui.pack"
    fi
  fi
  if [ -d "$SELF_DIR/fonts" ]; then
    if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
      rm -rf "$RUNTIME_DIR/resources/fonts"
      cp -a "$SELF_DIR/fonts" "$RUNTIME_DIR/resources/"
    else
      rm -rf "$RUNTIME_DIR/fonts"
      cp -a "$SELF_DIR/fonts" "$RUNTIME_DIR/"
    fi
  fi
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    FONT_CHECK="$RUNTIME_DIR/resources/fonts/ui_font.ttf"
    CJK_FONT_CHECK="$RUNTIME_DIR/resources/fonts/ui_font_02.ttf"
  else
    FONT_CHECK="$RUNTIME_DIR/fonts/ui_font.ttf"
    CJK_FONT_CHECK="$RUNTIME_DIR/fonts/ui_font_02.ttf"
  fi
  if [ ! -f "$FONT_CHECK" ]; then
    echo "[low_glibc] ERROR: packaged font missing in runtime dir: ui_font.ttf"
    exit 1
  fi
  if [ ! -f "$CJK_FONT_CHECK" ]; then
    echo "[low_glibc] ERROR: packaged Chinese font missing in runtime dir: ui_font_02.ttf"
    exit 1
  fi
  if [ -d "$SELF_DIR/sounds" ]; then
    if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
      rm -rf "$RUNTIME_DIR/resources/sounds"
      cp -a "$SELF_DIR/sounds" "$RUNTIME_DIR/resources/"
    else
      rm -rf "$RUNTIME_DIR/sounds"
      cp -a "$SELF_DIR/sounds" "$RUNTIME_DIR/"
    fi
  fi
  if [ -f "$SELF_DIR/native_keymap.ini" ]; then
    cp "$SELF_DIR/native_keymap.ini" "$RUNTIME_DIR/native_keymap.ini"
    [ "$TRIMUI_BRICK_LAYOUT" = "1" ] && cp "$SELF_DIR/native_keymap.ini" "$RUNTIME_DIR/reader.gptk"
  fi
  if [ -f "$SELF_DIR/native_config.ini" ]; then
    cp "$SELF_DIR/native_config.ini" "$RUNTIME_DIR/native_config.ini"
    [ "$TRIMUI_BRICK_LAYOUT" = "1" ] && cp "$SELF_DIR/native_config.ini" "$RUNTIME_DIR/reader.cfg"
  fi
  if [ "$PACKAGE_ONLINE_SOURCES" = "1" ]; then
    if [ -f "$SELF_DIR/online_sources.release.ini" ]; then
      cp "$SELF_DIR/online_sources.release.ini" "$RUNTIME_DIR/online_sources.ini"
    elif [ -f "$SELF_DIR/online_sources.ini" ]; then
      cp "$SELF_DIR/online_sources.ini" "$RUNTIME_DIR/online_sources.ini"
    fi
  fi
  if [ -n "$RELEASE_VERSION" ]; then
    printf '%s\n' "$RELEASE_VERSION" >"$RUNTIME_DIR/version.txt"
  fi
  mkdir -p "$RUNTIME_DIR/Downloads"

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
    for src in "$RUNTIME_DIR/rocreader_sdl" "$RUNTIME_DIR/reader" "$RUNTIME_DIR"/lib/*.so*; do
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
  LIBCURL_SO_ABS="${TARGET_CURL_SO:-}"
  if [ -n "$LIBCURL_SO_ABS" ] && [ ! -f "$RUNTIME_DIR/lib/$(basename "$LIBCURL_SO_ABS")" ]; then
    cp -L "$LIBCURL_SO_ABS" "$RUNTIME_DIR/lib/" || true
  fi

  TOOLCHAIN_LIB_DIR="$(dirname "$(dirname "$(command -v "$CXX_CMD")")")/aarch64-linux-gnu/lib64"
  if [ -f "$TOOLCHAIN_LIB_DIR/libstdc++.so.6" ]; then
    rm -f "$RUNTIME_DIR/lib/libstdc++.so.6" "$RUNTIME_DIR/lib/libstdc++.so.6."* 2>/dev/null || true
    cp -L "$TOOLCHAIN_LIB_DIR/libstdc++.so.6" "$RUNTIME_DIR/lib/libstdc++.so.6"
    echo "[low_glibc] bundled toolchain libstdc++: $TOOLCHAIN_LIB_DIR/libstdc++.so.6"
  else
    echo "[low_glibc] WARNING: toolchain libstdc++ not found: $TOOLCHAIN_LIB_DIR/libstdc++.so.6"
  fi
  if [ -f "$TOOLCHAIN_LIB_DIR/libgcc_s.so.1" ]; then
    cp -L "$TOOLCHAIN_LIB_DIR/libgcc_s.so.1" "$RUNTIME_DIR/lib/libgcc_s.so.1"
    echo "[low_glibc] bundled toolchain libgcc_s: $TOOLCHAIN_LIB_DIR/libgcc_s.so.1"
  fi

  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    SYSTEM_SDL_DIR="$RUNTIME_DIR/resources/lib_system_sdl"
  else
    SYSTEM_SDL_DIR="$RUNTIME_DIR/lib_system_sdl"
  fi
  mkdir -p "$SYSTEM_SDL_DIR"
  cp -a "$RUNTIME_DIR/lib/." "$SYSTEM_SDL_DIR/"
  rm -f \
    "$SYSTEM_SDL_DIR/libdrm.so."* \
    "$SYSTEM_SDL_DIR/libgbm.so."* \
    "$SYSTEM_SDL_DIR/libwayland-client.so."* \
    "$SYSTEM_SDL_DIR/libwayland-cursor.so."* \
    "$SYSTEM_SDL_DIR/libwayland-egl.so."* \
    "$SYSTEM_SDL_DIR/libwayland-server.so."* \
    "$SYSTEM_SDL_DIR/libX11.so."* \
    "$SYSTEM_SDL_DIR/libXau.so."* \
    "$SYSTEM_SDL_DIR/libxcb.so."* \
    "$SYSTEM_SDL_DIR/libXcursor.so."* \
    "$SYSTEM_SDL_DIR/libXdmcp.so."* \
    "$SYSTEM_SDL_DIR/libXext.so."* \
    "$SYSTEM_SDL_DIR/libXfixes.so."* \
    "$SYSTEM_SDL_DIR/libXi.so."* \
    "$SYSTEM_SDL_DIR/libXinerama.so."* \
    "$SYSTEM_SDL_DIR/libxkbcommon.so."* \
    "$SYSTEM_SDL_DIR/libXrandr.so."* \
    "$SYSTEM_SDL_DIR/libXrender.so."* \
    "$SYSTEM_SDL_DIR/libXss.so."* \
    "$SYSTEM_SDL_DIR/libXxf86vm.so."* \
    "$SYSTEM_SDL_DIR/libdecor-0.so."* \
    "$SYSTEM_SDL_DIR/libdbus-1.so."* \
    "$SYSTEM_SDL_DIR/libsystemd.so."* \
    "$SYSTEM_SDL_DIR/libffi.so."* \
    "$SYSTEM_SDL_DIR/libSDL2-2.0.so."* \
    "$SYSTEM_SDL_DIR/libSDL2_image-2.0.so."* \
    "$SYSTEM_SDL_DIR/libSDL2_ttf-2.0.so."* 2>/dev/null || true

  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    cat > "$LAUNCHER" <<'EOF'
#!/bin/sh
set -eu
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SELF_DIR"
BIN="$APP_DIR/reader"
LOG_FILE="${ROC_NATIVE_RUNTIME_LOG:-$APP_DIR/log.txt}"
LIB_FULL_DIR="$APP_DIR/lib"
LIB_SYSTEM_SDL_DIR="$APP_DIR/resources/lib_system_sdl"
MANUAL_WEB_TRANSPORT_DIR="$APP_DIR/manual_web_transport"
MANUAL_WEB_TRANSPORT_BRICK_DIR="$APP_DIR/manual_web_transport_brick"
LIB_DIR="$LIB_FULL_DIR"
PACKAGE_TAG="trimui-brick-20260426-system-volume-menu-start-select"

export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_NOMOUSE="${SDL_NOMOUSE:-1}"
export TRIMUI_BRICK_LAYOUT="${TRIMUI_BRICK_LAYOUT:-1}"
export ROCREADER_ROOT="$APP_DIR"
if [ -x "$MANUAL_WEB_TRANSPORT_BRICK_DIR/bin/wn04_fetch" ]; then
  export ROCREADER_MANUAL_WEB_FETCH="${ROCREADER_MANUAL_WEB_FETCH:-$MANUAL_WEB_TRANSPORT_BRICK_DIR/bin/wn04_fetch}"
  if [ -x "$MANUAL_WEB_TRANSPORT_BRICK_DIR/bin/wn04_fetch_zip" ]; then
    export ROCREADER_MANUAL_WEB_ZIP_FETCH="${ROCREADER_MANUAL_WEB_ZIP_FETCH:-$MANUAL_WEB_TRANSPORT_BRICK_DIR/bin/wn04_fetch_zip}"
  fi
  export ROCREADER_MANUAL_WEB_CURL="${ROCREADER_MANUAL_WEB_CURL:-$MANUAL_WEB_TRANSPORT_BRICK_DIR/bin/curl-impersonate}"
  export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-1}"
  export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
elif [ -x "$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch" ]; then
  export ROCREADER_MANUAL_WEB_FETCH="${ROCREADER_MANUAL_WEB_FETCH:-$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch}"
  if [ -x "$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch_zip" ]; then
    export ROCREADER_MANUAL_WEB_ZIP_FETCH="${ROCREADER_MANUAL_WEB_ZIP_FETCH:-$MANUAL_WEB_TRANSPORT_DIR/bin/wn04_fetch_zip}"
  fi
  export ROCREADER_MANUAL_WEB_CURL="${ROCREADER_MANUAL_WEB_CURL:-$MANUAL_WEB_TRANSPORT_DIR/bin/curl-impersonate}"
  export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-1}"
  export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
else
  export ROCREADER_MANUAL_WEB_TRANSPORT="${ROCREADER_MANUAL_WEB_TRANSPORT:-0}"
  export ROCREADER_MANUAL_WEB_CATALOG_ONLY="${ROCREADER_MANUAL_WEB_CATALOG_ONLY:-0}"
fi
if [ -z "${ROCREADER_CACHE_ROOT:-}" ]; then
  for cache_candidate in /mnt/UDISK/cache/ROCreader /mnt/UDISK/ROCreader/cache "$APP_DIR/cache"; do
    mkdir -p "$cache_candidate/txt_layouts" "$cache_candidate/cover_thumbs" 2>/dev/null || true
    if [ -d "$cache_candidate" ] && [ -w "$cache_candidate" ]; then
      export ROCREADER_CACHE_ROOT="$cache_candidate"
      break
    fi
  done
else
  export ROCREADER_CACHE_ROOT
fi
if [ -z "${ROCREADER_CACHE_ROOT:-}" ]; then
  export ROCREADER_CACHE_ROOT="$APP_DIR/cache"
fi
export ROCREADER_CARD1_ROOT="/mnt/SDCARD"
export ROCREADER_CARD2_ROOT="/mnt/sdcard"
export ROCREADER_TRIMUI_BRICK_STRICT_APP_ROOT="${ROCREADER_TRIMUI_BRICK_STRICT_APP_ROOT:-1}"
export ROCREADER_SCAN_CARD2="${ROCREADER_SCAN_CARD2:-0}"
export ROCREADER_SCREEN_PROFILE="${ROCREADER_SCREEN_PROFILE:-1024x768}"
export ROCREADER_SCREEN_W="${ROCREADER_SCREEN_W:-1024}"
export ROCREADER_SCREEN_H="${ROCREADER_SCREEN_H:-768}"
export ROCREADER_FULL_INPUT_LOG="${ROCREADER_FULL_INPUT_LOG:-0}"
export ROCREADER_LOG_MAX_BYTES="${ROCREADER_LOG_MAX_BYTES:-524288}"
export ROCREADER_PRELOAD_AVATARS="${ROCREADER_PRELOAD_AVATARS:-1}"
export ROCREADER_UPDATE_CONTENTS_URL="${ROCREADER_UPDATE_CONTENTS_URL:-https://github.com/LPF970915/ROCreader/tree/main/TrimuiBrick/Downloads}"
export ROCREADER_TXT_RESUME_STORE_PENDING_RAW="${ROCREADER_TXT_RESUME_STORE_PENDING_RAW:-0}"
export ROCREADER_PDF_LOW_MEMORY="${ROCREADER_PDF_LOW_MEMORY:-1}"
export ROCREADER_MUPDF_STORE_MB="${ROCREADER_MUPDF_STORE_MB:-24}"
export ROCREADER_SYSTEM_VOLUME_LEVELS="${ROCREADER_SYSTEM_VOLUME_LEVELS:-20}"
export ROCREADER_TRIMUI_SHMVAR_VOLUME="${ROCREADER_TRIMUI_SHMVAR_VOLUME:-1}"
export ROCREADER_TRIMUI_SHMVAR_PATH="${ROCREADER_TRIMUI_SHMVAR_PATH:-/usr/trimui/bin/shmvar}"
export ROCREADER_SYSTEM_VOLUME_OUTPUT_MAX_PERCENT="${ROCREADER_SYSTEM_VOLUME_OUTPUT_MAX_PERCENT:-100}"
export ROCREADER_SYSTEM_VOLUME_SFX_FOLLOWS_HARDWARE="${ROCREADER_SYSTEM_VOLUME_SFX_FOLLOWS_HARDWARE:-1}"
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/tmp/rocreader-xdg"
fi
mkdir -p "$XDG_RUNTIME_DIR" "$APP_DIR/cache" "$ROCREADER_CACHE_ROOT" 2>/dev/null || true
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true
cd "$APP_DIR"
LAUNCHER_PATH="$SELF_DIR/launch.sh"

set_runtime_libs() {
  lib_dir="$1"
  if [ -d "$lib_dir" ]; then
    LIB_DIR="$lib_dir"
  else
    LIB_DIR="$LIB_FULL_DIR"
  fi
  export LD_LIBRARY_PATH="$LIB_DIR:$MANUAL_WEB_TRANSPORT_BRICK_DIR/lib:$MANUAL_WEB_TRANSPORT_DIR/lib:$LIB_DIR/pulseaudio:/usr/lib:/lib:/mnt/vendor/lib:${LD_LIBRARY_PATH_BASE:-}"
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

log_cmd() {
  log_line "[launcher] $*"
  "$@" >>"$LOG_FILE" 2>&1 || log_line "[launcher] command failed rc=$?: $*"
}

write_update_status() {
  result="$1"
  version="${2:-}"
  {
    printf 'result=%s\n' "$result"
    [ -n "$version" ] && printf 'version=%s\n' "$version"
  } >"$APP_DIR/cache/update_boot_status.txt"
}

read_installed_version() {
  [ -f "$APP_DIR/version.txt" ] || return 1
  head -n 1 "$APP_DIR/version.txt"
}

extract_marker_value() {
  key="$1"
  marker="$2"
  awk -F= -v wanted="$key" '$1 == wanted { print substr($0, index($0, "=") + 1); exit }' "$marker"
}

extract_version_from_name() {
  file_name="$(basename "$1")"
  printf '%s\n' "$file_name" | sed -n 's/.*\(ver[0-9][0-9.]*\).*[.]zip$/\1/p'
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

find_pending_marker() {
  for root in /mnt/SDCARD /mnt/sdcard /mnt/mmc; do
    marker="$root/Downloads/ROCreader_update_pending.txt"
    [ -f "$marker" ] && { printf '%s' "$marker"; return 0; }
  done
  return 1
}

find_latest_download_zip() {
  best_path=""
  best_version=""
  for root in /mnt/SDCARD /mnt/sdcard /mnt/mmc; do
    downloads_dir="$root/Downloads"
    [ -d "$downloads_dir" ] || continue
    for zip_file in "$downloads_dir"/*.zip; do
      [ -f "$zip_file" ] || continue
      zip_version="$(extract_version_from_name "$zip_file")"
      [ -n "$zip_version" ] || continue
      if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
        case "$(basename "$zip_file")" in
          *"Trimui Brick"*.zip|*"trimui brick"*.zip|*"TrimuiBrick"*.zip|*"trimuibrick"*.zip) ;;
          *) continue ;;
        esac
      fi
      if [ -z "$best_path" ] || version_is_newer "$zip_version" "$best_version"; then
        best_path="$zip_file"
        best_version="$zip_version"
      fi
    done
  done
  [ -n "$best_path" ] && printf '%s\n' "$best_path"
}

cleanup_installed_download_zips() {
  installed_version="$1"
  [ -n "$installed_version" ] || return 0
  for root in /mnt/SDCARD /mnt/sdcard /mnt/mmc; do
    downloads_dir="$root/Downloads"
    [ -d "$downloads_dir" ] || continue
    for zip_file in "$downloads_dir"/*.zip; do
      [ -f "$zip_file" ] || continue
      zip_version="$(extract_version_from_name "$zip_file")"
      [ -n "$zip_version" ] || continue
      case "$(basename "$zip_file")" in
        *"Trimui Brick"*.zip|*"trimui brick"*.zip|*"TrimuiBrick"*.zip|*"trimuibrick"*.zip) ;;
        *) continue ;;
      esac
      if [ "$zip_version" = "$installed_version" ] || ! version_is_newer "$zip_version" "$installed_version"; then
        rm -f "$zip_file" 2>/dev/null || true
      fi
    done
  done
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

protect_local_online_sources() {
  staged_runtime="$1"
  [ "$TRIMUI_BRICK_LAYOUT" = "1" ] || return 0
  rm -f "$staged_runtime/online_sources.ini" 2>/dev/null || true
}

replace_runtime_entry() {
  name="$1"
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ] && [ "$name" = "online_sources.ini" ]; then
    log_line "[update] keep local online_sources.ini"
    return 0
  fi
  src="$2/$name"
  dst="$APP_DIR/$name"
  [ -e "$src" ] || return 0
  rm -rf "$dst"
  cp -a "$src" "$APP_DIR/"
}

find_staged_runtime_dir() {
  stage_dir="$1"
  for candidate in \
    "$stage_dir/Roms/ports/ROCreader" \
    "$stage_dir/Apps/ROCreader" \
    "$stage_dir/Roms/APPS/ROCreader" \
    "$stage_dir/APPS/ROCreader" \
    "$stage_dir/ROCreader"; do
    [ -d "$candidate" ] && { printf '%s' "$candidate"; return 0; }
  done
  return 1
}

perform_pending_update_if_any() {
  update_stage_dir="$APP_DIR/cache/update_stage"
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
    [ -n "$package_version" ] || package_version="$(extract_version_from_name "$package_path")"
    if [ -n "$package_version" ] && [ -n "$installed_version" ] && ! version_is_newer "$package_version" "$installed_version"; then
      log_line "[update] stale pending marker version=$package_version installed=$installed_version; clearing"
      rm -f "$marker" "$package_path" 2>/dev/null || true
      return 0
    fi
  fi
  [ -n "$package_path" ] || return 0

  log_line "[update] pending marker: ${marker:-none}"
  log_line "[update] installed version: ${installed_version:-unknown}"
  log_line "[update] latest zip: ${latest_zip:-none}"
  log_line "[update] package: $package_path"
  log_line "[update] package version: ${package_version:-unknown}"

  if [ ! -f "$package_path" ]; then
    log_line "[update] missing package, skip install"
    write_update_status "failed" "$package_version"
    return 1
  fi
  if ! extract_zip_to_stage "$package_path" "$update_stage_dir"; then
    log_line "[update] extract failed"
    write_update_status "failed" "$package_version"
    return 1
  fi
  staged_runtime="$(find_staged_runtime_dir "$update_stage_dir" || true)"
  if [ ! -d "$staged_runtime" ]; then
    log_line "[update] staged runtime missing under: $update_stage_dir"
    write_update_status "failed" "$package_version"
    rm -rf "$update_stage_dir"
    return 1
  fi

  protect_local_online_sources "$staged_runtime"
  if [ -z "$package_version" ] && [ -f "$staged_runtime/version.txt" ]; then
    package_version="$(head -n 1 "$staged_runtime/version.txt")"
  fi
  replace_runtime_entry "reader" "$staged_runtime"
  replace_runtime_entry "rocreader_sdl" "$staged_runtime"
  replace_runtime_entry "launch.sh" "$staged_runtime"
  replace_runtime_entry "config.json" "$staged_runtime"
  replace_runtime_entry "icon.png" "$staged_runtime"
  replace_runtime_entry "native_config.ini" "$staged_runtime"
  replace_runtime_entry "native_keymap.ini" "$staged_runtime"
  replace_runtime_entry "reader.cfg" "$staged_runtime"
  replace_runtime_entry "reader.gptk" "$staged_runtime"
  replace_runtime_entry "resources" "$staged_runtime"
  replace_runtime_entry "lib" "$staged_runtime"
  replace_runtime_entry "lib_system_sdl" "$staged_runtime"
  replace_runtime_entry "fonts" "$staged_runtime"
  replace_runtime_entry "sounds" "$staged_runtime"
  # URL sources and helpers are user-provided optional content.
  # Never install or overwrite them from an update package.
  chmod +x "$APP_DIR/reader" "$APP_DIR/launch.sh" 2>/dev/null || true
  chmod +x "$APP_DIR/rocreader_sdl" 2>/dev/null || true
  [ -n "$package_version" ] && printf '%s\n' "$package_version" >"$APP_DIR/version.txt"
  rm -f /mnt/SDCARD/Downloads/ROCreader_update_pending.txt /mnt/sdcard/Downloads/ROCreader_update_pending.txt /mnt/mmc/Downloads/ROCreader_update_pending.txt "$APP_DIR/Downloads/ROCreader_update_pending.txt"
  rm -f "$package_path" 2>/dev/null || true
  cleanup_installed_download_zips "$package_version"
  rm -rf "$update_stage_dir"
  write_update_status "success" "$package_version"
  log_line "[update] install success version=${package_version:-unknown}"
  return 0
}

pending_update_available() {
  marker="$(find_pending_marker || true)"
  installed_version="$(read_installed_version || true)"
  latest_zip="$(find_latest_download_zip || true)"
  latest_version=""
  if [ -n "$latest_zip" ]; then
    latest_version="$(extract_version_from_name "$latest_zip")"
  fi
  if [ -n "$latest_zip" ] && [ -n "$latest_version" ]; then
    if [ -z "$installed_version" ] || version_is_newer "$latest_version" "$installed_version"; then
      return 0
    fi
  fi
  if [ -n "$marker" ]; then
    package_version="$(extract_marker_value version "$marker")"
    [ -n "$package_version" ] || package_version="$(extract_version_from_name "$(dirname "$marker")/$(extract_marker_value filename "$marker")")"
    if [ -z "$package_version" ] || [ -z "$installed_version" ] || version_is_newer "$package_version" "$installed_version"; then
      return 0
    fi
    rm -f "$marker" 2>/dev/null || true
  fi
  return 1
}

if [ "${1:-}" = "--install-pending-update" ]; then
  perform_pending_update_if_any
  exit $?
fi

if pending_update_available; then
  export ROCREADER_BOOT_INSTALL_PENDING_UPDATE="${ROCREADER_BOOT_INSTALL_PENDING_UPDATE:-1}"
  export ROCREADER_UPDATE_INSTALL_COMMAND="${ROCREADER_UPDATE_INSTALL_COMMAND:-\"$LAUNCHER_PATH\" --install-pending-update}"
fi

maybe_install_after_exit() {
  if pending_update_available; then
    log_line "[update] pending package found after app exit; install deferred until next launch"
  fi
}

run_with_driver() {
  drv="$1"
  lib_dir="$2"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] try SDL_VIDEODRIVER=$drv libs=$LIB_DIR"
  SDL_VIDEODRIVER="$drv" "$BIN" >>"$LOG_FILE" 2>&1
  rc=$?
  log_line "[launcher] exit SDL_VIDEODRIVER=$drv rc=$rc"
  if [ "$rc" -eq 23 ]; then
    log_line "[update] installed during boot; restarting app"
    unset ROCREADER_BOOT_INSTALL_PENDING_UPDATE
    unset ROCREADER_UPDATE_INSTALL_COMMAND
    "$LAUNCHER_PATH" >>"$LOG_FILE" 2>&1
    return $?
  fi
  if [ "$rc" -eq 0 ]; then
    maybe_install_after_exit
  fi
  return "$rc"
}

run_default() {
  lib_dir="$1"
  set_runtime_libs "$lib_dir"
  log_line "[launcher] try default video libs=$LIB_DIR"
  "$BIN" >>"$LOG_FILE" 2>&1
  rc=$?
  log_line "[launcher] exit default video rc=$rc"
  if [ "$rc" -eq 23 ]; then
    log_line "[update] installed during boot; restarting app"
    unset ROCREADER_BOOT_INSTALL_PENDING_UPDATE
    unset ROCREADER_UPDATE_INSTALL_COMMAND
    "$LAUNCHER_PATH" >>"$LOG_FILE" 2>&1
    return $?
  fi
  if [ "$rc" -eq 0 ]; then
    maybe_install_after_exit
  fi
  return "$rc"
}

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
log_line "[launcher] package_tag=$PACKAGE_TAG"
log_line "[launcher] app=$APP_DIR"
log_line "[launcher] cache_root=${ROCREADER_CACHE_ROOT}"
log_line "[launcher] screen=${ROCREADER_SCREEN_W}x${ROCREADER_SCREEN_H}"
log_line "[launcher] update_url=${ROCREADER_UPDATE_CONTENTS_URL}"
log_line "[launcher] txt_resume_store_pending_raw=${ROCREADER_TXT_RESUME_STORE_PENDING_RAW}"
log_line "[launcher] pdf_low_memory=${ROCREADER_PDF_LOW_MEMORY} mupdf_store_mb=${ROCREADER_MUPDF_STORE_MB}"
log_line "[launcher] volume_levels=${ROCREADER_SYSTEM_VOLUME_LEVELS} volume_output_max=${ROCREADER_SYSTEM_VOLUME_OUTPUT_MAX_PERCENT} trimui_shmvar_volume=${ROCREADER_TRIMUI_SHMVAR_VOLUME} shmvar_path=${ROCREADER_TRIMUI_SHMVAR_PATH}"
log_line "[launcher] pwd=$(pwd)"
log_line "[launcher] bin=$BIN"
log_line "[launcher] log=$LOG_FILE"
log_line "[launcher] LD_LIBRARY_PATH_BASE=${LD_LIBRARY_PATH_BASE:-}"
log_cmd uname -a
log_cmd ls -la "$APP_DIR"
log_cmd ls -la "$APP_DIR/books"
log_cmd ls -la "$APP_DIR/lib"
log_cmd ls -la "$APP_DIR/resources"
if command -v df >/dev/null 2>&1; then
  log_cmd df -h
fi
if command -v mount >/dev/null 2>&1; then
  log_cmd mount
fi
if command -v ldd >/dev/null 2>&1; then
  log_cmd ldd "$BIN"
else
  log_line "[launcher] ldd not available"
fi

if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  run_with_driver "$SDL_VIDEODRIVER" "$LIB_FULL_DIR"
  exit $?
fi

try_libs() {
  lib_dir="$1"
  if run_default "$lib_dir"; then
    exit 0
  else
    rc=$?
  fi
  if [ "$rc" -eq 134 ] || [ "$rc" -eq 137 ]; then
    log_line "[launcher] process ended with rc=$rc; stop retrying video drivers"
    exit "$rc"
  fi
  for drv in KMSDRM kmsdrm wayland x11 fbcon directfb; do
    if run_with_driver "$drv" "$lib_dir"; then
      exit 0
    else
      rc=$?
    fi
    if [ "$rc" -eq 134 ] || [ "$rc" -eq 137 ]; then
      log_line "[launcher] process ended with rc=$rc; stop retrying video drivers"
      exit "$rc"
    fi
  done
}

try_libs "$LIB_SYSTEM_SDL_DIR"
try_libs "$LIB_FULL_DIR"
log_line "[launcher] all drivers failed"
exit 5
EOF
    cat > "$RUNTIME_DIR/config.json" <<'EOF'
{
  "label": "ROCreader",
  "launch": "launch.sh",
  "icon": "icon.png"
}
EOF
    cat > "$RUNTIME_DIR/README.md" <<'EOF'
# ROCreader for Trimui Brick

Copy the `Apps/ROCreader` folder to the Trimui Brick memory card.
Place books in `books`.
EOF
    : > "$RUNTIME_DIR/log.txt"
    [ -f "$SELF_DIR/ui/common/ROCreader_trimuibrick.png" ] && cp "$SELF_DIR/ui/common/ROCreader_trimuibrick.png" "$RUNTIME_DIR/icon.png"
    [ ! -f "$RUNTIME_DIR/icon.png" ] && [ -f "$SELF_DIR/ui/ROCreader.png" ] && cp "$SELF_DIR/ui/ROCreader.png" "$RUNTIME_DIR/icon.png"
    [ ! -f "$RUNTIME_DIR/icon.png" ] && [ -f "$SELF_DIR/ui/common/ROCreader.png" ] && cp "$SELF_DIR/ui/common/ROCreader.png" "$RUNTIME_DIR/icon.png"
  else
    cp "$SELF_DIR/ROCreader.sh" "$LAUNCHER"
    if [ -n "${ROCREADER_UPDATE_CONTENTS_URL:-}" ]; then
      python3 - "$LAUNCHER" "$ROCREADER_UPDATE_CONTENTS_URL" <<'PY'
import re
import sys

path, update_url = sys.argv[1], sys.argv[2]
with open(path, "r", encoding="utf-8") as fh:
    text = fh.read()
text = re.sub(
    r'export ROCREADER_UPDATE_CONTENTS_URL="\$\{ROCREADER_UPDATE_CONTENTS_URL:-[^}]*\}"',
    f'export ROCREADER_UPDATE_CONTENTS_URL="${{ROCREADER_UPDATE_CONTENTS_URL:-{update_url}}}"',
    text,
    count=1,
)
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    fh.write(text)
PY
    fi
  fi

  chmod +x "$RUNTIME_DIR/rocreader_sdl" 2>/dev/null || true
  chmod +x "$RUNTIME_DIR/reader" 2>/dev/null || true
  chmod +x "$LAUNCHER"

  mkdir -p "$DIST_ROOT"
  rm -f "$TARBALL"
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    tar -C "$DIST_ROOT" -czf "$TARBALL" Apps
  else
    tar -C "$DIST_ROOT" -czf "$TARBALL" APPS
  fi

  mkdir -p "$DOWNLOADS_ROOT"
  rm -rf "$ZIP_STAGE_ROOT"
  mkdir -p "$ZIP_STAGE_RUNTIME"
  if [ "$TRIMUI_BRICK_LAYOUT" != "1" ] && [ "$RELEASE_INCLUDE_IMGS" = "1" ]; then
    mkdir -p "$ZIP_STAGE_IMGS"
  fi
  cp -a "$RUNTIME_DIR/." "$ZIP_STAGE_RUNTIME/"
  if [ "$PACKAGE_ONLINE_SOURCES" != "1" ]; then
    rm -f "$ZIP_STAGE_RUNTIME/online_sources.ini" 2>/dev/null || true
    rm -rf "$ZIP_STAGE_RUNTIME/URL" "$ZIP_STAGE_RUNTIME/manual_web_transport" "$ZIP_STAGE_RUNTIME/manual_web_transport_brick" 2>/dev/null || true
  elif [ -f "$SELF_DIR/online_sources.release.ini" ]; then
    cp "$SELF_DIR/online_sources.release.ini" "$ZIP_STAGE_RUNTIME/online_sources.ini"
  fi
  if [ "$TRIMUI_BRICK_LAYOUT" = "1" ]; then
    mkdir -p "$ZIP_STAGE_RUNTIME/books" "$ZIP_STAGE_RUNTIME/book_covers" "$ZIP_STAGE_RUNTIME/cache" "$ZIP_STAGE_RUNTIME/Downloads"
    find "$ZIP_STAGE_RUNTIME/books" -mindepth 1 -delete 2>/dev/null || true
    FONT_STAGE_CHECK="$ZIP_STAGE_RUNTIME/resources/fonts/ui_font.ttf"
    CJK_FONT_STAGE_CHECK="$ZIP_STAGE_RUNTIME/resources/fonts/ui_font_02.ttf"
  else
    mkdir -p "$ZIP_STAGE_RUNTIME/books" "$ZIP_STAGE_RUNTIME/book_covers" "$ZIP_STAGE_RUNTIME/cache" "$ZIP_STAGE_RUNTIME/Downloads"
    find "$ZIP_STAGE_RUNTIME/books" -mindepth 1 -delete 2>/dev/null || true
    FONT_STAGE_CHECK="$ZIP_STAGE_RUNTIME/fonts/ui_font.ttf"
    CJK_FONT_STAGE_CHECK="$ZIP_STAGE_RUNTIME/fonts/ui_font_02.ttf"
  fi
  find "$ZIP_STAGE_RUNTIME/book_covers" -mindepth 1 -delete 2>/dev/null || true
  find "$ZIP_STAGE_RUNTIME/cache" -mindepth 1 -delete 2>/dev/null || true
  if [ ! -f "$FONT_STAGE_CHECK" ]; then
    echo "[low_glibc] ERROR: packaged font missing in release stage: ui_font.ttf"
    exit 1
  fi
  if [ ! -f "$CJK_FONT_STAGE_CHECK" ]; then
    echo "[low_glibc] ERROR: packaged Chinese font missing in release stage: ui_font_02.ttf"
    exit 1
  fi
  if [ "$TRIMUI_BRICK_LAYOUT" != "1" ]; then
    if [ "$RELEASE_IUX_APP" = "1" ]; then
      cp "$LAUNCHER" "$ZIP_STAGE_RUNTIME/launch.sh"
      mkdir -p "$ZIP_STAGE_ROOT/roms/ports"
      cp "$LAUNCHER" "$ZIP_STAGE_ROOT/roms/ports/ROCreader.sh"
      IUX_ICON="$SELF_DIR/ui/common/icon.png"
      [ -f "$IUX_ICON" ] || { echo "[low_glibc] ERROR: missing IUX icon: $IUX_ICON"; exit 1; }
      cp "$IUX_ICON" "$ZIP_STAGE_RUNTIME/rocreader.png"
      CONFIG_VERSION="${RELEASE_VERSION#ver}"
      python3 - "$ZIP_STAGE_RUNTIME/config.json" "$CONFIG_VERSION" <<'PY'
import json
import sys

path, version = sys.argv[1:]
config = {
    "software_code": "rocreader",
    "title": "ROCreader",
    "description": "ROC reader",
    "version": version,
    "exec": "launch.sh",
    "workdir": ".",
    "icon": "rocreader.png",
}
with open(path, "w", encoding="utf-8", newline="\n") as stream:
    json.dump(config, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
PY
      chmod +x "$ZIP_STAGE_RUNTIME/launch.sh" "$ZIP_STAGE_ROOT/roms/ports/ROCreader.sh" \
        "$ZIP_STAGE_RUNTIME/rocreader_sdl" 2>/dev/null || true
    elif [ "$RELEASE_INCLUDE_IMGS" = "1" ]; then
      mkdir -p "$ZIP_STAGE_IMGS"
      cp "$LAUNCHER" "$ZIP_STAGE_APPS/ROCreader.sh"
      if [ -f "$SELF_DIR/ui/ROCreader.png" ]; then
        cp "$SELF_DIR/ui/ROCreader.png" "$ZIP_STAGE_IMGS/ROCreader.png"
      elif [ -f "$SELF_DIR/ui/common/ROCreader.png" ]; then
        cp "$SELF_DIR/ui/common/ROCreader.png" "$ZIP_STAGE_IMGS/ROCreader.png"
      fi
    else
      cp "$LAUNCHER" "$ZIP_STAGE_APPS/ROCreader.sh"
    fi
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

  if [ "$RELEASE_IUX_APP" = "1" ]; then
    python3 - "$ZIPFILE" "$CONFIG_VERSION" "$SELF_DIR/ui/common/icon.png" <<'PY'
import json
import sys
import zipfile

archive_path, expected_version, expected_icon_path = sys.argv[1:]
app_root = "app/ROCreader"
required = {
    f"{app_root}/config.json",
    f"{app_root}/launch.sh",
    f"{app_root}/rocreader.png",
    f"{app_root}/rocreader_sdl",
    f"{app_root}/ui.pack",
    "roms/ports/ROCreader.sh",
}
with zipfile.ZipFile(archive_path) as archive:
    names = set(archive.namelist())
    missing = sorted(required - names)
    if missing:
        raise SystemExit(f"IUX release is missing: {missing[0]}")
    if any(name.startswith("roms/ports/ROCreader/") for name in names):
        raise SystemExit("IUX release duplicated the ROCreader runtime under ports")
    with open(expected_icon_path, "rb") as stream:
        expected_icon = stream.read()
    packaged_icon = archive.read(f"{app_root}/rocreader.png")
    if packaged_icon != expected_icon:
        raise SystemExit("packaged IUX icon does not match ui/common/icon.png")
    if len(packaged_icon) < 26 or packaged_icon[:8] != b"\x89PNG\r\n\x1a\n" or packaged_icon[25] != 6:
        raise SystemExit("IUX icon must be an RGBA PNG (PNG color type 6)")
    config = json.loads(archive.read(f"{app_root}/config.json").decode("utf-8"))
    expected = {
        "software_code": "rocreader",
        "title": "ROCreader",
        "version": expected_version,
        "exec": "launch.sh",
        "workdir": ".",
        "icon": "rocreader.png",
    }
    for key, value in expected.items():
        if config.get(key) != value:
            raise SystemExit(f"invalid IUX config field: {key}")
print(f"[low_glibc] verified IUX/ES release layout: {archive_path}")
PY
  fi

  if [ "${LEGACY_DOWNLOADS_MIRROR:-1}" = "1" ] && [ "$TRIMUI_BRICK_LAYOUT" != "1" ] &&
     { [ -z "${DOWNLOAD_TARGET_SUFFIX:-}" ] || [ "${DOWNLOAD_TARGET_SUFFIX:-}" = "H700" ]; }; then
    LEGACY_DOWNLOADS_ROOT="$SELF_DIR/Downloads"
    LEGACY_ZIPFILE="$LEGACY_DOWNLOADS_ROOT/$(basename "$ZIPFILE")"
    if [ "$DOWNLOADS_ROOT" != "$LEGACY_DOWNLOADS_ROOT" ]; then
      mkdir -p "$LEGACY_DOWNLOADS_ROOT"
      cp -f "$ZIPFILE" "$LEGACY_ZIPFILE"
      echo "[low_glibc] legacy H700 download mirror: $LEGACY_ZIPFILE"
    fi
  fi

  echo "[low_glibc] done"
  echo "[low_glibc] output: $TARBALL"
  echo "[low_glibc] download zip: $ZIPFILE"
} >>"$LOG_FILE" 2>&1

echo "[low_glibc] log: $LOG_FILE"
