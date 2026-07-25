#include "app_loop.h"

#include "app_bootstrap.h"
#include "../GKD350HUltra/gkd350h_ultra_profile.h"

#include <SDL.h>
#ifdef HAVE_SDL2_IMAGE
#include <SDL_image.h>
#endif
#ifdef HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif
#ifdef HAVE_SDL2_MIXER
#include <SDL_mixer.h>
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include "filesystem_compat.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <codecvt>
#include <limits>
#include <locale>
#if !defined(_WIN32) && __has_include(<iconv.h>)
#include <errno.h>
#include <iconv.h>
#define ROCREADER_HAS_ICONV 1
#else
#define ROCREADER_HAS_ICONV 0
#endif
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app_runtime.h"
#include "app_composition.h"
#include "app_context.h"
#include "app_layout.h"
#include "app_services.h"
#include "app_stores.h"
#include "avatar_badge_runtime.h"
#include "audio_runtime.h"
#include "book_library_service.h"
#include "book_scanner.h"
#include "boot_scene.h"
#include "boot_runtime.h"
#include "contributor_avatar_runtime.h"
#include "cover_cache_runtime.h"
#include "cover_service.h"
#include "epub_runtime.h"
#include "epub_reader.h"
#include "input_manager.h"
#include "lid_power_control.h"
#include "menu_scene.h"
#include "online_shelf_controller.h"
#include "pdf_reader.h"
#include "pdf_reader_module.h"
#include "pdf_runtime.h"
#include "progress_store.h"
#include "epub_reader_module.h"
#include "reader_core.h"
#include "reader_input_router.h"
#include "reader_manager.h"
#include "reader_progress_controller.h"
#include "reader_launch_service.h"
#include "reader_scene.h"
#include "rgds_interaction.h"
#include "rgds_render.h"
#include "rgds_runtime.h"
#include "reader_session_ops.h"
#include "reader_session_state.h"
#include "runtime_log.h"
#include "scene_manager.h"
#include "screen_profile.h"
#include "system_controls.h"
#include "system_settings_runtime.h"
#include "shelf_runtime.h"
#include "settings_runtime.h"
#include "sdl_utils.h"
#include "shelf_scene.h"
#include "storage_paths.h"
#include "status_bar_runtime.h"
#include "system_status.h"
#include "txt_reader_session.h"
#include "txt_reader_module.h"
#include "txt_reader_runtime.h"
#include "txt_text_service.h"
#include "txt_session_facade.h"
#include "txt_transcode_service.h"
#include "texture_registry.h"
#include "ui_assets.h"
#include "ui_assets_loader.h"
#include "ui_text_cache.h"
#include "version_update_runtime.h"
#include "volume_overlay.h"
#include "zip_image_reader_module.h"
#include "zip_image_runtime.h"
#include "animation.h"
#include "app_shell.h"

namespace {
constexpr float kCoverAspect = 2.0f / 3.0f;
constexpr Uint8 kUnfocusedAlpha = 255;
constexpr float kTitleMarqueePauseSec = 0.75f;
constexpr float kTitleMarqueeSpeedPx = 48.0f;
constexpr size_t kCoverCacheMaxEntries = 320;
constexpr size_t kCoverCacheMaxBytes = 96u * 1024u * 1024u;
constexpr Uint8 kSidebarMaskMaxAlpha = 84;
constexpr int kIdleWaitMs = 100;
constexpr float kCardLerpSpeed = 18.0f;
constexpr float kCardMoveLinearSpeedX = 860.0f;  // px/s for center move transition
constexpr float kCardMoveLinearSpeedY = 860.0f;  // px/s for center move transition
constexpr float kCardMoveTailRatio = 0.52f;      // last 52% enters slow tail
constexpr float kCardMoveTailMinMul = 0.12f;     // tail minimum speed multiplier
constexpr float kCardScaleLinearSpeedW = 140.0f; // px/s for width scale transition
constexpr float kCardScaleLinearSpeedH = 210.0f; // px/s for height scale transition
constexpr float kCardScaleTailRatio = 0.52f;     // last 52% enters slow tail
constexpr float kCardScaleTailMinMul = 0.10f;    // tail minimum speed multiplier
constexpr float kPageSlideDurationSec = 0.52f;
constexpr int kReaderTapStepPx = 56;
constexpr float kSettingsToggleGuardSec = 0.16f;
constexpr float kMenuToggleDebounceSec = 0.12f;
constexpr uint32_t kDeferredSaveDelayMs = 1500;
constexpr uint32_t kTxtResumeSaveDelayMs = 2000;
constexpr uint32_t kShelfScanCacheTtlMs = 3000;
constexpr size_t kShelfScanCacheMaxEntries = 24;
constexpr uint32_t kIdleFlushOnlyWaitMs = 250;
constexpr size_t kBootCountBatchEntries = 96;
constexpr size_t kBootScanBatchEntries = 48;
constexpr size_t kBootCoverGenerateBatchEntries = 1;
constexpr size_t kBootCoverPreloadBatchEntries = 2;
constexpr size_t kBootDefaultShelfPreloadWindows = 10;
constexpr size_t kBootOtherShelfPreloadWindows = 2;
constexpr size_t kShelfStreamPreloadBatchEntries = 1;
constexpr size_t kShelfStreamPreloadLookaheadPages = 6;
constexpr uint32_t kTransientMessageDurationMs = 1800;
constexpr uint32_t kReaderFastFlipThresholdMs = 200;
constexpr uint32_t kReaderPageFlipDebounceMs = 150;
constexpr int kTxtLineSpacing = 8;
constexpr int kTxtLayoutCacheVersion = 6;
constexpr size_t kTxtMaxBytes = 64 * 1024 * 1024;
constexpr size_t kTxtMaxWrappedLines = 250000;
constexpr size_t kTxtLayoutCacheMaxEntries = 4;
#ifdef HAVE_SDL2_TTF
constexpr size_t kTextCacheMaxEntries = 384;
#endif

void FatalSignalHandler(int sig) {
  runtime_log::Line(std::string("fatal signal: ") + std::to_string(sig));
  std::_Exit(128 + sig);
}

std::string NormalizePathKey(const std::string &path);
std::string ToLowerAscii(std::string s);

int ClampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string NormalizePathKey(const std::string &path) {
  if (path.empty()) return {};
  try {
    std::filesystem::path p(path);
    p = filesystem_compat::LexicallyNormal(p);
    std::string out = p.generic_string();
#ifdef _WIN32
    out = ToLowerAscii(out);
#endif
    return out;
  } catch (...) {
    return path;
  }
}

void HashAppend(uint64_t &hash, const std::string &value) {
  constexpr uint64_t kFnvPrime = 1099511628211ull;
  for (unsigned char c : value) {
    hash ^= static_cast<uint64_t>(c);
    hash *= kFnvPrime;
  }
  hash ^= 0xffu;
  hash *= kFnvPrime;
}

std::string FileFingerprintToken(const std::string &path) {
  std::error_code ec;
  const std::filesystem::path fs_path(path);
  const uintmax_t size = std::filesystem::file_size(fs_path, ec);
  const uintmax_t safe_size = ec ? 0 : size;
  ec.clear();
  const auto mtime_raw = std::filesystem::last_write_time(fs_path, ec);
  const long long mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
  return std::to_string(safe_size) + "|" + std::to_string(mtime);
}

std::string BuildShelfPreloadFingerprint(const std::vector<BookItem> &books,
                                         const std::vector<std::string> &books_roots,
                                         const std::vector<std::string> &cover_roots,
                                         int cover_w,
                                         int cover_h) {
  uint64_t hash = 1469598103934665603ull;
  HashAppend(hash, "shelf-cover-preload-v1");
  HashAppend(hash, std::to_string(cover_w) + "x" + std::to_string(cover_h));
  for (const auto &root : books_roots) HashAppend(hash, "root|" + NormalizePathKey(root));
  for (const auto &root : cover_roots) HashAppend(hash, "cover_root|" + NormalizePathKey(root));
  for (const BookItem &item : books) {
    const std::string path = book_library_service::RealPathForItem(item);
    HashAppend(hash, NormalizePathKey(path) + "|" + FileFingerprintToken(path));
  }
  return std::to_string(books.size()) + "|" + std::to_string(hash);
}

bool LoadShelfPreloadFingerprint(const std::filesystem::path &path, std::string &fingerprint) {
  std::ifstream in(path);
  if (!in) return false;
  std::getline(in, fingerprint);
  return !fingerprint.empty();
}

void SaveShelfPreloadFingerprint(const std::filesystem::path &path, const std::string &fingerprint) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (out) out << fingerprint << "\n";
}

std::string GetLowerExt(const std::string &path) {
  try {
    std::string ext = std::filesystem::path(path).extension().string();
    return ToLowerAscii(ext);
  } catch (...) {
    return {};
  }
}

std::string Utf8Ellipsize(const std::string &text, size_t max_chars) {
  if (text.empty() || max_chars == 0) return {};
  size_t chars = 0;
  size_t bytes = 0;
  while (bytes < text.size() && chars < max_chars) {
    const unsigned char c = static_cast<unsigned char>(text[bytes]);
    size_t len = 1;
    if ((c & 0x80) == 0x00) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (bytes + len > text.size()) break;
    bytes += len;
    ++chars;
  }
  if (bytes >= text.size()) return text;
  if (chars <= 1) return "...";
  return text.substr(0, bytes) + "...";
}

size_t Utf8CharLen(unsigned char c) {
  if ((c & 0x80) == 0x00) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

bool IsValidUtf8(const std::string &text) {
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t len = 0;
    uint32_t codepoint = 0;
    if ((c & 0x80) == 0x00) {
      len = 1;
      codepoint = c;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
      codepoint = c & 0x1F;
      if (codepoint < 0x02) return false;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      codepoint = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      codepoint = c & 0x07;
      if (codepoint > 0x04) return false;
    } else {
      return false;
    }
    if (i + len > text.size()) return false;
    for (size_t j = 1; j < len; ++j) {
      const unsigned char cc = static_cast<unsigned char>(text[i + j]);
      if ((cc & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (cc & 0x3F);
    }
    if ((len == 2 && codepoint < 0x80) ||
        (len == 3 && codepoint < 0x800) ||
        (len == 4 && codepoint < 0x10000) ||
        codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      return false;
    }
    i += len;
  }
  return true;
}

bool TryConvertUtf16BomToUtf8(const std::string &raw, std::string &out) {
  if (raw.size() < 2) return false;
  const unsigned char b0 = static_cast<unsigned char>(raw[0]);
  const unsigned char b1 = static_cast<unsigned char>(raw[1]);
  if (b0 == 0xFF && b1 == 0xFE) {
    std::u16string u16;
    u16.reserve((raw.size() - 2) / 2);
    for (size_t i = 2; i + 1 < raw.size(); i += 2) {
      char16_t ch = static_cast<char16_t>(
          static_cast<unsigned char>(raw[i]) |
          (static_cast<unsigned char>(raw[i + 1]) << 8));
      u16.push_back(ch);
    }
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    out = conv.to_bytes(u16);
    return true;
  }
  if (b0 == 0xFE && b1 == 0xFF) {
    std::u16string u16;
    u16.reserve((raw.size() - 2) / 2);
    for (size_t i = 2; i + 1 < raw.size(); i += 2) {
      char16_t ch = static_cast<char16_t>(
          (static_cast<unsigned char>(raw[i]) << 8) |
          static_cast<unsigned char>(raw[i + 1]));
      u16.push_back(ch);
    }
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    out = conv.to_bytes(u16);
    return true;
  }
  return false;
}

double ScoreDecodedTextCandidate(const std::string &text) {
  if (text.empty()) return 0.0;
  double score = 0.0;
  size_t total = 0;
  size_t weird = 0;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t len = Utf8CharLen(c);
    if (i + len > text.size()) {
      score -= 20.0;
      ++weird;
      ++total;
      break;
    }
    uint32_t codepoint = 0;
    if (len == 1) {
      codepoint = c;
    } else {
      codepoint = c & ((1u << (8 - len - 1)) - 1u);
      bool invalid = false;
      for (size_t j = 1; j < len; ++j) {
        const unsigned char cc = static_cast<unsigned char>(text[i + j]);
        if ((cc & 0xC0) != 0x80) {
          invalid = true;
          break;
        }
        codepoint = (codepoint << 6) | (cc & 0x3F);
      }
      if (invalid) {
        score -= 20.0;
        ++weird;
        ++total;
        i += len;
        continue;
      }
    }

    if (codepoint == 0xFFFD) {
      score -= 18.0;
      ++weird;
    } else if (codepoint == '\r' || codepoint == '\n' || codepoint == '\t') {
      score += 0.4;
    } else if (codepoint >= 0x20 && codepoint <= 0x7E) {
      score += 0.8;
    } else if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
               (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
               (codepoint >= 0x3040 && codepoint <= 0x30FF) ||
               (codepoint >= 0xAC00 && codepoint <= 0xD7AF)) {
      score += 2.4;
    } else if ((codepoint >= 0x3000 && codepoint <= 0x303F) ||
               (codepoint >= 0xFF00 && codepoint <= 0xFFEF)) {
      score += 1.6;
    } else if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F)) {
      score -= 10.0;
      ++weird;
    } else {
      score += 0.2;
    }

    ++total;
    i += len;
  }

  if (total == 0) return -1e9;
  score -= static_cast<double>(weird) * 1.5;
  score /= static_cast<double>(total);
  return score;
}

#if ROCREADER_HAS_ICONV
bool TryConvertEncodingToUtf8Iconv(const std::string &raw, const char *from_encoding, std::string &out) {
  iconv_t cd = iconv_open("UTF-8", from_encoding);
  if (cd == reinterpret_cast<iconv_t>(-1)) return false;
  out.clear();
  size_t in_left = raw.size();
  char *in_buf = const_cast<char *>(raw.data());
  std::vector<char> chunk(std::max<size_t>(4096, raw.size() * 4 + 32));
  while (true) {
    char *out_buf = chunk.data();
    size_t out_left = chunk.size();
    const size_t rc = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);
    out.append(chunk.data(), chunk.size() - out_left);
    if (rc != static_cast<size_t>(-1)) break;
    if (errno == E2BIG) continue;
    iconv_close(cd);
    out.clear();
    return false;
  }
  iconv_close(cd);
  return IsValidUtf8(out);
}
#endif

bool DecodeTextBytesToUtf8(const std::string &raw, std::string &out, std::string *detected_encoding = nullptr) {
  out.clear();
  if (raw.empty()) {
    if (detected_encoding) *detected_encoding = "empty";
    return true;
  }
  if (TryConvertUtf16BomToUtf8(raw, out)) {
    if (detected_encoding) *detected_encoding = "UTF-16";
    return true;
  }
  if (raw.size() >= 3 &&
      static_cast<unsigned char>(raw[0]) == 0xEF &&
      static_cast<unsigned char>(raw[1]) == 0xBB &&
      static_cast<unsigned char>(raw[2]) == 0xBF) {
    out.assign(raw.begin() + 3, raw.end());
    if (IsValidUtf8(out)) {
      if (detected_encoding) *detected_encoding = "UTF-8 BOM";
      return true;
    }
    out.clear();
  }

  struct Candidate {
    std::string text;
    std::string encoding;
    double score = -1e9;
  };
  Candidate best{};
  bool found_candidate = false;

  auto consider_candidate = [&](std::string candidate_text, const std::string &encoding) {
    if (!IsValidUtf8(candidate_text)) return;
    Candidate candidate;
    candidate.text = std::move(candidate_text);
    candidate.encoding = encoding;
    candidate.score = ScoreDecodedTextCandidate(candidate.text);
    if (!found_candidate || candidate.score > best.score + 0.08 ||
        (std::abs(candidate.score - best.score) <= 0.08 && encoding == "UTF-8")) {
      best = std::move(candidate);
      found_candidate = true;
    }
  };

  if (IsValidUtf8(raw)) {
    consider_candidate(raw, "UTF-8");
  }
#if ROCREADER_HAS_ICONV
  static const std::array<const char *, 4> kLegacyEncodings = {"GB18030", "GBK", "GB2312", "BIG5"};
  for (const char *encoding : kLegacyEncodings) {
    std::string converted;
    if (TryConvertEncodingToUtf8Iconv(raw, encoding, converted)) {
      consider_candidate(std::move(converted), encoding);
    }
  }
#endif
  if (!found_candidate) {
    out.clear();
    return false;
  }
  out = std::move(best.text);
  if (detected_encoding) *detected_encoding = best.encoding;
  return true;
}

bool ReadFileBytes(const std::filesystem::path &path, std::string &raw) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  try {
    std::ostringstream oss;
    oss << in.rdbuf();
    raw = oss.str();
    return in.good() || in.eof();
  } catch (...) {
    raw.clear();
    return false;
  }
}

bool WriteFileBytesAtomically(const std::filesystem::path &path, const std::string &data) {
  const std::filesystem::path temp_path = path.string() + ".rocreader_tmp";
  {
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      out.close();
      std::error_code cleanup_ec;
      std::filesystem::remove(temp_path, cleanup_ec);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp_path, path, ec);
  if (!ec) return true;
  ec.clear();
  std::filesystem::remove(path, ec);
  ec.clear();
  std::filesystem::rename(temp_path, path, ec);
  if (!ec) return true;
  std::filesystem::remove(temp_path, ec);
  return false;
}

} // namespace

int RunApp(int argc, char **argv) {
  (void)argc;
  InitializeProcessRuntime(argv ? argv[0] : nullptr, FatalSignalHandler);
  runtime_log::Line("main: start");
  AppBootstrapResult bootstrap = BootstrapSdlApp();
  if (!bootstrap.ok) return bootstrap.exit_code;
  SDL_Window *window = bootstrap.window;
  SDL_Renderer *renderer = bootstrap.renderer;
  const ScreenProfile screen_profile = bootstrap.screen_profile;
  const std::string device_model_token = bootstrap.device_model_token;
  bool verbose_log = bootstrap.verbose_log;
  rgds::Runtime rgds_runtime = std::move(bootstrap.rgds_runtime);
  const bool renderer_supports_target_textures = bootstrap.renderer_supports_target_textures;

  AppContext app_context = MakeAppContext(window, renderer, rgds_runtime, screen_profile, verbose_log);
  AppShell app_shell;
  app_shell.Initialize(app_context);

  AppInputDevices input_devices = OpenAppInputDevices(verbose_log);

  const AppRuntimePaths runtime_paths = ResolveAppRuntimePaths();
  const std::filesystem::path &exe_path = runtime_paths.exe_path;
  const std::filesystem::path &ui_path = runtime_paths.ui_path;

  UiAssets ui_assets;
  rgds::RenderResources rgds_render_resources;
  TextureRegistry texture_registry;
  auto forget_texture_size = [&](SDL_Texture *tex) { texture_registry.Forget(tex); };
  auto remember_texture_size = [&](SDL_Texture *tex, int w, int h) { texture_registry.Remember(tex, w, h); };
  auto get_texture_size = [&](SDL_Texture *tex, int &w, int &h) { texture_registry.Get(tex, w, h); };
  UiAssetsLoaderDeps ui_assets_loader_deps{
      renderer,
      exe_path,
      screen_profile.profile_name,
      LoadTextureFromFile,
      LoadSurfaceFromMemory,
      CreateTextureFromSurface,
      remember_texture_size,
  };
  UiAssetsLoadResult ui_assets_load_result = LoadUiAssets(ui_assets, ui_assets_loader_deps);
  if (!ui_assets_load_result.ui_pack_hit.empty() && verbose_log) {
    std::cout << "[native_h700] ui pack: " << ui_assets_load_result.ui_pack_hit.string()
              << " assets=" << ui_assets_load_result.packed_asset_count << "\n";
  }
  if (!ui_assets_load_result.ui_root_hit.empty() && verbose_log) {
    std::cout << "[native_h700] ui root: " << ui_assets_load_result.ui_root_hit.string() << "\n";
  }

  std::vector<ContributorAvatarEntry> contributor_avatar_entries;
  auto resolve_ui_root = [&]() -> std::filesystem::path {
    if (!ui_assets_load_result.ui_root_hit.empty()) {
      const std::filesystem::path common_root = ui_assets_load_result.ui_root_hit / "common";
      if (std::filesystem::exists(common_root)) return filesystem_compat::LexicallyNormal(common_root);
      return ui_assets_load_result.ui_root_hit;
    }
    const std::vector<std::filesystem::path> candidates = {
        exe_path / "ui" / "common",
        exe_path / ".." / "ui" / "common",
        std::filesystem::current_path() / "ui" / "common",
        exe_path / "ui",
        exe_path / ".." / "ui",
        std::filesystem::current_path() / "ui",
    };
    for (const auto &candidate : candidates) {
      if (std::filesystem::exists(candidate)) return filesystem_compat::LexicallyNormal(candidate);
    }
    return {};
  };
  const char *env_preload_avatars = std::getenv("ROCREADER_PRELOAD_AVATARS");
  const bool preload_avatars = !env_preload_avatars || std::string(env_preload_avatars) != "0";
  if (preload_avatars) {
    runtime_log::Line("main: contributor avatars preload begin");
    LoadContributorAvatarEntries(contributor_avatar_entries, resolve_ui_root(), exe_path, renderer,
                                 0,
                                 ScalePx(96),
                                 LoadSurfaceFromMemory, remember_texture_size, forget_texture_size);
    runtime_log::Line(std::string("main: contributor avatars preload done count=") +
                      std::to_string(contributor_avatar_entries.size()));
  } else {
    runtime_log::Line("main: contributor avatars preload skipped");
  }
  AvatarBadgeRuntime avatar_badge;
  avatar_badge.Configure(AvatarBadgeRuntimeDeps{
      renderer,
      contributor_avatar_entries,
      [](int value) { return ScalePx(value); },
      CreateScaledTextureCache,
      remember_texture_size,
      forget_texture_size,
  });

  AppStoragePaths app_storage_paths = InitializeAppStoragePaths(verbose_log);
  std::vector<std::string> &books_roots = app_storage_paths.books_roots;
  std::vector<std::string> &cover_roots = app_storage_paths.cover_roots;
  const std::filesystem::path &txt_layout_cache_dir = app_storage_paths.txt_layout_cache_dir;
  const std::filesystem::path &removable_txt_layout_cache_dir = app_storage_paths.removable_txt_layout_cache_dir;
  const std::filesystem::path &cover_thumb_cache_dir = app_storage_paths.cover_thumb_cache_dir;
  const std::filesystem::path &removable_cover_thumb_cache_dir = app_storage_paths.removable_cover_thumb_cache_dir;

  const AppPlatformEnv platform_env = ResolveAppPlatformEnv(device_model_token, screen_profile, rgds_runtime);
  const bool use_h700_defaults = platform_env.capabilities.use_h700_defaults;
  const AppConfigPaths app_config_paths = ResolveAppConfigPaths(runtime_paths);
  bool has_calibrated_keymap = HasCompletedKeyCalibration(app_config_paths.keymap_path.string());
  const InputProfile input_profile = platform_env.capabilities.input_profile;
  if (verbose_log) {
    std::cout << "[native_h700] keymap path: " << filesystem_compat::LexicallyNormal(app_config_paths.keymap_path).string() << "\n";
    std::cout << "[native_h700] config path: " << filesystem_compat::LexicallyNormal(app_config_paths.config_path).string() << "\n";
    std::cout << "[native_h700] power script path: " << filesystem_compat::LexicallyNormal(app_config_paths.power_script_path).string() << "\n";
    std::cout << "[native_h700] device model token: "
              << (device_model_token.empty() ? std::string("unknown") : device_model_token) << "\n";
    std::cout << "[native_h700] input profile: " << InputProfileName(input_profile) << "\n";
  }

  runtime_log::Line(std::string("main: keymap path: ") + filesystem_compat::LexicallyNormal(app_config_paths.keymap_path).string());
  runtime_log::Line(std::string("main: config path: ") + filesystem_compat::LexicallyNormal(app_config_paths.config_path).string());
  runtime_log::Line(std::string("main: input profile: ") + InputProfileName(input_profile));
  InputManager input(app_config_paths.keymap_path.string(), input_profile);
  runtime_log::Line(std::string("main: joy map: ") + input.DescribeJoyMap());
  if (verbose_log) {
    std::cout << "[native_h700] joy map: " << input.DescribeJoyMap() << "\n";
  }
  runtime_log::Line(std::string("main: pad map: ") + input.DescribePadMap());
  if (verbose_log) {
    std::cout << "[native_h700] pad map: " << input.DescribePadMap() << "\n";
  }
  AppRuntimeStores runtime_stores = InitializeAppRuntimeStores(app_config_paths, NormalizePathKey);
  ConfigStore &config = runtime_stores.config;
  ProgressStore &progress = runtime_stores.progress;
  RecentPathStore &favorites_store = runtime_stores.favorites_store;
  RecentPathStore &history_store = runtime_stores.history_store;
  app_context.config.config = &config;
  avatar_badge.SelectSavedOrDefault(config.Get().selected_contributor_avatar_label);
  runtime_log::Line(std::string("main: avatar badge selected index=") +
                    std::to_string(avatar_badge.SelectedIndex()) +
                    " texture=" + (avatar_badge.BadgeTexture() ? "yes" : "no"));
  AppSystemServices system_services = InitializeAppSystemServices(use_h700_defaults, app_config_paths.power_script_path);
  VolumeController &volume_controller = system_services.volume_controller;
  SystemStatusMonitor &system_status = system_services.system_status;
  SystemControlService &system_control_service = system_services.system_control_service;
  LidPowerController &lid_power_controller = system_services.lid_power_controller;
  AppSettingsStates settings_states =
      InitializeAppSettingsStates(config, system_control_service, use_h700_defaults, input_profile);
  SystemSettingsState &system_settings_state = settings_states.system_settings_state;
  TxtSettingsState &txt_settings_state = settings_states.txt_settings_state;
  std::function<void(int)> apply_txt_font_size_level = [&](int level) {
    const int clamped = ClampTxtFontSizeLevel(level);
    if (config.Mutable().txt_font_size_level != clamped) {
      config.Mutable().txt_font_size_level = clamped;
      config.MarkDirty();
    }
    txt_settings_state.font_size_level = clamped;
  };
  ContributorAvatarState contributor_avatar_state = InitializeContributorAvatarState(avatar_badge.SelectedIndex());

  AppUiState app_ui = InitializeAppUiState(config, system_settings_state.levels, volume_controller);
  rgds::InteractionState rgds_interaction;
  const bool is_rgds_runtime = input_profile == InputProfile::RGDS && platform_env.rgds_dual_screen;
  uint32_t last_system_volume_sync = 0;
  SfxBank sfx;
  bool sfx_ready = false;
  bool sfx_init_attempted = false;
  bool pending_volume_change_sfx = false;
  uint32_t pending_volume_change_sfx_due = 0;
  const bool system_volume_sfx_follows_hardware = SystemVolumeSfxFollowsHardwareEnabled();
  int runtime_sfx_volume = config.Get().sfx_volume;
  if (volume_controller.UsesSystemVolume() && system_volume_sfx_follows_hardware) {
    runtime_sfx_volume = SDL_MIX_MAXVOLUME;
  }
  sfx.SetVolume(runtime_sfx_volume);
  auto ensure_sfx_ready = [&]() -> bool {
    if (sfx_ready) return true;
    if (sfx_init_attempted) return false;
    sfx_init_attempted = true;
    sfx_ready = sfx.Init(exe_path);
    if (sfx_ready) {
      sfx.SetVolume(runtime_sfx_volume);
    }
    if (!sfx_ready && verbose_log) {
      std::cout << "[native_h700] sound: disabled (all audio backends failed)\n";
    }
    if (verbose_log) {
      std::cout << "[native_h700] sound init: backend=" << sfx.BackendName()
                << " ready=" << (sfx_ready ? "1" : "0")
                << " volume=" << runtime_sfx_volume << "\n";
    }
    return sfx_ready;
  };
  if (verbose_log) {
    std::cout << "[native_h700] sound: config_audio=" << (config.Get().audio ? "1" : "0")
              << " backend=" << sfx.BackendName()
              << " ready=deferred"
              << " volume=" << config.Get().sfx_volume << "\n";
  }
  auto play_sfx = [&](SfxId id) {
    if (!config.Get().audio) return;
    ensure_sfx_ready();
    sfx.Play(id);
  };
  auto flush_deferred_writes = [&](bool force) {
    const uint32_t tick_now = SDL_GetTicks();
    if ((force || config.ShouldFlush(tick_now, kDeferredSaveDelayMs)) && config.IsDirty()) config.Save();
    if ((force || progress.ShouldFlush(tick_now, kDeferredSaveDelayMs)) && progress.IsDirty()) progress.Save();
    if ((force || favorites_store.ShouldFlush(tick_now, kDeferredSaveDelayMs)) && favorites_store.IsDirty()) favorites_store.Save();
    if ((force || history_store.ShouldFlush(tick_now, kDeferredSaveDelayMs)) && history_store.IsDirty()) history_store.Save();
  };
  PdfRuntime pdf_runtime;
  EpubRuntime epub_runtime;
  ZipImageRuntime zip_image_runtime;
  ReaderManager reader_manager;
  PdfReaderModule pdf_reader_module(pdf_runtime);
  EpubReaderModule epub_reader_module;
  ZipImageReaderModule zip_image_reader_module(zip_image_runtime);
  if (verbose_log) {
    std::cout << "[native_h700] epub comic backend: " << epub_runtime.BackendName()
              << " (real_renderer=" << (epub_runtime.HasRealRenderer() ? "yes" : "no") << ")\n";
    std::cout << "[native_h700] zip image backend: " << zip_image_runtime.BackendName()
              << " (real_renderer=" << (zip_image_runtime.HasRealRenderer() ? "yes" : "no") << ")\n";
  }
  ShelfRenderCache shelf_render_cache;
  ShelfRuntimeState shelf_runtime;
  ShelfScene shelf_scene;
  uint64_t &shelf_content_version = shelf_runtime.content_version;

  AppScene &state = app_shell.Scenes().CurrentRef();
  BootRuntimeState boot_runtime;
  {
    std::error_code ec;
    const std::filesystem::path boot_status_path =
        std::filesystem::current_path(ec) / "cache" / "update_boot_status.txt";
    boot_runtime.language_index = SystemLanguageIndexFromConfigValue(config.Get().system_language);
    const char *boot_install_pending = std::getenv("ROCREADER_BOOT_INSTALL_PENDING_UPDATE");
    if (boot_install_pending && std::string(boot_install_pending) == "1") {
      InitializeBootRuntimePendingUpdate(boot_runtime);
    } else if (!ec) {
      InitializeBootRuntimeReplay(boot_runtime, boot_status_path);
    }
  }
  BootScene boot_scene(boot_runtime, [&]() { app_shell.RequestQuit(); });
  std::vector<BookItem> &shelf_items = shelf_runtime.items;
  CoverCacheRuntime cover_cache(kCoverCacheMaxEntries, kCoverCacheMaxBytes);
  ShelfSceneState shelf_state;
  shelf_state.title_marquee_wait = kTitleMarqueePauseSec;
  std::deque<BookItem> shelf_cover_preload_queue;
  std::unordered_set<std::string> shelf_cover_preload_queued_keys;
  uint64_t shelf_cover_preload_signature = 0;
  const std::filesystem::path shelf_cover_preload_manifest_path =
      cover_thumb_cache_dir.parent_path() / "shelf_cover_preload_manifest.txt";
  std::string saved_shelf_cover_preload_fingerprint;
  const bool shelf_cover_preload_manifest_loaded =
      LoadShelfPreloadFingerprint(shelf_cover_preload_manifest_path, saved_shelf_cover_preload_fingerprint);
  std::string pending_shelf_cover_preload_fingerprint;

  MenuSceneState menu_state;
  InitializeRgdsStartupState(is_rgds_runtime, menu_state, rgds_interaction, false);
  menu_state.items = {
      SettingId::KeyGuide,
      SettingId::KeyCalibration,
      SettingId::SystemControls,
      SettingId::TxtToUtf8,
      SettingId::ContributorAvatars,
      SettingId::ContactMe,
      SettingId::VersionUpdate,
      SettingId::UrlEntry,
      SettingId::ExitApp};
  KeyCalibrationState key_calibration_state;
  InitializeKeyCalibrationState(key_calibration_state);
  VersionUpdateState version_update_state{};
  ConfigureVersionUpdateProfile(version_update_state, input_profile);
  InitializeVersionUpdateState(version_update_state);
  OnlineShelfController online_shelf_controller;
  online_shelf_controller.InitializeFromRuntimeRoot();
  OnlineSourceState &online_source_state = online_shelf_controller.State();
  InitializeLidCloseScreenOffState(config, lid_power_controller, system_settings_state);
  uint32_t last_user_input_tick = SDL_GetTicks();
  enum class ScreenOffMode { Awake, Manual, Auto };
  ScreenOffMode screen_off_mode = ScreenOffMode::Awake;
  bool rgds_display_sleep_active = false;
  uint32_t power_key_ignore_until_tick = 0;
  uint32_t gkd_power_wake_due_tick = 0;
  uint32_t input_wake_next_resync_tick = 0;
  auto resync_input_after_screen_wake = [&]() {
    runtime_log::Line("main: input resync after screen wake");
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    RefreshAppInputDevices(input_devices, verbose_log);
    if (input_profile == InputProfile::H700Default ||
        input_profile == InputProfile::H70034xxSp ||
        input_profile == InputProfile::H70035xxH) {
      ReopenAppInputDevices(input_devices, verbose_log);
      const uint32_t now = SDL_GetTicks();
      input_wake_next_resync_tick = now + 220;
    }
    input.RefreshDevices();
    input.SuppressPowerUntilRelease();
  };
  TxtTranscodeJob txt_transcode_job{};
  ReaderUiState reader_ui{};
  std::string &current_book = reader_ui.current_book;
  ReaderProgress &reader = reader_ui.progress;
  ReaderMode &reader_mode = reader_ui.mode;
  bool &reader_progress_overlay_visible = reader_ui.progress_overlay_visible;
  float &hold_cooldown = reader_ui.hold_cooldown;
  auto current_category = [&]() -> ShelfCategory {
    return ClampShelfCategory(shelf_state.nav_selected_index);
  };
  auto online_shelf_active = [&]() {
    return online_shelf_controller.IsActive();
  };
  auto file_exists_fs = [&](const std::filesystem::path &path) -> bool {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec) && !ec;
  };
  auto file_exists = [&](const std::string &path) -> bool {
    return !path.empty() && file_exists_fs(std::filesystem::path(path));
  };
  auto item_real_path = [&](const BookItem &item) -> const std::string & {
    return book_library_service::RealPathForItem(item);
  };
  auto item_fs_path = [&](const BookItem &item) -> std::filesystem::path {
    if (!item.native_fs_path.empty()) return item.native_fs_path;
    const std::string &real_path = item_real_path(item);
    return real_path.empty() ? std::filesystem::path(item.path) : std::filesystem::path(real_path);
  };
  auto get_compatible_progress = [&](const BookItem &item) -> ReaderProgress {
    return book_library_service::CompatibleProgressForItem(item, progress);
  };
  auto make_shelf_runtime_deps = [&]() {
    return book_library_service::MakeShelfRuntimeDeps(
        NormalizePathKey,
        GetLowerExt,
        [&]() { return boot_runtime.scanned_books; },
        favorites_store,
        history_store,
        kShelfScanCacheTtlMs,
        kShelfScanCacheMaxEntries);
  };

  auto rebuild_shelf_items = [&]() {
    if (online_shelf_controller.RebuildShelfIfActive(shelf_runtime, shelf_state.nav_selected_index)) return;
    RebuildShelfItems(shelf_runtime, current_category(), shelf_state.current_folder, books_roots, make_shelf_runtime_deps());
  };
  std::string transient_message;
  uint32_t transient_message_until = 0;
  uint32_t transient_message_shown_at = 0;
  bool transient_message_dismiss_on_input = false;
  auto show_transient_message = [&](const std::string &message,
                                    uint32_t duration_ms = kTransientMessageDurationMs,
                                    bool dismiss_on_input = false) {
    transient_message = message;
    transient_message_shown_at = SDL_GetTicks();
    transient_message_until = transient_message_shown_at + duration_ms;
    transient_message_dismiss_on_input = dismiss_on_input;
  };
  auto any_button_just_pressed = [&]() {
    for (int i = 0; i < kButtonCount; ++i) {
      if (input.IsJustPressed(static_cast<Button>(i))) return true;
    }
    return false;
  };
  auto any_non_power_button_just_pressed = [&]() {
    for (int i = 0; i < kButtonCount; ++i) {
      const Button button = static_cast<Button>(i);
      if (button == Button::Power || !input.IsJustPressed(button)) continue;
      return true;
    }
    return false;
  };

  auto clear_cover_cache = [&]() {
    cover_cache.Clear(forget_texture_size);
    shelf_cover_preload_queue.clear();
    shelf_cover_preload_queued_keys.clear();
    shelf_cover_preload_signature = 0;
    ++shelf_content_version;
  };
  auto clear_history_and_refresh_shelf = [&]() {
    history_store.Clear();
    if (current_category() == ShelfCategory::History) {
      shelf_scene.ResetToCategoryRoot(shelf_state);
      clear_cover_cache();
      rebuild_shelf_items();
    }
  };
  auto clear_directory_files = [&](const std::filesystem::path &dir_path) {
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || ec) return;
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::directory_iterator it(dir_path, opts, ec), end; it != end; it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      std::filesystem::remove_all(it->path(), ec);
      ec.clear();
    }
  };

  auto cover_texture_w = [&]() {
    const int scale = online_shelf_active() ? 1 : 2;
    return std::max(FocusedCoverW(), Layout().cover_w * scale);
  };
  auto cover_texture_h = [&]() {
    const int scale = online_shelf_active() ? 1 : 2;
    return std::max(FocusedCoverH(), Layout().cover_h * scale);
  };
  auto make_cover_service_deps = [&]() {
    return CoverServiceDeps{
        renderer,
        cover_texture_w(),
        cover_texture_h(),
        kCoverAspect,
        cover_thumb_cache_dir,
        removable_cover_thumb_cache_dir,
        cover_roots,
        ui_assets.book_cover_txt,
        ui_assets.book_cover_pdf,
        &cover_cache.ManualPathCache(),
        NormalizePathKey,
        GetLowerExt,
        LoadSurfaceFromFile,
        LoadSurfaceFromMemory,
        CreateNormalizedCoverTexture,
        CreateTextureFromSurface,
        remember_texture_size,
    };
  };
  auto make_online_shelf_controller_deps = [&]() {
    return OnlineShelfControllerDeps{
        renderer,
        &cover_cache,
        cover_texture_w,
        cover_texture_h,
        kCoverAspect,
        file_exists_fs,
        LoadSurfaceFromFile,
        CreateNormalizedCoverTexture,
        CreateTextureFromSurface,
        remember_texture_size,
        get_texture_size,
        forget_texture_size,
        {},
        {},
    };
  };

  auto has_manual_cover_exact_or_fuzzy = [&](const BookItem &item) -> bool {
    CoverServiceDeps deps = make_cover_service_deps();
    return HasManualCoverExactOrFuzzy(item, deps);
  };

  auto has_cached_doc_cover_on_disk = [&](const std::string &doc_path) -> bool {
    CoverServiceDeps deps = make_cover_service_deps();
    return HasCachedDocCoverOnDisk(doc_path, deps);
  };

  auto create_doc_first_page_cover_texture = [&](const std::string &doc_path) -> SDL_Texture * {
    CoverServiceDeps deps = make_cover_service_deps();
    const std::string ext = GetLowerExt(doc_path);
    SDL_Texture *texture = nullptr;
    if (ext == ".pdf") texture = CreatePdfFirstPageCoverTexture(doc_path, deps);
    else if (ext == ".epub") texture = CreateEpubFirstImageCoverTextureLocal(doc_path, deps);
    else if (ext == ".zip" || ext == ".cbz") texture = CreateZipImageFirstImageCoverTextureLocal(doc_path, deps);
    if (texture) return texture;
    return nullptr;
  };

  auto get_cover_texture = [&](const BookItem &item) -> SDL_Texture * {
    if (item.is_remote) {
      OnlineShelfControllerDeps deps = make_online_shelf_controller_deps();
      return online_shelf_controller.GetCoverTexture(item, deps);
    }
    const std::string &real_path = item_real_path(item);
    const std::string cover_cache_key = std::to_string(static_cast<int>(current_category())) + "|" +
                                        real_path + "|cover|" + std::to_string(cover_texture_w()) + "x" +
                                        std::to_string(cover_texture_h());
    if (SDL_Texture *cached = cover_cache.FindTexture(cover_cache_key)) {
      return cached;
    }

    CoverServiceDeps deps = make_cover_service_deps();
    BookItem resolved_item = item;
    resolved_item.path = real_path;
    resolved_item.real_path = real_path;
    resolved_item.native_fs_path = item_fs_path(item);
    SDL_Texture *tex = ResolveBookCoverTexture(resolved_item, current_category(), deps);

    const bool shared_ui_cover = (tex == ui_assets.book_cover_txt ||
                                  tex == ui_assets.book_cover_pdf);
    const bool owned = (tex != nullptr && !shared_ui_cover);
    cover_cache.PutTexture(cover_cache_key, tex, owned, get_texture_size, forget_texture_size);
    return tex;
  };

  auto get_cached_cover_texture = [&](const BookItem &item) -> SDL_Texture * {
    if (item.is_remote) {
      OnlineShelfControllerDeps deps = make_online_shelf_controller_deps();
      return online_shelf_controller.GetCachedCoverTexture(item, deps);
    }
    const std::string &real_path = item_real_path(item);
    const std::string cover_cache_key = std::to_string(static_cast<int>(current_category())) + "|" +
                                        real_path + "|cover|" + std::to_string(cover_texture_w()) + "x" +
                                        std::to_string(cover_texture_h());
    return cover_cache.FindTexture(cover_cache_key);
  };

  auto preload_cover_texture_for_category = [&](ShelfCategory category, const BookItem &item) {
    if (item.is_remote) {
      OnlineShelfControllerDeps deps = make_online_shelf_controller_deps();
      (void)online_shelf_controller.PreloadCoverTexture(item, deps);
      return;
    }
    const std::string &real_path = item_real_path(item);
    const std::string cover_cache_key = std::to_string(static_cast<int>(category)) + "|" + real_path + "|cover|" +
                                        std::to_string(cover_texture_w()) + "x" +
                                        std::to_string(cover_texture_h());
    if (cover_cache.FindTexture(cover_cache_key)) return;

    CoverServiceDeps deps = make_cover_service_deps();
    BookItem resolved_item = item;
    resolved_item.path = real_path;
    resolved_item.real_path = real_path;
    resolved_item.native_fs_path = item_fs_path(item);
    SDL_Texture *tex = ResolveBookCoverTexture(resolved_item, category, deps);

    const bool shared_ui_cover = (tex == ui_assets.book_cover_txt ||
                                  tex == ui_assets.book_cover_pdf);
    const bool owned = (tex != nullptr && !shared_ui_cover);
    cover_cache.PutTexture(cover_cache_key, tex, owned, get_texture_size, forget_texture_size);
  };

  auto ensure_shelf_page_cover_textures = [&](int page) {
    if (page < 0 || ShelfGridCols() <= 0 || ShelfItemsPerPage() <= 0) return;
    const ShelfCategory category = current_category();
    const int start = page * ShelfGridCols();
    const int end = std::min<int>(start + ShelfItemsPerPage(), shelf_items.size());
    for (int i = start; i < end; ++i) {
      preload_cover_texture_for_category(category, shelf_items[i]);
    }
  };

  auto make_preload_queue_key = [&](ShelfCategory category, const BookItem &item) -> std::string {
    const std::string &real_path = book_library_service::RealPathForItem(item);
    return std::to_string(static_cast<int>(category)) + "|" + NormalizePathKey(real_path);
  };

  auto build_shelf_cover_preload_items = [&](const std::function<size_t(ShelfCategory)> &max_for_category)
      -> std::vector<BookItem> {
    if (online_shelf_active()) return {};
    struct PreloadEntry {
      ShelfCategory category = ShelfCategory::AllComics;
      BookItem item;
    };

    std::vector<PreloadEntry> entries;
    std::unordered_set<std::string> seen;
    const std::array<ShelfCategory, 4> categories = {
        ShelfCategory::AllBooks,
        ShelfCategory::Collections,
        ShelfCategory::History,
        ShelfCategory::AllComics,
    };
    for (ShelfCategory category : categories) {
      size_t category_count = 0;
      const size_t max_for_this_category = max_for_category ? max_for_category(category) : 0;
      std::vector<BookItem> base =
          ScanShelfBaseItems(shelf_runtime, category, std::string(), books_roots, make_shelf_runtime_deps());
      for (BookItem &item : base) {
        if (!ShelfMatchCategory(item, category, make_shelf_runtime_deps())) continue;
        if (max_for_this_category > 0 && category_count >= max_for_this_category) break;
        const std::string key = make_preload_queue_key(category, item);
        if (!seen.insert(key).second) continue;
        entries.push_back(PreloadEntry{category, std::move(item)});
        ++category_count;
      }
    }

    std::vector<BookItem> out;
    out.reserve(entries.size());
    for (const PreloadEntry &entry : entries) {
      BookItem item = entry.item;
      item.preload_category = static_cast<int>(entry.category);
      out.push_back(std::move(item));
    }
    return out;
  };

  auto boot_preload_item_limit = [&](ShelfCategory category) -> size_t {
    const size_t windows = (category == ShelfCategory::AllComics)
                               ? kBootDefaultShelfPreloadWindows
                               : kBootOtherShelfPreloadWindows;
    const size_t visible = static_cast<size_t>(ShelfItemsPerPage());
    const size_t cols = static_cast<size_t>(ShelfGridCols());
    if (windows == 0) return 0;
    return visible + (windows - 1) * cols;
  };

  auto build_boot_shelf_cover_preload_items = [&]() -> std::vector<BookItem> {
    if (online_shelf_active()) return {};
    const std::vector<BookItem> items = build_shelf_cover_preload_items(boot_preload_item_limit);
    pending_shelf_cover_preload_fingerprint =
        BuildShelfPreloadFingerprint(items, books_roots, cover_roots, cover_texture_w(), cover_texture_h());
    return items;
  };

  auto preload_shelf_cover_texture = [&](const BookItem &item) {
    ShelfCategory category = current_category();
    if (item.preload_category >= static_cast<int>(ShelfCategory::AllComics) &&
        item.preload_category <= static_cast<int>(ShelfCategory::History)) {
      category = static_cast<ShelfCategory>(item.preload_category);
    }
    preload_cover_texture_for_category(category, item);
  };

  auto queue_shelf_cover_preload = [&](ShelfCategory category, const BookItem &item) {
    const std::string key = make_preload_queue_key(category, item);
    if (!shelf_cover_preload_queued_keys.insert(key).second) return;
    BookItem queued = item;
    queued.preload_category = static_cast<int>(category);
    shelf_cover_preload_queue.push_back(std::move(queued));
  };

  auto queue_priority_shelf_cover_preload = [&](ShelfCategory category, const BookItem &item) {
    BookItem queued = item;
    queued.preload_category = static_cast<int>(category);
    shelf_cover_preload_queue.push_front(std::move(queued));
  };

  auto reset_shelf_cover_stream_preload = [&]() {
    shelf_cover_preload_queue.clear();
    shelf_cover_preload_queued_keys.clear();
    const std::vector<BookItem> items = build_shelf_cover_preload_items({});
    for (const BookItem &item : items) {
      ShelfCategory category = current_category();
      if (item.preload_category >= static_cast<int>(ShelfCategory::AllComics) &&
          item.preload_category <= static_cast<int>(ShelfCategory::History)) {
        category = static_cast<ShelfCategory>(item.preload_category);
      }
      queue_shelf_cover_preload(category, item);
    }
  };

  auto queue_visible_shelf_cover_lookahead = [&]() {
    if (state != AppScene::Shelf) return;
    if (online_shelf_active()) return;
    const uint64_t signature = (shelf_runtime.content_version << 32) ^
                               (static_cast<uint64_t>(shelf_state.nav_selected_index & 0xff) << 24) ^
                               static_cast<uint64_t>(shelf_state.shelf_page & 0xffffff);
    if (signature == shelf_cover_preload_signature) return;
    shelf_cover_preload_signature = signature;

    const ShelfCategory category = current_category();
    auto queue_index_priority = [&](int index) {
      if (index < 0 || index >= static_cast<int>(shelf_items.size())) return;
      queue_priority_shelf_cover_preload(category, shelf_items[index]);
    };
    queue_index_priority(shelf_state.focus_index);
    queue_index_priority(shelf_state.focus_index - 1);
    queue_index_priority(shelf_state.focus_index + 1);
    queue_index_priority(shelf_state.focus_index - ShelfGridCols());
    queue_index_priority(shelf_state.focus_index + ShelfGridCols());

    const int start = std::max(0, shelf_state.shelf_page * ShelfGridCols());
    const int max_count = ShelfItemsPerPage() * static_cast<int>(1 + kShelfStreamPreloadLookaheadPages);
    const int end = std::min<int>(start + max_count, shelf_items.size());
    for (int i = end - 1; i >= start; --i) {
      queue_priority_shelf_cover_preload(category, shelf_items[i]);
    }
  };

  auto process_shelf_cover_stream_preload = [&]() {
    if (state != AppScene::Shelf || input.AnyPressed() || shelf_state.page_animating) return;
    size_t processed = 0;
    while (processed < kShelfStreamPreloadBatchEntries && !shelf_cover_preload_queue.empty()) {
      BookItem item = std::move(shelf_cover_preload_queue.front());
      shelf_cover_preload_queue.pop_front();
      preload_shelf_cover_texture(item);
      ++processed;
    }
  };

#ifdef HAVE_SDL2_TTF
  UiTextCacheState ui_text_cache{};
  ui_text_cache.max_text_cache_entries = kTextCacheMaxEntries;
  UiTextCacheState status_text_cache{};
  status_text_cache.max_text_cache_entries = kTextCacheMaxEntries;
  UiTextCacheState menu_text_cache{};
  menu_text_cache.max_text_cache_entries = kTextCacheMaxEntries;
  auto scaled_font_pt = [&](int pt) {
    return std::max(1, static_cast<int>(std::round(static_cast<float>(pt) * Layout().ui_scale)));
  };
  const bool gkd_font_profile = input_profile == InputProfile::GKD350HUltra;
  auto profile_font_pt = [&](int pt) {
    return gkd_font_profile ? std::max(1, pt * gkd350h_ultra::kFontScale) : scaled_font_pt(pt);
  };
  auto reader_font_pt_for_level = [&](int level) {
    return profile_font_pt(TxtFontPointSizeForLevel(level));
  };
  const int body_font_pt = gkd_font_profile ? gkd350h_ultra::kBodyFontPt : scaled_font_pt(16);
  const int title_font_pt = gkd_font_profile ? gkd350h_ultra::kTitleFontPt : scaled_font_pt(24);
  const int status_font_pt = gkd_font_profile ? 32 : body_font_pt;
  const int menu_body_font_pt = gkd_font_profile ? gkd350h_ultra::kMenuBodyFontPt : body_font_pt;
  const int menu_title_font_pt = gkd_font_profile ? gkd350h_ultra::kMenuTitleFontPt : title_font_pt;
  int current_reader_font_pt = reader_font_pt_for_level(config.Get().txt_font_size_level);
  TxtTextServiceState txt_text_service{
      {},
      txt_layout_cache_dir,
      removable_txt_layout_cache_dir,
      kTxtLayoutCacheMaxEntries,
      kTxtMaxWrappedLines,
  };

  auto clear_text_cache = [&]() {
    ClearUiTextCache(ui_text_cache, forget_texture_size);
  };

  auto apply_epub_flow_theme = [&]() {
    epub_reader_module.SetFlowBaseFontPointSize(current_reader_font_pt);
    epub_reader_module.SetFlowColors(GetTxtBackgroundColor(config.Get().txt_background_color),
                                     GetTxtFontColor(config.Get().txt_font_color));
  };

  apply_txt_font_size_level = [&](int level) {
    const int clamped = ClampTxtFontSizeLevel(level);
    if (config.Mutable().txt_font_size_level != clamped) {
      config.Mutable().txt_font_size_level = clamped;
      config.MarkDirty();
      config.Save();
    }
    txt_settings_state.font_size_level = clamped;
    current_reader_font_pt = reader_font_pt_for_level(clamped);
    apply_epub_flow_theme();
    ShutdownUiTextCache(ui_text_cache, forget_texture_size);
  };

  std::function<void(const std::string &, bool)> persist_current_txt_resume_snapshot;

  auto get_title_ellipsized = [&](const std::string &raw_name, int text_area_w,
                                  const std::function<int(const std::string &)> &measure) -> std::string {
    return GetTitleEllipsized(ui_text_cache, raw_name, text_area_w, measure);
  };

  auto open_ui_font = [&]() {
    OpenUiFonts(ui_text_cache, exe_path, ui_path, body_font_pt, title_font_pt, current_reader_font_pt);
  };

  auto get_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    return GetUiTextTexture(ui_text_cache, renderer, text, color, UiTextRole::Body);
  };

  auto get_title_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    return GetUiTextTexture(ui_text_cache, renderer, text, color, UiTextRole::Title);
  };

  auto get_reader_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    return GetUiTextTexture(ui_text_cache, renderer, text, color, UiTextRole::Reader);
  };
  auto open_status_font = [&]() {
    OpenUiFonts(status_text_cache, exe_path, ui_path, status_font_pt, status_font_pt, current_reader_font_pt);
  };
  auto get_status_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    if (!gkd_font_profile) return get_text_texture(text, color);
    open_status_font();
    return GetUiTextTexture(status_text_cache, renderer, text, color, UiTextRole::Body);
  };
  auto open_menu_font = [&]() {
    OpenUiFonts(menu_text_cache, exe_path, ui_path, menu_body_font_pt, menu_title_font_pt, current_reader_font_pt);
  };
  auto get_menu_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    if (!gkd_font_profile) return get_text_texture(text, color);
    open_menu_font();
    return GetUiTextTexture(menu_text_cache, renderer, text, color, UiTextRole::Body);
  };
  auto get_menu_title_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    if (!gkd_font_profile) return get_title_text_texture(text, color);
    open_menu_font();
    return GetUiTextTexture(menu_text_cache, renderer, text, color, UiTextRole::Title);
  };
  rgds::LoadRenderResources(rgds_render_resources, rgds::RenderResourceLoadDeps{
      rgds_runtime,
      exe_path,
      ui_path,
      screen_profile.profile_name,
      LoadTextureFromFile,
      LoadSurfaceFromMemory,
      CreateTextureFromSurface,
      remember_texture_size,
      forget_texture_size,
      0,
      ScalePx(96),
      kTextCacheMaxEntries,
      body_font_pt,
      title_font_pt,
      current_reader_font_pt,
  });

  auto shelf_title_text = [&](const BookItem &item) -> std::string {
    if (item.is_remote) return item.name;
    if (item.is_dir) return item.name;
    try {
      const std::string stem = std::filesystem::path(item.path).stem().string();
      if (!stem.empty()) return stem;
    } catch (...) {
    }
    return item.name;
  };

  auto focused_title_needs_marquee = [&]() -> bool {
    if (state != AppScene::Shelf) return false;
    if (shelf_state.focus_index < 0 || shelf_state.focus_index >= static_cast<int>(shelf_items.size())) return false;
    SDL_Color title_color{248, 250, 255, 255};
    const std::string display = shelf_title_text(shelf_items[shelf_state.focus_index]);
    if (display.empty()) return false;
    TextCacheEntry *te = get_text_texture(display, title_color);
    const int text_area_w = std::max(8, FocusedCoverW() - Layout().title_text_pad_x * 2);
    if (te && te->texture) return te->w > text_area_w;
    return static_cast<int>(display.size()) * 8 > text_area_w;
  };

#endif

  auto clear_runtime_cache_files = [&]() {
    clear_cover_cache();
    clear_directory_files(cover_thumb_cache_dir);
    clear_directory_files(removable_cover_thumb_cache_dir);
#ifdef HAVE_SDL2_TTF
    ClearTxtLayoutCache(txt_text_service);
    clear_text_cache();
#endif
    clear_directory_files(txt_layout_cache_dir);
    clear_directory_files(removable_txt_layout_cache_dir);
    if (verbose_log) {
      std::cout << "[native_h700] runtime caches cleared: both cards cover thumbs + txt layouts/resume\n";
    }
  };

  TxtTranscodeServiceDeps txt_transcode_deps{
      books_roots,
      NormalizePathKey,
      GetLowerExt,
      ReadFileBytes,
      DecodeTextBytesToUtf8,
      WriteFileBytesAtomically,
      clear_runtime_cache_files,
      verbose_log,
  };

  auto start_txt_transcode_job = [&]() { StartTxtTranscodeJob(txt_transcode_job, txt_transcode_deps); };
  auto process_txt_transcode_step = [&]() { ProcessTxtTranscodeStep(txt_transcode_job, txt_transcode_deps); };

  auto destroy_shelf_render_cache = [&]() {
    DestroyShelfRenderCache(shelf_render_cache, forget_texture_size);
  };

  auto invalidate_all_render_cache = [&]() {};

  auto close_text_reader = [&]() {
    reader_ui.Txt() = TxtReaderState{};
    reader_progress_overlay_visible = false;
    if (reader_mode == ReaderMode::Txt) {
      reader_mode = ReaderMode::None;
    }
  };

  auto clamp_text_scroll = [&]() {
    const int max_scroll = std::max(0, reader_ui.Txt().content_h - reader_ui.Txt().viewport_h);
    reader_ui.Txt().scroll_px = ClampInt(reader_ui.Txt().scroll_px, 0, max_scroll);
    if (!reader_ui.Txt().loading) {
      reader.scroll_y = reader_ui.Txt().scroll_px;
      if (!reader_ui.Txt().line_source_offsets.empty()) {
        const size_t top_line = std::min(
            reader_ui.Txt().line_source_offsets.size() - 1,
            static_cast<size_t>(std::max(0, reader_ui.Txt().scroll_px / std::max(1, reader_ui.Txt().line_h))));
        reader.scroll_x = static_cast<int>(std::min<size_t>(
            reader_ui.Txt().line_source_offsets[top_line], static_cast<size_t>(std::numeric_limits<int>::max())));
      } else {
        reader.scroll_x = 0;
      }
    }
  };

  auto get_text_viewport_bounds = [&]() -> SDL_Rect {
    open_ui_font();
    const int font_pt = reader_font_pt_for_level(config.Get().txt_font_size_level);
    int font_h = font_pt + 4;
#ifdef HAVE_SDL2_TTF
    if (ui_text_cache.reader_font) font_h = TTF_FontHeight(ui_text_cache.reader_font);
#endif
    const bool use_rgds_virtual_canvas = is_rgds_runtime;
    const int viewport_w = use_rgds_virtual_canvas ? rgds::kVirtualReaderW : Layout().screen_w;
    const int viewport_h = use_rgds_virtual_canvas ? rgds::kVirtualReaderH : Layout().screen_h;
    SDL_Rect bounds = GetTxtViewportBounds(
        use_rgds_virtual_canvas ? nullptr : renderer,
        TxtViewportRequest{
            viewport_w,
            viewport_h,
            Layout().txt_margin_x,
            Layout().txt_margin_y,
            font_pt,
            font_h + ScalePx(kTxtLineSpacing),
        });
    if (use_rgds_virtual_canvas) {
      runtime_log::Line("main: RGDS txt viewport locked to virtual reader canvas bounds=" +
                        std::to_string(bounds.x) + "," + std::to_string(bounds.y) + " " +
                        std::to_string(bounds.w) + "x" + std::to_string(bounds.h) +
                        " canvas=" + std::to_string(viewport_w) + "x" + std::to_string(viewport_h));
    }
    return bounds;
  };

#ifdef HAVE_SDL2_TTF
  const std::string kTxtParagraphIndent = u8"銆€銆€";
  const std::string kTxtParagraphIndentAscii = "  ";

#endif

  auto append_wrapped_text_line = [&](TxtReaderState &state, const std::string &line,
                                      size_t source_line_offset) -> bool {
#ifndef HAVE_SDL2_TTF
    (void)state;
    (void)line;
    (void)source_line_offset;
    return false;
#else
    return AppendWrappedTextLine(state, line, ui_text_cache.reader_font,
                                 source_line_offset, txt_text_service.max_wrapped_lines);
#endif
  };

  auto make_txt_session_facade_deps = [&]() {
    return TxtSessionFacadeDeps{
        reader_ui,
        txt_text_service,
        open_ui_font,
        [&]() -> bool { return ui_text_cache.reader_font != nullptr; },
        [&]() -> int {
#ifdef HAVE_SDL2_TTF
          return ui_text_cache.reader_font ? TTF_FontHeight(ui_text_cache.reader_font) : 0;
#else
          return 0;
#endif
        },
        get_text_viewport_bounds,
        NormalizePathKey,
        [&](const std::string &raw, std::string &out) { return DecodeTextBytesToUtf8(raw, out); },
        append_wrapped_text_line,
        invalidate_all_render_cache,
        clamp_text_scroll,
        txt_layout_cache_dir / "epub_assets",
        ScalePx(kTxtLineSpacing),
        kTxtMaxBytes,
        kTxtMaxWrappedLines,
        kTxtResumeSaveDelayMs,
        kTxtLayoutCacheVersion,
    };
  };

  TxtSessionFacade txt_session_facade{make_txt_session_facade_deps()};

  persist_current_txt_resume_snapshot = [&](const std::string &book_path, bool force) {
    txt_session_facade.PersistResumeSnapshot(book_path, force);
  };

  auto open_text_book = [&](const std::string &path) -> bool {
    return txt_session_facade.OpenTextBook(path);
  };

  auto open_epub_text_book = [&](const std::string &path) -> bool {
    return txt_session_facade.OpenEpubTextFallback(path);
  };

  auto text_scroll_by = [&](int delta_px) {
    txt_session_facade.ScrollBy(current_book, delta_px);
  };

  auto text_page_by = [&](int dir) {
    txt_session_facade.PageBy(current_book, dir);
  };

  auto text_jump_to_percent = [&](int pct) {
    txt_session_facade.JumpToPercent(current_book, pct);
  };

  TxtReaderModule txt_reader_module(
      reader_ui,
      TxtReaderModuleCallbacks{
          open_text_book,
          close_text_reader,
          text_scroll_by,
          text_page_by,
          text_jump_to_percent,
      });
  reader_manager.Register(ReaderMode::Pdf, &pdf_reader_module);
  reader_manager.Register(ReaderMode::Epub, &epub_reader_module);
  reader_manager.Register(ReaderMode::Txt, &txt_reader_module);
  reader_manager.Register(ReaderMode::ZipImage, &zip_image_reader_module);
  ReaderScene reader_scene([&]() {
    app_shell.Scenes().EnterShelf();
    if (is_rgds_runtime) {
      rgds::CloseReaderToShelf(rgds_interaction);
      InitializeRgdsStartupState(is_rgds_runtime, menu_state, rgds_interaction, false);
    }
    app_shell.StartSceneFlash();
  });
  MenuScene menu_scene;

  auto make_reader_progress_controller_deps = [&]() {
    return ReaderProgressControllerDeps{
        reader_ui,
        pdf_runtime,
        epub_runtime,
        zip_image_runtime,
        &reader_manager,
        text_jump_to_percent,
    };
  };
  ReaderProgressControllerDeps reader_progress_deps = make_reader_progress_controller_deps();

  auto reader_jump_to_percent = [&](int pct) { ReaderJumpToPercent(reader_progress_deps, pct); };
  auto reader_jump_to_txt_source_offset = [&](size_t source_offset) {
    txt_session_facade.JumpToSourceOffset(current_book, source_offset);
  };

  auto current_reader_progress_pct = [&]() -> int {
    return CurrentReaderProgressPercent(reader_progress_deps);
  };
  auto make_reader_progress_input_config = [&]() {
    return MakeReaderSceneProgressInputConfig(ScalePx(kReaderTapStepPx));
  };
  auto make_reader_progress_overlay_metrics = [&]() {
    return MakeReaderSceneProgressOverlayMetrics(Layout());
  };
  auto make_reader_scene_input_services = [&]() {
    return MakeReaderSceneInputServices(
        close_text_reader,
        persist_current_txt_resume_snapshot,
        ReaderSceneInputActions{
            current_reader_progress_pct,
            reader_jump_to_percent,
            reader_jump_to_txt_source_offset,
            text_scroll_by,
            text_page_by,
            show_transient_message,
        });
  };
  auto make_reader_scene_input_deps = [&](float frame_dt, bool &transient_message_dismissed) {
    return ReaderSceneInputDeps{
        input,
        reader_ui,
        progress,
        &reader_manager,
        pdf_runtime,
        epub_runtime,
        zip_image_runtime,
        frame_dt,
        make_reader_progress_input_config(),
        is_rgds_runtime,
        transient_message_dismissed,
        make_reader_scene_input_services(),
    };
  };
  auto make_reader_scene_render_services = [&](SDL_Renderer *target_renderer = nullptr) {
    SDL_Renderer *render_target = target_renderer ? target_renderer : renderer;
    return MakeReaderSceneRenderServices(
        render_target,
        [](int value) { return ScalePx(value); },
        [render_target](int x, int y, int w, int h, SDL_Color c, bool filled) {
          DrawRect(render_target, x, y, w, h, c, filled);
        },
        clamp_text_scroll,
        get_text_texture,
        get_reader_text_texture,
        get_menu_text_texture);
  };
  auto make_rgds_reader_render_deps = [&](SDL_Renderer *target_renderer, float frame_dt, bool tick_modules,
                                          const rgds::ReaderLayout &reader_layout) {
    return ReaderSceneRenderDeps{
        target_renderer,
        reader_ui,
        &reader_manager,
        reader_progress_deps,
        frame_dt,
        reader_layout.canvas_w,
        reader_layout.canvas_h,
        GetTxtBackgroundColor(config.Get().txt_background_color),
        GetTxtFontColor(config.Get().txt_font_color),
        Layout().settings_sidebar_w,
        make_reader_progress_overlay_metrics(),
        reader_layout.overlay_viewport,
        true,
        true,
        make_reader_scene_render_services(target_renderer),
        tick_modules,
    };
  };
  auto make_reader_render_deps = [&](SDL_Renderer *target_renderer, float frame_dt) {
    return ReaderSceneRenderDeps{
        target_renderer,
        reader_ui,
        &reader_manager,
        reader_progress_deps,
        frame_dt,
        Layout().screen_w,
        Layout().screen_h,
        GetTxtBackgroundColor(config.Get().txt_background_color),
        GetTxtFontColor(config.Get().txt_font_color),
        Layout().settings_sidebar_w,
        make_reader_progress_overlay_metrics(),
        SDL_Rect{0, 0, 0, 0},
        false,
        false,
        make_reader_scene_render_services(target_renderer),
    };
  };
  auto make_menu_scene_layout_metrics = [&]() {
    return MakeMenuSceneLayoutMetrics(Layout());
  };
  auto make_menu_scene_input_context = [&](const NativeConfig &ui_cfg,
                                           float frame_dt,
                                           MenuSceneState &menu_state_ref,
                                           MenuSceneInputServices services) {
    return MakeMenuSceneInputContext(input,
                                     ui_cfg,
                                     input_profile,
                                     frame_dt,
                                     menu_state_ref,
                                     system_settings_state,
                                     txt_settings_state,
                                     contributor_avatar_state,
                                     contributor_avatar_entries.size(),
                                     key_calibration_state,
                                     version_update_state,
                                     online_source_state,
                                     std::move(services));
  };
  auto make_system_settings_callbacks = [&]() {
    return SystemSettingsCallbacks{
        [&](SystemControlLevels &levels) {
          system_control_service.Refresh(levels);
        },
        [&](int delta, SystemControlLevels &levels) {
          int saved_percent = app_ui.volume_display_percent;
          bool ok = false;
          if (input_profile == InputProfile::GKD350HUltra && volume_controller.UsesSystemVolume()) {
            ok = volume_controller.AdjustBySteps(delta, saved_percent);
            if (ok) {
              const int max_level = volume_controller.MaxLevel();
              levels.volume.available = true;
              levels.volume.max_level = max_level;
              levels.volume.level = std::clamp((saved_percent * max_level + 50) / 100, 0, max_level);
              runtime_sfx_volume = system_volume_sfx_follows_hardware
                                       ? SDL_MIX_MAXVOLUME
                                       : std::clamp((saved_percent * SDL_MIX_MAXVOLUME + 50) / 100,
                                                    0, SDL_MIX_MAXVOLUME);
              sfx.SetVolume(runtime_sfx_volume);
            }
          } else {
            ok = system_control_service.AdjustVolume(delta, levels);
            if (levels.volume.available) {
              saved_percent =
                  std::clamp((levels.volume.level * 100) / std::max(1, levels.volume.max_level), 0, 100);
            }
          }
          if (ok) {
            app_ui.volume_display_percent = saved_percent;
            if (config.Mutable().system_volume_percent != saved_percent) {
              config.Mutable().system_volume_percent = saved_percent;
              config.MarkDirty();
            }
          }
          return ok;
        },
        [&](int delta, SystemControlLevels &levels) {
          const bool ok = system_control_service.AdjustBrightness(delta, levels);
          if (ok && levels.brightness.available) {
            const int saved_level =
                std::clamp(levels.brightness.level, 0, std::max(1, levels.brightness.max_level));
            if (config.Mutable().screen_brightness_level != saved_level) {
              config.Mutable().screen_brightness_level = saved_level;
              config.MarkDirty();
            }
          }
          return ok;
        },
        [&](SystemSettingsState &settings_state) {
          settings_state.lid_close_screen_off_enabled = config.Get().lid_close_screen_off;
          settings_state.auto_sleep_interval_index = ClampAutoSleepIntervalIndex(config.Get().auto_sleep_interval_index);
          settings_state.system_language_index = SystemLanguageIndexFromConfigValue(config.Get().system_language);
        },
        [&](bool enabled, SystemSettingsState &settings_state) {
          NativeConfig &cfg = config.Mutable();
          cfg.lid_close_screen_off = enabled;
          config.MarkDirty();
          lid_power_controller.SetEnabled(enabled);
          settings_state.lid_close_screen_off_enabled = enabled;
          last_user_input_tick = SDL_GetTicks();
          screen_off_mode = ScreenOffMode::Awake;
          return true;
        },
        [&](int delta, SystemSettingsState &settings_state) {
          const int next_index = ClampAutoSleepIntervalIndex(settings_state.auto_sleep_interval_index + delta);
          if (next_index == settings_state.auto_sleep_interval_index) return false;
          config.Mutable().auto_sleep_interval_index = next_index;
          config.MarkDirty();
          config.Save();
          settings_state.auto_sleep_interval_index = next_index;
          last_user_input_tick = SDL_GetTicks();
          screen_off_mode = ScreenOffMode::Awake;
          return true;
        },
        [&](int delta, SystemSettingsState &settings_state) {
          const int language_count = std::max(1, SystemLanguageCount());
          int next_index = settings_state.system_language_index;
          if (delta > 0) {
            next_index = (next_index + 1) % language_count;
          } else if (delta < 0) {
            next_index = (next_index - 1 + language_count) % language_count;
          }
          if (next_index == settings_state.system_language_index) return false;
          config.Mutable().system_language = SystemLanguageConfigValue(next_index);
          config.MarkDirty();
          config.Save();
          settings_state.system_language_index = next_index;
          last_user_input_tick = SDL_GetTicks();
          return true;
        },
        [&]() {
          clear_runtime_cache_files();
          return true;
        },
        [&]() {
          clear_history_and_refresh_shelf();
          return true;
        },
    };
  };
  auto make_txt_settings_callbacks = [&]() {
    return TxtSettingsCallbacks{
        [&](TxtSettingsState &settings_state) {
          settings_state.background_color = ClampTxtColorIndex(config.Get().txt_background_color);
          settings_state.font_color = ClampTxtColorIndex(config.Get().txt_font_color);
          settings_state.font_size_level = ClampTxtFontSizeLevel(config.Get().txt_font_size_level);
          if (settings_state.selected_row == 0) settings_state.selected_option = settings_state.background_color;
          else if (settings_state.selected_row == 1) settings_state.selected_option = settings_state.font_color;
          else if (settings_state.selected_row == 2) settings_state.selected_option = 1;
          else settings_state.selected_option = 0;
        },
        [&](int color_index, TxtSettingsState &settings_state) {
          const int clamped = ClampTxtColorIndex(color_index);
          config.Mutable().txt_background_color = clamped;
          config.MarkDirty();
          config.Save();
          settings_state.background_color = clamped;
          settings_state.selected_option = clamped;
          apply_epub_flow_theme();
          return true;
        },
        [&](int color_index, TxtSettingsState &settings_state) {
          const int clamped = ClampTxtColorIndex(color_index);
          config.Mutable().txt_font_color = clamped;
          config.MarkDirty();
          config.Save();
          settings_state.font_color = clamped;
          settings_state.selected_option = clamped;
          apply_epub_flow_theme();
          return true;
        },
        [&](int delta, TxtSettingsState &settings_state) {
          const int next_level = ClampTxtFontSizeLevel(settings_state.font_size_level + delta);
          if (next_level == settings_state.font_size_level) return false;
          apply_txt_font_size_level(next_level);
          if (reader_mode == ReaderMode::Txt && reader_ui.Txt().open && !current_book.empty()) {
            reader = txt_reader_module.Progress();
            open_text_book(current_book);
          }
          settings_state.font_size_level = next_level;
          settings_state.selected_option = delta < 0 ? 0 : 1;
          return true;
        },
        [&]() {
          start_txt_transcode_job();
          return true;
        },
    };
  };
  auto make_settings_input_actions = [&](bool menu_toggle_request = false,
                                         std::function<void()> on_close_override = {}) {
    return SettingsRuntimeInputActions{
        menu_toggle_request,
        on_close_override ? std::move(on_close_override) : std::function<void()>([&]() {
          app_shell.Scenes().ReturnFromSettings();
        }),
        [&]() { app_shell.RequestQuit(); },
        clear_history_and_refresh_shelf,
        clear_runtime_cache_files,
        start_txt_transcode_job,
        [&](int selected_index) {
          if (selected_index < 0 || selected_index >= static_cast<int>(contributor_avatar_entries.size())) return;
          const std::string &selected_label = contributor_avatar_entries[selected_index].raw_label;
          if (config.Mutable().selected_contributor_avatar_label != selected_label) {
            config.Mutable().selected_contributor_avatar_label = selected_label;
            config.MarkDirty();
            config.Save();
          }
          avatar_badge.SelectIndex(selected_index);
        },
    };
  };
  auto make_key_calibration_callbacks = [&]() {
    return KeyCalibrationCallbacks{
        [&](KeyCalibrationState &state) {
          const bool saved =
              SaveKeyCalibrationMapping(app_config_paths.keymap_path.string(), device_model_token, input_profile, state);
          has_calibrated_keymap = HasCompletedKeyCalibration(app_config_paths.keymap_path.string());
          return saved;
        },
        [&]() { app_shell.RequestQuit(); },
    };
  };
  auto make_settings_render_services = [&](SDL_Renderer *target_renderer,
                                           const std::function<void()> &draw_volume_overlay) {
    return MakeMenuSceneRenderServices(MenuSceneRenderServiceCallbacks{
        [&, target_renderer](int x, int y, int w, int h, SDL_Color c, bool filled) {
          DrawRect(target_renderer ? target_renderer : renderer, x, y, w, h, c, filled);
        },
        get_texture_size,
        get_menu_text_texture,
        get_menu_title_text_texture,
        get_reader_text_texture,
        Utf8Ellipsize,
        draw_volume_overlay,
    });
  };
  auto make_menu_scene_render_context = [&](const NativeConfig &ui_cfg,
                                            const std::function<void()> &draw_volume_overlay) {
    return MenuSceneRenderContext{
        renderer,
        ui_assets,
        ui_cfg,
        input_profile,
        menu_state,
        kSidebarMaskMaxAlpha,
        txt_transcode_job,
        system_settings_state,
        txt_settings_state,
        contributor_avatar_entries,
        contributor_avatar_state,
        key_calibration_state,
        has_calibrated_keymap,
        version_update_state,
        online_source_state,
        make_menu_scene_layout_metrics(),
        make_settings_render_services(renderer, draw_volume_overlay),
        true,
    };
  };
  auto make_boot_scene_render_deps = [&]() {
    return BootSceneRenderDeps{
        renderer,
        SystemLanguageIndexFromConfigValue(config.Get().system_language),
        Layout().screen_w,
        Layout().screen_h,
        [&](int x, int y, int w, int h, SDL_Color c, bool filled) { DrawRect(renderer, x, y, w, h, c, filled); },
        get_text_texture,
    };
  };
  auto make_boot_scene_tick_deps = [&]() {
    return BootSceneTickDeps{
        books_roots,
        kBootCountBatchEntries,
        kBootScanBatchEntries,
        kBootCoverGenerateBatchEntries,
        kBootCoverPreloadBatchEntries,
        GetLowerExt,
        [&](const std::string &doc_path) {
          const std::string ext = GetLowerExt(doc_path);
          if (ext == ".pdf") return pdf_runtime.HasRealRenderer();
          if (ext == ".epub") return true;
          if (ext == ".zip" || ext == ".cbz") return zip_image_runtime.HasRealRenderer();
          return false;
        },
        has_manual_cover_exact_or_fuzzy,
        has_cached_doc_cover_on_disk,
        create_doc_first_page_cover_texture,
        [&](SDL_Texture *generated) {
          forget_texture_size(generated);
          SDL_DestroyTexture(generated);
        },
        [&]() {
          return build_boot_shelf_cover_preload_items();
        },
        preload_shelf_cover_texture,
        [&]() { return boot_scene.InstallPendingUpdateFromEnvironment(); },
        [&]() { boot_scene.RestartAfterInstalledUpdate(); },
        [&](size_t total_books, size_t cover_generate_count) {
          if (!pending_shelf_cover_preload_fingerprint.empty() &&
              (!shelf_cover_preload_manifest_loaded ||
               pending_shelf_cover_preload_fingerprint != saved_shelf_cover_preload_fingerprint)) {
            SaveShelfPreloadFingerprint(shelf_cover_preload_manifest_path,
                                        pending_shelf_cover_preload_fingerprint);
          }
          boot_scene.FinishScanAndEnterShelf(
              total_books,
              cover_generate_count,
              BootSceneFinishDeps{
                  BootSceneShelfResetDeps{
                      shelf_state,
                      kTitleMarqueePauseSec,
                  },
                  rebuild_shelf_items,
                  reset_shelf_cover_stream_preload,
                  [&]() { app_shell.Scenes().EnterShelf(); },
                  verbose_log,
              });
        },
    };
  };
  auto make_volume_overlay_render_deps = [&](uint32_t frame_now) {
    return VolumeOverlayRenderDeps{
        renderer,
        frame_now,
        app_ui.volume_display_until,
        app_ui.volume_display_percent,
        Layout().top_bar_y,
        Layout().top_bar_h,
        [](int value) { return ScalePx(value); },
        get_text_texture,
    };
  };
  auto make_status_bar_render_deps = [&]() {
    return StatusBarRenderDeps{
        renderer,
        &system_status.Snapshot(),
        input_profile,
        config.Get().theme,
        Layout().screen_w,
        Layout().top_bar_y,
        Layout().top_bar_h,
        avatar_badge.BadgeTexture(),
        [](int value) { return ScalePx(value); },
        [&](int x, int y, int w, int h, SDL_Color c, bool filled) { DrawRect(renderer, x, y, w, h, c, filled); },
        get_status_text_texture,
    };
  };
  auto make_rgds_bottom_render_deps = [&](uint32_t frame_now,
                                          const rgds::ReaderLayout &reader_layout,
                                          ReaderSceneRenderDeps *reader_render_deps,
                                          bool render_reader_scene_on_bottom) {
    return rgds::BottomRenderDeps{
        rgds_runtime,
        rgds_render_resources,
        rgds_interaction,
        state,
        frame_now,
        config.Get(),
        input_profile,
        menu_scene,
        menu_state,
        kSidebarMaskMaxAlpha,
        txt_transcode_job,
        system_settings_state,
        txt_settings_state,
        is_rgds_runtime ? nullptr : &contributor_avatar_entries,
        contributor_avatar_state,
        key_calibration_state,
        has_calibrated_keymap,
        version_update_state,
        online_source_state,
        make_menu_scene_layout_metrics(),
        reader_layout,
        &reader_scene,
        reader_render_deps,
        render_reader_scene_on_bottom,
        get_texture_size,
        Utf8Ellipsize,
    };
  };
  auto make_version_update_callbacks = [&]() {
    return VersionUpdateCallbacks{
        [&](VersionUpdateState &update_state) { BeginVersionUpdateDownload(update_state); },
    };
  };
  auto make_shelf_reader_launch_handler_deps = [&]() {
    return ShelfReaderLaunchHandlerDeps{
        renderer,
        [&]() { return Layout().screen_w; },
        [&]() { return Layout().screen_h; },
        reader_ui,
        &reader_manager,
        &pdf_runtime,
        &epub_runtime,
        &zip_image_runtime,
        [&]() { return current_reader_font_pt; },
        [&]() { return GetTxtBackgroundColor(config.Get().txt_background_color); },
        [&]() { return GetTxtFontColor(config.Get().txt_font_color); },
        open_text_book,
        close_text_reader,
        file_exists,
        item_real_path,
        get_compatible_progress,
        GetLowerExt,
        open_epub_text_book,
        [&](const std::string &path) { history_store.Add(path); },
        [&]() {
          InitializeRgdsReaderState(is_rgds_runtime, menu_state, rgds_interaction);
          app_shell.Scenes().EnterReader();
        },
        [&]() {
          if (is_rgds_runtime) {
            InitializeRgdsStartupState(is_rgds_runtime, menu_state, rgds_interaction, false);
          }
          app_shell.Scenes().EnterShelf();
        },
        [&]() { app_shell.StartSceneFlash(); },
        [&](const std::string &message) { show_transient_message(message); },
    };
  };
  auto open_shelf_local_item = [&](const BookItem &item) {
    auto open_local_item = MakeShelfReaderLaunchHandler(make_shelf_reader_launch_handler_deps());
    return open_local_item(item);
  };
  auto make_online_shelf_action_deps = [&]() {
    OnlineShelfControllerDeps deps = make_online_shelf_controller_deps();
    deps.show_message = [&](const std::string &message) { show_transient_message(message); };
    return deps;
  };
  auto make_online_shelf_open_deps = [&]() {
    OnlineShelfControllerDeps deps = make_online_shelf_action_deps();
    deps.open_local_item = open_shelf_local_item;
    return deps;
  };
  auto make_shelf_scene_input_services = [&]() {
    return ShelfSceneInputServices{
        focused_title_needs_marquee,
        clear_cover_cache,
        rebuild_shelf_items,
        [&](const std::string &path) { favorites_store.Add(path); },
        [&](const std::string &path) { favorites_store.Remove(path); },
        current_category,
        [&](const BookItem &item) {
          OnlineShelfControllerDeps deps = make_online_shelf_open_deps();
          return online_shelf_controller.OpenOrDownloadBook(item, deps);
        },
        [&](const BookItem &item) {
          OnlineShelfControllerDeps deps = make_online_shelf_action_deps();
          const bool ok = online_shelf_controller.MarkForLocal(item, deps);
          if (ok) rebuild_shelf_items();
          return ok;
        },
        [&](const BookItem &item) {
          OnlineShelfControllerDeps deps = make_online_shelf_action_deps();
          const bool ok = online_shelf_controller.UnmarkForLocal(item, deps);
          if (ok) rebuild_shelf_items();
          return ok;
        },
        [&]() -> int { return online_shelf_controller.NavItemCount(); },
    };
  };
  auto make_shelf_scene_input_context = [&](const NativeConfig &ui_cfg, float frame_dt) {
    return ShelfSceneInputContext{
        input,
        shelf_runtime,
        shelf_state,
        ShelfGridCols(),
        frame_dt,
        ui_cfg.animations,
        kPageSlideDurationSec,
        kTitleMarqueePauseSec,
        ScaleFloat(kTitleMarqueeSpeedPx),
        make_shelf_scene_input_services(),
    };
  };
  auto make_shelf_scene_render_services = [&]() {
    OnlineShelfControllerDeps online_status_deps = make_online_shelf_controller_deps();
    return MakeShelfSceneRenderServices(ShelfSceneRenderServiceCallbacks{
        [&](int x, int y, int w, int h, SDL_Color c, bool fill) { DrawRect(renderer, x, y, w, h, c, fill); },
        [&](SDL_Texture *tex, int &w, int &h) { get_texture_size(tex, w, h); },
        [&](const BookItem &item) { return get_cover_texture(item); },
        [&](const BookItem &item) { return get_cached_cover_texture(item); },
        [&](int page) { ensure_shelf_page_cover_textures(page); },
        get_text_texture,
        [&](const std::string &raw_name, int text_area_w, const std::function<int(const std::string &)> &measure) {
          return get_title_ellipsized(raw_name, text_area_w, measure);
        },
        [&](const BookItem &item) { return shelf_title_text(item); },
        [&](int index) -> std::string { return online_shelf_controller.NavLabelText(index); },
        [&]() -> int { return online_shelf_controller.NavItemCount(); },
        [&]() { return online_shelf_controller.IsActive(); },
        [&, online_status_deps](const BookItem &item) {
          return online_shelf_controller.RemoteCoverLoading(item, online_status_deps);
        },
        [&, online_status_deps](const BookItem &item) -> std::string {
          return online_shelf_controller.RemoteBookStatusText(item, online_status_deps);
        },
        [&, online_status_deps](const BookItem &item) -> float {
          return online_shelf_controller.RemoteBookStatusProgress(item, online_status_deps);
        },
        forget_texture_size,
    });
  };
  auto make_shelf_scene_render_context = [&](float frame_dt, bool animations_enabled) {
    return ShelfSceneRenderContext{
        renderer,
        ui_assets,
#ifdef HAVE_SDL2_TTF
        &ui_text_cache,
#else
        nullptr,
#endif
        shelf_runtime,
        shelf_state,
        shelf_render_cache,
        MakeShelfSceneLayoutMetrics(Layout()),
        ShelfGridCols(),
        ShelfItemsPerPage(),
        frame_dt,
        animations_enabled,
        renderer_supports_target_textures,
        shelf_content_version,
        static_cast<float>(kUnfocusedAlpha),
        static_cast<float>(FocusedCoverW()),
        static_cast<float>(FocusedCoverH()),
        kCoverAspect,
        kCardMoveLinearSpeedX,
        kCardMoveLinearSpeedY,
        kCardMoveTailRatio,
        kCardMoveTailMinMul,
        kCardScaleLinearSpeedW,
        kCardScaleLinearSpeedH,
        kCardScaleTailRatio,
        kCardScaleTailMinMul,
        make_shelf_scene_render_services(),
    };
  };
  auto make_menu_scene_input_services = [&](bool menu_toggle_request = false,
                                            std::function<void()> on_close_override = {}) {
    return MakeMenuSceneInputServices(
        make_system_settings_callbacks(),
        make_txt_settings_callbacks(),
        make_key_calibration_callbacks(),
        make_version_update_callbacks(),
        make_settings_input_actions(menu_toggle_request, std::move(on_close_override)));
  };
  auto make_online_shelf_input_tick_handlers = [&]() {
    return OnlineShelfInputTickHandlers{
        [&]() { shelf_scene.ResetToCategoryRoot(shelf_state); },
        [&]() { clear_cover_cache(); },
        [&]() { rebuild_shelf_items(); },
        [&]() { reset_shelf_cover_stream_preload(); },
        [&]() { app_shell.Scenes().EnterShelf(); },
    };
  };
  auto make_online_shelf_present_tick_handlers = [&]() {
    return OnlineShelfPresentTickHandlers{
        [&]() { clear_cover_cache(); },
        [&]() { rebuild_shelf_items(); },
        [&]() { reset_shelf_cover_stream_preload(); },
    };
  };
  auto make_online_shelf_deferred_disconnect_handlers = [&]() {
    return OnlineShelfDeferredDisconnectHandlers{
        [&]() { shelf_state.nav_selected_index = 0; },
        [&]() { shelf_scene.ResetToCategoryRoot(shelf_state); },
        [&]() { clear_cover_cache(); },
        [&]() { rebuild_shelf_items(); },
        [&]() { reset_shelf_cover_stream_preload(); },
        [&]() { app_shell.Scenes().EnterShelf(); },
    };
  };

  uint32_t prev_ticks = SDL_GetTicks();
  uint32_t force_quit_chord_started_at = 0;
  while (app_shell.IsRunning()) {
    const AppFrameTiming frame = app_shell.BeginFrame(prev_ticks);
    const uint32_t frame_begin_ticks = frame.frame_begin_ticks;
    const uint32_t now = frame.now;
    const float dt = frame.dt;

    hold_cooldown = std::max(0.0f, hold_cooldown - dt);
    menu_scene.Tick(menu_state, dt);
    TickAppUiState(app_ui, dt);
    const VersionUpdateTickResult version_update_tick = TickVersionUpdateState(version_update_state, dt);

    input.BeginFrame(dt);
    const bool animate_enabled = config.Get().animations;
    const bool contributor_marquee_active =
        state == AppScene::Settings &&
        menu_scene.IsSelected(menu_state, SettingId::ContributorAvatars) &&
        !contributor_avatar_entries.empty();
    const bool version_update_download_active =
        state == AppScene::Settings &&
        menu_scene.IsSelected(menu_state, SettingId::VersionUpdate) &&
        version_update_state.download_in_progress;
    const bool online_book_download_active = online_shelf_controller.IsDownloadActive();
    const bool has_active_animation =
        state == AppScene::Boot || input.AnyPressed() ||
        txt_transcode_job.active ||
        online_book_download_active ||
        (reader_mode == ReaderMode::Txt && reader_ui.Txt().open && reader_ui.Txt().loading) ||
        version_update_download_active ||
        contributor_marquee_active ||
        (animate_enabled &&
         (menu_scene.IsAnimating(menu_state) || app_shell.IsSceneFlashAnimating() || shelf_state.page_animating ||
          shelf_state.any_grid_animating));
    const bool needs_periodic_tick =
        (state == AppScene::Shelf && shelf_state.title_marquee_active) ||
        (state == AppScene::Reader && reader_scene.IsRenderPending(reader_ui, &reader_manager)) ||
        (state == AppScene::Settings && menu_scene.IsSelected(menu_state, SettingId::VersionUpdate) &&
         version_update_tick.state_changed);
    const uint32_t loop_now = SDL_GetTicks();
    const bool has_pending_flush =
        config.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        progress.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        favorites_store.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        history_store.ShouldFlush(loop_now, kDeferredSaveDelayMs);
    const int idle_wait_ms = (!has_active_animation && has_pending_flush && !needs_periodic_tick) ? static_cast<int>(kIdleFlushOnlyWaitMs) : kIdleWaitMs;
    auto is_user_input_event = [](const SDL_Event &event) {
      switch (event.type) {
      case SDL_KEYDOWN:
        return event.key.repeat == 0;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_JOYBUTTONDOWN:
        return true;
      case SDL_JOYHATMOTION:
        return event.jhat.value != SDL_HAT_CENTERED;
      default:
        return false;
      }
    };
    auto note_user_input = [&](const SDL_Event &event) {
      if (!is_user_input_event(event)) return;
      if (screen_off_mode != ScreenOffMode::Awake) return;
      last_user_input_tick = SDL_GetTicks();
    };
    auto maybe_trigger_auto_sleep = [&]() {
      const bool supports_auto_sleep =
          use_h700_defaults || input_profile == InputProfile::GKD350HUltra;
      if (screen_off_mode != ScreenOffMode::Awake || !supports_auto_sleep || !config.Get().lid_close_screen_off) return;
      const uint32_t now = SDL_GetTicks();
      const uint32_t idle_ms = AutoSleepIntervalMsFromIndex(system_settings_state.auto_sleep_interval_index);
      if (now - last_user_input_tick < idle_ms) return;
      const bool auto_sleep_triggered =
          input_profile == InputProfile::GKD350HUltra
              ? lid_power_controller.TriggerPowerKeyScreenOff(input_profile)
              : lid_power_controller.TriggerAutoIfEnabled();
      if (auto_sleep_triggered) {
        if (input_profile == InputProfile::RGDS) {
          rgds_display_sleep_active = true;
          last_user_input_tick = now;
          runtime_log::Line("main: RGDS auto sleep activated");
        } else {
          screen_off_mode = ScreenOffMode::Auto;
          if (input_profile == InputProfile::GKD350HUltra) {
            runtime_log::Line("main: GKD350HUltra auto sleep activated");
          }
        }
      }
    };
    const AppEventPumpResult event_pump =
        app_shell.PumpEvents(input, has_active_animation, idle_wait_ms, note_user_input);
    bool input_end_frame_done = false;
    bool observed_input_this_frame = false;
    if (has_active_animation) {
      maybe_trigger_auto_sleep();
    } else {
      if (event_pump.had_event) {
        maybe_trigger_auto_sleep();
      } else {
        maybe_trigger_auto_sleep();
        system_status.Poll(SDL_GetTicks());
        if (has_pending_flush && !needs_periodic_tick) {
        // Wake only to flush deferred IO; keep the current frame untouched.
          observed_input_this_frame = input.EndFrame();
          if (observed_input_this_frame) {
            if (screen_off_mode == ScreenOffMode::Awake) last_user_input_tick = SDL_GetTicks();
            input_end_frame_done = true;
          }
          if (!observed_input_this_frame && screen_off_mode == ScreenOffMode::Awake) {
            flush_deferred_writes(false);
            app_shell.ResetFrameClock(prev_ticks);
            continue;
          }
        }
        if (!needs_periodic_tick && !has_pending_flush) {
        // Fully idle: no input, no animation, no incremental loading.
        // Skip update/render work and keep sleeping until something changes.
          observed_input_this_frame = input.EndFrame();
          if (observed_input_this_frame) {
            if (screen_off_mode == ScreenOffMode::Awake) last_user_input_tick = SDL_GetTicks();
            input_end_frame_done = true;
          }
          if (!observed_input_this_frame && screen_off_mode == ScreenOffMode::Awake) {
            app_shell.ResetFrameClock(prev_ticks);
            continue;
          }
        }
      }
    }
    if (!input_end_frame_done) {
      observed_input_this_frame = input.EndFrame();
      if (observed_input_this_frame && screen_off_mode == ScreenOffMode::Awake) last_user_input_tick = SDL_GetTicks();
    }

    const uint32_t power_now = SDL_GetTicks();
    const bool key_calibration_capturing_before_power =
        state == AppScene::Settings &&
        menu_scene.IsSelected(menu_state, SettingId::KeyCalibration) &&
        key_calibration_state.phase == KeyCalibrationPhase::Capturing;
    if (input_wake_next_resync_tick != 0 && observed_input_this_frame) {
      input_wake_next_resync_tick = 0;
    } else if (input_wake_next_resync_tick != 0 &&
               SDL_TICKS_PASSED(power_now, input_wake_next_resync_tick)) {
      runtime_log::Line("main: deferred H700 input resync after wake");
      ReopenAppInputDevices(input_devices, verbose_log);
      input.RefreshDevices();
      input.SuppressPowerUntilRelease();
      input_wake_next_resync_tick = 0;
    }
    const bool power_input_allowed = SDL_TICKS_PASSED(power_now, power_key_ignore_until_tick);

    if (is_rgds_runtime && rgds_display_sleep_active) {
      if (power_input_allowed && input.IsJustPressed(Button::Power)) {
        runtime_log::Line("main: RGDS wake requested");
        const bool wake_ok = lid_power_controller.TriggerScreenOn(input_profile);
        rgds_display_sleep_active = false;
        last_user_input_tick = SDL_GetTicks();
        power_key_ignore_until_tick = last_user_input_tick + 450;
        resync_input_after_screen_wake();
        if (wake_ok) {
          runtime_log::Line("main: RGDS wake completed");
        } else {
          runtime_log::Line("main: RGDS wake failed");
        }
      }
      input.ResetAll();
      app_shell.ResetFrameClock(prev_ticks);
      continue;
    }

    if (screen_off_mode != ScreenOffMode::Awake) {
      if (input_profile == InputProfile::GKD350HUltra &&
          gkd_power_wake_due_tick != 0 &&
          SDL_TICKS_PASSED(power_now, gkd_power_wake_due_tick)) {
        gkd_power_wake_due_tick = 0;
        const bool wake_ok = lid_power_controller.TriggerScreenOn(input_profile);
        runtime_log::Line(std::string("main: GKD350HUltra deferred power-key wake ") +
                          (wake_ok ? "completed" : "failed"));
        if (wake_ok) {
          screen_off_mode = ScreenOffMode::Awake;
          last_user_input_tick = SDL_GetTicks();
          power_key_ignore_until_tick = last_user_input_tick + 900;
          resync_input_after_screen_wake();
        }
        input.ResetAll();
        app_shell.ResetFrameClock(prev_ticks);
        continue;
      }

      const bool h700_hardware_wake_seen =
          (input_profile == InputProfile::H700Default ||
           input_profile == InputProfile::H70034xxSp ||
           input_profile == InputProfile::H70035xxH) &&
          any_non_power_button_just_pressed();
      const bool gkd_auto_sleep_wake_seen =
          input_profile == InputProfile::GKD350HUltra &&
          screen_off_mode == ScreenOffMode::Auto &&
          any_non_power_button_just_pressed();
      const bool power_wake_seen =
          power_input_allowed && input.IsJustPressed(Button::Power);
      if (input_profile == InputProfile::GKD350HUltra && power_wake_seen) {
        // ROCKNIX also handles this physical key. Wait until its handler has
        // observed the original display state, then force the compositor
        // output on. Doing this immediately races ROCKNIX and turns the panel
        // straight back off; doing nothing leaves the Sway output disabled.
        gkd_power_wake_due_tick = power_now + 650;
        power_key_ignore_until_tick = power_now + 1200;
        input.SuppressPowerUntilRelease();
        runtime_log::Line("main: GKD350HUltra deferred power-key wake scheduled");
        input.ResetAll();
        app_shell.ResetFrameClock(prev_ticks);
        continue;
      }
      if (power_wake_seen ||
          h700_hardware_wake_seen || gkd_auto_sleep_wake_seen) {
        const bool wake_ok = lid_power_controller.TriggerScreenOn(input_profile);
        if (input_profile == InputProfile::GKD350HUltra) {
          runtime_log::Line(std::string("main: GKD350HUltra wake ") +
                            (wake_ok ? "completed" : "failed"));
          gkd_power_wake_due_tick = 0;
          if (!wake_ok) {
            input.ResetAll();
            app_shell.ResetFrameClock(prev_ticks);
            continue;
          }
        }
        screen_off_mode = ScreenOffMode::Awake;
        last_user_input_tick = SDL_GetTicks();
        power_key_ignore_until_tick = last_user_input_tick + 450;
        resync_input_after_screen_wake();
      }
      input.ResetAll();
      app_shell.ResetFrameClock(prev_ticks);
      continue;
    }

    if (!key_calibration_capturing_before_power && power_input_allowed && input.IsJustPressed(Button::Power)) {
      if (lid_power_controller.TriggerPowerKeyScreenOff(input_profile)) {
        if (input_profile == InputProfile::RGDS) {
          rgds_display_sleep_active = true;
          runtime_log::Line("main: RGDS manual sleep activated");
        } else {
          screen_off_mode = ScreenOffMode::Manual;
          if (input_profile == InputProfile::GKD350HUltra) {
            gkd_power_wake_due_tick = 0;
          }
        }
        last_user_input_tick = SDL_GetTicks();
        power_key_ignore_until_tick = last_user_input_tick + 450;
        input.SuppressPowerUntilRelease();
        input.ResetAll();
        app_shell.ResetFrameClock(prev_ticks);
        continue;
      }
    }

    const bool key_calibration_capturing =
        state == AppScene::Settings &&
        menu_scene.IsSelected(menu_state, SettingId::KeyCalibration) &&
        key_calibration_state.phase == KeyCalibrationPhase::Capturing;

    system_status.Poll(now);

    const OnlineShelfControllerTickResult online_tick = online_shelf_controller.TickAfterInput(shelf_runtime);
    if (online_tick.download_finished) {
      show_transient_message(online_tick.download_success ? u8"\u4e0b\u8f7d\u6210\u529f"
                                                          : u8"\u4e0b\u8f7d\u5931\u8d25");
    }
    if (is_rgds_runtime && online_tick.refresh_roots_after_disconnect) {
      HandleOnlineShelfDeferredDisconnect(online_shelf_controller,
                                          books_roots,
                                          cover_roots,
                                          make_online_shelf_deferred_disconnect_handlers());
    }

    if (reader_mode == ReaderMode::Txt && reader_ui.Txt().open && reader_ui.Txt().loading) {
      const bool txt_scroll_input_active =
          state == AppScene::Reader &&
          (input.IsPressed(Button::Up) || input.IsPressed(Button::Down) ||
           input.IsPressed(Button::Left) || input.IsPressed(Button::Right));
      txt_session_facade.TickLoading(current_book,
                                     txt_scroll_input_active ? 1 : 5,
                                     txt_scroll_input_active ? 8192 : 24576);
    }
    process_txt_transcode_step();
    flush_deferred_writes(false);
    if (!is_rgds_runtime && state == AppScene::Settings && volume_controller.UsesSystemVolume() &&
        now - last_system_volume_sync >= 250) {
      int synced_volume_percent = app_ui.volume_display_percent;
      if (volume_controller.RefreshPercent(synced_volume_percent)) {
        app_ui.volume_display_percent = synced_volume_percent;
        config.Mutable().system_volume_percent = synced_volume_percent;
        if (input_profile == InputProfile::GKD350HUltra) {
          const int max_level = volume_controller.MaxLevel();
          system_settings_state.levels.volume.available = true;
          system_settings_state.levels.volume.max_level = max_level;
          system_settings_state.levels.volume.level =
              std::clamp((synced_volume_percent * max_level + 50) / 100, 0, max_level);
        }
      }
      if (input_profile != InputProfile::GKD350HUltra &&
          system_control_service.RefreshVolumeOnly(system_settings_state.levels.volume) &&
          system_settings_state.levels.volume.available) {
        const int menu_volume_percent = std::clamp(
            (system_settings_state.levels.volume.level * 100) /
                std::max(1, system_settings_state.levels.volume.max_level),
            0, 100);
        app_ui.volume_display_percent = menu_volume_percent;
        config.Mutable().system_volume_percent = menu_volume_percent;
      }
      last_system_volume_sync = now;
    }
    if (!key_calibration_capturing) {
      HandleVolumeControls(
          app_ui, input, now, volume_controller, config,
          [&](int volume) {
            runtime_sfx_volume = std::clamp(volume, 0, SDL_MIX_MAXVOLUME);
            sfx.SetVolume(runtime_sfx_volume);
          },
          [&]() { play_sfx(SfxId::Change); },
          [&](uint32_t due) {
            if (!pending_volume_change_sfx) {
              pending_volume_change_sfx = true;
              pending_volume_change_sfx_due = due;
            }
          });
    }
    if (pending_volume_change_sfx && SDL_TICKS_PASSED(now, pending_volume_change_sfx_due)) {
      pending_volume_change_sfx = false;
      play_sfx(SfxId::Change);
    }

    bool transient_message_dismissed_this_frame = false;
    if (transient_message_dismiss_on_input && !transient_message.empty() &&
        SDL_TICKS_PASSED(now, transient_message_shown_at + 1) && any_button_just_pressed()) {
      transient_message.clear();
      transient_message_until = 0;
      transient_message_dismiss_on_input = false;
      transient_message_dismissed_this_frame = true;
    }

    if (!key_calibration_capturing &&
        (state == AppScene::Shelf || (!is_rgds_runtime && state == AppScene::Settings))) {
      if (input.IsJustPressed(Button::Up) || input.IsJustPressed(Button::Down) ||
          input.IsJustPressed(Button::Left) || input.IsJustPressed(Button::Right)) {
        play_sfx(SfxId::Move);
      } else if (input.IsJustPressed(Button::B) || input.IsJustPressed(Button::Y)) {
        play_sfx(SfxId::Back);
      } else if (input.IsJustPressed(Button::A) || input.IsJustPressed(Button::X)) {
        play_sfx(SfxId::Select);
      } else if (input.IsJustPressed(Button::L1) || input.IsJustPressed(Button::L2) ||
                 input.IsJustPressed(Button::R1) || input.IsJustPressed(Button::R2)) {
        play_sfx(SfxId::Change);
      }
    }

    rgds::InteractionResult rgds_input_result;
    if (is_rgds_runtime && !key_calibration_capturing) {
      rgds_input_result = rgds::HandleFrameInput(rgds_interaction, input, state, now);
      if (rgds_input_result.play_change_sfx) play_sfx(SfxId::Change);
      if (rgds_input_result.play_back_sfx) play_sfx(SfxId::Back);
    }
    if (!key_calibration_capturing && input.IsPressed(Button::Start) && input.IsPressed(Button::Select)) {
      if (force_quit_chord_started_at == 0) force_quit_chord_started_at = now;
      if (now - force_quit_chord_started_at >= 1500) {
        runtime_log::Line(std::string("main: quit requested by held Start+Select chord profile=") +
                          InputProfileName(input_profile));
        app_shell.RequestQuit();
      }
    } else {
      force_quit_chord_started_at = 0;
    }

    if (is_rgds_runtime) {
      if (rgds_input_result.menu_key_consumed && state == AppScene::Reader && rgds_interaction.menu_open) {
        SnapRgdsMenuOpenState(menu_state, true);
      }
      if (state == AppScene::Settings) {
        app_shell.Scenes().Set(app_shell.Scenes().SettingsReturnScene());
      }
    } else {
      const MenuToggleAction menu_toggle_action =
          key_calibration_capturing
              ? MenuToggleAction::None
              : HandleMenuToggleInput(app_ui, input, state == AppScene::Settings, state == AppScene::Shelf,
                                      state == AppScene::Reader, menu_scene.CanCloseWithToggle(menu_state), 0.0f,
                                      false,
                                      kMenuToggleDebounceSec, input_profile);
      if (menu_toggle_action == MenuToggleAction::CloseSettings) {
        runtime_log::Line(std::string("main: menu toggle close profile=") + InputProfileName(input_profile));
        menu_scene.BeginClose(
            menu_state,
            MenuSceneAnimationConfig{config.Get().animations, 0.16f, 0.20f, kSettingsToggleGuardSec});
        play_sfx(SfxId::Back);
      } else if (menu_toggle_action == MenuToggleAction::OpenFromShelf ||
                 menu_toggle_action == MenuToggleAction::OpenFromReader) {
        runtime_log::Line(std::string("main: menu toggle open from=") +
                          (menu_toggle_action == MenuToggleAction::OpenFromShelf ? "shelf" : "reader") +
                          " profile=" + InputProfileName(input_profile));
        app_shell.Scenes().OpenSettingsFrom(
            (menu_toggle_action == MenuToggleAction::OpenFromShelf) ? AppScene::Shelf : AppScene::Reader);
        runtime_log::Line("main: menu toggle scene opened");
        const bool menu_open_animations =
            config.Get().animations && input_profile != InputProfile::GKD350HUltra;
        menu_scene.BeginOpen(
            menu_state,
            MenuSceneAnimationConfig{menu_open_animations, 0.16f, 0.20f, kSettingsToggleGuardSec});
        runtime_log::Line("main: menu toggle animation opened");
        runtime_log::Line("main: menu toggle sfx begin");
        play_sfx(SfxId::Back);
        runtime_log::Line("main: menu toggle sfx done");
      }
    }

    if (state == AppScene::Boot) {
      boot_scene.Tick(dt, make_boot_scene_tick_deps());
    } else if (state == AppScene::Shelf) {
      const NativeConfig &ui_cfg = config.Get();
      if (!is_rgds_runtime || rgds::RoutesInputToTop(rgds_interaction)) {
        shelf_scene.HandleInput(make_shelf_scene_input_context(ui_cfg, dt));
      } else {
        PrepareMenuSceneInputState(system_settings_state,
                                   menu_scene.IsSelected(menu_state, SettingId::SystemControls),
                                   system_control_service,
                                   contributor_avatar_state,
                                   contributor_avatar_entries.size());
        MenuSceneInputContext menu_input_context = make_menu_scene_input_context(
            ui_cfg,
            dt,
            menu_state,
            make_menu_scene_input_services(false, [&]() {
              rgds_interaction.focus_top = true;
              SnapRgdsMenuOpenState(menu_state, true);
            }));
        if (!rgds_input_result.select_key_consumed) menu_scene.HandleInput(menu_input_context);
      }
      ensure_shelf_page_cover_textures(shelf_state.shelf_page);
      queue_visible_shelf_cover_lookahead();
    } else if (state == AppScene::Settings) {
      const NativeConfig &ui_cfg = config.Get();
      PrepareMenuSceneInputState(system_settings_state,
                                 menu_scene.IsSelected(menu_state, SettingId::SystemControls),
                                 system_control_service,
                                 contributor_avatar_state,
                                  contributor_avatar_entries.size());
      MenuSceneInputContext menu_input_context =
          make_menu_scene_input_context(ui_cfg, dt, menu_state, make_menu_scene_input_services());
      menu_scene.HandleInput(menu_input_context);
      const OnlineShelfControllerTickResult online_after_input = online_shelf_controller.TickAfterInput(shelf_runtime);
      if (online_after_input.refresh_roots_after_disconnect) {
        HandleOnlineShelfDeferredDisconnect(online_shelf_controller,
                                            books_roots,
                                            cover_roots,
                                            make_online_shelf_deferred_disconnect_handlers());
      }
      ApplyOnlineShelfInputTickResult(online_after_input, make_online_shelf_input_tick_handlers());
      if (is_rgds_runtime) {
        app_shell.Scenes().Set(AppScene::Shelf);
      }
    } else if (state == AppScene::Reader) {
      if (is_rgds_runtime && rgds::RoutesInputToMenu(rgds_interaction, state)) {
        const NativeConfig &ui_cfg = config.Get();
        PrepareMenuSceneInputState(system_settings_state,
                                   menu_scene.IsSelected(menu_state, SettingId::SystemControls),
                                   system_control_service,
                                   contributor_avatar_state,
                                   contributor_avatar_entries.size());
        MenuSceneInputContext menu_input_context = make_menu_scene_input_context(
            ui_cfg,
            dt,
            menu_state,
            make_menu_scene_input_services(input.IsJustPressed(Button::Menu) && !rgds_input_result.menu_key_consumed,
                                           [&]() {
                                             rgds_interaction.menu_open = false;
                                             rgds_interaction.focus_top = rgds_interaction.focus_before_menu_top;
                                           }));
        if (!rgds_input_result.menu_key_consumed && !rgds_input_result.select_key_consumed) {
          menu_scene.HandleInput(menu_input_context);
        }
      } else {
        reader_scene.HandleInput(make_reader_scene_input_deps(dt, transient_message_dismissed_this_frame));
      }
      if (is_rgds_runtime && state == AppScene::Settings) {
        app_shell.Scenes().Set(AppScene::Reader);
      }
    }

    shelf_state.any_grid_animating = false;
    if (animate_enabled) {
      app_shell.TickSceneFlash(dt, true);
      shelf_state.page_slide.Update(dt);
      if (shelf_state.page_animating && !shelf_state.page_slide.IsAnimating() &&
          shelf_state.page_slide.Value() >= 0.999f) {
        shelf_state.page_animating = false;
        shelf_state.page_slide.Snap(0.0f);
      }
    } else {
      shelf_state.page_animating = false;
      shelf_state.page_slide.Snap(0.0f);
      app_shell.TickSceneFlash(dt, false);
      if (!is_rgds_runtime && state != AppScene::Settings) menu_scene.SnapClosed(menu_state);
    }

    // Draw
    app_shell.BeginDraw();
    app_shell.BeginTopDraw();

    if (state == AppScene::Boot) {
      boot_scene.Draw(make_boot_scene_render_deps());
    } else {
      const NativeConfig &cfg = config.Get();
      boot_runtime.language_index = SystemLanguageIndexFromConfigValue(cfg.system_language);
      const SDL_Color bg = (cfg.theme == 0) ? SDL_Color{22, 23, 29, 255} : SDL_Color{238, 237, 233, 255};
      DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, bg);

      std::function<void()> draw_volume_overlay = []() {};
      std::function<void()> draw_system_status_overlay = []() {};
      if (state == AppScene::Shelf || (!is_rgds_runtime && state == AppScene::Settings)) {
        draw_volume_overlay = [&]() { DrawVolumeOverlay(make_volume_overlay_render_deps(now)); };
        draw_system_status_overlay = [&]() { DrawStatusBarRuntime(make_status_bar_render_deps()); };
        shelf_scene.Draw(make_shelf_scene_render_context(dt, animate_enabled));
        draw_system_status_overlay();

        if (state != AppScene::Settings) {
          draw_volume_overlay();
        }
      }

      if (state == AppScene::Reader) {
        if (is_rgds_runtime && rgds_runtime.reader_canvas) {
          IReaderModule *rgds_active_reader_module = reader_manager.Module(reader_mode);
          if (rgds_active_reader_module && rgds_active_reader_module->IsOpen()) {
            const ReaderProgress pre_tick_progress = rgds_active_reader_module->Progress();
            const rgds::ReaderLayout pre_tick_layout =
                rgds::ResolveReaderLayout(reader_mode, rgds_active_reader_module, pre_tick_progress.rotation);
            rgds_active_reader_module->UpdateViewport(pre_tick_layout.mode == rgds::ReaderLayoutMode::HorizontalSpread
                                                          ? rgds::kScreenW
                                                          : pre_tick_layout.canvas_w,
                                                      pre_tick_layout.mode == rgds::ReaderLayoutMode::HorizontalSpread
                                                          ? rgds::kScreenH
                                                          : pre_tick_layout.canvas_h);
            rgds_active_reader_module->Tick(dt);
          }
          const ReaderProgress rgds_active_progress =
              rgds_active_reader_module && rgds_active_reader_module->IsOpen()
                  ? rgds_active_reader_module->Progress()
                  : ReaderProgress{};
          const rgds::ReaderLayout rgds_reader_layout =
              rgds::ResolveReaderLayout(reader_mode, rgds_active_reader_module, rgds_active_progress.rotation);
          rgds::DrawTopReaderSlice(rgds_runtime, renderer, reader_scene,
                                   make_rgds_reader_render_deps(renderer, dt, false, rgds_reader_layout),
                                   rgds_reader_layout);
        } else {
          reader_scene.Draw(make_reader_render_deps(renderer, dt));
        }
      }

      if (!is_rgds_runtime && state == AppScene::Settings) {
        menu_scene.Draw(make_menu_scene_render_context(cfg, draw_volume_overlay));
        draw_system_status_overlay();
        if (online_shelf_controller.HasDeferredConnect()) {
          app_shell.Present();
          HandleOnlineShelfDeferredConnect(online_shelf_controller,
                                           shelf_runtime,
                                           make_online_shelf_input_tick_handlers());
          continue;
        }
      }

#ifdef HAVE_SDL2_TTF
      if (!transient_message.empty() && now < transient_message_until) {
        const SDL_Color text_color{245, 245, 245, 255};
        if (TextCacheEntry *te = get_text_texture(transient_message, text_color); te && te->texture) {
          const int pad_x = ScalePx(24);
          const int pad_y = ScalePx(12);
          const int box_w = te->w + pad_x * 2;
          const int box_h = te->h + pad_y * 2;
          const int box_x = std::max(0, (Layout().screen_w - box_w) / 2);
          const int box_y = std::max(0, Layout().screen_h - box_h - ScalePx(72));
          DrawRect(renderer, box_x, box_y, box_w, box_h, SDL_Color{0, 0, 0, 210});
          DrawRect(renderer, box_x, box_y, box_w, box_h, SDL_Color{255, 255, 255, 40}, false);
          SDL_Rect td{box_x + pad_x, box_y + pad_y, te->w, te->h};
          SDL_RenderCopy(renderer, te->texture, nullptr, &td);
        }
      } else if (now >= transient_message_until) {
        transient_message.clear();
      }
#endif
    }

    app_shell.DrawSceneFlash();
    if (is_rgds_runtime) {
      rgds::DrawFocusFlash(renderer, now, rgds_interaction, true);
    }
    IReaderModule *rgds_bottom_reader_module = reader_manager.Module(reader_mode);
    const ReaderProgress rgds_bottom_progress =
        rgds_bottom_reader_module && rgds_bottom_reader_module->IsOpen()
            ? rgds_bottom_reader_module->Progress()
            : ReaderProgress{};
    const rgds::ReaderLayout rgds_bottom_reader_layout =
        rgds::ResolveReaderLayout(reader_mode, rgds_bottom_reader_module, rgds_bottom_progress.rotation);
    const bool rgds_bottom_direct_reader_render =
        is_rgds_runtime && state == AppScene::Reader &&
        rgds_bottom_reader_module && rgds_bottom_reader_module->IsOpen() &&
        !rgds::IsImageReaderMode(reader_mode, rgds_bottom_reader_module) &&
        !rgds_runtime.spanning && !rgds_runtime.stacked_preview;
    ReaderSceneRenderDeps rgds_bottom_reader_render_deps =
        make_rgds_reader_render_deps(rgds_runtime.bottom_renderer ? rgds_runtime.bottom_renderer : renderer,
                                     dt,
                                     false,
                                     rgds_bottom_reader_layout);
    rgds::DrawBottomScreen(make_rgds_bottom_render_deps(now,
                                                        rgds_bottom_reader_layout,
                                                        &rgds_bottom_reader_render_deps,
                                                        rgds_bottom_direct_reader_render));
    app_shell.Present();
    if (is_rgds_runtime && online_shelf_controller.HasDeferredConnect()) {
      HandleOnlineShelfDeferredConnect(online_shelf_controller,
                                       shelf_runtime,
                                       make_online_shelf_input_tick_handlers());
      continue;
    }
    if (state == AppScene::Shelf) {
      const OnlineShelfControllerTickResult online_after_present =
          online_shelf_controller.TickAfterPresent(shelf_state.nav_selected_index, shelf_state.focus_index,
                                                   shelf_state.shelf_page, ShelfGridCols());
      ApplyOnlineShelfPresentTickResult(online_after_present, make_online_shelf_present_tick_handlers());
    }
    process_shelf_cover_stream_preload();

    app_shell.ThrottleFrame(frame_begin_ticks, contributor_marquee_active, has_active_animation, needs_periodic_tick);
  }

  if (!current_book.empty()) {
    if (reader_mode == ReaderMode::Pdf && pdf_runtime.IsOpen()) {
      const PdfRuntimeProgress active_pdf = pdf_runtime.Progress();
      reader.page = active_pdf.page;
      reader.scroll_x = active_pdf.scroll_x;
      reader.scroll_y = active_pdf.scroll_y;
      reader.zoom = active_pdf.zoom;
      reader.rotation = active_pdf.rotation;
    } else if (reader_mode == ReaderMode::Epub && reader_manager.Module(ReaderMode::Epub)->IsOpen()) {
      const IReaderModule *epub_module = reader_manager.Module(ReaderMode::Epub);
      const ReaderProgress active_epub = epub_module->Progress();
      reader.page = active_epub.page;
      reader.scroll_x = std::string(epub_module->BackendName()) == "epub-flow" ? 0 : active_epub.scroll_x;
      reader.scroll_y = active_epub.scroll_y;
      reader.zoom = active_epub.zoom;
      reader.rotation = active_epub.rotation;
    } else if (reader_mode == ReaderMode::ZipImage && zip_image_runtime.IsOpen()) {
      const ZipImageRuntimeProgress active_zip = zip_image_runtime.Progress();
      reader.page = active_zip.page;
      reader.scroll_x = active_zip.scroll_x;
      reader.scroll_y = active_zip.scroll_y;
      reader.zoom = active_zip.zoom;
      reader.rotation = active_zip.rotation;
    } else if (reader_mode == ReaderMode::Txt && reader_ui.Txt().open) {
      reader = txt_reader_module.Progress();
      reader_ui.Txt().resume_cache_dirty = true;
      persist_current_txt_resume_snapshot(current_book, true);
    } else if (state != AppScene::Reader) {
      // Not actively reading anymore.
      current_book.clear();
    }
    if (!current_book.empty()) {
      ReaderProgress save_reader = reader;
      progress.Set(current_book, save_reader);
      history_store.Add(current_book);
    }
  }
  flush_deferred_writes(true);
  ShutdownVersionUpdateState(version_update_state);
  clear_cover_cache();
  avatar_badge.Shutdown();
  DestroyContributorAvatarEntries(contributor_avatar_entries, forget_texture_size);
  rgds::DestroyRenderResources(rgds_render_resources, forget_texture_size);
  DestroyUiAssets(ui_assets, forget_texture_size);
#ifdef HAVE_SDL2_TTF
  ShutdownUiTextCache(ui_text_cache, forget_texture_size);
  ShutdownUiTextCache(status_text_cache, forget_texture_size);
  ShutdownUiTextCache(menu_text_cache, forget_texture_size);
#endif
  destroy_shelf_render_cache();
  pdf_runtime.Close();
  epub_runtime.Close();
  zip_image_runtime.Close();
  CloseAppInputDevices(input_devices);
  sfx.Shutdown();
  rgds::Destroy(rgds_runtime);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
#ifdef HAVE_SDL2_IMAGE
  IMG_Quit();
#endif
#ifdef HAVE_SDL2_TTF
  TTF_Quit();
#endif
  SDL_Quit();
  return 0;
}

