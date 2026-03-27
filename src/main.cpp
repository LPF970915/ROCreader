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
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <codecvt>
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

#include "book_scanner.h"
#include "cover_resolver.h"
#include "epub_comic_reader.h"
#include "epub_reader.h"
#include "pdf_reader.h"
#include "pdf_runtime.h"
#include "storage_paths.h"
#include "animation.h"

namespace {
struct LayoutMetrics {
  int screen_w = 720;
  int screen_h = 480;
  int safe_margin_x = 20;
  int top_bar_y = 0;
  int top_bar_h = 30;
  int nav_bar_y = 30;
  int nav_bar_h = 50;
  int main_grid_y = 80;
  int main_grid_h = 350;
  int bottom_bar_y = 430;
  int bottom_bar_h = 50;
  int cover_w = 140;
  int cover_h = 210;
  int card_frame_w = 180;
  int card_frame_h = 250;
  int grid_gap_x = 33;
  int grid_gap_y = 43;
  int grid_start_x = 33;
  int grid_start_y = 100;
  int title_overlay_h = 36;
  int title_text_pad_x = 2;
  int title_text_pad_bottom = 4;
  int title_marquee_gap_px = 24;
  int settings_sidebar_w = 240;
  int settings_y_offset = 0;
  int settings_content_offset_y = 35;
  int txt_margin_x = 32;
  int txt_margin_y = 20;
  int nav_l1_x = 21;
  int nav_l1_y = 46;
  int nav_r1_x = 667;
  int nav_r1_y = 46;
  int nav_start_x = 90;
  int nav_slot_w = 135;
  int nav_y = 42;
  int reader_progress_panel_margin_x = 18;
  int reader_progress_panel_margin_bottom = 12;
  int reader_progress_bar_margin_x = 34;
  int reader_progress_percent_margin_x = 34;
};

constexpr LayoutMetrics layout_720x480{
    720, 480, 20,
    0, 30,
    30, 50,
    80, 350,
    430, 50,
    140, 210,
    180, 250,
    33, 43,
    33, 100,
    36, 2, 4, 24,
    240, 0, 35,
    32, 20,
    21, 46,
    667, 46,
    90, 135, 42,
    18, 12, 34, 34,
};

constexpr LayoutMetrics layout_640x480{
    640, 480, 16,
    0, 30,
    30, 50,
    80, 350,
    430, 50,
    140, 210,
    180, 250,
    33, 43,
    33, 100,
    36, 2, 4, 24,
    220, 0, 35,
    28, 20,
    14, 46,
    587, 46,
    72, 124, 42,
    18, 12, 34, 34,
};

constexpr int kDefaultScreenW = layout_720x480.screen_w;
constexpr int kDefaultScreenH = layout_720x480.screen_h;
constexpr int kGridCols = 4;
constexpr int kGridRows = 2;
constexpr int kItemsPerPage = kGridCols * kGridRows;
constexpr float kFocusScaleBase = 1.0f;
constexpr float kFocusScaleCurrent = 1.045f; // reduce current zoomed size by 5%
constexpr float kFocusScale = kFocusScaleCurrent;
constexpr float kCoverAspect = 2.0f / 3.0f;
constexpr Uint8 kUnfocusedAlpha = 255;
constexpr float kTitleMarqueePauseSec = 0.75f;
constexpr float kTitleMarqueeSpeedPx = 48.0f;
constexpr int kButtonCount = 17;
constexpr size_t kCoverCacheMaxEntries = 160;
constexpr size_t kCoverCacheMaxBytes = 24u * 1024u * 1024u;
constexpr Uint8 kSidebarMaskMaxAlpha = 84;
constexpr float kSceneFadeFlashAlpha = 0.82f;
constexpr float kSceneFadeFlashDurationSec = 0.18f;
constexpr int kIdleWaitMs = 100;
constexpr uint32_t kActiveFrameBudgetMs = 33;
constexpr uint32_t kPeriodicTickFrameBudgetMs = 50;
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
constexpr uint32_t kReaderFastFlipThresholdMs = 200;
constexpr uint32_t kReaderPageFlipDebounceMs = 150;
constexpr int kReaderTexturePoolSize = 6;
constexpr int kTxtLineSpacing = 8;
constexpr int kTxtFontPt = 22;
constexpr size_t kTxtMaxBytes = 12 * 1024 * 1024;
constexpr size_t kTxtMaxWrappedLines = 30000;
constexpr size_t kTxtLayoutCacheMaxEntries = 4;
#ifdef HAVE_SDL2_TTF
constexpr size_t kTextCacheMaxEntries = 128;
#endif

const LayoutMetrics *g_layout = &layout_720x480;

const LayoutMetrics &Layout() { return *g_layout; }

const LayoutMetrics &SelectLayoutProfile(int screen_w, int screen_h) {
  if (screen_w == layout_640x480.screen_w && screen_h == layout_640x480.screen_h) return layout_640x480;
  return layout_720x480;
}

int FocusedCoverW() { return static_cast<int>(Layout().cover_w * kFocusScale + 0.5f); }
int FocusedCoverH() { return static_cast<int>(Layout().cover_h * kFocusScale + 0.5f); }

std::string NormalizePathKey(const std::string &path);

enum class State { Boot, Shelf, Settings, Reader };
enum class BootPhase { CountBooks, ScanBooks, GenerateCovers, Finalize, Done };

enum class Button {
  Up,
  Down,
  Left,
  Right,
  A,
  B,
  X,
  Y,
  Menu,
  L1,
  L2,
  R1,
  R2,
  Start,
  Select,
  VolUp,
  VolDown,
};

const char *ButtonName(Button b) {
  switch (b) {
  case Button::Up: return "Up";
  case Button::Down: return "Down";
  case Button::Left: return "Left";
  case Button::Right: return "Right";
  case Button::A: return "A";
  case Button::B: return "B";
  case Button::X: return "X";
  case Button::Y: return "Y";
  case Button::Menu: return "Menu";
  case Button::L1: return "L1";
  case Button::L2: return "L2";
  case Button::R1: return "R1";
  case Button::R2: return "R2";
  case Button::Start: return "Start";
  case Button::Select: return "Select";
  case Button::VolUp: return "VolUp";
  case Button::VolDown: return "VolDown";
  default: return "Invalid";
  }
}

const char *SdlEventName(Uint32 type) {
  switch (type) {
  case SDL_KEYDOWN: return "SDL_KEYDOWN";
  case SDL_KEYUP: return "SDL_KEYUP";
  case SDL_CONTROLLERBUTTONDOWN: return "SDL_CONTROLLERBUTTONDOWN";
  case SDL_CONTROLLERBUTTONUP: return "SDL_CONTROLLERBUTTONUP";
  case SDL_CONTROLLERAXISMOTION: return "SDL_CONTROLLERAXISMOTION";
  case SDL_JOYBUTTONDOWN: return "SDL_JOYBUTTONDOWN";
  case SDL_JOYBUTTONUP: return "SDL_JOYBUTTONUP";
  case SDL_JOYHATMOTION: return "SDL_JOYHATMOTION";
  case SDL_JOYAXISMOTION: return "SDL_JOYAXISMOTION";
  default: return "SDL_EVENT_UNKNOWN";
  }
}

enum class SettingId { KeyGuide, ClearHistory, CleanCache, TxtToUtf8, ContactMe, ExitApp };

struct BtnState {
  bool down = false;
  bool just_pressed = false;
  bool just_released = false;
  bool repeated = false;
  bool long_pressed = false;
  float hold_time = 0.0f;
  float repeat_timer = 0.0f;
  bool repeat_active = false;
};

struct ReaderProgress {
  int page = 0;
  int rotation = 0;
  float zoom = 1.0f;
  int scroll_x = 0;
  int scroll_y = 0;
};

struct NativeConfig {
  int theme = 0;
  bool animations = true;
  bool audio = true;
  int sfx_volume = 96; // SDL scale: 0..128
};

enum class SfxId { Move, Select, Back, Change };

class SfxBank {
public:
  bool Init(const std::filesystem::path &exe_path) {
    const std::filesystem::path root = ResolveSoundsRoot(exe_path);
    std::cout << "[native_h700] sound root: " << root.lexically_normal().string() << "\n";

#ifdef HAVE_SDL2_MIXER
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
      Mix_AllocateChannels(8);
      move_ = LoadMixChunk(root / "move.wav");
      select_ = LoadMixChunk(root / "select.wav");
      back_ = LoadMixChunk(root / "back.wav");
      change_ = LoadMixChunk(root / "change.wav");
      backend_ = Backend::Mixer;
      ready_ = true;
      return true;
    }
    std::cerr << "[native_h700] mixer init failed, fallback to SDL audio: " << Mix_GetError() << "\n";
#endif

    SDL_AudioSpec desired{};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &audio_spec_, 0);
    if (audio_dev_ == 0) {
      backend_ = Backend::None;
      ready_ = false;
      std::cerr << "[native_h700] SDL audio init failed: " << SDL_GetError() << "\n";
      return false;
    }
    SDL_PauseAudioDevice(audio_dev_, 0);

    move_pcm_ = LoadPcm(root / "move.wav");
    select_pcm_ = LoadPcm(root / "select.wav");
    back_pcm_ = LoadPcm(root / "back.wav");
    change_pcm_ = LoadPcm(root / "change.wav");
    backend_ = Backend::SdlQueue;
    ready_ = true;
    return true;
  }

  void Shutdown() {
#ifdef HAVE_SDL2_MIXER
    FreeMixChunk(move_);
    FreeMixChunk(select_);
    FreeMixChunk(back_);
    FreeMixChunk(change_);
    if (backend_ == Backend::Mixer) Mix_CloseAudio();
#endif
    FreePcm(move_pcm_);
    FreePcm(select_pcm_);
    FreePcm(back_pcm_);
    FreePcm(change_pcm_);
    if (audio_dev_ != 0) {
      SDL_CloseAudioDevice(audio_dev_);
      audio_dev_ = 0;
    }
    backend_ = Backend::None;
    ready_ = false;
  }

  void Play(SfxId id) {
    if (!ready_) return;
    if (backend_ == Backend::Mixer) {
#ifdef HAVE_SDL2_MIXER
      Mix_Chunk *chunk = nullptr;
      if (id == SfxId::Move) chunk = move_;
      else if (id == SfxId::Select) chunk = select_;
      else if (id == SfxId::Back) chunk = back_;
      else if (id == SfxId::Change) chunk = change_;
      if (chunk) Mix_PlayChannel(-1, chunk, 0);
#endif
      return;
    }
    if (backend_ == Backend::SdlQueue && audio_dev_ != 0) {
      const PcmData *pcm = nullptr;
      if (id == SfxId::Move) pcm = &move_pcm_;
      else if (id == SfxId::Select) pcm = &select_pcm_;
      else if (id == SfxId::Back) pcm = &back_pcm_;
      else if (id == SfxId::Change) pcm = &change_pcm_;
      if (pcm && pcm->data && pcm->len > 0) {
        SDL_ClearQueuedAudio(audio_dev_);
        if (volume_ >= SDL_MIX_MAXVOLUME) {
          SDL_QueueAudio(audio_dev_, pcm->data, pcm->len);
        } else if (volume_ > 0) {
          std::vector<Uint8> mixed(static_cast<size_t>(pcm->len), 0);
          SDL_MixAudioFormat(mixed.data(), pcm->data, audio_spec_.format, pcm->len, volume_);
          SDL_QueueAudio(audio_dev_, mixed.data(), pcm->len);
        }
      }
    }
  }

  void SetVolume(int volume) {
    if (volume < 0) volume_ = 0;
    else if (volume > SDL_MIX_MAXVOLUME) volume_ = SDL_MIX_MAXVOLUME;
    else volume_ = volume;
#ifdef HAVE_SDL2_MIXER
    if (backend_ == Backend::Mixer) {
      Mix_Volume(-1, volume_);
      if (move_) Mix_VolumeChunk(move_, volume_);
      if (select_) Mix_VolumeChunk(select_, volume_);
      if (back_) Mix_VolumeChunk(back_, volume_);
      if (change_) Mix_VolumeChunk(change_, volume_);
    }
#endif
  }

  int Volume() const { return volume_; }

  const char *BackendName() const {
    if (backend_ == Backend::Mixer) return "mixer";
    if (backend_ == Backend::SdlQueue) return "sdl_queue";
    return "none";
  }

private:
  struct PcmData {
    Uint8 *data = nullptr;
    Uint32 len = 0;
  };

  enum class Backend {
    None,
    Mixer,
    SdlQueue,
  };

  static std::filesystem::path ResolveSoundsRoot(const std::filesystem::path &exe_path) {
    const std::vector<std::filesystem::path> roots = {
        exe_path / "sounds",
        exe_path / ".." / "sounds",
        std::filesystem::current_path() / "sounds",
    };
    for (const auto &candidate : roots) {
      if (std::filesystem::exists(candidate / "move.wav")) return candidate.lexically_normal();
    }
    return (exe_path / "sounds").lexically_normal();
  }

#ifdef HAVE_SDL2_MIXER
  static Mix_Chunk *LoadMixChunk(const std::filesystem::path &path) {
    Mix_Chunk *chunk = Mix_LoadWAV(path.string().c_str());
    if (!chunk) {
      std::cerr << "[native_h700] sound load failed: " << path.string()
                << " err=" << Mix_GetError() << "\n";
    }
    return chunk;
  }

  static void FreeMixChunk(Mix_Chunk *&chunk) {
    if (chunk) {
      Mix_FreeChunk(chunk);
      chunk = nullptr;
    }
  }

  Mix_Chunk *move_ = nullptr;
  Mix_Chunk *select_ = nullptr;
  Mix_Chunk *back_ = nullptr;
  Mix_Chunk *change_ = nullptr;
#endif
  PcmData move_pcm_{};
  PcmData select_pcm_{};
  PcmData back_pcm_{};
  PcmData change_pcm_{};
  SDL_AudioDeviceID audio_dev_ = 0;
  SDL_AudioSpec audio_spec_{};
  Backend backend_ = Backend::None;
  bool ready_ = false;
  int volume_ = SDL_MIX_MAXVOLUME;

  PcmData LoadPcm(const std::filesystem::path &path) {
    PcmData out{};
    SDL_AudioSpec wav_spec{};
    Uint8 *wav_buf = nullptr;
    Uint32 wav_len = 0;
    if (!SDL_LoadWAV(path.string().c_str(), &wav_spec, &wav_buf, &wav_len)) {
      std::cerr << "[native_h700] sound load failed: " << path.string()
                << " err=" << SDL_GetError() << "\n";
      return out;
    }

    SDL_AudioCVT cvt{};
    if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                          audio_spec_.format, audio_spec_.channels, audio_spec_.freq) < 0) {
      std::cerr << "[native_h700] sound convert setup failed: " << path.string()
                << " err=" << SDL_GetError() << "\n";
      SDL_FreeWAV(wav_buf);
      return out;
    }

    if (!cvt.needed) {
      out.data = static_cast<Uint8 *>(SDL_malloc(wav_len));
      if (out.data) {
        SDL_memcpy(out.data, wav_buf, wav_len);
        out.len = wav_len;
      }
      SDL_FreeWAV(wav_buf);
      return out;
    }

    cvt.len = static_cast<int>(wav_len);
    cvt.buf = static_cast<Uint8 *>(SDL_malloc(cvt.len * cvt.len_mult));
    if (!cvt.buf) {
      SDL_FreeWAV(wav_buf);
      return out;
    }
    SDL_memcpy(cvt.buf, wav_buf, wav_len);
    SDL_FreeWAV(wav_buf);
    if (SDL_ConvertAudio(&cvt) != 0) {
      std::cerr << "[native_h700] sound convert failed: " << path.string()
                << " err=" << SDL_GetError() << "\n";
      SDL_free(cvt.buf);
      return out;
    }

    out.data = static_cast<Uint8 *>(SDL_malloc(cvt.len_cvt));
    if (out.data) {
      SDL_memcpy(out.data, cvt.buf, cvt.len_cvt);
      out.len = static_cast<Uint32>(cvt.len_cvt);
    }
    SDL_free(cvt.buf);
    return out;
  }

  static void FreePcm(PcmData &pcm) {
    if (pcm.data) {
      SDL_free(pcm.data);
      pcm.data = nullptr;
      pcm.len = 0;
    }
  }
};

class VolumeController {
public:
  explicit VolumeController(bool prefer_system) : prefer_system_(prefer_system) {}

  bool UsesSystemVolume() const { return prefer_system_; }

  bool AdjustUp() { return AdjustByPercent(+5); }
  bool AdjustDown() { return AdjustByPercent(-5); }

private:
  bool AdjustByPercent(int delta_percent) {
    if (!prefer_system_) return false;
    const std::string delta = (delta_percent >= 0) ? (std::to_string(delta_percent) + "%+")
                                                   : (std::to_string(-delta_percent) + "%-");
    std::cout << "[native_h700] volume adjust request: mode=system delta=" << delta << "\n";

    if (!last_working_command_.empty()) {
      const std::string cmd = last_working_command_ + delta + " unmute >/dev/null 2>&1";
      std::cout << "[native_h700] volume command retry: " << cmd << "\n";
      if (Run(cmd)) {
        std::cout << "[native_h700] volume command success: cached\n";
        return true;
      }
      std::cout << "[native_h700] volume command failed: cached\n";
      last_working_command_.clear();
    }

    static const std::array<const char *, 4> kPrefixes = {
        "amixer -q sset ",
        "/usr/bin/amixer -q sset ",
        "amixer -q -c 0 sset ",
        "/usr/bin/amixer -q -c 0 sset ",
    };
    static const std::array<const char *, 6> kControls = {
        "Master",
        "Speaker",
        "Playback",
        "PCM",
        "Headphone",
        "DAC",
    };

    for (const char *prefix : kPrefixes) {
      for (const char *control : kControls) {
        const std::string base = std::string(prefix) + "'" + control + "' ";
        const std::string cmd = base + delta + " unmute >/dev/null 2>&1";
        std::cout << "[native_h700] volume command try: control=" << control
                  << " cmd=" << cmd << "\n";
        if (Run(cmd)) {
          last_working_command_ = base;
          std::cout << "[native_h700] volume command success: control=" << control << "\n";
          return true;
        }
      }
    }
    std::cout << "[native_h700] volume command failed: no matching amixer control\n";
    return false;
  }

  static bool Run(const std::string &command) {
    return std::system(command.c_str()) == 0;
  }

  bool prefer_system_ = false;
  std::string last_working_command_;
};

enum class ShelfCategory {
  AllComics = 0,
  AllBooks = 1,
  Collections = 2,
  History = 3,
};

enum class ReaderMode {
  None = 0,
  Pdf = 1,
  Txt = 2,
  Epub = 3,
};

enum class ReaderRenderQuality {
  Low = 0,
  Full = 1,
};

struct ReaderRenderCache {
  int page = -1;
  int rotation = 0;
  float scale = 1.0f;
  ReaderRenderQuality quality = ReaderRenderQuality::Full;
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  int display_w = 0;
  int display_h = 0;
  uint32_t last_use = 0;
};

struct ReaderPageRenderMode {
  int display_w = 0;
  int display_h = 0;
};

struct ReaderViewState {
  int page = 0;
  float zoom = 1.0f;
  int rotation = 0;

  bool operator==(const ReaderViewState &other) const {
    return page == other.page &&
           rotation == other.rotation &&
           std::abs(zoom - other.zoom) < 0.0005f;
  }

  bool operator!=(const ReaderViewState &other) const { return !(*this == other); }
};

struct ReaderAdaptiveRenderState {
  uint32_t last_page_flip_tick = 0;
  bool pending_page_active = false;
  int pending_page = -1;
  bool pending_page_top = true;
  uint32_t pending_page_commit_tick = 0;
  bool fast_flip_mode = false;
  int last_scroll_dir = 1;
};

struct ReaderAsyncRenderJob {
  bool active = false;
  bool prefetch = false;
  ReaderMode mode = ReaderMode::None;
  std::string path;
  ReaderViewState state;
  int page = 0;
  float target_scale = 1.0f;
  int rotation = 0;
  int display_w = 0;
  int display_h = 0;
  uint64_t serial = 0;
};

struct ReaderAsyncRenderResult {
  bool ready = false;
  bool success = false;
  ReaderMode mode = ReaderMode::None;
  std::string path;
  ReaderViewState state;
  int page = 0;
  float target_scale = 1.0f;
  int rotation = 0;
  int display_w = 0;
  int display_h = 0;
  int src_w = 0;
  int src_h = 0;
  std::vector<unsigned char> rgba;
  uint64_t serial = 0;
};

struct ReaderTexturePoolEntry {
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  bool in_use = false;
  uint32_t last_use = 0;
};

struct TxtReaderState {
  bool open = false;
  std::vector<std::string> lines;
  int scroll_px = 0;
  int target_scroll_px = 0;
  int viewport_x = Layout().txt_margin_x;
  int viewport_y = Layout().txt_margin_y;
  int viewport_w = Layout().screen_w - Layout().txt_margin_x * 2;
  int viewport_h = Layout().screen_h - Layout().txt_margin_y * 2;
  int line_h = 28;
  int content_h = 0;
  std::string pending_raw;
  std::string pending_line;
  std::string cache_key;
  size_t parse_pos = 0;
  bool loading = false;
  bool truncated = false;
  bool limit_hit = false;
  bool truncation_notice_added = false;
  uint32_t last_resume_cache_save = 0;
  bool resume_cache_dirty = false;
};

struct TxtLayoutCacheEntry {
  std::vector<std::string> lines;
  int viewport_w = 0;
  int viewport_h = 0;
  int line_h = 0;
  int content_h = 0;
  bool truncated = false;
  bool limit_hit = false;
  uint32_t last_use = 0;
};

struct TxtResumeCacheEntry {
  std::vector<std::string> lines;
  std::string pending_raw;
  std::string pending_line;
  int viewport_w = 0;
  int viewport_h = 0;
  int line_h = 0;
  int content_h = 0;
  int scroll_px = 0;
  int target_scroll_px = 0;
  size_t parse_pos = 0;
  bool loading = false;
  bool truncated = false;
  bool limit_hit = false;
  bool truncation_notice_added = false;
};

struct TxtTranscodeJob {
  bool active = false;
  std::vector<std::string> files;
  size_t total = 0;
  size_t processed = 0;
  size_t converted = 0;
  size_t failed = 0;
  std::string current_file;
};

struct CoverCacheEntry {
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  size_t bytes = 0;
  uint32_t last_use = 0;
  bool owned = true;
};

struct ShelfRenderCache {
  SDL_Texture *texture = nullptr;
  int focus_index = -1;
  int shelf_page = -1;
  int nav_selected_index = -1;
  uint64_t content_version = 0;
};

struct ShelfScanCacheEntry {
  std::vector<BookItem> items;
  uint32_t last_scan_tick = 0;
};

struct UiAssets {
  SDL_Texture *background_main = nullptr;
  SDL_Texture *top_status_bar = nullptr;
  SDL_Texture *bottom_hint_bar = nullptr;
  SDL_Texture *nav_l1_icon = nullptr;
  SDL_Texture *nav_r1_icon = nullptr;
  SDL_Texture *nav_selected_pill = nullptr;
  SDL_Texture *book_under_shadow = nullptr;
  SDL_Texture *book_select = nullptr;
  SDL_Texture *book_title_shadow = nullptr;
  SDL_Texture *book_cover_txt = nullptr;
  SDL_Texture *book_cover_pdf = nullptr;
  SDL_Texture *settings_preview_theme = nullptr;
  SDL_Texture *settings_preview_animations = nullptr;
  SDL_Texture *settings_preview_audio = nullptr;
  SDL_Texture *settings_preview_keyguide = nullptr;
  SDL_Texture *settings_preview_contact = nullptr;
  SDL_Texture *settings_preview_clean_history = nullptr;
  SDL_Texture *settings_preview_clean_cache = nullptr;
  SDL_Texture *settings_preview_txt_to_utf8 = nullptr;
  SDL_Texture *settings_preview_exit = nullptr;
};

struct GridItemAnim {
  float x = 0.0f;
  float y = 0.0f;
  float cx = 0.0f;
  float cy = 0.0f;
  float w = static_cast<float>(Layout().cover_w);
  float h = static_cast<float>(Layout().cover_h);
  float alpha = 255.0f;

  float tcx = 0.0f;
  float tcy = 0.0f;
  float tw = static_cast<float>(Layout().cover_w);
  float th = static_cast<float>(Layout().cover_h);
  float t_alpha = 255.0f;

  bool initialized = false;

  void SnapToTarget() {
    cx = tcx;
    cy = tcy;
    w = tw;
    h = th;
    alpha = t_alpha;
    x = cx - w * 0.5f;
    y = cy - h * 0.5f;
    initialized = true;
  }

  void Update(float dt) {
    if (!initialized) {
      SnapToTarget();
      return;
    }
    auto move_linear_with_tail = [&](float current, float target, float speed_px_s, float base_span,
                                     float tail_ratio, float tail_min_mul) {
      const float delta = target - current;
      const float remain = std::abs(delta);
      const float span = std::max(1.0f, std::abs(base_span));
      const float tail_band = span * tail_ratio;
      float speed_mul = 1.0f;
      if (remain < tail_band) {
        const float u = remain / std::max(1.0f, tail_band); // 0..1
        speed_mul = tail_min_mul + (1.0f - tail_min_mul) * u;
      }
      const float max_step = speed_px_s * speed_mul * dt;
      if (std::abs(delta) <= max_step) return target;
      return current + ((delta > 0.0f) ? max_step : -max_step);
    };
    cx = move_linear_with_tail(cx, tcx, kCardMoveLinearSpeedX, static_cast<float>(Layout().cover_w + Layout().grid_gap_x),
                               kCardMoveTailRatio, kCardMoveTailMinMul);
    cy = move_linear_with_tail(cy, tcy, kCardMoveLinearSpeedY, static_cast<float>(Layout().cover_h + Layout().grid_gap_y),
                               kCardMoveTailRatio, kCardMoveTailMinMul);
    // Linear scaling with a soft tail to reduce end jitter.
    w = move_linear_with_tail(w, tw, kCardScaleLinearSpeedW, static_cast<float>(FocusedCoverW() - Layout().cover_w),
                              kCardScaleTailRatio, kCardScaleTailMinMul);
    h = move_linear_with_tail(h, th, kCardScaleLinearSpeedH, static_cast<float>(FocusedCoverH() - Layout().cover_h),
                              kCardScaleTailRatio, kCardScaleTailMinMul);
    alpha += (t_alpha - alpha) * kCardLerpSpeed * dt;

    // Avoid 1px oscillation/jitter near the target due to float-to-int rounding.
    if (std::abs(tcx - cx) < 0.15f) cx = tcx;
    if (std::abs(tcy - cy) < 0.15f) cy = tcy;
    if (std::abs(tw - w) < 0.15f) w = tw;
    if (std::abs(th - h) < 0.15f) h = th;
    if (std::abs(t_alpha - alpha) < 0.8f) alpha = t_alpha;
    x = cx - w * 0.5f;
    y = cy - h * 0.5f;
  }

  bool IsAnimating() const {
    return std::abs(tcx - cx) > 0.25f || std::abs(tcy - cy) > 0.25f || std::abs(tw - w) > 0.25f ||
           std::abs(th - h) > 0.25f || std::abs(t_alpha - alpha) > 0.8f;
  }
};

#ifdef HAVE_SDL2_TTF
struct TextCacheEntry {
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  uint32_t last_use = 0;
};

struct TitleEllipsisCacheEntry {
  std::string display;
  uint32_t last_use = 0;
};
#endif

SDL_Texture *LoadTextureFromFile(SDL_Renderer *renderer, const std::string &path) {
#ifdef HAVE_SDL2_IMAGE
  SDL_Surface *surface = IMG_Load(path.c_str());
#else
  SDL_Surface *surface = SDL_LoadBMP(path.c_str());
#endif
  if (!surface) return nullptr;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  return tex;
}

SDL_Surface *LoadSurfaceFromFile(const std::string &path) {
#ifdef HAVE_SDL2_IMAGE
  return IMG_Load(path.c_str());
#else
  return SDL_LoadBMP(path.c_str());
#endif
}

SDL_Surface *LoadSurfaceFromMemory(const void *data, size_t size) {
  if (!data || size == 0) return nullptr;
  SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
  if (!rw) return nullptr;
#ifdef HAVE_SDL2_IMAGE
  SDL_Surface *surface = IMG_Load_RW(rw, 1);
#else
  SDL_Surface *surface = SDL_LoadBMP_RW(rw, 1);
#endif
  return surface;
}

SDL_Texture *CreateNormalizedCoverTexture(SDL_Renderer *renderer, SDL_Surface *src_surface) {
  if (!renderer || !src_surface || src_surface->w <= 0 || src_surface->h <= 0) return nullptr;
  SDL_Surface *dst_surface =
      SDL_CreateRGBSurfaceWithFormat(0, Layout().cover_w, Layout().cover_h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!dst_surface) return nullptr;

  const float src_aspect = static_cast<float>(src_surface->w) / static_cast<float>(src_surface->h);
  SDL_Rect src{0, 0, src_surface->w, src_surface->h};
  if (src_aspect > kCoverAspect) {
    src.w = std::max(1, static_cast<int>(std::round(static_cast<float>(src_surface->h) * kCoverAspect)));
    src.x = (src_surface->w - src.w) / 2;
  } else if (src_aspect < kCoverAspect) {
    src.h = std::max(1, static_cast<int>(std::round(static_cast<float>(src_surface->w) / kCoverAspect)));
    src.y = (src_surface->h - src.h) / 2;
  }

  if (SDL_BlitScaled(src_surface, &src, dst_surface, nullptr) != 0) {
    SDL_FreeSurface(dst_surface);
    return nullptr;
  }
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, dst_surface);
  SDL_FreeSurface(dst_surface);
  return tex;
}

SDL_Texture *CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface) {
  if (!renderer || !surface) return nullptr;
  return SDL_CreateTextureFromSurface(renderer, surface);
}

class InputManager {
public:
  InputManager(const std::string &mapping_path, bool h700_defaults) {
    pad_map_.fill(InvalidButton());
    joy_map_.fill(InvalidButton());
    LoadDefaultPadMap();
    LoadDefaultJoyMap(h700_defaults);
    LoadOverrides(mapping_path);
  }

  void BeginFrame(float dt) {
    dt_ = dt;
    for (auto &s : states_) {
      s.just_pressed = false;
      s.just_released = false;
      s.repeated = false;
      s.long_pressed = false;
    }
  }

  void HandleEvent(const SDL_Event &e) {
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      const Button mapped = KeyToButton(e.key.keysym.sym);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " sym=" << SDL_GetKeyName(e.key.keysym.sym)
                << " keycode=" << static_cast<int>(e.key.keysym.sym)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: keydown sym=" << SDL_GetKeyName(e.key.keysym.sym)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, true);
    } else if (e.type == SDL_KEYUP) {
      const Button mapped = KeyToButton(e.key.keysym.sym);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " sym=" << SDL_GetKeyName(e.key.keysym.sym)
                << " keycode=" << static_cast<int>(e.key.keysym.sym)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: keyup sym=" << SDL_GetKeyName(e.key.keysym.sym)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, false);
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
      const Button mapped = PadToButton(e.cbutton.button);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " button=" << static_cast<int>(e.cbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: pad down button=" << static_cast<int>(e.cbutton.button)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, true);
    } else if (e.type == SDL_CONTROLLERBUTTONUP) {
      const Button mapped = PadToButton(e.cbutton.button);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " button=" << static_cast<int>(e.cbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: pad up button=" << static_cast<int>(e.cbutton.button)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, false);
    } else if (e.type == SDL_CONTROLLERAXISMOTION) {
      // Mirror both analog sticks to D-pad semantics for navigation parity.
      constexpr int kDeadzone = 16000;
      const int axis = e.caxis.axis;
      const int val = static_cast<int>(e.caxis.value);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " axis=" << axis
                << " value=" << val << "\n";
      if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_RIGHTX) {
        SetDown(Button::Left, val < -kDeadzone);
        SetDown(Button::Right, val > kDeadzone);
      } else if (axis == SDL_CONTROLLER_AXIS_LEFTY || axis == SDL_CONTROLLER_AXIS_RIGHTY) {
        SetDown(Button::Up, val < -kDeadzone);
        SetDown(Button::Down, val > kDeadzone);
      }
    } else if (e.type == SDL_JOYBUTTONDOWN) {
      const Button mapped = JoyButtonToButton(e.jbutton.button);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " button=" << static_cast<int>(e.jbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: joy down button=" << static_cast<int>(e.jbutton.button)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, true);
    } else if (e.type == SDL_JOYBUTTONUP) {
      const Button mapped = JoyButtonToButton(e.jbutton.button);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " button=" << static_cast<int>(e.jbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
      if (mapped == Button::VolUp || mapped == Button::VolDown) {
        std::cout << "[native_h700] volume event: joy up button=" << static_cast<int>(e.jbutton.button)
                  << " mapped=" << ButtonName(mapped) << "\n";
      }
      SetDown(mapped, false);
    } else if (e.type == SDL_JOYHATMOTION) {
      const uint8_t v = e.jhat.value;
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " hat=" << static_cast<int>(e.jhat.hat)
                << " value=" << static_cast<int>(v) << "\n";
      SetDown(Button::Up, (v & SDL_HAT_UP) != 0);
      SetDown(Button::Down, (v & SDL_HAT_DOWN) != 0);
      SetDown(Button::Left, (v & SDL_HAT_LEFT) != 0);
      SetDown(Button::Right, (v & SDL_HAT_RIGHT) != 0);
    } else if (e.type == SDL_JOYAXISMOTION) {
      // Some firmwares report D-pad as joystick axes instead of hat/button events.
      constexpr int kDeadzone = 16000;
      const int axis = e.jaxis.axis;
      const int val = static_cast<int>(e.jaxis.value);
      std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
                << " axis=" << axis
                << " value=" << val << "\n";
      if (axis == 0 || axis == 6) {
        SetDown(Button::Left, val < -kDeadzone);
        SetDown(Button::Right, val > kDeadzone);
      } else if (axis == 1 || axis == 7) {
        SetDown(Button::Up, val < -kDeadzone);
        SetDown(Button::Down, val > kDeadzone);
      }
    }
  }

  void EndFrame() {
    for (auto &s : states_) {
      if (!s.down) {
        s.hold_time = 0.0f;
        s.repeat_timer = 0.0f;
        s.repeat_active = false;
        continue;
      }

      s.hold_time += dt_;
      if (s.hold_time >= 0.8f) s.long_pressed = true;

      if (!s.repeat_active) {
        s.repeat_active = true;
        s.repeat_timer = 0.4f;
        s.repeated = true;
      } else {
        s.repeat_timer -= dt_;
        if (s.repeat_timer <= 0.0f) {
          s.repeated = true;
          s.repeat_timer = 0.1f;
        }
      }
    }
  }

  bool IsPressed(Button b) const { return Get(b).down; }
  bool IsJustPressed(Button b) const { return Get(b).just_pressed; }
  bool IsJustReleased(Button b) const { return Get(b).just_released; }
  bool IsRepeated(Button b) const { return Get(b).repeated; }
  bool IsLongPressed(Button b) const { return Get(b).long_pressed; }
  float HoldTime(Button b) const { return Get(b).hold_time; }
  bool AnyPressed() const {
    for (const auto &s : states_) {
      if (s.down) return true;
    }
    return false;
  }

private:
  static Button InvalidButton() { return static_cast<Button>(-1); }

  static bool IsValid(Button b) {
    const int i = static_cast<int>(b);
    return i >= 0 && i < kButtonCount;
  }

  static Button KeyToButton(SDL_Keycode k) {
    constexpr SDL_Keycode kVolumeUpFallback = static_cast<SDL_Keycode>(1073741952);
    constexpr SDL_Keycode kVolumeDownFallback = static_cast<SDL_Keycode>(1073741953);
    switch (k) {
    case SDLK_UP: return Button::Up;
    case SDLK_DOWN: return Button::Down;
    case SDLK_LEFT: return Button::Left;
    case SDLK_RIGHT: return Button::Right;
    case SDLK_a: return Button::A;
    case SDLK_b: return Button::B;
    case SDLK_x: return Button::X;
    case SDLK_y: return Button::Y;
    case SDLK_m: return Button::Menu;
    case SDLK_ESCAPE: return Button::B;
    case SDLK_RETURN: return Button::A;
    case SDLK_BACKSPACE: return Button::Select;
    case SDLK_TAB: return Button::Start;
    case SDLK_q: return Button::L1;
    case SDLK_w: return Button::R1;
    case SDLK_e: return Button::L2;
    case SDLK_r: return Button::R2;
    case SDLK_1: return Button::L1;
    case SDLK_2: return Button::L2;
    case SDLK_4: return Button::R1;
    case SDLK_3: return Button::R2;
    case SDLK_z: return Button::Start;
    case SDLK_c: return Button::Select;
#ifdef SDLK_VOLUMEUP
    case SDLK_VOLUMEUP: return Button::VolUp;
#endif
#ifdef SDLK_VOLUMEDOWN
    case SDLK_VOLUMEDOWN: return Button::VolDown;
#endif
    case kVolumeUpFallback: return Button::VolUp;
    case kVolumeDownFallback: return Button::VolDown;
#ifdef SDLK_PLUS
    case SDLK_PLUS: return Button::VolUp;
#endif
#ifdef SDLK_KP_PLUS
    case SDLK_KP_PLUS: return Button::VolUp;
#endif
#ifdef SDLK_MINUS
    case SDLK_MINUS: return Button::VolDown;
#endif
#ifdef SDLK_KP_MINUS
    case SDLK_KP_MINUS: return Button::VolDown;
#endif
    default: return static_cast<Button>(-1);
    }
  }

  Button PadToButton(uint8_t b) const {
    if (b >= pad_map_.size()) return InvalidButton();
    return pad_map_[b];
  }

  Button JoyButtonToButton(uint8_t b) const {
    if (b >= joy_map_.size()) return InvalidButton();
    return joy_map_[b];
  }

  void LoadDefaultPadMap() {
    pad_map_[SDL_CONTROLLER_BUTTON_DPAD_UP] = Button::Up;
    pad_map_[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = Button::Down;
    pad_map_[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = Button::Left;
    pad_map_[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = Button::Right;
    pad_map_[SDL_CONTROLLER_BUTTON_A] = Button::A;
    pad_map_[SDL_CONTROLLER_BUTTON_B] = Button::B;
    pad_map_[SDL_CONTROLLER_BUTTON_X] = Button::X;
    pad_map_[SDL_CONTROLLER_BUTTON_Y] = Button::Y;
    pad_map_[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = Button::L1;
    pad_map_[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = Button::R1;
    pad_map_[SDL_CONTROLLER_BUTTON_LEFTSTICK] = Button::L2;
    pad_map_[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = Button::R2;
    pad_map_[SDL_CONTROLLER_BUTTON_BACK] = Button::Select;
    pad_map_[SDL_CONTROLLER_BUTTON_START] = Button::Start;
  }

  void LoadDefaultJoyMap(bool h700_defaults) {
    // Common buttons.
    joy_map_[0] = Button::A;
    joy_map_[1] = Button::B;
    joy_map_[4] = Button::L1;
    joy_map_[5] = Button::R1;
    if (h700_defaults) {
      // H700: swap X/Y to match device-reported button indices.
      joy_map_[2] = Button::Y;
      joy_map_[3] = Button::X;
      // H700 target mapping: Select/Start open menu; L2/R2 rotate pages.
      joy_map_[6] = Button::Select;
      joy_map_[7] = Button::Start;
      joy_map_[8] = Button::Menu;
      joy_map_[9] = Button::Menu;
      joy_map_[10] = Button::L2;
      joy_map_[11] = Button::R2;
      joy_map_[12] = Button::Select;
      joy_map_[13] = Button::Start;
      joy_map_[15] = Button::VolDown;
      joy_map_[16] = Button::VolUp;
    } else {
      joy_map_[2] = Button::X;
      joy_map_[3] = Button::Y;
      // Windows/dev current behavior.
      joy_map_[6] = Button::L2;
      joy_map_[7] = Button::R2;
      joy_map_[8] = Button::Select;
      joy_map_[9] = Button::Start;
      joy_map_[10] = Button::Menu;
      joy_map_[11] = Button::Menu;
      joy_map_[12] = Button::Select;
      joy_map_[13] = Button::Start;
      joy_map_[15] = Button::VolDown;
      joy_map_[16] = Button::VolUp;
    }
  }

  static std::string Trim(std::string s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
  }

  static bool ParseButtonName(const std::string &raw, Button &out) {
    std::string n;
    n.reserve(raw.size());
    for (char c : raw) {
      if (c == ' ' || c == '\t' || c == '-') continue;
      n.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (n == "UP") out = Button::Up;
    else if (n == "DOWN") out = Button::Down;
    else if (n == "LEFT") out = Button::Left;
    else if (n == "RIGHT") out = Button::Right;
    else if (n == "A") out = Button::A;
    else if (n == "B") out = Button::B;
    else if (n == "X") out = Button::X;
    else if (n == "Y") out = Button::Y;
    else if (n == "MENU") out = Button::Menu;
    else if (n == "L1") out = Button::L1;
    else if (n == "L2") out = Button::L2;
    else if (n == "R1") out = Button::R1;
    else if (n == "R2") out = Button::R2;
    else if (n == "START") out = Button::Start;
    else if (n == "SELECT") out = Button::Select;
    else if (n == "VOLUP" || n == "VOLUMEUP") out = Button::VolUp;
    else if (n == "VOLDOWN" || n == "VOLUMEDOWN") out = Button::VolDown;
    else if (n == "NONE" || n == "DISABLED" || n == "INVALID") out = InvalidButton();
    else return false;
    return true;
  }

  void LoadOverrides(const std::string &mapping_path) {
    std::ifstream in(mapping_path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      line = Trim(line);
      if (line.empty() || line[0] == '#' || line[0] == ';') continue;
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = Trim(line.substr(0, eq));
      const std::string val = Trim(line.substr(eq + 1));
      Button mapped = InvalidButton();
      if (!ParseButtonName(val, mapped)) continue;
      if (key.rfind("joy.", 0) == 0) {
        const int idx = std::atoi(key.substr(4).c_str());
        if (idx >= 0 && idx < static_cast<int>(joy_map_.size())) joy_map_[idx] = mapped;
      } else if (key.rfind("pad.", 0) == 0) {
        const int idx = std::atoi(key.substr(4).c_str());
        if (idx >= 0 && idx < static_cast<int>(pad_map_.size())) pad_map_[idx] = mapped;
      }
    }
  }

  void SetDown(Button b, bool down) {
    if (!IsValid(b)) return;
    BtnState &s = states_[static_cast<int>(b)];
    if (down && !s.down) {
      s.down = true;
      s.just_pressed = true;
      s.hold_time = 0.0f;
    } else if (!down && s.down) {
      s.down = false;
      s.just_released = true;
    }
  }

  const BtnState &Get(Button b) const {
    static BtnState empty;
    if (!IsValid(b)) return empty;
    return states_[static_cast<int>(b)];
  }

  std::array<BtnState, kButtonCount> states_{};
  std::array<Button, 32> pad_map_{};
  std::array<Button, 32> joy_map_{};
  float dt_ = 1.0f / 60.0f;
};

class ProgressStore {
public:
  explicit ProgressStore(std::string path) : path_(std::move(path)) { Load(); }

  ReaderProgress Get(const std::string &book) const {
    auto it = map_.find(book);
    return it == map_.end() ? ReaderProgress{} : it->second;
  }
  void Set(const std::string &book, const ReaderProgress &p) {
    auto it = map_.find(book);
    if (it != map_.end() &&
        it->second.page == p.page &&
        it->second.rotation == p.rotation &&
        std::abs(it->second.zoom - p.zoom) < 0.0001f &&
        it->second.scroll_x == p.scroll_x &&
        it->second.scroll_y == p.scroll_y) {
      return;
    }
    map_[book] = p;
    MarkDirty();
  }
  bool IsDirty() const { return dirty_; }
  bool ShouldFlush(uint32_t now, uint32_t delay_ms) const {
    return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
  }
  void MarkDirty() {
    dirty_ = true;
    last_dirty_tick_ = SDL_GetTicks();
  }
  void Save() {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const auto &kv : map_) {
      out << kv.first << "\t" << kv.second.page << "\t" << kv.second.rotation << "\t" << kv.second.zoom << "\t"
          << kv.second.scroll_x << "\t" << kv.second.scroll_y << "\n";
    }
    dirty_ = false;
    last_dirty_tick_ = 0;
  }

private:
  void Load() {
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      const size_t p = line.find('\t');
      if (p == std::string::npos) continue;
      const std::string key = line.substr(0, p);
      std::vector<std::string> cols;
      std::string rest = line.substr(p + 1);
      size_t start = 0;
      while (true) {
        size_t t = rest.find('\t', start);
        if (t == std::string::npos) {
          cols.push_back(rest.substr(start));
          break;
        }
        cols.push_back(rest.substr(start, t - start));
        start = t + 1;
      }
      if (cols.size() < 5) continue;
      ReaderProgress rp;
      rp.page = std::stoi(cols[0]);
      rp.rotation = std::stoi(cols[1]);
      rp.zoom = std::stof(cols[2]);
      rp.scroll_x = std::stoi(cols[3]);
      rp.scroll_y = std::stoi(cols[4]);
      map_[key] = rp;
    }
  }

  std::string path_;
  std::unordered_map<std::string, ReaderProgress> map_;
  bool dirty_ = false;
  uint32_t last_dirty_tick_ = 0;
};

class ConfigStore {
public:
  explicit ConfigStore(std::string path) : path_(std::move(path)) { Load(); }
  const NativeConfig &Get() const { return cfg_; }
  NativeConfig &Mutable() { return cfg_; }
  bool IsDirty() const { return dirty_; }
  bool ShouldFlush(uint32_t now, uint32_t delay_ms) const {
    return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
  }
  void MarkDirty() {
    dirty_ = true;
    last_dirty_tick_ = SDL_GetTicks();
  }
  void Save() {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << "theme=" << cfg_.theme << "\n";
    out << "animations=" << (cfg_.animations ? 1 : 0) << "\n";
    out << "audio=" << (cfg_.audio ? 1 : 0) << "\n";
    out << "sfx_volume=" << cfg_.sfx_volume << "\n";
    dirty_ = false;
    last_dirty_tick_ = 0;
  }

private:
  void Load() {
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = line.substr(0, eq);
      const std::string v = line.substr(eq + 1);
      if (k == "theme") cfg_.theme = std::stoi(v);
      else if (k == "animations") cfg_.animations = (v == "1");
      else if (k == "audio") cfg_.audio = (v == "1");
      else if (k == "sfx_volume") cfg_.sfx_volume = std::stoi(v);
    }
    if (cfg_.sfx_volume < 0) cfg_.sfx_volume = 0;
    if (cfg_.sfx_volume > SDL_MIX_MAXVOLUME) cfg_.sfx_volume = SDL_MIX_MAXVOLUME;
  }

  std::string path_;
  NativeConfig cfg_;
  bool dirty_ = false;
  uint32_t last_dirty_tick_ = 0;
};

class PathSetStore {
public:
  explicit PathSetStore(std::string path) : path_(std::move(path)) { Load(); }

  bool Contains(const std::string &book_path) const {
    return set_.find(NormalizePathKey(book_path)) != set_.end();
  }

  void Add(const std::string &book_path) {
    if (set_.insert(NormalizePathKey(book_path)).second) MarkDirty();
  }
  void Remove(const std::string &book_path) {
    if (set_.erase(NormalizePathKey(book_path)) > 0) MarkDirty();
  }

  bool Toggle(const std::string &book_path) {
    const std::string key = NormalizePathKey(book_path);
    auto it = set_.find(key);
    if (it != set_.end()) {
      set_.erase(it);
      MarkDirty();
      return false;
    }
    set_.insert(key);
    MarkDirty();
    return true;
  }

  void Clear() {
    if (set_.empty()) return;
    set_.clear();
    MarkDirty();
  }

  bool IsDirty() const { return dirty_; }
  bool ShouldFlush(uint32_t now, uint32_t delay_ms) const {
    return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
  }
  void MarkDirty() {
    dirty_ = true;
    last_dirty_tick_ = SDL_GetTicks();
  }
  void Save() {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const auto &v : set_) out << v << "\n";
    dirty_ = false;
    last_dirty_tick_ = 0;
  }

private:
  void Load() {
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      set_.insert(NormalizePathKey(line));
    }
  }

  std::string path_;
  std::unordered_set<std::string> set_;
  bool dirty_ = false;
  uint32_t last_dirty_tick_ = 0;
};

class RecentPathStore {
public:
  explicit RecentPathStore(std::string path) : path_(std::move(path)) { Load(); }

  bool Contains(const std::string &book_path) const {
    return set_.find(NormalizePathKey(book_path)) != set_.end();
  }

  void Add(const std::string &book_path) {
    const std::string key = NormalizePathKey(book_path);
    if (key.empty()) return;
    auto it = std::find(order_.begin(), order_.end(), key);
    if (it != order_.end()) {
      if (it == order_.begin()) return;
      order_.erase(it);
      order_.insert(order_.begin(), key);
      MarkDirty();
      return;
    }
    set_.insert(key);
    order_.insert(order_.begin(), key);
    MarkDirty();
  }

  void Remove(const std::string &book_path) {
    const std::string key = NormalizePathKey(book_path);
    if (set_.erase(key) == 0) return;
    order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
    MarkDirty();
  }

  void Clear() {
    if (order_.empty()) return;
    set_.clear();
    order_.clear();
    MarkDirty();
  }

  const std::vector<std::string> &OrderedPaths() const { return order_; }

  bool IsDirty() const { return dirty_; }
  bool ShouldFlush(uint32_t now, uint32_t delay_ms) const {
    return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
  }
  void MarkDirty() {
    dirty_ = true;
    last_dirty_tick_ = SDL_GetTicks();
  }
  void Save() {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const auto &v : order_) out << v << "\n";
    dirty_ = false;
    last_dirty_tick_ = 0;
  }

private:
  void Load() {
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      const std::string key = NormalizePathKey(line);
      if (key.empty() || !set_.insert(key).second) continue;
      order_.push_back(key);
    }
  }

  std::string path_;
  std::unordered_set<std::string> set_;
  std::vector<std::string> order_;
  bool dirty_ = false;
  uint32_t last_dirty_tick_ = 0;
};

void DrawRect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c, bool fill = true) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
  SDL_Rect rc{x, y, w, h};
  if (fill) SDL_RenderFillRect(r, &rc);
  else SDL_RenderDrawRect(r, &rc);
}

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
    p = p.lexically_normal();
    std::string out = p.generic_string();
#ifdef _WIN32
    out = ToLowerAscii(out);
#endif
    return out;
  } catch (...) {
    return path;
  }
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

int main(int, char **) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
    std::cerr << "[native_h700] SDL init failed: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_JoystickEventState(SDL_ENABLE);
#ifdef HAVE_SDL2_TTF
  if (TTF_Init() != 0) {
    std::cerr << "[native_h700] SDL2_ttf init warning: " << TTF_GetError() << "\n";
  }
#endif
#ifdef HAVE_SDL2_IMAGE
  const int img_flags = IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP;
  const int img_ok = IMG_Init(img_flags);
  if ((img_ok & img_flags) == 0) {
    std::cerr << "[native_h700] SDL2_image init warning: " << IMG_GetError() << "\n";
  }
#endif
  const char *env_windowed = std::getenv("ROCREADER_WINDOWED");
  const char *env_fullscreen = std::getenv("ROCREADER_FULLSCREEN");
  const bool force_windowed = env_windowed && std::string(env_windowed) == "1";
  const bool force_fullscreen = env_fullscreen && std::string(env_fullscreen) == "1";

  uint32_t win_flags = SDL_WINDOW_SHOWN;
#if defined(__arm__) || defined(__aarch64__)
  const bool default_fullscreen = true;
#else
  const bool default_fullscreen = false;
#endif
  if ((default_fullscreen && !force_windowed) || force_fullscreen) {
    win_flags |= SDL_WINDOW_FULLSCREEN;
  }
  g_layout = &SelectLayoutProfile(kDefaultScreenW, kDefaultScreenH);

  SDL_Window *window =
      SDL_CreateWindow("ROCreader Native H700",
                       SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED,
                       Layout().screen_w,
                       Layout().screen_h,
                       win_flags);
  if (!window) {
    std::cerr << "[native_h700] window failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 2;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    std::cerr << "[native_h700] renderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 3;
  }
  SDL_RendererInfo renderer_info{};
  if (SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
    std::cout << "[native_h700] renderer: " << (renderer_info.name ? renderer_info.name : "unknown")
              << " flags=0x" << std::hex << renderer_info.flags << std::dec
              << " accelerated=" << ((renderer_info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no")
              << " vsync=" << ((renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no") << "\n";
  }
  const bool renderer_supports_target_textures = (renderer_info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;

  std::vector<SDL_GameController *> opened_controllers;
  std::vector<SDL_Joystick *> opened_joysticks;
  const int joystick_count = SDL_NumJoysticks();
  std::cout << "[native_h700] joysticks: " << joystick_count << "\n";
  for (int i = 0; i < joystick_count; ++i) {
    if (SDL_IsGameController(i)) {
      SDL_GameController *gc = SDL_GameControllerOpen(i);
      if (gc) {
        opened_controllers.push_back(gc);
        std::cout << "[native_h700] opened gamecontroller idx=" << i << "\n";
        continue;
      }
    }
    SDL_Joystick *js = SDL_JoystickOpen(i);
    if (js) {
      opened_joysticks.push_back(js);
      std::cout << "[native_h700] opened joystick idx=" << i << "\n";
    }
  }

  std::string exe_dir = ".";
  if (char *base = SDL_GetBasePath(); base && *base) {
    exe_dir = base;
    SDL_free(base);
  }
  std::filesystem::path exe_path(exe_dir);
  std::filesystem::path ui_path = exe_path / "ui";
  auto resolve_runtime_file = [&](const std::string &name) -> std::filesystem::path {
    const std::vector<std::filesystem::path> candidates = {
        exe_path / name,
        exe_path / ".." / name,
        std::filesystem::current_path() / name,
    };
    for (const auto &p : candidates) {
      if (std::filesystem::exists(p)) return p.lexically_normal();
    }
    return (exe_path / name).lexically_normal();
  };

  UiAssets ui_assets;
  std::unordered_map<SDL_Texture *, SDL_Point> texture_sizes;
  std::vector<std::filesystem::path> ui_roots;
  ui_roots.push_back(exe_path / "ui");
  ui_roots.push_back(exe_path / ".." / "ui");
  ui_roots.push_back(std::filesystem::current_path() / "ui");
  std::vector<std::filesystem::path> ui_pack_paths;
  ui_pack_paths.push_back(exe_path / "ui.pack");
  ui_pack_paths.push_back(exe_path / ".." / "ui.pack");
  ui_pack_paths.push_back(std::filesystem::current_path() / "ui.pack");
  std::unordered_map<std::string, std::vector<unsigned char>> packed_ui_assets;
  std::filesystem::path ui_root_hit;
  std::filesystem::path ui_pack_hit;
  auto xor_ui_payload = [&](std::vector<unsigned char> &data, const std::string &name) {
    static const std::string key = "ROCreader::native_h700::ui_pack";
    if (name.empty()) return;
    for (size_t i = 0; i < data.size(); ++i) {
      const unsigned char k = static_cast<unsigned char>(key[i % key.size()]);
      const unsigned char n = static_cast<unsigned char>(name[i % name.size()]);
      data[i] = static_cast<unsigned char>(data[i] ^ k ^ n ^ static_cast<unsigned char>((i * 131u) & 0xFFu));
    }
  };
  auto load_ui_pack = [&]() {
    for (const auto &pack_path : ui_pack_paths) {
      if (!std::filesystem::exists(pack_path)) continue;
      std::ifstream in(pack_path, std::ios::binary);
      if (!in) continue;
      char magic[8] = {};
      in.read(magic, sizeof(magic));
      if (!in || std::string(magic, sizeof(magic)) != "RCUIPK01") continue;
      uint32_t count = 0;
      in.read(reinterpret_cast<char *>(&count), sizeof(count));
      if (!in) continue;
      std::unordered_map<std::string, std::vector<unsigned char>> loaded;
      bool ok = true;
      for (uint32_t i = 0; i < count; ++i) {
        uint16_t name_len = 0;
        uint32_t original_size = 0;
        uint32_t enc_size = 0;
        in.read(reinterpret_cast<char *>(&name_len), sizeof(name_len));
        if (!in) { ok = false; break; }
        std::string name(name_len, '\0');
        if (name_len > 0) in.read(name.data(), static_cast<std::streamsize>(name_len));
        in.read(reinterpret_cast<char *>(&original_size), sizeof(original_size));
        in.read(reinterpret_cast<char *>(&enc_size), sizeof(enc_size));
        if (!in) { ok = false; break; }
        std::vector<unsigned char> payload(enc_size, 0);
        if (enc_size > 0) in.read(reinterpret_cast<char *>(payload.data()), static_cast<std::streamsize>(enc_size));
        if (!in) { ok = false; break; }
        xor_ui_payload(payload, name);
        if (payload.size() != original_size) {
          ok = false;
          break;
        }
        loaded[name] = std::move(payload);
      }
      if (!ok) continue;
      packed_ui_assets = std::move(loaded);
      ui_pack_hit = pack_path;
      break;
    }
  };
  load_ui_pack();
  auto load_ui_asset = [&](SDL_Texture *&slot, const std::string &name) {
    if (slot) {
      SDL_DestroyTexture(slot);
      slot = nullptr;
    }
    auto packed_it = packed_ui_assets.find(name);
    if (packed_it != packed_ui_assets.end()) {
      SDL_Surface *surface = LoadSurfaceFromMemory(packed_it->second.data(), packed_it->second.size());
      if (surface) {
        slot = CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
      }
      if (!slot) {
        std::cerr << "[native_h700] ui asset load failed from pack: " << name << " err=" << SDL_GetError() << "\n";
      } else {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(slot, nullptr, nullptr, &tw, &th);
        texture_sizes[slot] = SDL_Point{tw, th};
      }
      return;
    }
    std::filesystem::path fp;
    for (const auto &root : ui_roots) {
      const auto candidate = root / name;
      if (std::filesystem::exists(candidate)) {
        fp = candidate;
        if (ui_root_hit.empty()) ui_root_hit = root;
        break;
      }
    }
    if (fp.empty()) {
      std::cerr << "[native_h700] ui asset not found: " << name << "\n";
      return;
    }
    slot = LoadTextureFromFile(renderer, fp.string());
    if (!slot) {
      std::cerr << "[native_h700] ui asset load failed: " << fp.string() << " err=" << SDL_GetError() << "\n";
    } else {
      int tw = 0;
      int th = 0;
      SDL_QueryTexture(slot, nullptr, nullptr, &tw, &th);
      texture_sizes[slot] = SDL_Point{tw, th};
    }
  };
  auto forget_texture_size = [&](SDL_Texture *tex) {
    if (!tex) return;
    texture_sizes.erase(tex);
  };
  auto remember_texture_size = [&](SDL_Texture *tex, int w, int h) {
    if (!tex) return;
    texture_sizes[tex] = SDL_Point{w, h};
  };
  auto get_texture_size = [&](SDL_Texture *tex, int &w, int &h) {
    w = 0;
    h = 0;
    if (!tex) return;
    auto it = texture_sizes.find(tex);
    if (it != texture_sizes.end()) {
      w = it->second.x;
      h = it->second.y;
      return;
    }
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    remember_texture_size(tex, w, h);
  };
  load_ui_asset(ui_assets.background_main, "background_main.png");
  load_ui_asset(ui_assets.top_status_bar, "top_status_bar.png");
  load_ui_asset(ui_assets.bottom_hint_bar, "bottom_hint_bar.png");
  load_ui_asset(ui_assets.nav_l1_icon, "nav_l1_icon.png");
  load_ui_asset(ui_assets.nav_r1_icon, "nav_r1_icon.png");
  load_ui_asset(ui_assets.nav_selected_pill, "nav_selected_pill.png");
  load_ui_asset(ui_assets.book_under_shadow, "book_under_shadow.png");
  load_ui_asset(ui_assets.book_select, "book_select.png");
  load_ui_asset(ui_assets.book_title_shadow, "book_title_shadow.png");
  load_ui_asset(ui_assets.book_cover_txt, "book_cover_txt.png");
  load_ui_asset(ui_assets.book_cover_pdf, "book_cover_pdf.png");
  load_ui_asset(ui_assets.settings_preview_theme, "settings_preview_theme.png");
  load_ui_asset(ui_assets.settings_preview_animations, "settings_preview_animations.png");
  load_ui_asset(ui_assets.settings_preview_audio, "settings_preview_audio.png");
  load_ui_asset(ui_assets.settings_preview_keyguide, "Menu_Button Instructions.png");
  load_ui_asset(ui_assets.settings_preview_contact, "Menu_Contact Me.png");
  load_ui_asset(ui_assets.settings_preview_clean_history, "Menu_CleanHistory.png");
  load_ui_asset(ui_assets.settings_preview_clean_cache, "Menu_CleanCache.png");
  load_ui_asset(ui_assets.settings_preview_txt_to_utf8, "Menu_TXTtoUTF8.png");
  load_ui_asset(ui_assets.settings_preview_exit, "Menu_Exit.png");
  if (!ui_pack_hit.empty()) {
    std::cout << "[native_h700] ui pack: " << ui_pack_hit.string() << " assets=" << packed_ui_assets.size() << "\n";
  }
  if (!ui_root_hit.empty()) {
    std::cout << "[native_h700] ui root: " << ui_root_hit.string() << "\n";
  }

  const std::vector<std::string> books_roots = storage_paths::DetectBooksRoots();
  const std::vector<std::string> cover_roots = storage_paths::DetectCoverRoots();
  const std::vector<std::string> rocreader_roots = storage_paths::DetectRocreaderRoots();
  std::filesystem::path txt_layout_cache_dir =
      !rocreader_roots.empty() ? (std::filesystem::path(rocreader_roots.front()) / "cache" / "txt_layouts")
                               : (exe_path / ".." / "cache" / "txt_layouts");
  std::filesystem::path cover_thumb_cache_dir =
      !rocreader_roots.empty() ? (std::filesystem::path(rocreader_roots.front()) / "cache" / "cover_thumbs")
                               : (exe_path / ".." / "cache" / "cover_thumbs");
  {
    std::error_code ec;
    std::filesystem::create_directories(txt_layout_cache_dir, ec);
  }
  {
    std::error_code ec;
    std::filesystem::create_directories(cover_thumb_cache_dir, ec);
  }
  std::cout << "[native_h700] books roots:";
  for (const auto &r : books_roots) std::cout << " " << r;
  std::cout << "\n";
  std::cout << "[native_h700] cover roots:";
  for (const auto &r : cover_roots) std::cout << " " << r;
  std::cout << "\n";

  const std::filesystem::path keymap_path = resolve_runtime_file("native_keymap.ini");
#if defined(__arm__) || defined(__aarch64__)
  const bool use_h700_defaults = true;
#else
  const bool use_h700_defaults = false;
#endif
  const std::filesystem::path config_path = resolve_runtime_file("native_config.ini");
  const std::filesystem::path progress_path = resolve_runtime_file("native_progress.tsv");
  const std::filesystem::path favorites_path = resolve_runtime_file("native_favorites.txt");
  const std::filesystem::path history_path = resolve_runtime_file("native_history.txt");
  std::cout << "[native_h700] keymap path: " << keymap_path.lexically_normal().string() << "\n";
  std::cout << "[native_h700] config path: " << config_path.lexically_normal().string() << "\n";

  InputManager input(keymap_path.string(), use_h700_defaults);
  ConfigStore config(config_path.string());
  if (!config.Get().audio) {
    config.Mutable().audio = true;
    config.MarkDirty();
    config.Save();
    std::cout << "[native_h700] sound: force enable audio=1 on startup\n";
  }
  ProgressStore progress(progress_path.string());
  RecentPathStore favorites_store(favorites_path.string());
  RecentPathStore history_store(history_path.string());
  VolumeController volume_controller(use_h700_defaults);
  bool warned_system_volume_fallback = false;
  std::cout << "[native_h700] volume controller: prefer_system="
            << (volume_controller.UsesSystemVolume() ? "1" : "0") << "\n";
  int volume_display_percent = ClampInt((config.Get().sfx_volume * 100) / std::max(1, SDL_MIX_MAXVOLUME), 0, 100);
  uint32_t volume_display_until = 0;
  SfxBank sfx;
  bool sfx_ready = false;
  bool sfx_init_attempted = false;
  sfx.SetVolume(config.Get().sfx_volume);
  auto ensure_sfx_ready = [&]() -> bool {
    if (sfx_ready) return true;
    if (sfx_init_attempted) return false;
    sfx_init_attempted = true;
    sfx_ready = sfx.Init(exe_path);
    if (!sfx_ready) {
      std::cout << "[native_h700] sound: disabled (all audio backends failed)\n";
    }
    std::cout << "[native_h700] sound init: backend=" << sfx.BackendName()
              << " ready=" << (sfx_ready ? "1" : "0")
              << " volume=" << config.Get().sfx_volume << "\n";
    return sfx_ready;
  };
  std::cout << "[native_h700] sound: config_audio=" << (config.Get().audio ? "1" : "0")
            << " backend=" << sfx.BackendName()
            << " ready=deferred"
            << " volume=" << config.Get().sfx_volume << "\n";
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
  EpubComicReader epub_comic;
  std::cout << "[native_h700] epub comic backend: " << epub_comic.BackendName()
            << " (real_renderer=" << (epub_comic.HasRealRenderer() ? "yes" : "no") << ")\n";
  ReaderRenderCache render_cache;
  ReaderRenderCache secondary_render_cache;
  ReaderRenderCache tertiary_render_cache;
  std::array<ReaderTexturePoolEntry, kReaderTexturePoolSize> reader_texture_pool{};
  ReaderViewState display_state;
  ReaderViewState target_state;
  ReaderViewState ready_state;
  bool display_state_valid = false;
  bool ready_state_valid = false;
  ReaderAdaptiveRenderState adaptive_render;
  ReaderAsyncRenderJob reader_async_requested_job;
  ReaderAsyncRenderJob reader_async_inflight_job;
  ReaderAsyncRenderResult reader_async_result;
  uint64_t reader_async_job_serial = 0;
  uint64_t reader_async_latest_target_serial = 0;
  std::atomic<bool> reader_async_cancel_requested{false};
  SDL_mutex *reader_async_mutex = SDL_CreateMutex();
  SDL_cond *reader_async_cond = SDL_CreateCond();
  bool reader_async_worker_running = true;
  const Uint32 reader_async_event_type = SDL_RegisterEvents(1);
  std::unordered_map<int, SDL_Point> reader_page_size_cache;
  ShelfRenderCache shelf_render_cache;
  uint64_t shelf_content_version = 1;

  State state = State::Boot;
  State settings_return_state = State::Shelf;
  float boot_timer = 0.0f;
  BootPhase boot_phase = BootPhase::CountBooks;
  std::vector<std::string> boot_supported_paths;
  size_t boot_scan_index = 0;
  std::vector<std::string> boot_cover_generate_queue;
  size_t boot_cover_generate_index = 0;
  size_t boot_total_books = 0;
  std::filesystem::recursive_directory_iterator boot_count_it;
  std::filesystem::recursive_directory_iterator boot_count_end;
  size_t boot_count_root_index = 0;
  bool boot_count_iterator_active = false;
  std::string boot_status_text = "Loading resources...(0/0)";
  std::vector<BookItem> shelf_items;
  std::unordered_map<std::string, ShelfScanCacheEntry> shelf_scan_cache;
  std::unordered_map<std::string, CoverCacheEntry> cover_textures;
  std::string current_folder;
  std::unordered_map<std::string, int> folder_focus;
  int focus_index = 0;
  std::unordered_map<int, GridItemAnim> grid_item_anims;
  int shelf_page = 0;
  int page_anim_from = 0;
  int page_anim_to = 0;
  int page_anim_dir = 0;
  bool page_animating = false;
  animation::TweenFloat page_slide(0.0f);
  bool any_grid_animating = false;
  int title_focus_index = -1;
  float title_marquee_wait = kTitleMarqueePauseSec;
  float title_marquee_offset = 0.0f;
  bool title_marquee_active = false;

  animation::TweenFloat menu_anim(0.0f);
  animation::TweenFloat scene_flash(0.0f);
  bool menu_closing = false;
  float settings_toggle_guard = 0.0f;
  bool settings_close_armed = true;
  bool menu_toggle_armed = true;
  float menu_toggle_cooldown = 0.0f;
  const int menu_width = Layout().settings_sidebar_w;
  std::vector<SettingId> menu_items = {
      SettingId::KeyGuide,
      SettingId::ClearHistory,
      SettingId::CleanCache,
      SettingId::TxtToUtf8,
      SettingId::ContactMe,
      SettingId::ExitApp};
  int menu_selected = 0;
  TxtTranscodeJob txt_transcode_job{};

  std::string current_book;
  ReaderProgress reader{};
  ReaderMode reader_mode = ReaderMode::None;
  TxtReaderState txt_reader{};
  bool reader_progress_overlay_visible = false;
  float hold_cooldown = 0.0f;
  std::array<float, kButtonCount> hold_speed{};
  std::array<bool, kButtonCount> long_fired{};
  int nav_selected_index = 0; // 0: ALL COMICS, 1: ALL BOOKS, 2: COLLECTIONS, 3: HISTORY
  bool warned_mock_pdf_backend = false;
  bool warned_epub_backend = false;

  auto current_category = [&]() -> ShelfCategory {
    return static_cast<ShelfCategory>(ClampInt(nav_selected_index, 0, 3));
  };

  auto shelf_scan_cache_key = [&](ShelfCategory cat, const std::string &folder) {
    std::ostringstream oss;
    oss << static_cast<int>(cat) << "|";
    if (folder.empty()) {
      oss << "<root>";
      for (const auto &root : books_roots) {
        oss << "|" << NormalizePathKey(root);
      }
    } else {
      oss << NormalizePathKey(folder);
    }
    return oss.str();
  };

  auto prune_shelf_scan_cache = [&]() {
    while (shelf_scan_cache.size() > kShelfScanCacheMaxEntries) {
      auto oldest = shelf_scan_cache.end();
      for (auto it = shelf_scan_cache.begin(); it != shelf_scan_cache.end(); ++it) {
        if (oldest == shelf_scan_cache.end() || it->second.last_scan_tick < oldest->second.last_scan_tick) {
          oldest = it;
        }
      }
      if (oldest == shelf_scan_cache.end()) break;
      shelf_scan_cache.erase(oldest);
    }
  };

  auto match_category = [&](const BookItem &item, ShelfCategory category) -> bool {
    if (item.is_dir) {
      // Folders are category-specific; AllBooks is filtered at scan stage.
      return category == ShelfCategory::AllComics || category == ShelfCategory::AllBooks;
    }
    const std::string ext = GetLowerExt(item.path);
    if (category == ShelfCategory::AllComics) {
      return ext == ".pdf" || ext == ".epub";
    }
    if (category == ShelfCategory::AllBooks) {
      return ext == ".txt";
    }
    if (category == ShelfCategory::Collections) {
      return favorites_store.Contains(item.path);
    }
    if (category == ShelfCategory::History) {
      return history_store.Contains(item.path);
    }
    return true;
  };

  auto scan_base_items = [&]() -> std::vector<BookItem> {
    const ShelfCategory cat = current_category();
    const std::string cache_key = shelf_scan_cache_key(cat, current_folder);
    const uint32_t cache_now = SDL_GetTicks();
    auto cache_it = shelf_scan_cache.find(cache_key);
    if (cache_it != shelf_scan_cache.end() && cache_now - cache_it->second.last_scan_tick < kShelfScanCacheTtlMs) {
      return cache_it->second.items;
    }

    auto is_pure_ext_folder = [&](const std::string &folder_path, const std::string &wanted_ext) -> bool {
      std::error_code ec;
      const std::filesystem::path root(folder_path);
      if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return false;
      const auto opts = std::filesystem::directory_options::skip_permission_denied;
      bool has_match = false;
      for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) continue;
        const auto &entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = GetLowerExt(entry.path().string());
        if (ext == wanted_ext) {
          has_match = true;
          continue;
        }
        return false;
      }
      return has_match;
    };

    if (cat == ShelfCategory::AllComics) {
      std::vector<BookItem> out;
      if (current_folder.empty()) {
        for (const auto &root : books_roots) {
          std::error_code ec;
          const std::filesystem::path root_path(root);
          if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
          const auto opts = std::filesystem::directory_options::skip_permission_denied;
          for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
            if (ec) continue;
            const auto &entry = *it;
            if (entry.is_regular_file(ec)) {
              const std::string ext = GetLowerExt(entry.path().string());
              if (ext != ".pdf" && ext != ".epub") continue;
              BookItem item;
              item.name = entry.path().filename().string();
              item.path = entry.path().string();
              item.is_dir = false;
              out.push_back(std::move(item));
            } else if (entry.is_directory(ec)) {
              const bool pure_pdf = is_pure_ext_folder(entry.path().string(), ".pdf");
              const bool pure_epub = is_pure_ext_folder(entry.path().string(), ".epub");
              if (!pure_pdf && !pure_epub) continue;
              BookItem item;
              item.name = entry.path().filename().string();
              item.path = entry.path().string();
              item.is_dir = true;
              out.push_back(std::move(item));
            }
          }
        }
      } else {
        std::error_code ec;
        const std::filesystem::path root_path(current_folder);
        if (std::filesystem::exists(root_path, ec) && std::filesystem::is_directory(root_path, ec)) {
          const auto opts = std::filesystem::directory_options::skip_permission_denied;
          for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
            if (ec) continue;
            const auto &entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            const std::string ext = GetLowerExt(entry.path().string());
            if (ext != ".pdf" && ext != ".epub") continue;
            BookItem item;
            item.name = entry.path().filename().string();
            item.path = entry.path().string();
            item.is_dir = false;
            out.push_back(std::move(item));
          }
        }
      }
      std::sort(out.begin(), out.end(), [](const BookItem &a, const BookItem &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
      });
      shelf_scan_cache[cache_key] = ShelfScanCacheEntry{out, cache_now};
      prune_shelf_scan_cache();
      return out;
    }

    if (cat == ShelfCategory::AllBooks) {
      std::vector<BookItem> out;
      if (current_folder.empty()) {
        for (const auto &root : books_roots) {
          std::error_code ec;
          const std::filesystem::path root_path(root);
          if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
          const auto opts = std::filesystem::directory_options::skip_permission_denied;
          for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
            if (ec) continue;
            const auto &entry = *it;
            if (entry.is_regular_file(ec)) {
              if (GetLowerExt(entry.path().string()) != ".txt") continue;
              BookItem item;
              item.name = entry.path().filename().string();
              item.path = entry.path().string();
              item.is_dir = false;
              out.push_back(std::move(item));
            } else if (entry.is_directory(ec)) {
              if (!is_pure_ext_folder(entry.path().string(), ".txt")) continue;
              BookItem item;
              item.name = entry.path().filename().string();
              item.path = entry.path().string();
              item.is_dir = true;
              out.push_back(std::move(item));
            }
          }
        }
      } else {
        std::error_code ec;
        const std::filesystem::path root_path(current_folder);
        if (std::filesystem::exists(root_path, ec) && std::filesystem::is_directory(root_path, ec)) {
          const auto opts = std::filesystem::directory_options::skip_permission_denied;
          for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
            if (ec) continue;
            const auto &entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            if (GetLowerExt(entry.path().string()) != ".txt") continue;
            BookItem item;
            item.name = entry.path().filename().string();
            item.path = entry.path().string();
            item.is_dir = false;
            out.push_back(std::move(item));
          }
        }
      }
      std::sort(out.begin(), out.end(), [](const BookItem &a, const BookItem &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
      });
      shelf_scan_cache[cache_key] = ShelfScanCacheEntry{out, cache_now};
      prune_shelf_scan_cache();
      return out;
    }

    if (current_folder.empty() && cat == ShelfCategory::Collections) {
      std::unordered_map<std::string, BookItem> found;
      for (const auto &root : books_roots) {
        std::error_code ec;
        const std::filesystem::path root_path(root);
        if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
          if (ec) continue;
          const auto &entry = *it;
          if (!entry.is_regular_file(ec)) continue;
          const std::string ext = GetLowerExt(entry.path().string());
          if (ext != ".pdf" && ext != ".txt" && ext != ".epub") continue;
          BookItem item;
          item.name = entry.path().filename().string();
          item.path = entry.path().string();
          item.is_dir = false;
          found.emplace(NormalizePathKey(item.path), std::move(item));
        }
      }

      std::vector<BookItem> out;
      for (const auto &path_key : favorites_store.OrderedPaths()) {
        auto it = found.find(path_key);
        if (it != found.end()) out.push_back(it->second);
      }
      shelf_scan_cache[cache_key] = ShelfScanCacheEntry{out, cache_now};
      prune_shelf_scan_cache();
      return out;
    }

    if (current_folder.empty() && cat == ShelfCategory::History) {
      std::unordered_map<std::string, BookItem> found;
      for (const auto &root : books_roots) {
        std::error_code ec;
        const std::filesystem::path root_path(root);
        if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
          if (ec) continue;
          const auto &entry = *it;
          if (!entry.is_regular_file(ec)) continue;
          const std::string ext = GetLowerExt(entry.path().string());
          if (ext != ".pdf" && ext != ".txt" && ext != ".epub") continue;
          BookItem item;
          item.name = entry.path().filename().string();
          item.path = entry.path().string();
          item.is_dir = false;
          found.emplace(NormalizePathKey(item.path), std::move(item));
        }
      }

      std::vector<BookItem> out;
      for (const auto &path_key : history_store.OrderedPaths()) {
        auto it = found.find(path_key);
        if (it != found.end()) out.push_back(it->second);
      }
      shelf_scan_cache[cache_key] = ShelfScanCacheEntry{out, cache_now};
      prune_shelf_scan_cache();
      return out;
    }
    std::vector<BookItem> out =
        current_folder.empty() ? BookScanner::ScanRoots(books_roots) : BookScanner::ScanPath(current_folder, false);
    shelf_scan_cache[cache_key] = ShelfScanCacheEntry{out, cache_now};
    prune_shelf_scan_cache();
    return out;
  };

  auto rebuild_shelf_items = [&]() {
    const std::vector<BookItem> base = scan_base_items();
    shelf_items.clear();
    shelf_items.reserve(base.size());
    const ShelfCategory cat = current_category();
    for (const auto &item : base) {
      if (match_category(item, cat)) shelf_items.push_back(item);
    }
    ++shelf_content_version;
  };

  auto prune_cover_cache = [&]() {
    auto cover_cache_total_bytes = [&]() -> size_t {
      size_t total = 0;
      for (const auto &kv : cover_textures) total += kv.second.bytes;
      return total;
    };
    while (cover_textures.size() > kCoverCacheMaxEntries || cover_cache_total_bytes() > kCoverCacheMaxBytes) {
      auto oldest = cover_textures.end();
      for (auto it = cover_textures.begin(); it != cover_textures.end(); ++it) {
        if (oldest == cover_textures.end() || it->second.last_use < oldest->second.last_use) oldest = it;
      }
      if (oldest == cover_textures.end()) break;
      if (oldest->second.texture && oldest->second.owned) {
        forget_texture_size(oldest->second.texture);
        SDL_DestroyTexture(oldest->second.texture);
      }
      cover_textures.erase(oldest);
    }
  };

  auto clear_cover_cache = [&]() {
    for (auto &kv : cover_textures) {
      if (kv.second.texture && kv.second.owned) {
        forget_texture_size(kv.second.texture);
        SDL_DestroyTexture(kv.second.texture);
      }
    }
    cover_textures.clear();
    ++shelf_content_version;
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

  auto make_pdf_cover_cache_key = [&](const std::string &doc_path) {
    std::error_code ec;
    const uintmax_t file_size = std::filesystem::file_size(std::filesystem::path(doc_path), ec);
    const auto mtime_raw = std::filesystem::last_write_time(std::filesystem::path(doc_path), ec);
    const long long file_mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
    return NormalizePathKey(doc_path) + "|" + std::to_string(ec ? 0 : file_size) + "|" + std::to_string(file_mtime) +
           "|" + std::to_string(Layout().cover_w) + "x" + std::to_string(Layout().cover_h);
  };

  auto get_pdf_cover_cache_file = [&](const std::string &doc_path) -> std::filesystem::path {
    const std::string cache_key = make_pdf_cover_cache_key(doc_path);
    const size_t hash_value = std::hash<std::string>{}(cache_key);
    std::ostringstream oss;
    oss << std::hex << hash_value << ".bmp";
    return cover_thumb_cache_dir / oss.str();
  };

  auto load_cached_pdf_cover_texture = [&](const std::string &doc_path) -> SDL_Texture * {
    const std::filesystem::path cache_file = get_pdf_cover_cache_file(doc_path);
    std::error_code ec;
    if (!std::filesystem::exists(cache_file, ec) || ec) return nullptr;
    SDL_Surface *cover_surface = LoadSurfaceFromFile(cache_file.string());
    if (!cover_surface) return nullptr;
    SDL_Texture *normalized = CreateNormalizedCoverTexture(renderer, cover_surface);
    if (!normalized) normalized = CreateTextureFromSurface(renderer, cover_surface);
    SDL_FreeSurface(cover_surface);
    if (normalized) {
      remember_texture_size(normalized, Layout().cover_w, Layout().cover_h);
      return normalized;
    }
    return nullptr;
  };

  auto save_pdf_cover_cache_to_disk = [&](const std::string &doc_path, const std::vector<unsigned char> &cover_rgba) {
    if (cover_rgba.size() != static_cast<size_t>(Layout().cover_w * Layout().cover_h * 4)) return;
    std::error_code ec;
    std::filesystem::create_directories(cover_thumb_cache_dir, ec);
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char *>(cover_rgba.data()),
                                           Layout().cover_w,
                                           Layout().cover_h,
                                           32,
                                           Layout().cover_w * 4,
                                           SDL_PIXELFORMAT_RGBA32);
    if (!surface) return;
    const std::filesystem::path cache_file = get_pdf_cover_cache_file(doc_path);
    SDL_SaveBMP(surface, cache_file.string().c_str());
    SDL_FreeSurface(surface);
  };

  auto create_doc_first_page_cover_texture = [&](const std::string &doc_path) -> SDL_Texture * {
    if (SDL_Texture *cached = load_cached_pdf_cover_texture(doc_path)) {
      return cached;
    }

    PdfReader preview_pdf;
    const bool opened = preview_pdf.Open(doc_path);
    if (!opened) return nullptr;
    const bool has_real = preview_pdf.HasRealRenderer();
    if (!has_real) {
      preview_pdf.Close();
      return nullptr;
    }

    int page_w = 0;
    int page_h = 0;
    const bool sized = preview_pdf.PageSize(0, page_w, page_h);
    if (!sized || page_w <= 0 || page_h <= 0) {
      preview_pdf.Close();
      return nullptr;
    }

    // Render first page at a moderate resolution, then center-crop to 2:3 and
    // resample into the fixed cover texture size.
    const float desired_w = static_cast<float>(Layout().cover_w) * 1.6f;
    const float desired_h = static_cast<float>(Layout().cover_h) * 1.6f;
    const float scale_w = desired_w / static_cast<float>(std::max(1, page_w));
    const float scale_h = desired_h / static_cast<float>(std::max(1, page_h));
    const float render_scale = std::max(0.1f, std::max(scale_w, scale_h));

    std::vector<unsigned char> rgba;
    int src_w = 0;
    int src_h = 0;
    const bool rendered = preview_pdf.RenderPageRGBA(0, render_scale, rgba, src_w, src_h);
    if (!rendered || src_w <= 0 || src_h <= 0) {
      preview_pdf.Close();
      return nullptr;
    }
    preview_pdf.Close();

    SDL_Rect src_crop{0, 0, src_w, src_h};
    const float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
    if (src_aspect > kCoverAspect) {
      src_crop.w = std::max(1, static_cast<int>(std::round(static_cast<float>(src_h) * kCoverAspect)));
      src_crop.x = (src_w - src_crop.w) / 2;
    } else if (src_aspect < kCoverAspect) {
      src_crop.h = std::max(1, static_cast<int>(std::round(static_cast<float>(src_w) / kCoverAspect)));
      src_crop.y = (src_h - src_crop.h) / 2;
    }

    std::vector<unsigned char> cover_rgba(static_cast<size_t>(Layout().cover_w * Layout().cover_h * 4), 0);
    for (int dy = 0; dy < Layout().cover_h; ++dy) {
      const int sy = src_crop.y + ((dy * src_crop.h + (Layout().cover_h / 2)) / Layout().cover_h);
      const int sy_clamped = ClampInt(sy, 0, src_h - 1);
      for (int dx = 0; dx < Layout().cover_w; ++dx) {
        const int sx = src_crop.x + ((dx * src_crop.w + (Layout().cover_w / 2)) / Layout().cover_w);
        const int sx_clamped = ClampInt(sx, 0, src_w - 1);
        const size_t si = static_cast<size_t>((sy_clamped * src_w + sx_clamped) * 4);
        const size_t di = static_cast<size_t>((dy * Layout().cover_w + dx) * 4);
        cover_rgba[di + 0] = rgba[si + 0];
        cover_rgba[di + 1] = rgba[si + 1];
        cover_rgba[di + 2] = rgba[si + 2];
        cover_rgba[di + 3] = rgba[si + 3];
      }
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         Layout().cover_w,
                                         Layout().cover_h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    if (SDL_UpdateTexture(tex, nullptr, cover_rgba.data(), Layout().cover_w * 4) != 0) {
      SDL_DestroyTexture(tex);
      return nullptr;
    }
    save_pdf_cover_cache_to_disk(doc_path, cover_rgba);
    return tex;
  };

  auto make_epub_cover_cache_key = [&](const std::string &doc_path,
                                       uintmax_t logical_size,
                                       long long logical_mtime) {
    return NormalizePathKey(doc_path) + "|" + std::to_string(logical_size) + "|" + std::to_string(logical_mtime) +
           "|" + std::to_string(Layout().cover_w) + "x" + std::to_string(Layout().cover_h) + "|epub-cover-v1";
  };

  auto get_epub_cover_cache_file = [&](const std::string &doc_path,
                                       uintmax_t logical_size,
                                       long long logical_mtime) -> std::filesystem::path {
    const std::string cache_key = make_epub_cover_cache_key(doc_path, logical_size, logical_mtime);
    const size_t hash_value = std::hash<std::string>{}(cache_key);
    std::ostringstream oss;
    oss << std::hex << hash_value << ".bmp";
    return cover_thumb_cache_dir / oss.str();
  };

  auto load_cached_epub_cover_texture = [&](const std::string &doc_path,
                                            uintmax_t logical_size,
                                            long long logical_mtime) -> SDL_Texture * {
    const std::filesystem::path cache_file = get_epub_cover_cache_file(doc_path, logical_size, logical_mtime);
    std::error_code ec;
    if (!std::filesystem::exists(cache_file, ec) || ec) return nullptr;
    SDL_Surface *cover_surface = LoadSurfaceFromFile(cache_file.string());
    if (!cover_surface) return nullptr;
    SDL_Texture *normalized = CreateNormalizedCoverTexture(renderer, cover_surface);
    if (!normalized) normalized = CreateTextureFromSurface(renderer, cover_surface);
    SDL_FreeSurface(cover_surface);
    if (normalized) {
      remember_texture_size(normalized, Layout().cover_w, Layout().cover_h);
      return normalized;
    }
    return nullptr;
  };

  auto save_epub_cover_cache_to_disk = [&](const std::string &doc_path,
                                           uintmax_t logical_size,
                                           long long logical_mtime,
                                           SDL_Surface *cover_surface) {
    if (!cover_surface) return;
    std::error_code ec;
    std::filesystem::create_directories(cover_thumb_cache_dir, ec);
    const std::filesystem::path cache_file = get_epub_cover_cache_file(doc_path, logical_size, logical_mtime);
    SDL_SaveBMP(cover_surface, cache_file.string().c_str());
  };

  auto create_epub_first_image_cover_texture = [&](const std::string &doc_path) -> SDL_Texture * {
    EpubReader epub;
    EpubReader::CoverImage cover_image;
    std::string error;
    if (!epub.ExtractCoverImage(doc_path, cover_image, error)) return nullptr;

    if (SDL_Texture *cached =
            load_cached_epub_cover_texture(doc_path, cover_image.logical_size, cover_image.logical_mtime)) {
      return cached;
    }

    SDL_Surface *cover_surface = LoadSurfaceFromMemory(cover_image.bytes.data(), cover_image.bytes.size());
    if (!cover_surface) return nullptr;
    SDL_Texture *normalized = CreateNormalizedCoverTexture(renderer, cover_surface);
    if (!normalized) normalized = CreateTextureFromSurface(renderer, cover_surface);
    if (normalized) {
      remember_texture_size(normalized, Layout().cover_w, Layout().cover_h);
      save_epub_cover_cache_to_disk(doc_path, cover_image.logical_size, cover_image.logical_mtime, cover_surface);
    }
    SDL_FreeSurface(cover_surface);
    return normalized;
  };

  auto load_cover_from_path = [&](const std::string &cover_path) -> SDL_Texture * {
    if (cover_path.empty()) return nullptr;
    SDL_Surface *cover_surface = LoadSurfaceFromFile(cover_path);
    if (cover_surface) {
      SDL_Texture *normalized = CreateNormalizedCoverTexture(renderer, cover_surface);
      if (!normalized) normalized = CreateTextureFromSurface(renderer, cover_surface);
      SDL_FreeSurface(cover_surface);
      if (normalized) return normalized;
    }
    return nullptr;
  };

  auto has_manual_cover_exact_or_fuzzy = [&](const BookItem &item) -> bool {
    const std::string exact_cover_path =
        cover_resolver::ResolveCoverPathExact(item.path, item.is_dir, cover_roots);
    if (!exact_cover_path.empty()) return true;
    const std::string fuzzy_cover_path =
        cover_resolver::ResolveCoverPathFuzzy(item.path, item.is_dir, cover_roots);
    return !fuzzy_cover_path.empty();
  };

  auto has_cached_pdf_cover_on_disk = [&](const std::string &doc_path) -> bool {
    std::error_code ec;
    const std::filesystem::path cache_file = get_pdf_cover_cache_file(doc_path);
    return std::filesystem::exists(cache_file, ec) && !ec;
  };

  auto load_manual_cover_exact_then_fuzzy = [&](const BookItem &item) -> SDL_Texture * {
    const std::string exact_cover_path =
        cover_resolver::ResolveCoverPathExact(item.path, item.is_dir, cover_roots);
    if (!exact_cover_path.empty()) {
      SDL_Texture *tex = load_cover_from_path(exact_cover_path);
      if (tex) return tex;
    }
    const std::string fuzzy_cover_path =
        cover_resolver::ResolveCoverPathFuzzy(item.path, item.is_dir, cover_roots);
    if (!fuzzy_cover_path.empty()) {
      SDL_Texture *tex = load_cover_from_path(fuzzy_cover_path);
      if (tex) return tex;
    }
    return nullptr;
  };

  auto load_txt_cover = [&](const BookItem &item) -> SDL_Texture * {
    const bool txt_file = (!item.is_dir && GetLowerExt(item.path) == ".txt");
    const bool txt_folder = (item.is_dir && current_category() == ShelfCategory::AllBooks);
    if (!txt_file && !txt_folder) return nullptr;
    return ui_assets.book_cover_txt;
  };

  auto detect_comic_folder_type = [&](const std::string &folder_path) -> std::string {
    std::error_code ec;
    const std::filesystem::path root(folder_path);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return {};
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    bool has_pdf = false;
    bool has_epub = false;
    for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
      if (ec) continue;
      const auto &entry = *it;
      if (!entry.is_regular_file(ec)) continue;
      const std::string ext = GetLowerExt(entry.path().string());
      if (ext == ".pdf") has_pdf = true;
      else if (ext == ".epub") has_epub = true;
      else continue;
    }
    if (has_pdf) return ".pdf";
    if (has_epub) return ".epub";
    return {};
  };

  auto find_first_doc_in_folder = [&](const std::string &folder_path, const std::string &wanted_ext) -> std::string {
    std::error_code ec;
    const std::filesystem::path root(folder_path);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return {};
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    std::vector<std::string> matches;
    for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
      if (ec) continue;
      const auto &entry = *it;
      if (!entry.is_regular_file(ec)) continue;
      if (GetLowerExt(entry.path().string()) != wanted_ext) continue;
      matches.push_back(entry.path().string());
    }
    std::sort(matches.begin(), matches.end());
    return matches.empty() ? std::string{} : matches.front();
  };

  auto load_comic_cover = [&](const BookItem &item) -> SDL_Texture * {
    if (item.is_dir) {
      SDL_Texture *tex = load_manual_cover_exact_then_fuzzy(item);
      if (tex) return tex;
      const std::string kind = detect_comic_folder_type(item.path);
      if (kind == ".pdf") {
        const std::string pdf_path = find_first_doc_in_folder(item.path, ".pdf");
        if (!pdf_path.empty()) {
          tex = create_doc_first_page_cover_texture(pdf_path);
          if (tex) return tex;
        }
      } else if (kind == ".epub") {
        const std::string epub_path = find_first_doc_in_folder(item.path, ".epub");
        if (!epub_path.empty()) {
          tex = create_epub_first_image_cover_texture(epub_path);
          if (tex) return tex;
        }
      }
      if (kind == ".pdf") return ui_assets.book_cover_pdf ? ui_assets.book_cover_pdf : ui_assets.book_cover_txt;
      return ui_assets.book_cover_pdf ? ui_assets.book_cover_pdf : ui_assets.book_cover_txt;
    }
    const std::string ext = GetLowerExt(item.path);
    if (ext != ".pdf" && ext != ".epub") return nullptr;
    SDL_Texture *tex = load_manual_cover_exact_then_fuzzy(item);
    if (tex) return tex;
    if (ext == ".pdf") {
      tex = create_doc_first_page_cover_texture(item.path);
      if (tex) return tex;
    } else if (ext == ".epub") {
      tex = create_epub_first_image_cover_texture(item.path);
      if (tex) return tex;
    }
    return ui_assets.book_cover_pdf ? ui_assets.book_cover_pdf : ui_assets.book_cover_txt;
  };

  auto get_cover_texture = [&](const BookItem &item) -> SDL_Texture * {
    auto it = cover_textures.find(item.path);
    if (it != cover_textures.end()) {
      it->second.last_use = SDL_GetTicks();
      return it->second.texture;
    }

    SDL_Texture *tex = nullptr;
    const std::string ext = item.is_dir ? std::string{} : GetLowerExt(item.path);
    if ((item.is_dir && current_category() == ShelfCategory::AllComics) ||
        ext == ".pdf" || ext == ".epub") {
      tex = load_comic_cover(item);
    } else {
      tex = load_txt_cover(item);
      if (!tex) {
        tex = load_manual_cover_exact_then_fuzzy(item);
      }
    }

    const bool shared_ui_cover = (tex == ui_assets.book_cover_txt ||
                                  tex == ui_assets.book_cover_pdf);
    const bool owned = (tex != nullptr && !shared_ui_cover);
    int tw = 0;
    int th = 0;
    if (tex) get_texture_size(tex, tw, th);
    const size_t tex_bytes = (owned && tw > 0 && th > 0) ? (static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u) : 0u;
    cover_textures[item.path] = CoverCacheEntry{tex, tw, th, tex_bytes, SDL_GetTicks(), owned};
    prune_cover_cache();
    return tex;
  };

#ifdef HAVE_SDL2_TTF
  TTF_Font *ui_font = nullptr;
  TTF_Font *ui_font_title = nullptr;
  TTF_Font *ui_font_reader = nullptr;
  std::unordered_map<std::string, TextCacheEntry> text_cache;
  std::unordered_map<std::string, TitleEllipsisCacheEntry> title_ellipsize_cache;
  std::unordered_map<std::string, TxtLayoutCacheEntry> txt_layout_cache;
  bool ui_font_attempted = false;

  auto clear_text_cache = [&]() {
    for (auto &kv : text_cache) {
      if (kv.second.texture) {
        forget_texture_size(kv.second.texture);
        SDL_DestroyTexture(kv.second.texture);
      }
    }
    text_cache.clear();
    title_ellipsize_cache.clear();
  };

  auto make_txt_layout_cache_key = [&](const std::string &path, const SDL_Rect &bounds, int line_h,
                                       uintmax_t file_size, long long file_mtime) {
    return NormalizePathKey(path) + "|" + std::to_string(file_size) + "|" + std::to_string(file_mtime) + "|" +
           std::to_string(bounds.w) + "x" + std::to_string(bounds.h) + "|" + std::to_string(line_h);
  };

  auto prune_txt_layout_cache = [&]() {
    while (txt_layout_cache.size() > kTxtLayoutCacheMaxEntries) {
      auto oldest = txt_layout_cache.end();
      for (auto it = txt_layout_cache.begin(); it != txt_layout_cache.end(); ++it) {
        if (oldest == txt_layout_cache.end() || it->second.last_use < oldest->second.last_use) oldest = it;
      }
      if (oldest == txt_layout_cache.end()) break;
      txt_layout_cache.erase(oldest);
    }
  };

  auto get_txt_layout_cache_file = [&](const std::string &cache_key) -> std::filesystem::path {
    const size_t hash_value = std::hash<std::string>{}(cache_key);
    std::ostringstream oss;
    oss << std::hex << hash_value << ".bin";
    return txt_layout_cache_dir / oss.str();
  };

  auto get_txt_resume_cache_file = [&](const std::string &cache_key) -> std::filesystem::path {
    const size_t hash_value = std::hash<std::string>{}(cache_key);
    std::ostringstream oss;
    oss << std::hex << hash_value << ".resume.bin";
    return txt_layout_cache_dir / oss.str();
  };

  auto load_txt_layout_cache_from_disk = [&](const std::string &cache_key, TxtLayoutCacheEntry &entry) -> bool {
    std::ifstream in(get_txt_layout_cache_file(cache_key), std::ios::binary);
    if (!in) return false;
    auto read_u32 = [&](uint32_t &v) -> bool {
      return static_cast<bool>(in.read(reinterpret_cast<char *>(&v), sizeof(v)));
    };
    auto read_i32 = [&](int &v) -> bool {
      return static_cast<bool>(in.read(reinterpret_cast<char *>(&v), sizeof(v)));
    };
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t line_count = 0;
    uint32_t truncated_u32 = 0;
    uint32_t limit_hit_u32 = 0;
    if (!read_u32(magic) || !read_u32(version)) return false;
    if (magic != 0x54585443u || version != 2u) return false;
    if (!read_i32(entry.viewport_w) || !read_i32(entry.viewport_h) || !read_i32(entry.line_h) ||
        !read_i32(entry.content_h) || !read_u32(truncated_u32) || !read_u32(limit_hit_u32) ||
        !read_u32(line_count)) {
      return false;
    }
    entry.truncated = (truncated_u32 != 0);
    entry.limit_hit = (limit_hit_u32 != 0);
    entry.lines.clear();
    entry.lines.reserve(line_count);
    for (uint32_t i = 0; i < line_count; ++i) {
      uint32_t len = 0;
      if (!read_u32(len)) return false;
      std::string line(len, '\0');
      if (len > 0 && !in.read(line.data(), static_cast<std::streamsize>(len))) return false;
      entry.lines.push_back(std::move(line));
    }
    entry.last_use = SDL_GetTicks();
    return true;
  };

  auto save_txt_layout_cache_to_disk = [&](const std::string &cache_key, const TxtLayoutCacheEntry &entry) {
    std::error_code ec;
    std::filesystem::create_directories(txt_layout_cache_dir, ec);
    std::ofstream out(get_txt_layout_cache_file(cache_key), std::ios::binary | std::ios::trunc);
    if (!out) return;
    const uint32_t magic = 0x54585443u;
    const uint32_t version = 2u;
    const uint32_t truncated_u32 = entry.truncated ? 1u : 0u;
    const uint32_t limit_hit_u32 = entry.limit_hit ? 1u : 0u;
    const uint32_t line_count = static_cast<uint32_t>(std::min<size_t>(entry.lines.size(), 0xffffffffu));
    auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto write_i32 = [&](int v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    write_u32(magic);
    write_u32(version);
    write_i32(entry.viewport_w);
    write_i32(entry.viewport_h);
    write_i32(entry.line_h);
    write_i32(entry.content_h);
    write_u32(truncated_u32);
    write_u32(limit_hit_u32);
    write_u32(line_count);
    for (uint32_t i = 0; i < line_count; ++i) {
      const std::string &line = entry.lines[i];
      const uint32_t len = static_cast<uint32_t>(std::min<size_t>(line.size(), 0xffffffffu));
      write_u32(len);
      if (len > 0) out.write(line.data(), static_cast<std::streamsize>(len));
    }
  };

  auto load_txt_resume_cache_from_disk = [&](const std::string &cache_key, TxtResumeCacheEntry &entry) -> bool {
    std::ifstream in(get_txt_resume_cache_file(cache_key), std::ios::binary);
    if (!in) return false;
    auto read_u32 = [&](uint32_t &v) -> bool {
      return static_cast<bool>(in.read(reinterpret_cast<char *>(&v), sizeof(v)));
    };
    auto read_u64 = [&](uint64_t &v) -> bool {
      return static_cast<bool>(in.read(reinterpret_cast<char *>(&v), sizeof(v)));
    };
    auto read_i32 = [&](int &v) -> bool {
      return static_cast<bool>(in.read(reinterpret_cast<char *>(&v), sizeof(v)));
    };
    auto read_string = [&](std::string &s) -> bool {
      uint32_t len = 0;
      if (!read_u32(len)) return false;
      s.assign(len, '\0');
      return len == 0 || static_cast<bool>(in.read(s.data(), static_cast<std::streamsize>(len)));
    };
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t line_count = 0;
    uint32_t loading_u32 = 0;
    uint32_t truncated_u32 = 0;
    uint32_t limit_hit_u32 = 0;
    uint32_t trunc_notice_u32 = 0;
    uint64_t parse_pos_u64 = 0;
    if (!read_u32(magic) || !read_u32(version)) return false;
    if (magic != 0x54585253u || version != 1u) return false;
    if (!read_i32(entry.viewport_w) || !read_i32(entry.viewport_h) || !read_i32(entry.line_h) ||
        !read_i32(entry.content_h) || !read_i32(entry.scroll_px) || !read_i32(entry.target_scroll_px) ||
        !read_u64(parse_pos_u64) || !read_u32(loading_u32) || !read_u32(truncated_u32) ||
        !read_u32(limit_hit_u32) || !read_u32(trunc_notice_u32) || !read_u32(line_count)) {
      return false;
    }
    entry.parse_pos = static_cast<size_t>(parse_pos_u64);
    entry.loading = (loading_u32 != 0);
    entry.truncated = (truncated_u32 != 0);
    entry.limit_hit = (limit_hit_u32 != 0);
    entry.truncation_notice_added = (trunc_notice_u32 != 0);
    entry.lines.clear();
    entry.lines.reserve(line_count);
    for (uint32_t i = 0; i < line_count; ++i) {
      std::string line;
      if (!read_string(line)) return false;
      entry.lines.push_back(std::move(line));
    }
    if (!read_string(entry.pending_line) || !read_string(entry.pending_raw)) return false;
    return true;
  };

  auto save_txt_resume_cache_to_disk = [&](const std::string &cache_key, const TxtReaderState &state) {
    std::error_code ec;
    std::filesystem::create_directories(txt_layout_cache_dir, ec);
    std::ofstream out(get_txt_resume_cache_file(cache_key), std::ios::binary | std::ios::trunc);
    if (!out) return;
    const uint32_t magic = 0x54585253u;
    const uint32_t version = 1u;
    const uint32_t loading_u32 = state.loading ? 1u : 0u;
    const uint32_t truncated_u32 = state.truncated ? 1u : 0u;
    const uint32_t limit_hit_u32 = state.limit_hit ? 1u : 0u;
    const uint32_t trunc_notice_u32 = state.truncation_notice_added ? 1u : 0u;
    const uint32_t line_count = static_cast<uint32_t>(std::min<size_t>(state.lines.size(), 0xffffffffu));
    const uint64_t parse_pos_u64 = static_cast<uint64_t>(state.parse_pos);
    auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto write_u64 = [&](uint64_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto write_i32 = [&](int v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    auto write_string = [&](const std::string &s) {
      const uint32_t len = static_cast<uint32_t>(std::min<size_t>(s.size(), 0xffffffffu));
      write_u32(len);
      if (len > 0) out.write(s.data(), static_cast<std::streamsize>(len));
    };
    write_u32(magic);
    write_u32(version);
    write_i32(state.viewport_w);
    write_i32(state.viewport_h);
    write_i32(state.line_h);
    write_i32(state.content_h);
    write_i32(state.scroll_px);
    write_i32(state.target_scroll_px);
    write_u64(parse_pos_u64);
    write_u32(loading_u32);
    write_u32(truncated_u32);
    write_u32(limit_hit_u32);
    write_u32(trunc_notice_u32);
    write_u32(line_count);
    for (uint32_t i = 0; i < line_count; ++i) write_string(state.lines[i]);
    write_string(state.pending_line);
    write_string(state.pending_raw);
  };

  auto persist_current_txt_resume_snapshot = [&](const std::string &book_path, bool force) {
    if (book_path.empty()) return;
    if (reader_mode != ReaderMode::Txt || !txt_reader.open) return;
    if (!force && !txt_reader.resume_cache_dirty) return;
    const uint32_t now = SDL_GetTicks();
    if (!force && txt_reader.last_resume_cache_save != 0 && now - txt_reader.last_resume_cache_save < kTxtResumeSaveDelayMs) return;

    TxtReaderState snapshot = txt_reader;
    snapshot.scroll_px = txt_reader.scroll_px;
    snapshot.target_scroll_px = txt_reader.scroll_px;
    snapshot.resume_cache_dirty = false;
    snapshot.last_resume_cache_save = now;
    if (snapshot.cache_key.empty()) {
      const SDL_Rect bounds{snapshot.viewport_x, snapshot.viewport_y, snapshot.viewport_w, snapshot.viewport_h};
      std::error_code ec;
      const uintmax_t file_size = std::filesystem::file_size(std::filesystem::path(book_path), ec);
      const auto mtime_raw = std::filesystem::last_write_time(std::filesystem::path(book_path), ec);
      const long long file_mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
      snapshot.cache_key =
          make_txt_layout_cache_key(book_path, bounds, snapshot.line_h, ec ? 0 : file_size, file_mtime);
    }
    if (snapshot.cache_key.empty()) return;
    save_txt_resume_cache_to_disk(snapshot.cache_key, snapshot);
    txt_reader.last_resume_cache_save = now;
    txt_reader.resume_cache_dirty = false;
  };

  auto prune_text_cache = [&]() {
    while (text_cache.size() > kTextCacheMaxEntries) {
      auto oldest = text_cache.end();
      for (auto it = text_cache.begin(); it != text_cache.end(); ++it) {
        if (oldest == text_cache.end() || it->second.last_use < oldest->second.last_use) oldest = it;
      }
      if (oldest == text_cache.end()) break;
      if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
      text_cache.erase(oldest);
    }
  };

  auto get_title_ellipsized = [&](const std::string &raw_name, int text_area_w,
                                  const std::function<int(const std::string &)> &measure) -> std::string {
    if (raw_name.empty()) return raw_name;
    const std::string key = raw_name + "|" + std::to_string(text_area_w);
    auto it = title_ellipsize_cache.find(key);
    if (it != title_ellipsize_cache.end()) {
      it->second.last_use = SDL_GetTicks();
      return it->second.display;
    }
    std::string display = raw_name;
    if (measure(display) > text_area_w) {
      for (size_t max_chars = 24; max_chars >= 2; --max_chars) {
        display = Utf8Ellipsize(raw_name, max_chars);
        if (measure(display) <= text_area_w || max_chars == 2) break;
      }
    }
    title_ellipsize_cache[key] = TitleEllipsisCacheEntry{display, SDL_GetTicks()};
    if (title_ellipsize_cache.size() > 128) {
      auto oldest = title_ellipsize_cache.end();
      for (auto eit = title_ellipsize_cache.begin(); eit != title_ellipsize_cache.end(); ++eit) {
        if (oldest == title_ellipsize_cache.end() || eit->second.last_use < oldest->second.last_use) oldest = eit;
      }
      if (oldest != title_ellipsize_cache.end()) title_ellipsize_cache.erase(oldest);
    }
    return display;
  };

  auto open_ui_font = [&]() {
    if (ui_font_attempted) return;
    ui_font_attempted = true;
    if (ui_font) return;
    const std::vector<std::string> candidates = {
        // Project-owned font (preferred): native_h700/ui_font.ttf
        (exe_path / ".." / "ui_font.ttf").lexically_normal().string(),
        (std::filesystem::current_path() / "ui_font.ttf").lexically_normal().string(),
        "ui_font.ttf",
        (ui_path / "fonts" / "ui_font.ttf").string(),
        (ui_path / "fonts" / "ui_font.otf").string(),
        (exe_path / "fonts" / "ui_font.ttf").string(),
        (exe_path.parent_path() / "fonts" / "ui_font.ttf").string(),
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/arial.ttf",
        "/Roms/APPS/ROCreader/fonts/ui_font.ttf",
        "/mnt/mmc/ROCreader/fonts/ui_font.ttf",
        "/mnt/mmc/Roms/ROCreader/fonts/ui_font.ttf",
        "/mnt/mmc2/ROCreader/fonts/ui_font.ttf",
        "/mnt/mmc2/Roms/ROCreader/fonts/ui_font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    };
    for (const auto &p : candidates) {
      if (!std::filesystem::exists(p)) continue;
      ui_font = TTF_OpenFont(p.c_str(), 16);
      if (ui_font) {
        ui_font_title = TTF_OpenFont(p.c_str(), 24); // settings header title
        ui_font_reader = TTF_OpenFont(p.c_str(), kTxtFontPt);
        std::cout << "[native_h700] ui font: " << p;
        if (p.find("ui_font.ttf") != std::string::npos) {
          std::cout << " (project font)";
        }
        std::cout << "\n";
        break;
      }
    }
    if (!ui_font) {
      std::cerr << "[native_h700] warning: ui font not found, title text disabled\n";
    }
  };

  auto make_text_key = [&](const std::string &text, SDL_Color color) {
    return text + "|" + std::to_string(static_cast<int>(color.r)) + "," +
           std::to_string(static_cast<int>(color.g)) + "," + std::to_string(static_cast<int>(color.b));
  };

  auto get_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    if (!ui_font || text.empty()) return nullptr;
    const std::string key = make_text_key(text, color);
    auto it = text_cache.find(key);
    if (it != text_cache.end()) {
      it->second.last_use = SDL_GetTicks();
      return &it->second;
    }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(ui_font, text.c_str(), color);
    if (!surf) return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    const int w = surf->w;
    const int h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    TextCacheEntry entry;
    entry.texture = tex;
    entry.w = w;
    entry.h = h;
    entry.last_use = SDL_GetTicks();
    auto [ins_it, _] = text_cache.emplace(key, entry);
    prune_text_cache();
    return &ins_it->second;
  };

  auto get_title_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    if (!ui_font_title || text.empty()) return nullptr;
    const std::string key = "t24|" + make_text_key(text, color);
    auto it = text_cache.find(key);
    if (it != text_cache.end()) {
      it->second.last_use = SDL_GetTicks();
      return &it->second;
    }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(ui_font_title, text.c_str(), color);
    if (!surf) return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    const int w = surf->w;
    const int h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    TextCacheEntry entry;
    entry.texture = tex;
    entry.w = w;
    entry.h = h;
    entry.last_use = SDL_GetTicks();
    auto [ins_it, _] = text_cache.emplace(key, entry);
    prune_text_cache();
    return &ins_it->second;
  };

  auto get_reader_text_texture = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    open_ui_font();
    if (!ui_font_reader || text.empty()) return nullptr;
    const std::string key = "r|" + make_text_key(text, color);
    auto it = text_cache.find(key);
    if (it != text_cache.end()) {
      it->second.last_use = SDL_GetTicks();
      return &it->second;
    }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(ui_font_reader, text.c_str(), color);
    if (!surf) return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    const int w = surf->w;
    const int h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    TextCacheEntry entry;
    entry.texture = tex;
    entry.w = w;
    entry.h = h;
    entry.last_use = SDL_GetTicks();
    auto [ins_it, _] = text_cache.emplace(key, entry);
    prune_text_cache();
    return &ins_it->second;
  };

  auto shelf_title_text = [&](const BookItem &item) -> std::string {
    if (item.is_dir) return item.name;
    try {
      const std::string stem = std::filesystem::path(item.path).stem().string();
      if (!stem.empty()) return stem;
    } catch (...) {
    }
    return item.name;
  };

  auto focused_title_needs_marquee = [&]() -> bool {
    if (state != State::Shelf) return false;
    if (focus_index < 0 || focus_index >= static_cast<int>(shelf_items.size())) return false;
    SDL_Color title_color{248, 250, 255, 255};
    const std::string display = shelf_title_text(shelf_items[focus_index]);
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
#ifdef HAVE_SDL2_TTF
    txt_layout_cache.clear();
    clear_text_cache();
#endif
    clear_directory_files(txt_layout_cache_dir);
    std::cout << "[native_h700] runtime caches cleared: cover thumbs + txt layouts/resume\n";
  };

  auto collect_scanned_txt_files = [&]() {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto &root : books_roots) {
      std::error_code ec;
      if (!std::filesystem::exists(root, ec) || ec) continue;
      const auto opts = std::filesystem::directory_options::skip_permission_denied;
      for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) {
          ec.clear();
          continue;
        }
        if (!it->is_regular_file(ec) || ec) {
          ec.clear();
          continue;
        }
        const std::string path = it->path().string();
        if (GetLowerExt(path) != ".txt") continue;
        const std::string key = NormalizePathKey(path);
        if (seen.insert(key).second) out.push_back(path);
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  };

  auto start_txt_transcode_job = [&]() {
    if (txt_transcode_job.active) return;
    txt_transcode_job = TxtTranscodeJob{};
    txt_transcode_job.files = collect_scanned_txt_files();
    txt_transcode_job.total = txt_transcode_job.files.size();
    txt_transcode_job.active = txt_transcode_job.total > 0;
    txt_transcode_job.current_file.clear();
    std::cout << "[native_h700] txt transcode queued: files=" << txt_transcode_job.total << "\n";
  };

  auto process_txt_transcode_step = [&]() {
    if (!txt_transcode_job.active) return;
    if (txt_transcode_job.processed >= txt_transcode_job.total) {
      txt_transcode_job.active = false;
      txt_transcode_job.current_file.clear();
      clear_runtime_cache_files();
      std::cout << "[native_h700] txt transcode finished: processed=" << txt_transcode_job.processed
                << " converted=" << txt_transcode_job.converted
                << " failed=" << txt_transcode_job.failed << "\n";
      return;
    }

    const size_t idx = txt_transcode_job.processed;
    const std::filesystem::path file_path(txt_transcode_job.files[idx]);
    txt_transcode_job.current_file = file_path.filename().string();

    std::string raw;
    std::string utf8;
    std::string detected_encoding;
    bool success = ReadFileBytes(file_path, raw) && DecodeTextBytesToUtf8(raw, utf8, &detected_encoding);
    bool converted = false;
    if (success) {
      if (utf8 != raw) {
        success = WriteFileBytesAtomically(file_path, utf8);
        converted = success;
      }
    }
    if (!success) {
      ++txt_transcode_job.failed;
      std::cout << "[native_h700] txt transcode failed: " << file_path.string() << "\n";
    } else if (converted) {
      ++txt_transcode_job.converted;
      std::cout << "[native_h700] txt transcoded: " << file_path.string()
                << " encoding=" << detected_encoding << "\n";
    }
    ++txt_transcode_job.processed;
    if (txt_transcode_job.processed >= txt_transcode_job.total) {
      txt_transcode_job.active = false;
      txt_transcode_job.current_file.clear();
      clear_runtime_cache_files();
      std::cout << "[native_h700] txt transcode finished: processed=" << txt_transcode_job.processed
                << " converted=" << txt_transcode_job.converted
                << " failed=" << txt_transcode_job.failed << "\n";
    }
  };

  auto destroy_render_cache = [&](ReaderRenderCache &cache) {
    if (cache.texture) {
      bool pooled = false;
      for (auto &slot : reader_texture_pool) {
        if (slot.texture == cache.texture) {
          slot.in_use = false;
          slot.last_use = SDL_GetTicks();
          pooled = true;
          break;
        }
      }
      if (!pooled) {
        forget_texture_size(cache.texture);
        SDL_DestroyTexture(cache.texture);
      }
    }
    cache = ReaderRenderCache{};
  };

  auto acquire_reader_texture = [&](int w, int h) -> SDL_Texture * {
    const uint32_t now = SDL_GetTicks();
    for (auto &slot : reader_texture_pool) {
      if (slot.texture && !slot.in_use && slot.w == w && slot.h == h) {
        slot.in_use = true;
        slot.last_use = now;
        return slot.texture;
      }
    }

    for (auto &slot : reader_texture_pool) {
      if (!slot.texture) {
        SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) return nullptr;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        remember_texture_size(tex, w, h);
        slot.texture = tex;
        slot.w = w;
        slot.h = h;
        slot.in_use = true;
        slot.last_use = now;
        return tex;
      }
    }

    ReaderTexturePoolEntry *evict = nullptr;
    for (auto &slot : reader_texture_pool) {
      if (slot.in_use) continue;
      if (!evict || slot.last_use < evict->last_use) evict = &slot;
    }
    if (!evict) return nullptr;
    if (evict->texture) {
      forget_texture_size(evict->texture);
      SDL_DestroyTexture(evict->texture);
    }
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) {
      evict->texture = nullptr;
      evict->w = 0;
      evict->h = 0;
      evict->in_use = false;
      evict->last_use = 0;
      return nullptr;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    remember_texture_size(tex, w, h);
    evict->texture = tex;
    evict->w = w;
    evict->h = h;
    evict->in_use = true;
    evict->last_use = now;
    return tex;
  };

  auto destroy_shelf_render_cache = [&]() {
    if (shelf_render_cache.texture) {
      forget_texture_size(shelf_render_cache.texture);
      SDL_DestroyTexture(shelf_render_cache.texture);
    }
    shelf_render_cache = ShelfRenderCache{};
  };

  auto invalidate_shelf_render_cache = [&]() {
    destroy_shelf_render_cache();
  };

  auto invalidate_all_render_cache = [&]() {
    destroy_render_cache(render_cache);
    destroy_render_cache(secondary_render_cache);
    destroy_render_cache(tertiary_render_cache);
  };

  auto reader_has_real_renderer = [&]() -> bool {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.HasRealRenderer();
    if (reader_mode == ReaderMode::Epub) return epub_comic.HasRealRenderer();
    return false;
  };

  auto reader_is_open = [&]() -> bool {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.IsOpen();
    if (reader_mode == ReaderMode::Epub) return epub_comic.IsOpen();
    return false;
  };

  auto reader_page_count = [&]() -> int {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.PageCount();
    if (reader_mode == ReaderMode::Epub) return epub_comic.PageCount();
    return 0;
  };

  auto reader_current_page = [&]() -> int {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.CurrentPage();
    if (reader_mode == ReaderMode::Epub) return epub_comic.CurrentPage();
    return 0;
  };

  auto reader_set_page = [&](int page_index) {
    if (reader_mode == ReaderMode::Pdf) return;
    if (reader_mode == ReaderMode::Epub) epub_comic.SetPage(page_index);
  };

  auto reader_current_page_size = [&](int &w, int &h) -> bool {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.PageSize(pdf_runtime.CurrentPage(), w, h);
    if (reader_mode == ReaderMode::Epub) return epub_comic.CurrentPageSize(w, h);
    return false;
  };

  auto reader_page_size = [&](int page_index, int &w, int &h) -> bool {
    if (reader_mode == ReaderMode::Pdf) return pdf_runtime.PageSize(page_index, w, h);
    if (reader_mode == ReaderMode::Epub) return epub_comic.PageSize(page_index, w, h);
    return false;
  };

  struct ReaderAsyncWorkerCtx {
    SDL_mutex *mutex = nullptr;
    SDL_cond *cond = nullptr;
    bool *running = nullptr;
    ReaderAsyncRenderJob *requested = nullptr;
    ReaderAsyncRenderJob *inflight = nullptr;
    ReaderAsyncRenderResult *result = nullptr;
    uint64_t *latest_target_serial = nullptr;
    std::atomic<bool> *cancel_requested = nullptr;
    Uint32 event_type = 0;
  };

  auto reset_reader_async_state = [&]() {
    SDL_LockMutex(reader_async_mutex);
    reader_async_requested_job = ReaderAsyncRenderJob{};
    reader_async_inflight_job = ReaderAsyncRenderJob{};
    reader_async_result = ReaderAsyncRenderResult{};
    reader_async_latest_target_serial = 0;
    reader_async_cancel_requested.store(false);
    SDL_UnlockMutex(reader_async_mutex);
    ready_state_valid = false;
  };

  const auto reader_async_worker_main = [](void *userdata) -> int {
    auto *ctx = static_cast<ReaderAsyncWorkerCtx *>(userdata);
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    EpubComicReader worker_epub;
    ReaderMode open_mode = ReaderMode::None;
    std::string open_path;

    for (;;) {
      SDL_LockMutex(ctx->mutex);
      while (*ctx->running && (!ctx->requested->active || ctx->result->ready)) {
        SDL_CondWait(ctx->cond, ctx->mutex);
      }
      if (!*ctx->running) {
        SDL_UnlockMutex(ctx->mutex);
        break;
      }
      ReaderAsyncRenderJob job = *ctx->requested;
      ctx->requested->active = false;
      *ctx->inflight = job;
      if (!job.prefetch && ctx->cancel_requested) {
        ctx->cancel_requested->store(false);
      }
      SDL_UnlockMutex(ctx->mutex);

      bool success = false;
      int src_w = 0;
      int src_h = 0;
      std::vector<unsigned char> rgba;

      if (job.mode == ReaderMode::Epub) {
        if (open_mode != ReaderMode::Epub || open_path != job.path) {
          worker_epub.Close();
          open_mode = ReaderMode::None;
          open_path.clear();
          if (worker_epub.Open(job.path)) {
            open_mode = ReaderMode::Epub;
            open_path = job.path;
          }
        }
        if (open_mode == ReaderMode::Epub) {
          success = worker_epub.RenderPageRGBA(job.page, job.target_scale, rgba, src_w, src_h, ctx->cancel_requested);
        }
      }

      SDL_LockMutex(ctx->mutex);
      const bool requested_differs =
          ctx->requested->active &&
          (ctx->requested->mode != job.mode ||
           ctx->requested->path != job.path ||
           ctx->requested->page != job.page ||
           ctx->requested->rotation != job.rotation ||
           std::abs(ctx->requested->target_scale - job.target_scale) >= 0.0005f);
      const bool stale_job =
          requested_differs ||
          (!job.prefetch && job.serial != *ctx->latest_target_serial) ||
          (job.prefetch && job.serial < *ctx->latest_target_serial);
      if (*ctx->running && !stale_job) {
        ReaderAsyncRenderResult next_result;
        next_result.ready = true;
        next_result.success = success;
        next_result.mode = job.mode;
        next_result.path = job.path;
        next_result.state = job.state;
        next_result.page = job.page;
        next_result.target_scale = job.target_scale;
        next_result.rotation = job.rotation;
        next_result.display_w = job.display_w;
        next_result.display_h = job.display_h;
        next_result.src_w = src_w;
        next_result.src_h = src_h;
        next_result.rgba = std::move(rgba);
        next_result.serial = job.serial;
        *ctx->result = std::move(next_result);
      }
      *ctx->inflight = ReaderAsyncRenderJob{};
      SDL_UnlockMutex(ctx->mutex);

      if (!stale_job && ctx->event_type != static_cast<Uint32>(-1)) {
        SDL_Event ready_event{};
        ready_event.type = ctx->event_type;
        SDL_PushEvent(&ready_event);
      }
    }

    worker_epub.Close();
    return 0;
  };

  ReaderAsyncWorkerCtx reader_async_worker_ctx{
      reader_async_mutex,
      reader_async_cond,
      &reader_async_worker_running,
      &reader_async_requested_job,
      &reader_async_inflight_job,
      &reader_async_result,
      &reader_async_latest_target_serial,
      &reader_async_cancel_requested,
      reader_async_event_type,
  };
  SDL_Thread *reader_async_thread =
      SDL_CreateThread(reader_async_worker_main, "reader_async", &reader_async_worker_ctx);

  std::function<float(const ReaderViewState &)> reader_target_scale_for_state;

  auto promote_async_render_result = [&]() {
    ReaderAsyncRenderResult ready_result;
    bool has_result = false;
    SDL_LockMutex(reader_async_mutex);
    if (reader_async_result.ready) {
      ready_result = std::move(reader_async_result);
      reader_async_result = ReaderAsyncRenderResult{};
      has_result = true;
      SDL_CondSignal(reader_async_cond);
    }
    SDL_UnlockMutex(reader_async_mutex);
    if (!has_result) return false;
    if (!ready_result.success) return false;
    if (ready_result.mode != reader_mode || ready_result.path != current_book) return false;
    if (!reader_is_open()) return false;
    const auto result_neighbor_cache_slot = [&](int page) -> ReaderRenderCache * {
      return (page < target_state.page) ? &secondary_render_cache : &tertiary_render_cache;
    };
    const int focus_page = target_state.page;
    const bool is_target_page = ready_result.page == focus_page;
    const bool is_neighbor_page = ready_result.page == focus_page - 1 || ready_result.page == focus_page + 1;
    if (!is_target_page && !is_neighbor_page && (!display_state_valid || ready_result.page != display_state.page)) return false;
    if (ready_result.rotation != target_state.rotation) return false;

    const float target_scale = reader_target_scale_for_state(target_state);
    if (std::abs(ready_result.target_scale - target_scale) >= 0.0005f) return false;

    ReaderViewState result_state;
    result_state.page = ready_result.page;
    result_state.rotation = ready_result.rotation;
    if (ready_result.page == target_state.page && ready_result.rotation == target_state.rotation) {
      result_state.zoom = target_state.zoom;
    } else if (display_state_valid &&
               ready_result.page == display_state.page &&
               ready_result.rotation == display_state.rotation) {
      result_state.zoom = display_state.zoom;
    } else {
      result_state.zoom = target_state.zoom;
    }
    if (result_state == target_state) {
      ready_state = result_state;
      ready_state_valid = true;
    }

    int rw = ready_result.src_w;
    int rh = ready_result.src_h;
    std::vector<unsigned char> rot;
    const std::vector<unsigned char> &rgba = ready_result.rgba;
    if (ready_result.rotation == 90 || ready_result.rotation == 270) {
      rw = ready_result.src_h;
      rh = ready_result.src_w;
      rot.assign(static_cast<size_t>(rw * rh * 4), 0);
      for (int y = 0; y < ready_result.src_h; ++y) {
        for (int x = 0; x < ready_result.src_w; ++x) {
          const int src = (y * ready_result.src_w + x) * 4;
          int dx = 0, dy = 0;
          if (ready_result.rotation == 90) { dx = ready_result.src_h - 1 - y; dy = x; }
          else { dx = y; dy = ready_result.src_w - 1 - x; }
          const int dst = (dy * rw + dx) * 4;
          rot[dst + 0] = rgba[src + 0];
          rot[dst + 1] = rgba[src + 1];
          rot[dst + 2] = rgba[src + 2];
          rot[dst + 3] = rgba[src + 3];
        }
      }
    } else if (ready_result.rotation == 180) {
      rot.assign(static_cast<size_t>(rw * rh * 4), 0);
      for (int y = 0; y < ready_result.src_h; ++y) {
        for (int x = 0; x < ready_result.src_w; ++x) {
          const int src = (y * ready_result.src_w + x) * 4;
          const int dx = ready_result.src_w - 1 - x;
          const int dy = ready_result.src_h - 1 - y;
          const int dst = (dy * rw + dx) * 4;
          rot[dst + 0] = rgba[src + 0];
          rot[dst + 1] = rgba[src + 1];
          rot[dst + 2] = rgba[src + 2];
          rot[dst + 3] = rgba[src + 3];
        }
      }
    }

    const unsigned char *pixels = (ready_result.rotation == 0) ? rgba.data() : rot.data();
    SDL_Texture *tex = acquire_reader_texture(rw, rh);
    if (!tex) return false;
    if (SDL_UpdateTexture(tex, nullptr, pixels, rw * 4) != 0) {
      for (auto &slot : reader_texture_pool) {
        if (slot.texture == tex) {
          slot.in_use = false;
          slot.last_use = SDL_GetTicks();
          break;
        }
      }
      return false;
    }
    const uint32_t stamp = SDL_GetTicks();
    ReaderRenderCache fresh_cache;
    fresh_cache.texture = tex;
    fresh_cache.page = ready_result.page;
    fresh_cache.rotation = ready_result.rotation;
    fresh_cache.scale = ready_result.target_scale;
    fresh_cache.quality = ReaderRenderQuality::Full;
    fresh_cache.w = rw;
    fresh_cache.h = rh;
    fresh_cache.display_w = ready_result.display_w;
    fresh_cache.display_h = ready_result.display_h;
    fresh_cache.last_use = stamp;

    if (display_state_valid && result_state == display_state &&
        std::abs(ready_result.target_scale - render_cache.scale) < 0.0005f) {
      destroy_render_cache(render_cache);
      render_cache = fresh_cache;
    } else {
      ReaderRenderCache *target_cache = result_neighbor_cache_slot(ready_result.page);
      destroy_render_cache(*target_cache);
      *target_cache = fresh_cache;
    }
    return true;
  };

  auto request_reader_async_render = [&](int page, float target_scale, int display_w, int display_h, bool prefetch) -> bool {
    ReaderAsyncRenderJob next_job;
    next_job.active = true;
    next_job.prefetch = prefetch;
    next_job.mode = reader_mode;
    next_job.path = current_book;
    next_job.state = target_state;
    next_job.page = page;
    next_job.target_scale = target_scale;
    next_job.rotation = target_state.rotation;
    next_job.display_w = display_w;
    next_job.display_h = display_h;
    next_job.serial = ++reader_async_job_serial;
    if (next_job.serial == 0) next_job.serial = ++reader_async_job_serial;

    SDL_LockMutex(reader_async_mutex);
    if (!prefetch) {
      reader_async_latest_target_serial = next_job.serial;
      reader_async_cancel_requested.store(true);
    }
    const bool inflight_same =
        reader_async_inflight_job.active &&
        reader_async_inflight_job.mode == next_job.mode &&
        reader_async_inflight_job.path == next_job.path &&
        reader_async_inflight_job.page == next_job.page &&
        reader_async_inflight_job.rotation == next_job.rotation &&
        std::abs(reader_async_inflight_job.target_scale - next_job.target_scale) < 0.0005f;
    const bool requested_same =
        reader_async_requested_job.active &&
        reader_async_requested_job.mode == next_job.mode &&
        reader_async_requested_job.path == next_job.path &&
        reader_async_requested_job.page == next_job.page &&
        reader_async_requested_job.rotation == next_job.rotation &&
        std::abs(reader_async_requested_job.target_scale - next_job.target_scale) < 0.0005f;
    const bool busy_with_target =
        reader_async_requested_job.active || (reader_async_inflight_job.active && !reader_async_inflight_job.prefetch);
    const bool keep_requested = next_job.prefetch && reader_async_requested_job.active;
    const bool allow_prefetch =
        (prefetch &&
        display_state_valid &&
        display_state == target_state &&
        !ready_state_valid &&
        !busy_with_target &&
        !reader_async_inflight_job.active);
    const bool allow_request = prefetch ? allow_prefetch : true;
    bool accepted = false;
    if (allow_request && !inflight_same && !requested_same && !keep_requested) {
      reader_async_requested_job = std::move(next_job);
      SDL_CondSignal(reader_async_cond);
      accepted = true;
    }
    SDL_UnlockMutex(reader_async_mutex);
    return accepted;
  };

  auto reader_page_size_cached = [&](int page_index, int &w, int &h) -> bool {
    auto it = reader_page_size_cache.find(page_index);
    if (it != reader_page_size_cache.end() && it->second.x > 0 && it->second.y > 0) {
      w = it->second.x;
      h = it->second.y;
      return true;
    }
    if (!reader_page_size(page_index, w, h) || w <= 0 || h <= 0) return false;
    reader_page_size_cache[page_index] = SDL_Point{w, h};
    return true;
  };

  auto visible_reader_render_cache = [&]() -> const ReaderRenderCache * {
    if (render_cache.texture &&
        display_state_valid &&
        render_cache.page == display_state.page &&
        render_cache.rotation == display_state.rotation) {
      return &render_cache;
    }
    return nullptr;
  };

  auto matching_reader_render_cache = [&](int page, int rotation, float target_scale, ReaderRenderQuality quality)
      -> ReaderRenderCache * {
    ReaderRenderCache *caches[3] = {&render_cache, &secondary_render_cache, &tertiary_render_cache};
    for (ReaderRenderCache *cache : caches) {
      if (!cache->texture) continue;
      if (cache->page != page || cache->rotation != rotation || cache->quality != quality) continue;
      if (std::abs(cache->scale - target_scale) >= 0.0005f) continue;
      return cache;
    }
    return nullptr;
  };

  auto visible_reader_render_cache_for_page = [&](int page, int rotation, float target_scale) -> const ReaderRenderCache * {
    if (render_cache.texture &&
        display_state_valid &&
        render_cache.page == page &&
        render_cache.rotation == rotation &&
        std::abs(render_cache.scale - target_scale) < 0.0005f) {
      return &render_cache;
    }
    return nullptr;
  };

  auto neighbor_cache_slot_for_page = [&](int page) -> ReaderRenderCache * {
    return (page < target_state.page) ? &secondary_render_cache : &tertiary_render_cache;
  };

  auto effective_display_size = [&](int page, int rotation, float target_scale, int &out_w, int &out_h) -> bool {
    int pw = 0, ph = 0;
    if (!reader_page_size_cached(page, pw, ph)) return false;
    const int base_w = std::max(1, static_cast<int>(std::round(static_cast<float>(pw) * target_scale)));
    const int base_h = std::max(1, static_cast<int>(std::round(static_cast<float>(ph) * target_scale)));
    if (rotation == 90 || rotation == 270) {
      out_w = base_h;
      out_h = base_w;
    } else {
      out_w = base_w;
      out_h = base_h;
    }
    return true;
  };

  reader_target_scale_for_state = [&](const ReaderViewState &state) -> float {
    int pw = 0;
    int ph = 0;
    if (!reader_page_size_cached(state.page, pw, ph) || pw <= 0 || ph <= 0) {
      return std::max(0.1f, std::min(6.0f, state.zoom));
    }
    float auto_scale = 1.0f;
    if (state.rotation == 0 || state.rotation == 180) {
      auto_scale = std::max(0.1f, static_cast<float>(Layout().screen_w) / static_cast<float>(pw));
    } else {
      const float fit_h = static_cast<float>(Layout().screen_h) / static_cast<float>(pw);
      const float need_overflow = static_cast<float>(Layout().screen_w + 200) / static_cast<float>(ph);
      auto_scale = std::max(0.1f, std::max(fit_h, need_overflow));
    }
    return std::max(0.1f, std::min(6.0f, auto_scale * state.zoom));
  };

  auto effective_display_size_for_state = [&](const ReaderViewState &state, int &out_w, int &out_h) -> bool {
    const float target_scale = reader_target_scale_for_state(state);
    return effective_display_size(state.page, state.rotation, target_scale, out_w, out_h);
  };

  auto reader_page_render_mode_for_state = [&](const ReaderViewState &state) -> ReaderPageRenderMode {
    ReaderPageRenderMode mode;
    if (!reader_is_open()) return mode;
    const float target_scale = reader_target_scale_for_state(state);
    if (!effective_display_size(state.page, state.rotation, target_scale, mode.display_w, mode.display_h)) {
      mode.display_w = Layout().screen_w;
      mode.display_h = Layout().screen_h;
    }
    return mode;
  };

  auto prune_reader_neighbor_caches = [&](int center_page, int rotation, float target_scale) {
    ReaderRenderCache *neighbors[2] = {&secondary_render_cache, &tertiary_render_cache};
    for (ReaderRenderCache *cache : neighbors) {
      if (!cache->texture) continue;
      const bool keep_page =
          (cache->page == center_page) ||
          (cache->page == center_page - 1) ||
          (cache->page == center_page + 1);
      const bool keep_rotation = cache->rotation == rotation;
      const bool keep_scale = std::abs(cache->scale - target_scale) < 0.0005f;
      const bool keep_quality = cache->quality == ReaderRenderQuality::Full;
      if (!keep_page || !keep_rotation || !keep_scale || !keep_quality) {
        destroy_render_cache(*cache);
      }
    }
  };

  auto current_reader_axis_sign = [&]() {
    if (target_state.rotation == 0) return std::pair<int, int>{1, 1};   // axis 1=y, sign +1
    if (target_state.rotation == 90) return std::pair<int, int>{0, -1}; // axis 0=x, sign -1
    if (target_state.rotation == 180) return std::pair<int, int>{1, -1};
    return std::pair<int, int>{0, 1};
  };

  auto current_reader_display_size = [&](int &out_w, int &out_h) -> bool {
    if (!reader_is_open()) return false;
    if (effective_display_size_for_state(target_state, out_w, out_h)) return true;
    if (display_state_valid) return effective_display_size_for_state(display_state, out_w, out_h);
    return false;
  };

  auto promote_ready_target_to_display = [&](float target_scale, ReaderRenderQuality quality) -> bool {
    if (!ready_state_valid || ready_state != target_state) return false;
    ReaderRenderCache *cached =
        matching_reader_render_cache(target_state.page, target_state.rotation, target_scale, quality);
    if (!cached) return false;
    if (cached != &render_cache) {
      ReaderRenderCache previous_display = render_cache;
      render_cache = *cached;
      *cached = previous_display;
    }
    render_cache.last_use = SDL_GetTicks();
    display_state = target_state;
    display_state_valid = true;
    ready_state_valid = false;
    return true;
  };

  auto ensure_render = [&]() {
    if (!reader_is_open()) return false;
    promote_async_render_result();
    const int page = target_state.page;
    const float target_scale = reader_target_scale_for_state(target_state);
    int display_w = 0;
    int display_h = 0;
    if (!effective_display_size(page, target_state.rotation, target_scale, display_w, display_h)) {
      display_w = Layout().screen_w;
      display_h = Layout().screen_h;
    }
    constexpr ReaderRenderQuality quality = ReaderRenderQuality::Full;
    prune_reader_neighbor_caches(page, target_state.rotation, target_scale);
    const bool display_matches_target =
        display_state_valid &&
        display_state.page == target_state.page &&
        display_state.rotation == target_state.rotation &&
        std::abs(display_state.zoom - target_state.zoom) < 0.0005f &&
        render_cache.texture &&
        render_cache.page == page &&
        render_cache.rotation == target_state.rotation &&
        render_cache.quality == quality &&
        std::abs(render_cache.scale - target_scale) < 0.0005f;

    if (!display_matches_target) {
      if (!promote_ready_target_to_display(target_scale, quality)) {
        request_reader_async_render(page, target_scale, display_w, display_h, false);
        return display_state_valid && render_cache.texture;
      }
    } else {
      render_cache.last_use = SDL_GetTicks();
    }

    if (!display_state_valid) {
      if (!promote_ready_target_to_display(target_scale, quality)) {
        request_reader_async_render(page, target_scale, display_w, display_h, false);
        return false;
      }
    }

    if (adaptive_render.pending_page_active) return true;

    const int page_count = std::max(1, reader_page_count());
    const int next_page = page + 1;
    const int prev_page = page - 1;
    if (next_page < page_count &&
        !matching_reader_render_cache(next_page, target_state.rotation, target_scale, quality)) {
      int next_display_w = 0;
      int next_display_h = 0;
      if (effective_display_size(next_page, target_state.rotation, target_scale, next_display_w, next_display_h)) {
        request_reader_async_render(next_page, target_scale, next_display_w, next_display_h, true);
      }
    } else if (prev_page >= 0 &&
               !matching_reader_render_cache(prev_page, target_state.rotation, target_scale, quality)) {
      int prev_display_w = 0;
      int prev_display_h = 0;
      if (effective_display_size(prev_page, target_state.rotation, target_scale, prev_display_w, prev_display_h)) {
        request_reader_async_render(prev_page, target_scale, prev_display_w, prev_display_h, true);
      }
    }
    return true;
  };

  auto clamp_scroll = [&]() {
    int display_w = 0;
    int display_h = 0;
    if (!current_reader_display_size(display_w, display_h)) {
      if (const ReaderRenderCache *cache = visible_reader_render_cache()) {
        display_w = cache->display_w;
        display_h = cache->display_h;
      } else {
        reader.scroll_x = reader.scroll_y = 0;
        return;
      }
    }
    const int max_x = std::max(0, display_w - Layout().screen_w);
    const int max_y = std::max(0, display_h - Layout().screen_h);
    reader.scroll_x = ClampInt(reader.scroll_x, 0, max_x);
    reader.scroll_y = ClampInt(reader.scroll_y, 0, max_y);
  };

  auto set_scroll_edge = [&](bool top) {
    clamp_scroll();
    int display_w = 0;
    int display_h = 0;
    if (!current_reader_display_size(display_w, display_h)) {
      if (const ReaderRenderCache *cache = visible_reader_render_cache()) {
        display_w = cache->display_w;
        display_h = cache->display_h;
      }
    }
    const int max_x = std::max(0, display_w - Layout().screen_w);
    const int max_y = std::max(0, display_h - Layout().screen_h);
    const auto [axis, sign] = current_reader_axis_sign();
    if (axis == 1) {
      reader.scroll_x = 0;
      if (top) reader.scroll_y = (sign > 0) ? 0 : max_y;
      else reader.scroll_y = (sign > 0) ? max_y : 0;
    } else {
      reader.scroll_y = 0;
      if (top) reader.scroll_x = (sign > 0) ? 0 : max_x;
      else reader.scroll_x = (sign > 0) ? max_x : 0;
    }
  };

  auto commit_target_view = [&](ReaderViewState next_state, bool align_to_edge, bool edge_top) {
    if (!reader_is_open()) return;
    const int page_count = std::max(1, reader_page_count());
    next_state.page = ClampInt(next_state.page, 0, page_count - 1);
    next_state.zoom = std::max(0.25f, std::min(6.0f, next_state.zoom));
    next_state.rotation %= 360;
    if (next_state.rotation < 0) next_state.rotation += 360;
    next_state.rotation = ((next_state.rotation + 45) / 90) * 90;
    next_state.rotation %= 360;

    const ReaderViewState previous_state = target_state;
    const bool target_changed = next_state != previous_state;
    const bool page_changed = next_state.page != previous_state.page;
    if (target_changed) {
      SDL_LockMutex(reader_async_mutex);
      reader_async_latest_target_serial = ++reader_async_job_serial;
      reader_async_cancel_requested.store(true);
      SDL_UnlockMutex(reader_async_mutex);
      ready_state = ReaderViewState{};
      ready_state_valid = false;
    }
    if (page_changed) {
      reader_set_page(next_state.page);
      reader.scroll_x = 0;
      reader.scroll_y = 0;
    }
    reader.rotation = next_state.rotation;
    reader.zoom = next_state.zoom;
    target_state = next_state;

    if (align_to_edge) set_scroll_edge(edge_top);
    else clamp_scroll();

    if (page_changed) {
      adaptive_render.pending_page_active = false;
      adaptive_render.fast_flip_mode = false;
    }
  };

  auto queue_page_flip = [&](int page_action) {
    if (page_action == 0 || !reader_is_open()) return;
    const uint32_t now = SDL_GetTicks();
    const bool rapid_flip =
        adaptive_render.pending_page_active ||
        (adaptive_render.last_page_flip_tick > 0 &&
         now - adaptive_render.last_page_flip_tick <= kReaderFastFlipThresholdMs);
    const int page_count = std::max(1, reader_page_count());
    const int base_page = adaptive_render.pending_page_active ? adaptive_render.pending_page : target_state.page;
    const int target_page = ClampInt(base_page + page_action, 0, page_count - 1);
    if (target_page == base_page) return;
    adaptive_render.pending_page_active = true;
    adaptive_render.pending_page = target_page;
    adaptive_render.pending_page_top = (page_action > 0);
    adaptive_render.pending_page_commit_tick = now + kReaderPageFlipDebounceMs;
    adaptive_render.fast_flip_mode = rapid_flip;
    adaptive_render.last_page_flip_tick = now;
  };

  auto flush_pending_page_flip = [&]() {
    if (!adaptive_render.pending_page_active || !reader_is_open()) return;
    const uint32_t now = SDL_GetTicks();
    if (now < adaptive_render.pending_page_commit_tick) return;
    adaptive_render.pending_page_active = false;
    adaptive_render.fast_flip_mode = false;
    const ReaderViewState current_view = target_state;
    const int current_page = current_view.page;
    const int target_page = ClampInt(adaptive_render.pending_page, 0, std::max(0, reader_page_count() - 1));
    if (target_page == current_page) return;
    ReaderViewState next_view = current_view;
    next_view.page = target_page;
    commit_target_view(next_view, true, adaptive_render.pending_page_top);
  };

  auto flush_pending_page_flip_now = [&]() {
    if (!adaptive_render.pending_page_active) return;
    adaptive_render.pending_page_commit_tick = 0;
    flush_pending_page_flip();
  };

  auto long_dir_for_button = [&](Button b) -> int {
    if (target_state.rotation == 0) {
      if (b == Button::Down) return 1;
      if (b == Button::Up) return -1;
    } else if (target_state.rotation == 270) {
      if (b == Button::Right) return 1;
      if (b == Button::Left) return -1;
    } else if (target_state.rotation == 90) {
      if (b == Button::Left) return 1;
      if (b == Button::Right) return -1;
    } else {
      if (b == Button::Up) return 1;
      if (b == Button::Down) return -1;
    }
    return 0;
  };

  auto tap_page_action_for_button = [&](Button b) -> int {
    // Keep page-flip on the non-scroll axis:
    // rot 0:   Left/Right flip pages
    // rot 90:  Up/Down flip pages
    // rot 180: Left/Right flip pages (reversed)
    // rot 270: Up/Down flip pages (reversed)
    if (target_state.rotation == 0) {
      if (b == Button::Right) return 1;
      if (b == Button::Left) return -1;
    } else if (target_state.rotation == 90) {
      if (b == Button::Down) return 1;
      if (b == Button::Up) return -1;
    } else if (target_state.rotation == 180) {
      if (b == Button::Left) return 1;
      if (b == Button::Right) return -1;
    } else { // 270
      if (b == Button::Up) return 1;
      if (b == Button::Down) return -1;
    }
    return 0;
  };

  auto scroll_by_dir = [&](int dir, int step_px) {
    if (!ensure_render()) return;
    adaptive_render.last_scroll_dir = (dir >= 0) ? 1 : -1;
    const auto [axis, sign] = current_reader_axis_sign();
    int display_w = 0;
    int display_h = 0;
    if (!current_reader_display_size(display_w, display_h)) {
      if (const ReaderRenderCache *cache = visible_reader_render_cache()) {
        display_w = cache->display_w;
        display_h = cache->display_h;
      } else {
        return;
      }
    }
    const int max_x = std::max(0, display_w - Layout().screen_w);
    const int max_y = std::max(0, display_h - Layout().screen_h);
    int *pos = (axis == 1) ? &reader.scroll_y : &reader.scroll_x;
    int max_pos = (axis == 1) ? max_y : max_x;
    int old = *pos;
    const int delta = step_px * dir * sign;
    *pos = ClampInt(*pos + delta, 0, max_pos);
    if (*pos != old) return;
    if (hold_cooldown > 0.0f) return;
    if (dir > 0) {
      const ReaderViewState current_view = target_state;
      const int target_page = ClampInt(current_view.page + 1, 0, std::max(0, reader_page_count() - 1));
      if (target_page != current_view.page) {
        ReaderViewState next_view = current_view;
        next_view.page = target_page;
        commit_target_view(next_view, true, true);
        hold_cooldown = 0.16f;
      }
    } else {
      const ReaderViewState current_view = target_state;
      const int target_page = ClampInt(current_view.page - 1, 0, std::max(0, reader_page_count() - 1));
      if (target_page != current_view.page) {
        ReaderViewState next_view = current_view;
        next_view.page = target_page;
        commit_target_view(next_view, true, false);
        hold_cooldown = 0.16f;
      }
    }
  };

  auto close_text_reader = [&]() {
    txt_reader = TxtReaderState{};
    reader_progress_overlay_visible = false;
    if (reader_mode == ReaderMode::Txt) {
      reader_mode = ReaderMode::None;
    }
  };

  auto clamp_text_scroll = [&]() {
    const int max_scroll = std::max(0, txt_reader.content_h - txt_reader.viewport_h);
    txt_reader.scroll_px = ClampInt(txt_reader.scroll_px, 0, max_scroll);
    if (!txt_reader.loading) {
      reader.scroll_y = txt_reader.scroll_px;
    }
  };

  auto get_text_viewport_bounds = [&]() -> SDL_Rect {
    int output_w = Layout().screen_w;
    int output_h = Layout().screen_h;
    if (renderer) {
      int rw = 0;
      int rh = 0;
      if (SDL_GetRendererOutputSize(renderer, &rw, &rh) == 0) {
        if (rw > 0) output_w = rw;
        if (rh > 0) output_h = rh;
      }
    }
    const int margin_x = std::max(12, std::min(Layout().txt_margin_x, std::max(0, output_w / 12)));
    const int margin_y = std::max(12, std::min(Layout().txt_margin_y, std::max(0, output_h / 12)));
    SDL_Rect rect{};
    rect.x = margin_x;
    rect.y = margin_y;
    rect.w = std::max(100, output_w - margin_x * 2);
    rect.h = std::max(100, output_h - margin_y * 2);
    return rect;
  };

#ifdef HAVE_SDL2_TTF
  const std::string kTxtParagraphIndent = u8"銆€銆€";
  const std::string kTxtParagraphIndentAscii = "  ";

  auto normalize_text_paragraph = [&](const std::string &line) -> std::string {
    auto is_ignorable_at = [&](size_t pos, size_t &len) -> bool {
      if (pos >= line.size()) {
        len = 0;
        return false;
      }
      const unsigned char c = static_cast<unsigned char>(line[pos]);
      if (c == ' ' || c == '\t') {
        len = 1;
        return true;
      }
      if (pos + 3 <= line.size() &&
          static_cast<unsigned char>(line[pos]) == 0xE3 &&
          static_cast<unsigned char>(line[pos + 1]) == 0x80 &&
          static_cast<unsigned char>(line[pos + 2]) == 0x80) {
        len = 3;
        return true;
      }
      len = 0;
      return false;
    };

    size_t start = 0;
    while (start < line.size()) {
      size_t len = 0;
      if (!is_ignorable_at(start, len)) break;
      start += len;
    }

    size_t end = line.size();
    while (end > start) {
      size_t probe = end - 1;
      if (probe >= 2 &&
          static_cast<unsigned char>(line[probe - 2]) == 0xE3 &&
          static_cast<unsigned char>(line[probe - 1]) == 0x80 &&
          static_cast<unsigned char>(line[probe]) == 0x80) {
        end -= 3;
        continue;
      }
      if (line[probe] == ' ' || line[probe] == '\t') {
        --end;
        continue;
      }
      break;
    }

    if (end <= start) return "";
    return kTxtParagraphIndentAscii + line.substr(start, end - start);
  };

  auto wrap_text_line = [&](const std::string &line, int max_width_px) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (line.empty()) {
      out.push_back("");
      return out;
    }
    if (!ui_font_reader) {
      out.push_back(line);
      return out;
    }
    max_width_px = std::max(40, max_width_px);
    std::string expanded;
    expanded.reserve(line.size() + 8);
    std::vector<size_t> char_offsets;
    char_offsets.reserve(line.size() + 1);
    char_offsets.push_back(0);
    for (size_t pos = 0; pos < line.size();) {
      const unsigned char c = static_cast<unsigned char>(line[pos]);
      size_t len = Utf8CharLen(c);
      if (pos + len > line.size()) len = 1;
      if (len == 1 && line[pos] == '\t') expanded += "    ";
      else expanded.append(line, pos, len);
      pos += len;
      char_offsets.push_back(expanded.size());
    }
    const size_t total_chars = char_offsets.size() - 1;
    size_t start_char = 0;
    while (start_char < total_chars) {
      size_t lo = start_char + 1;
      size_t hi = total_chars;
      size_t best = start_char + 1;
      while (lo <= hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const size_t byte_start = char_offsets[start_char];
        const size_t byte_end = char_offsets[mid];
        const std::string candidate = expanded.substr(byte_start, byte_end - byte_start);
        int text_w = 0;
        if (TTF_SizeUTF8(ui_font_reader, candidate.c_str(), &text_w, nullptr) == 0 && text_w <= max_width_px) {
          best = mid;
          lo = mid + 1;
        } else {
          if (mid == 0) break;
          hi = mid - 1;
        }
      }
      size_t break_char = best;
      if (best < total_chars) {
        for (size_t scan = best; scan > start_char; --scan) {
          const size_t b0 = char_offsets[scan - 1];
          const size_t b1 = char_offsets[scan];
          if (b1 == b0 + 1) {
            const char ch = expanded[b0];
            if (ch == ' ' || ch == '-' || ch == ',' || ch == '.' || ch == ';' || ch == ':' ||
                ch == '!' || ch == '?') {
              break_char = scan;
              break;
            }
          }
        }
      }
      const size_t byte_start = char_offsets[start_char];
      const size_t byte_end = char_offsets[break_char];
      out.emplace_back(expanded.substr(byte_start, byte_end - byte_start));
      start_char = break_char;
      while (start_char < total_chars) {
        const size_t b0 = char_offsets[start_char];
        const size_t b1 = char_offsets[start_char + 1];
        if (b1 != b0 + 1 || expanded[b0] != ' ') break;
        ++start_char;
      }
    }
    if (out.empty()) out.push_back("");
    return out;
  };
#endif

  auto append_wrapped_text_line = [&](TxtReaderState &state, const std::string &line) -> bool {
#ifndef HAVE_SDL2_TTF
    (void)state;
    (void)line;
    return false;
#else
    const std::string paragraph = normalize_text_paragraph(line);
    if (paragraph.empty()) return true;
    const int wrap_width_px = std::max(40, state.viewport_w - 6);
    std::vector<std::string> wrapped = wrap_text_line(paragraph, wrap_width_px);
    for (const std::string &wline : wrapped) {
      state.lines.push_back(wline);
      if (state.lines.size() >= kTxtMaxWrappedLines) {
        state.content_h = static_cast<int>(state.lines.size()) * state.line_h;
        return false;
      }
    }
    state.content_h = static_cast<int>(state.lines.size()) * state.line_h;
    return true;
#endif
  };

  auto finalize_text_reader_loading = [&](TxtReaderState &state, const std::string *cache_key = nullptr) {
    if ((state.truncated || state.limit_hit || state.lines.size() >= kTxtMaxWrappedLines) &&
        !state.truncation_notice_added) {
      state.lines.push_back("");
      state.lines.push_back("[TXT preview truncated]");
      state.truncation_notice_added = true;
    }
    if (state.lines.empty()) state.lines.emplace_back("");
    state.content_h = static_cast<int>(state.lines.size()) * state.line_h;
    state.resume_cache_dirty = true;
    if (cache_key && !cache_key->empty() && !state.loading) {
      TxtLayoutCacheEntry entry;
      entry.lines = state.lines;
      entry.viewport_w = state.viewport_w;
      entry.viewport_h = state.viewport_h;
      entry.line_h = state.line_h;
      entry.content_h = state.content_h;
      entry.truncated = state.truncated;
      entry.limit_hit = state.limit_hit;
      entry.last_use = SDL_GetTicks();
      txt_layout_cache[*cache_key] = std::move(entry);
      save_txt_layout_cache_to_disk(*cache_key, txt_layout_cache[*cache_key]);
      prune_txt_layout_cache();
    }
  };

  auto process_text_layout_chunk = [&](TxtReaderState &state, uint32_t budget_ms, size_t byte_budget,
                                       const std::string *cache_key = nullptr) {
    if (!state.open || !state.loading) return;
    const uint32_t started = SDL_GetTicks();
    size_t consumed = 0;
    const size_t prev_parse_pos = state.parse_pos;
    const size_t prev_line_count = state.lines.size();
    while (state.parse_pos < state.pending_raw.size() && !state.limit_hit) {
      const char ch = state.pending_raw[state.parse_pos++];
      ++consumed;
      if (ch == '\n' || ch == '\r') {
        if (!append_wrapped_text_line(state, state.pending_line)) {
          state.limit_hit = true;
          break;
        }
        state.pending_line.clear();
        if (ch == '\r' && state.parse_pos < state.pending_raw.size() && state.pending_raw[state.parse_pos] == '\n') {
          ++state.parse_pos;
          ++consumed;
        }
      } else {
        state.pending_line.push_back(ch);
      }
      if (consumed >= byte_budget) break;
      if (budget_ms > 0 && SDL_GetTicks() - started >= budget_ms) break;
    }
    if (!state.limit_hit && state.parse_pos >= state.pending_raw.size()) {
      if (!append_wrapped_text_line(state, state.pending_line)) {
        state.limit_hit = true;
      }
      state.pending_line.clear();
    }
    const int max_scroll = std::max(0, state.content_h - state.viewport_h);
    state.scroll_px = ClampInt(state.target_scroll_px, 0, max_scroll);
    if (state.parse_pos != prev_parse_pos || state.lines.size() != prev_line_count) {
      state.resume_cache_dirty = true;
    }
    if (state.limit_hit || state.parse_pos >= state.pending_raw.size()) {
      state.loading = false;
      state.pending_raw.clear();
      state.pending_line.clear();
      state.scroll_px = ClampInt(state.target_scroll_px, 0, std::max(0, state.content_h - state.viewport_h));
      finalize_text_reader_loading(state, cache_key);
    }
  };

  auto warm_text_reader_to_target = [&](TxtReaderState &state, const std::string *cache_key = nullptr) {
    if (!state.loading) return;
    const int desired_bottom = state.target_scroll_px + state.viewport_h;
    if (desired_bottom <= state.viewport_h) return;
    while (state.loading && state.content_h < desired_bottom) {
      process_text_layout_chunk(state, 0, 262144, cache_key);
    }
    state.scroll_px = ClampInt(state.target_scroll_px, 0, std::max(0, state.content_h - state.viewport_h));
  };

  auto open_text_book = [&](const std::string &path) -> bool {
#ifndef HAVE_SDL2_TTF
    (void)path;
    std::cerr << "[reader] txt reader requires SDL_ttf build support.\n";
    return false;
#else
    open_ui_font();
    if (!ui_font_reader) {
      std::cerr << "[reader] txt reader failed: ui font unavailable.\n";
      return false;
    }
    const SDL_Rect text_bounds = get_text_viewport_bounds();
    int font_h = TTF_FontHeight(ui_font_reader);
    if (font_h <= 0) font_h = 24;
    const int line_h = font_h + kTxtLineSpacing;
    std::error_code meta_ec;
    const uintmax_t cache_file_size = std::filesystem::file_size(std::filesystem::path(path), meta_ec);
    const auto cache_mtime_raw = std::filesystem::last_write_time(std::filesystem::path(path), meta_ec);
    const long long cache_file_mtime = meta_ec ? 0LL : static_cast<long long>(cache_mtime_raw.time_since_epoch().count());
    const std::string txt_cache_key =
        make_txt_layout_cache_key(path, text_bounds, line_h, meta_ec ? 0 : cache_file_size, cache_file_mtime);

    TxtReaderState next{};
    next.open = true;
    next.viewport_x = text_bounds.x;
    next.viewport_y = text_bounds.y;
    next.viewport_w = text_bounds.w;
    next.viewport_h = text_bounds.h;
    next.line_h = line_h;
    next.cache_key = txt_cache_key;

    auto txt_cache_it = txt_layout_cache.find(next.cache_key);
    if (txt_cache_it != txt_layout_cache.end()) {
      txt_cache_it->second.last_use = SDL_GetTicks();
      next.lines = txt_cache_it->second.lines;
      next.content_h = txt_cache_it->second.content_h;
      next.truncated = txt_cache_it->second.truncated;
      next.limit_hit = txt_cache_it->second.limit_hit;
      next.truncation_notice_added = true;
      next.loading = false;
      next.target_scroll_px = std::max(0, reader.scroll_y);
      next.scroll_px = ClampInt(next.target_scroll_px, 0, std::max(0, next.content_h - next.viewport_h));
      txt_reader = std::move(next);
      reader_mode = ReaderMode::Txt;
      reader_progress_overlay_visible = false;
      invalidate_all_render_cache();
      clamp_text_scroll();
      return true;
    }
    TxtLayoutCacheEntry disk_cache_entry;
    if (load_txt_layout_cache_from_disk(next.cache_key, disk_cache_entry)) {
      disk_cache_entry.last_use = SDL_GetTicks();
      txt_layout_cache[next.cache_key] = disk_cache_entry;
      prune_txt_layout_cache();
      next.lines = disk_cache_entry.lines;
      next.content_h = disk_cache_entry.content_h;
      next.truncated = disk_cache_entry.truncated;
      next.limit_hit = disk_cache_entry.limit_hit;
      next.truncation_notice_added = true;
      next.loading = false;
      next.target_scroll_px = std::max(0, reader.scroll_y);
      next.scroll_px = ClampInt(next.target_scroll_px, 0, std::max(0, next.content_h - next.viewport_h));
      txt_reader = std::move(next);
      reader_mode = ReaderMode::Txt;
      reader_progress_overlay_visible = false;
      invalidate_all_render_cache();
      clamp_text_scroll();
      return true;
    }
    TxtResumeCacheEntry resume_cache_entry;
    if (load_txt_resume_cache_from_disk(next.cache_key, resume_cache_entry)) {
      const int restored_scroll_px = std::max(std::max(0, reader.scroll_y), std::max(0, resume_cache_entry.scroll_px));
      next.lines = std::move(resume_cache_entry.lines);
      next.pending_raw = std::move(resume_cache_entry.pending_raw);
      next.pending_line = std::move(resume_cache_entry.pending_line);
      next.content_h = resume_cache_entry.content_h;
      next.parse_pos = resume_cache_entry.parse_pos;
      next.loading = resume_cache_entry.loading;
      next.truncated = resume_cache_entry.truncated;
      next.limit_hit = resume_cache_entry.limit_hit;
      next.truncation_notice_added = resume_cache_entry.truncation_notice_added;
      next.target_scroll_px = restored_scroll_px;
      next.scroll_px = ClampInt(next.target_scroll_px, 0, std::max(0, next.content_h - next.viewport_h));
      next.last_resume_cache_save = SDL_GetTicks();
      next.resume_cache_dirty = false;
      txt_reader = std::move(next);
      txt_reader.scroll_px = ClampInt(txt_reader.target_scroll_px, 0, std::max(0, txt_reader.content_h - txt_reader.viewport_h));
      reader_mode = ReaderMode::Txt;
      reader_progress_overlay_visible = false;
      invalidate_all_render_cache();
      clamp_text_scroll();
      return true;
    }

    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) {
      std::cerr << "[reader] txt open failed: " << path << "\n";
      return false;
    }
    std::string raw;
    try {
      std::error_code ec;
      const auto fsz = std::filesystem::file_size(std::filesystem::path(path), ec);
      if (!ec && fsz > 0) {
        const size_t cap = static_cast<size_t>(std::min<uintmax_t>(fsz, kTxtMaxBytes));
        raw.resize(cap);
        in.read(raw.data(), static_cast<std::streamsize>(cap));
        raw.resize(static_cast<size_t>(in.gcount()));
      } else {
        std::ostringstream oss;
        oss << in.rdbuf();
        raw = oss.str();
        if (raw.size() > kTxtMaxBytes) raw.resize(kTxtMaxBytes);
      }
    } catch (...) {
      std::cerr << "[reader] txt read failed (exception): " << path << "\n";
      return false;
    }
    bool truncated = false;
    if (raw.size() >= kTxtMaxBytes) {
      truncated = true;
    }
    std::string decoded;
    if (DecodeTextBytesToUtf8(raw, decoded)) {
      raw = std::move(decoded);
    }
    next.pending_raw = std::move(raw);
    next.pending_line.reserve(256);
    next.parse_pos = 0;
    next.loading = true;
    next.truncated = truncated;
    next.limit_hit = false;
    next.truncation_notice_added = false;
    next.lines.reserve(1024);
    next.target_scroll_px = std::max(0, reader.scroll_y);
    next.scroll_px = 0;
    next.last_resume_cache_save = 0;
    next.resume_cache_dirty = true;

    txt_reader = std::move(next);
    process_text_layout_chunk(txt_reader, 8, 32768, &txt_reader.cache_key);
    warm_text_reader_to_target(txt_reader, &txt_reader.cache_key);
    if (!txt_reader.loading) finalize_text_reader_loading(txt_reader, &txt_reader.cache_key);
    txt_reader.scroll_px = ClampInt(txt_reader.scroll_px, 0, std::max(0, txt_reader.content_h - txt_reader.viewport_h));
    reader_mode = ReaderMode::Txt;
    reader_progress_overlay_visible = false;
    invalidate_all_render_cache();
    clamp_text_scroll();
    return true;
#endif
  };

  auto text_scroll_by = [&](int delta_px) {
    if (reader_mode != ReaderMode::Txt || !txt_reader.open) return;
    txt_reader.scroll_px += delta_px;
    txt_reader.target_scroll_px = txt_reader.scroll_px;
    clamp_text_scroll();
    txt_reader.resume_cache_dirty = true;
    persist_current_txt_resume_snapshot(current_book, false);
  };

  auto text_page_by = [&](int dir) {
    if (reader_mode != ReaderMode::Txt || !txt_reader.open) return;
    const int step = std::max(80, txt_reader.viewport_h - txt_reader.line_h);
    text_scroll_by(dir * step);
  };

  auto start_next_boot_count_root = [&]() -> bool {
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    while (boot_count_root_index < books_roots.size()) {
      const std::filesystem::path root_path(books_roots[boot_count_root_index++]);
      std::error_code ec;
      if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
      boot_count_it = std::filesystem::recursive_directory_iterator(root_path, opts, ec);
      if (ec) continue;
      boot_count_end = std::filesystem::recursive_directory_iterator();
      boot_count_iterator_active = true;
      return true;
    }
    boot_count_iterator_active = false;
    return false;
  };

  auto boot_progress_ratio = [&]() -> float {
    switch (boot_phase) {
    case BootPhase::CountBooks: {
      const float pulse = std::fmod(boot_timer * 0.85f, 1.0f);
      return 0.05f + pulse * 0.15f;
    }
    case BootPhase::ScanBooks:
      return boot_total_books == 0 ? 0.55f
                                   : (0.20f + 0.35f * (static_cast<float>(boot_scan_index) /
                                                       static_cast<float>(std::max<size_t>(1, boot_total_books))));
    case BootPhase::GenerateCovers:
      return boot_cover_generate_queue.empty()
                 ? 1.0f
                 : (0.55f + 0.45f * (static_cast<float>(boot_cover_generate_index) /
                                     static_cast<float>(std::max<size_t>(1, boot_cover_generate_queue.size()))));
    case BootPhase::Finalize:
    case BootPhase::Done:
      return 1.0f;
    }
    return 0.0f;
  };

  auto make_boot_scan_text = [&](size_t current, size_t total) {
    return std::string(u8"\u8d44\u6e90\u52a0\u8f7d\u4e2d...\uff08") + std::to_string(current) + "/" +
           std::to_string(total) + u8"\uff09";
  };

  auto make_boot_cover_text = [&](size_t current, size_t total) {
    return std::string(u8"\u5c01\u9762\u7f13\u5b58\u751f\u6210\u4e2d...\uff08") + std::to_string(current) + "/" +
           std::to_string(total) + u8"\uff09";
  };

  bool running = true;
  uint32_t prev_ticks = SDL_GetTicks();
  while (running) {
    const uint32_t frame_begin_ticks = SDL_GetTicks();
    uint32_t now = SDL_GetTicks();
    float dt = std::max(0.0f, (now - prev_ticks) / 1000.0f);
    prev_ticks = now;
    hold_cooldown = std::max(0.0f, hold_cooldown - dt);
    settings_toggle_guard = std::max(0.0f, settings_toggle_guard - dt);
    menu_toggle_cooldown = std::max(0.0f, menu_toggle_cooldown - dt);

    input.BeginFrame(dt);
    SDL_Event e;
    const bool animate_enabled = config.Get().animations;
    const bool has_active_animation =
        state == State::Boot || input.AnyPressed() ||
        txt_transcode_job.active ||
        (reader_mode == ReaderMode::Txt && txt_reader.open && txt_reader.loading) ||
        (animate_enabled && (menu_anim.IsAnimating() || scene_flash.IsAnimating() || page_animating || any_grid_animating));
    const bool needs_periodic_tick = (state == State::Shelf && title_marquee_active);
    const uint32_t loop_now = SDL_GetTicks();
    const bool has_pending_flush =
        config.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        progress.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        favorites_store.ShouldFlush(loop_now, kDeferredSaveDelayMs) ||
        history_store.ShouldFlush(loop_now, kDeferredSaveDelayMs);
    const int idle_wait_ms = (!has_active_animation && has_pending_flush && !needs_periodic_tick) ? static_cast<int>(kIdleFlushOnlyWaitMs) : kIdleWaitMs;
    if (has_active_animation) {
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = false;
        input.HandleEvent(e);
      }
    } else {
      if (SDL_WaitEventTimeout(&e, idle_wait_ms)) {
        if (e.type == SDL_QUIT) running = false;
        input.HandleEvent(e);
        while (SDL_PollEvent(&e)) {
          if (e.type == SDL_QUIT) running = false;
          input.HandleEvent(e);
        }
      } else if (has_pending_flush && !needs_periodic_tick) {
        // Wake only to flush deferred IO; keep the current frame untouched.
        input.EndFrame();
        flush_deferred_writes(false);
        prev_ticks = SDL_GetTicks();
        continue;
      } else if (!needs_periodic_tick && !has_pending_flush) {
        // Fully idle: no input, no animation, no incremental loading.
        // Skip update/render work and keep sleeping until something changes.
        prev_ticks = SDL_GetTicks();
        input.EndFrame();
        continue;
      }
    }
    input.EndFrame();

    if (reader_mode == ReaderMode::Txt && txt_reader.open && txt_reader.loading) {
      process_text_layout_chunk(txt_reader, 5, 24576, &txt_reader.cache_key);
      clamp_text_scroll();
      persist_current_txt_resume_snapshot(current_book, false);
    }
    process_txt_transcode_step();
    flush_deferred_writes(false);

    const bool vol_up_pressed = input.IsJustPressed(Button::VolUp) || input.IsRepeated(Button::VolUp);
    const bool vol_down_pressed = input.IsJustPressed(Button::VolDown) || input.IsRepeated(Button::VolDown);
    if (vol_up_pressed || vol_down_pressed) {
      std::cout << "[native_h700] volume handler: up=" << (vol_up_pressed ? "1" : "0")
                << " down=" << (vol_down_pressed ? "1" : "0")
                << " prefer_system=" << (volume_controller.UsesSystemVolume() ? "1" : "0")
                << " app_volume=" << config.Get().sfx_volume << "\n";
      bool system_volume_changed = false;
      if (volume_controller.UsesSystemVolume()) {
        if (vol_up_pressed) system_volume_changed = volume_controller.AdjustUp() || system_volume_changed;
        if (vol_down_pressed) system_volume_changed = volume_controller.AdjustDown() || system_volume_changed;
        if (system_volume_changed) {
          std::cout << "[native_h700] system volume: "
                    << (vol_up_pressed && vol_down_pressed ? "unchanged-step"
                                                           : (vol_up_pressed ? "up" : "down"))
                    << "\n";
        } else if (!warned_system_volume_fallback) {
          warned_system_volume_fallback = true;
          std::cout << "[native_h700] system volume control unavailable, fallback to app sfx volume\n";
        }
      }

      if (!system_volume_changed) {
        NativeConfig &cfg = config.Mutable();
        const int old_volume = cfg.sfx_volume;
        if (vol_up_pressed) cfg.sfx_volume = std::min(SDL_MIX_MAXVOLUME, cfg.sfx_volume + 8);
        if (vol_down_pressed) cfg.sfx_volume = std::max(0, cfg.sfx_volume - 8);
        if (cfg.sfx_volume != old_volume) {
          sfx.SetVolume(cfg.sfx_volume);
          config.MarkDirty();
          std::cout << "[native_h700] sound volume: " << cfg.sfx_volume << "\n";
          if (cfg.audio && cfg.sfx_volume > 0) {
            sfx.Play(SfxId::Change);
          }
        }
        volume_display_percent = ClampInt((cfg.sfx_volume * 100) / std::max(1, SDL_MIX_MAXVOLUME), 0, 100);
      } else {
        if (vol_up_pressed) volume_display_percent = ClampInt(volume_display_percent + 5, 0, 100);
        if (vol_down_pressed) volume_display_percent = ClampInt(volume_display_percent - 5, 0, 100);
      }
      volume_display_until = now + 1500;
    }

    const bool start_just_pressed = input.IsJustPressed(Button::Start);
    const bool select_just_pressed = input.IsJustPressed(Button::Select);
    const bool menu_toggle_pressed = input.IsPressed(Button::Start) || input.IsPressed(Button::Select);
    if (!menu_toggle_pressed && menu_toggle_cooldown <= 0.0f) {
      menu_toggle_armed = true;
    }

    if (state == State::Shelf || state == State::Settings) {
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

    if (input.IsPressed(Button::Start) && input.IsPressed(Button::Select)) running = false;

    // Dedicated settings toggle path (single entry for Start / Select mapping).
    const bool menu_toggle_request = start_just_pressed || select_just_pressed;
    if (menu_toggle_request && menu_toggle_armed && menu_toggle_cooldown <= 0.0f) {
      menu_toggle_armed = false;
      menu_toggle_cooldown = kMenuToggleDebounceSec;
      if (state == State::Settings) {
        const NativeConfig &ui_cfg = config.Get();
        if (settings_close_armed && settings_toggle_guard <= 0.0f && !menu_closing) {
          if (ui_cfg.animations) menu_anim.AnimateTo(0.0f, 0.16f, animation::Ease::InOutCubic);
          else menu_anim.Snap(0.0f);
          menu_closing = true;
          play_sfx(SfxId::Back);
        }
      } else if (state == State::Shelf) {
        const NativeConfig &ui_cfg = config.Get();
        settings_return_state = State::Shelf;
        state = State::Settings;
        menu_anim.Snap(0.0f);
        if (ui_cfg.animations) menu_anim.AnimateTo(1.0f, 0.20f, animation::Ease::OutCubic);
        else menu_anim.Snap(1.0f);
        settings_toggle_guard = kSettingsToggleGuardSec;
        settings_close_armed = false;
        menu_closing = false;
        play_sfx(SfxId::Back);
      } else if (state == State::Reader) {
        settings_return_state = State::Reader;
        state = State::Settings;
        menu_anim.Snap(0.0f);
        if (config.Get().animations) menu_anim.AnimateTo(1.0f, 0.20f, animation::Ease::OutCubic);
        else menu_anim.Snap(1.0f);
        settings_toggle_guard = kSettingsToggleGuardSec;
        settings_close_armed = false;
        menu_closing = false;
        play_sfx(SfxId::Back);
      }
    }

    if (state == State::Boot) {
      boot_timer += dt;
      if (boot_phase == BootPhase::CountBooks) {
        if (!boot_count_iterator_active) {
          start_next_boot_count_root();
        }
        size_t processed = 0;
        while (processed < kBootCountBatchEntries) {
          if (!boot_count_iterator_active) {
            if (!start_next_boot_count_root()) break;
          }
          if (boot_count_it == boot_count_end) {
            boot_count_iterator_active = false;
            continue;
          }
          std::error_code ec;
          const auto entry = *boot_count_it;
          boot_count_it.increment(ec);
          ++processed;
          if (ec || !entry.is_regular_file(ec)) continue;
          const std::string ext = GetLowerExt(entry.path().string());
          if (ext == ".pdf" || ext == ".txt") {
            boot_supported_paths.push_back(entry.path().string());
          }
        }
        boot_status_text = make_boot_scan_text(0, 0);
        if (!boot_count_iterator_active && boot_count_root_index >= books_roots.size()) {
          std::sort(boot_supported_paths.begin(), boot_supported_paths.end());
          boot_total_books = boot_supported_paths.size();
          boot_scan_index = 0;
          boot_cover_generate_queue.clear();
          boot_phase = BootPhase::ScanBooks;
          boot_status_text = make_boot_scan_text(0, boot_total_books);
        }
      } else if (boot_phase == BootPhase::ScanBooks) {
        size_t processed = 0;
        while (processed < kBootScanBatchEntries && boot_scan_index < boot_supported_paths.size()) {
          const std::string &book_path = boot_supported_paths[boot_scan_index];
          const std::string ext = GetLowerExt(book_path);
          if (ext == ".pdf") {
            BookItem item;
            item.name = std::filesystem::path(book_path).filename().string();
            item.path = book_path;
            item.is_dir = false;
            if (!has_manual_cover_exact_or_fuzzy(item) &&
                !has_cached_pdf_cover_on_disk(book_path) &&
                pdf_runtime.HasRealRenderer()) {
              boot_cover_generate_queue.push_back(book_path);
            }
          }
          ++boot_scan_index;
          ++processed;
        }
        boot_status_text = make_boot_scan_text(boot_scan_index, boot_total_books);
        if (boot_scan_index >= boot_supported_paths.size()) {
          boot_cover_generate_index = 0;
          boot_phase = BootPhase::GenerateCovers;
          boot_status_text = make_boot_cover_text(0, boot_cover_generate_queue.size());
          if (boot_cover_generate_queue.empty()) {
            boot_phase = BootPhase::Finalize;
          }
        }
      } else if (boot_phase == BootPhase::GenerateCovers) {
        size_t processed = 0;
        while (processed < kBootCoverGenerateBatchEntries &&
               boot_cover_generate_index < boot_cover_generate_queue.size()) {
          const std::string &doc_path = boot_cover_generate_queue[boot_cover_generate_index];
          if (SDL_Texture *generated = create_doc_first_page_cover_texture(doc_path)) {
            forget_texture_size(generated);
            SDL_DestroyTexture(generated);
          }
          ++boot_cover_generate_index;
          ++processed;
        }
        boot_status_text = make_boot_cover_text(boot_cover_generate_index, boot_cover_generate_queue.size());
        if (boot_cover_generate_index >= boot_cover_generate_queue.size()) {
          boot_phase = BootPhase::Finalize;
        }
      }
      if (boot_phase == BootPhase::Finalize) {
        current_folder.clear();
        nav_selected_index = 0;
        rebuild_shelf_items();
        focus_index = 0;
        shelf_page = 0;
        page_animating = false;
        page_slide.Snap(0.0f);
        grid_item_anims.clear();
        title_focus_index = -1;
        title_marquee_active = false;
        title_marquee_offset = 0.0f;
        title_marquee_wait = kTitleMarqueePauseSec;
        std::cout << "[native_h700] boot scan complete: books=" << boot_total_books
                  << " cover_generate=" << boot_cover_generate_queue.size() << "\n";
        state = State::Shelf;
        boot_phase = BootPhase::Done;
        std::cout << "[native_h700] shelf items: " << shelf_items.size() << "\n";
        for (size_t i = 0; i < shelf_items.size() && i < 12; ++i) {
          std::cout << "[native_h700] item[" << i << "] "
                    << (shelf_items[i].is_dir ? "[DIR] " : "[BOOK] ")
                    << shelf_items[i].name << " | " << shelf_items[i].path << "\n";
        }
      }
    } else if (state == State::Shelf) {
      const NativeConfig &ui_cfg = config.Get();
      const int prev_page = shelf_page;
      auto sync_focus_with_page = [&]() {
        if (shelf_items.empty()) {
          focus_index = 0;
          shelf_page = 0;
          return;
        }
        const int max_index = static_cast<int>(shelf_items.size()) - 1;
        const int max_row_page = max_index / kGridCols;
        shelf_page = ClampInt(shelf_page, 0, max_row_page);
        int col = ClampInt(focus_index % kGridCols, 0, kGridCols - 1);
        focus_index = std::min(max_index, shelf_page * kGridCols + col);
      };
      sync_focus_with_page();
      const bool marquee_needed = focused_title_needs_marquee();
      if (focus_index != title_focus_index || marquee_needed != title_marquee_active) {
        title_focus_index = focus_index;
        title_marquee_active = marquee_needed;
        title_marquee_wait = kTitleMarqueePauseSec;
        title_marquee_offset = 0.0f;
      } else if (title_marquee_active) {
        if (title_marquee_wait > 0.0f) {
          title_marquee_wait = std::max(0.0f, title_marquee_wait - dt);
        } else {
          title_marquee_offset += kTitleMarqueeSpeedPx * dt;
          if (title_marquee_offset > 8192.0f) title_marquee_offset = std::fmod(title_marquee_offset, 8192.0f);
        }
      }

      if (input.IsJustPressed(Button::L1)) {
        nav_selected_index = (nav_selected_index + 3) % 4;
        current_folder.clear();
        clear_cover_cache();
        rebuild_shelf_items();
        focus_index = 0;
        shelf_page = 0;
        page_animating = false;
        page_slide.Snap(0.0f);
        grid_item_anims.clear();
        sync_focus_with_page();
      } else if (input.IsJustPressed(Button::R1)) {
        nav_selected_index = (nav_selected_index + 1) % 4;
        current_folder.clear();
        clear_cover_cache();
        rebuild_shelf_items();
        focus_index = 0;
        shelf_page = 0;
        page_animating = false;
        page_slide.Snap(0.0f);
        grid_item_anims.clear();
        sync_focus_with_page();
      } else if (input.IsJustPressed(Button::B)) {
        if (!current_folder.empty()) {
          current_folder.clear();
          clear_cover_cache();
          rebuild_shelf_items();
          focus_index = folder_focus[""];
          shelf_page = (shelf_items.empty()) ? 0 : (focus_index / kGridCols);
          page_animating = false;
          page_slide.Snap(0.0f);
          grid_item_anims.clear();
          sync_focus_with_page();
        }
      } else if (input.IsJustPressed(Button::Left) || input.IsRepeated(Button::Left)) {
        if (!shelf_items.empty()) {
          const int max_index = static_cast<int>(shelf_items.size()) - 1;
          const int max_row_page = max_index / kGridCols;
          int col = focus_index % kGridCols;
          if (col > 0) {
            --col;
          } else if (shelf_page > 0) {
            --shelf_page;
            col = kGridCols - 1;
          }
          shelf_page = ClampInt(shelf_page, 0, max_row_page);
          focus_index = std::min(max_index, shelf_page * kGridCols + col);
        }
      } else if (input.IsJustPressed(Button::Right) || input.IsRepeated(Button::Right)) {
        if (!shelf_items.empty()) {
          const int max_index = static_cast<int>(shelf_items.size()) - 1;
          const int max_row_page = max_index / kGridCols;
          int col = focus_index % kGridCols;
          if (col < kGridCols - 1) {
            ++col;
          } else if (shelf_page < max_row_page) {
            ++shelf_page;
            col = 0;
          }
          shelf_page = ClampInt(shelf_page, 0, max_row_page);
          focus_index = std::min(max_index, shelf_page * kGridCols + col);
        }
      } else if (input.IsJustPressed(Button::Up) || input.IsRepeated(Button::Up)) {
        if (!shelf_items.empty()) {
          const int max_index = static_cast<int>(shelf_items.size()) - 1;
          const int max_row_page = max_index / kGridCols;
          const int col = focus_index % kGridCols;
          if (shelf_page > 0) --shelf_page;
          shelf_page = ClampInt(shelf_page, 0, max_row_page);
          focus_index = std::min(max_index, shelf_page * kGridCols + col);
        }
      } else if (input.IsJustPressed(Button::Down) || input.IsRepeated(Button::Down)) {
        if (!shelf_items.empty()) {
          const int max_index = static_cast<int>(shelf_items.size()) - 1;
          const int max_row_page = max_index / kGridCols;
          const int col = focus_index % kGridCols;
          if (shelf_page < max_row_page) ++shelf_page;
          shelf_page = ClampInt(shelf_page, 0, max_row_page);
          focus_index = std::min(max_index, shelf_page * kGridCols + col);
        }
      } else if (input.IsJustPressed(Button::X) && !shelf_items.empty()) {
        const BookItem &item = shelf_items[focus_index];
        if (!item.is_dir) {
          favorites_store.Add(item.path);
          if (current_category() == ShelfCategory::Collections) {
            rebuild_shelf_items();
            page_animating = false;
            page_slide.Snap(0.0f);
            grid_item_anims.clear();
            sync_focus_with_page();
          }
        }
      } else if (input.IsJustPressed(Button::Y) && !shelf_items.empty()) {
        const BookItem &item = shelf_items[focus_index];
        if (!item.is_dir) {
          favorites_store.Remove(item.path);
          if (current_category() == ShelfCategory::Collections) {
            rebuild_shelf_items();
            page_animating = false;
            page_slide.Snap(0.0f);
            grid_item_anims.clear();
            sync_focus_with_page();
          }
        }
      } else if (input.IsJustPressed(Button::A) && !shelf_items.empty()) {
        const BookItem &item = shelf_items[focus_index];
        if (item.is_dir && current_folder.empty()) {
          folder_focus[current_folder] = focus_index;
          current_folder = item.path;
          clear_cover_cache();
          rebuild_shelf_items();
          focus_index = 0;
          shelf_page = 0;
          page_animating = false;
          page_slide.Snap(0.0f);
          grid_item_anims.clear();
        } else if (!item.is_dir) {
          history_store.Add(item.path);
          current_book = item.path;
          reader = progress.Get(current_book);
          const std::string ext = GetLowerExt(current_book);
          std::cout << "[reader] open request: " << current_book << " ext=" << ext << "\n";
          bool opened = false;
          if (ext == ".txt") {
            opened = open_text_book(current_book);
          } else if (ext == ".pdf") {
            PdfRuntimeProgress pdf_progress;
            pdf_progress.page = reader.page;
            pdf_progress.rotation = reader.rotation;
            pdf_progress.zoom = reader.zoom;
            pdf_progress.scroll_y = reader.scroll_y;
            if (pdf_runtime.Open(renderer, current_book, Layout().screen_w, Layout().screen_h, pdf_progress)) {
              reader_page_size_cache.clear();
              adaptive_render = ReaderAdaptiveRenderState{};
              reset_reader_async_state();
              close_text_reader();
              reader_mode = ReaderMode::Pdf;
              invalidate_all_render_cache();
              display_state = ReaderViewState{};
              ready_state = ReaderViewState{};
              display_state_valid = false;
              ready_state_valid = false;
              const PdfRuntimeProgress active_pdf = pdf_runtime.Progress();
              target_state = ReaderViewState{active_pdf.page, active_pdf.zoom, active_pdf.rotation};
              clamp_scroll();
              opened = true;
            }
            if (!opened && !pdf_runtime.HasRealRenderer()) {
              if (!warned_mock_pdf_backend) {
                std::cerr << "[reader] blocked: current build has no real document backend. "
                             "Please rebuild with REQUIRE_MUPDF=1 and install MuPDF (preferred) or poppler-cpp.\n";
                warned_mock_pdf_backend = true;
              }
            }
          } else if (ext == ".epub") {
            if (epub_comic.Open(current_book)) {
              reader_page_size_cache.clear();
              adaptive_render = ReaderAdaptiveRenderState{};
              reset_reader_async_state();
              close_text_reader();
              pdf_runtime.Close();
              reader_mode = ReaderMode::Epub;
              epub_comic.SetPage(reader.page);
              invalidate_all_render_cache();
              display_state = ReaderViewState{};
              ready_state = ReaderViewState{};
              display_state_valid = false;
              ready_state_valid = false;
              target_state = ReaderViewState{reader_current_page(), reader.zoom, reader.rotation};
              clamp_scroll();
              opened = true;
            }
            if (!opened && !epub_comic.HasRealRenderer()) {
              if (!warned_epub_backend) {
                std::cerr << "[reader] blocked: current build has no epub comic backend. "
                             "Please rebuild with libzip (pkg-config libzip) available.\n";
                warned_epub_backend = true;
              }
            }
          } else {
            // Keep unsupported formats in shelf for now.
            std::cerr << "[reader] unsupported format for runtime reader: " << current_book << "\n";
            opened = false;
          }

          if (opened) {
            reader_progress_overlay_visible = false;
            state = State::Reader;
            scene_flash.Snap(kSceneFadeFlashAlpha);
            scene_flash.AnimateTo(0.0f, kSceneFadeFlashDurationSec, animation::Ease::OutCubic);
          } else {
            if (ext == ".pdf" || ext == ".epub") {
              std::cerr << "[reader] failed to open: " << current_book << "\n";
            }
            current_book.clear();
            close_text_reader();
            pdf_runtime.Close();
            epub_comic.Close();
            reader_page_size_cache.clear();
            invalidate_all_render_cache();
          }
          for (auto &v : hold_speed) v = 0.0f;
          for (auto &v : long_fired) v = false;
        }
      }
      const int new_page = shelf_page;
      if (new_page != prev_page) {
        if (ui_cfg.animations) {
          page_anim_from = prev_page;
          page_anim_to = new_page;
          page_anim_dir = (new_page > prev_page) ? 1 : -1;
          page_animating = true;
          page_slide.Snap(0.0f);
          page_slide.AnimateTo(1.0f, kPageSlideDurationSec, animation::Ease::OutCubic);
        } else {
          page_animating = false;
          page_slide.Snap(0.0f);
        }
      }
      shelf_page = new_page;
    } else if (state == State::Settings) {
      const NativeConfig &ui_cfg = config.Get();
      if (!ui_cfg.animations) {
        menu_anim.Snap(menu_closing ? 0.0f : 1.0f);
      }
      menu_anim.Update(dt);
      if (menu_closing && menu_anim.Value() <= 0.0001f && !menu_anim.IsAnimating()) state = settings_return_state;

      if (!settings_close_armed) {
        const bool any_toggle_held = input.IsPressed(Button::Start) || input.IsPressed(Button::Select);
        if (!any_toggle_held) settings_close_armed = true;
      }

      if (settings_close_armed &&
          settings_toggle_guard <= 0.0f &&
          !menu_closing && (input.IsJustPressed(Button::B) ||
                            menu_toggle_request)) {
        if (ui_cfg.animations) menu_anim.AnimateTo(0.0f, 0.16f, animation::Ease::InOutCubic);
        else menu_anim.Snap(0.0f);
        menu_closing = true;
      } else if (!menu_closing) {
        const int prev_menu_selected = menu_selected;
        if (input.IsJustPressed(Button::Up) || input.IsRepeated(Button::Up)) {
          menu_selected = ClampInt(menu_selected - 1, 0, static_cast<int>(menu_items.size()) - 1);
        } else if (input.IsJustPressed(Button::Down) || input.IsRepeated(Button::Down)) {
          menu_selected = ClampInt(menu_selected + 1, 0, static_cast<int>(menu_items.size()) - 1);
        } else if (input.IsJustPressed(Button::A) || input.IsJustPressed(Button::Right)) {
          const SettingId id = menu_items[menu_selected];
          if (id == SettingId::ExitApp) {
            running = false;
          } else if (id == SettingId::ClearHistory) {
            history_store.Clear();
            if (current_category() == ShelfCategory::History) {
              current_folder.clear();
              clear_cover_cache();
              rebuild_shelf_items();
              focus_index = 0;
              shelf_page = 0;
              page_animating = false;
              page_slide.Snap(0.0f);
              grid_item_anims.clear();
            }
          } else if (id == SettingId::CleanCache) {
            clear_runtime_cache_files();
          } else if (id == SettingId::TxtToUtf8) {
            start_txt_transcode_job();
          }
        }
        if (menu_selected != prev_menu_selected) {
        }
      }
    } else if (state == State::Reader) {
      if (input.IsJustPressed(Button::B)) {
        if (reader_mode != ReaderMode::Txt) flush_pending_page_flip_now();
        if (reader_mode == ReaderMode::Pdf && pdf_runtime.IsOpen()) {
          const PdfRuntimeProgress active_pdf = pdf_runtime.Progress();
          reader.page = active_pdf.page;
          reader.scroll_y = active_pdf.scroll_y;
          reader.zoom = active_pdf.zoom;
          reader.rotation = active_pdf.rotation;
        } else if (reader_mode == ReaderMode::Epub && epub_comic.IsOpen()) {
          reader.page = epub_comic.CurrentPage();
        } else if (reader_mode == ReaderMode::Txt && txt_reader.open) {
          reader.page = (txt_reader.line_h > 0) ? (txt_reader.scroll_px / txt_reader.line_h) : 0;
          reader.scroll_y = txt_reader.scroll_px;
          txt_reader.resume_cache_dirty = true;
          persist_current_txt_resume_snapshot(current_book, true);
        }
        ReaderProgress save_reader = reader;
        progress.Set(current_book, save_reader);
        if (reader_mode == ReaderMode::Pdf) {
          pdf_runtime.Close();
          reader_page_size_cache.clear();
          reset_reader_async_state();
        } else if (reader_mode == ReaderMode::Epub) {
          epub_comic.Close();
          reader_page_size_cache.clear();
          reset_reader_async_state();
        } else if (reader_mode == ReaderMode::Txt) {
          close_text_reader();
        }
        invalidate_all_render_cache();
        display_state = ReaderViewState{};
        ready_state = ReaderViewState{};
        target_state = ReaderViewState{};
        display_state_valid = false;
        ready_state_valid = false;
        reader_mode = ReaderMode::None;
        reader_progress_overlay_visible = false;
        state = State::Shelf;
        scene_flash.Snap(kSceneFadeFlashAlpha);
        scene_flash.AnimateTo(0.0f, kSceneFadeFlashDurationSec, animation::Ease::OutCubic);
      } else {
        if (input.IsJustPressed(Button::X)) {
          reader_progress_overlay_visible = !reader_progress_overlay_visible;
        }
        if (reader_mode == ReaderMode::Txt && txt_reader.open) {
          std::array<Button, 2> vdirs = {Button::Up, Button::Down};
          for (Button b : vdirs) {
            int bi = static_cast<int>(b);
            const int long_dir = (b == Button::Down) ? 1 : -1;
            if (input.IsPressed(b)) {
              const float hold = input.HoldTime(b);
              const float delay = 0.30f;
              const float speed_min = 120.0f;
              const float speed_max = 620.0f;
              const float speed_accel = 860.0f;
              if (hold >= delay) {
                long_fired[bi] = true;
                hold_speed[bi] = (hold_speed[bi] <= 0.0f)
                                     ? speed_min
                                     : std::min(speed_max, hold_speed[bi] + speed_accel * dt);
                const int step_px = std::max(1, static_cast<int>(hold_speed[bi] * dt));
                text_scroll_by(long_dir * step_px);
              } else {
                hold_speed[bi] = 0.0f;
              }
            } else {
              hold_speed[bi] = 0.0f;
            }
          }
          for (Button b : vdirs) {
            int bi = static_cast<int>(b);
            if (!input.IsJustReleased(b)) continue;
            hold_speed[bi] = 0.0f;
            if (long_fired[bi]) {
              long_fired[bi] = false;
              continue;
            }
            const int tap_dir = (b == Button::Down) ? 1 : -1;
            text_scroll_by(tap_dir * kReaderTapStepPx);
          }
          if (input.IsJustPressed(Button::Right)) {
            text_page_by(1);
          } else if (input.IsJustPressed(Button::Left)) {
            text_page_by(-1);
          }
          reader.page = (txt_reader.line_h > 0) ? (txt_reader.scroll_px / txt_reader.line_h) : 0;
          reader.scroll_y = txt_reader.scroll_px;
        } else if (reader_mode == ReaderMode::Pdf) {
          const int pdf_rotation = pdf_runtime.Progress().rotation;
          auto pdf_long_dir_for_button = [&](Button b) -> int {
            if (pdf_rotation == 0) {
              if (b == Button::Down) return 1;
              if (b == Button::Up) return -1;
            } else if (pdf_rotation == 90) {
              if (b == Button::Left) return 1;   // left -> scroll down
              if (b == Button::Right) return -1; // right -> scroll up
            } else if (pdf_rotation == 180) {
              if (b == Button::Up) return 1;
              if (b == Button::Down) return -1;
            } else { // 270
              if (b == Button::Left) return -1;  // left -> scroll up
              if (b == Button::Right) return 1;  // right -> scroll down
            }
            return 0;
          };
          auto pdf_tap_page_action_for_button = [&](Button b) -> int {
            if (pdf_rotation == 0) {
              if (b == Button::Right) return 1;
              if (b == Button::Left) return -1;
            } else if (pdf_rotation == 90) {
              if (b == Button::Up) return -1;    // up -> previous page
              if (b == Button::Down) return 1;   // down -> next page
            } else if (pdf_rotation == 180) {
              if (b == Button::Left) return 1;   // left -> next page
              if (b == Button::Right) return -1; // right -> previous page
            } else { // 270
              if (b == Button::Up) return 1;     // up -> next page
              if (b == Button::Down) return -1;  // down -> previous page
            }
            return 0;
          };
          if (input.IsJustPressed(Button::L2)) {
            pdf_runtime.RotateLeft();
          }
          if (input.IsJustPressed(Button::R2)) {
            pdf_runtime.RotateRight();
          }
          if (input.IsJustPressed(Button::L1)) {
            pdf_runtime.ZoomOut();
          }
          if (input.IsJustPressed(Button::R1)) {
            pdf_runtime.ZoomIn();
          }
          if (input.IsJustPressed(Button::A)) {
            pdf_runtime.ResetView();
          }

          std::array<Button, 4> dirs = {Button::Up, Button::Down, Button::Left, Button::Right};
          for (Button b : dirs) {
            int bi = static_cast<int>(b);
            int long_dir = pdf_long_dir_for_button(b);
            if (long_dir == 0) {
              hold_speed[bi] = 0.0f;
              continue;
            }
            if (input.IsPressed(b) && input.HoldTime(b) >= 0.28f) {
              long_fired[bi] = true;
              pdf_runtime.ScrollByPixels(long_dir * 20);
            } else if (!input.IsPressed(b)) {
              hold_speed[bi] = 0.0f;
            }
          }

          for (Button b : dirs) {
            int bi = static_cast<int>(b);
            if (!input.IsJustReleased(b)) continue;
            if (long_fired[bi]) {
              long_fired[bi] = false;
              continue;
            }
            const int tap_dir = pdf_long_dir_for_button(b);
            if (tap_dir != 0) {
              pdf_runtime.ScrollByPixels(tap_dir * 60);
            } else {
              const int page_action = pdf_tap_page_action_for_button(b);
              if (page_action != 0) {
                pdf_runtime.JumpByScreen(page_action);
              }
            }
          }
        } else {
          if (input.IsJustPressed(Button::L2) || input.IsJustPressed(Button::R2) ||
              input.IsJustPressed(Button::L1) || input.IsJustPressed(Button::R1) ||
              input.IsJustPressed(Button::A)) {
            flush_pending_page_flip_now();
          }
          if (input.IsJustPressed(Button::L2)) {
            ReaderViewState next_view = target_state;
            next_view.rotation = (next_view.rotation + 270) % 360;
            commit_target_view(next_view, true, true);
          }
          if (input.IsJustPressed(Button::R2)) {
            ReaderViewState next_view = target_state;
            next_view.rotation = (next_view.rotation + 90) % 360;
            commit_target_view(next_view, true, true);
          }
          if (input.IsJustPressed(Button::L1)) {
            ReaderViewState next_view = target_state;
            next_view.zoom = std::max(0.25f, next_view.zoom / 1.1f);
            commit_target_view(next_view, false, true);
          }
          if (input.IsJustPressed(Button::R1)) {
            ReaderViewState next_view = target_state;
            next_view.zoom = std::min(6.0f, next_view.zoom * 1.1f);
            commit_target_view(next_view, false, true);
          }
          if (input.IsJustPressed(Button::A)) {
            ReaderViewState next_view = target_state;
            next_view.zoom = 1.0f;
            commit_target_view(next_view, false, true);
            reader.scroll_x = reader.scroll_y = 0;
            clamp_scroll();
          }

          std::array<Button, 4> dirs = {Button::Up, Button::Down, Button::Left, Button::Right};
          for (Button b : dirs) {
            int bi = static_cast<int>(b);
            int long_dir = long_dir_for_button(b);
            if (long_dir == 0) {
              hold_speed[bi] = 0.0f;
              continue;
            }
            if (input.IsPressed(b)) {
              const float hold = input.HoldTime(b);
              float delay = (target_state.rotation == 0) ? 0.33f : 0.28f;
              float speed_min = (target_state.rotation == 0) ? 95.0f : 120.0f;
              float speed_max = (target_state.rotation == 0) ? 500.0f : 680.0f;
              float speed_accel = (target_state.rotation == 0) ? 620.0f : 920.0f;
              if (hold >= delay) {
                flush_pending_page_flip_now();
                long_fired[bi] = true;
                hold_speed[bi] = (hold_speed[bi] <= 0.0f) ? speed_min : std::min(speed_max, hold_speed[bi] + speed_accel * dt);
                const int step_px = std::max(1, static_cast<int>(hold_speed[bi] * dt));
                scroll_by_dir(long_dir, step_px);
              } else {
                hold_speed[bi] = 0.0f;
              }
            } else {
              hold_speed[bi] = 0.0f;
            }
          }

          for (Button b : dirs) {
            int bi = static_cast<int>(b);
            if (!input.IsJustReleased(b)) continue;
            hold_speed[bi] = 0.0f;
            if (long_fired[bi]) {
              long_fired[bi] = false;
              continue;
            }
            const int tap_dir = long_dir_for_button(b);
            if (tap_dir != 0) {
              flush_pending_page_flip_now();
              scroll_by_dir(tap_dir, kReaderTapStepPx);
            } else {
              const int page_action = tap_page_action_for_button(b);
              if (page_action > 0) {
                queue_page_flip(1);
              } else if (page_action < 0) {
                queue_page_flip(-1);
              }
            }
          }
        }
      }
    }

    if (state == State::Reader && reader_mode != ReaderMode::Txt && reader_mode != ReaderMode::Pdf) {
      flush_pending_page_flip();
    }

    any_grid_animating = false;
    if (animate_enabled) {
      scene_flash.Update(dt);
      page_slide.Update(dt);
      if (page_animating && !page_slide.IsAnimating() && page_slide.Value() >= 0.999f) {
        page_animating = false;
        page_slide.Snap(0.0f);
      }
    } else {
      page_animating = false;
      page_slide.Snap(0.0f);
      scene_flash.Snap(0.0f);
      if (state != State::Settings) menu_anim.Snap(0.0f);
    }

    // Draw
    SDL_SetRenderDrawColor(renderer, 26, 27, 31, 255);
    SDL_RenderClear(renderer);

    if (state == State::Boot) {
      DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{20, 20, 24, 255});
      const int bar_x = 40;
      const int bar_y = Layout().screen_h / 2;
      const int bar_w = Layout().screen_w - 80;
      const float progress = std::clamp(boot_progress_ratio(), 0.0f, 1.0f);
      const int fill_w = static_cast<int>(std::round(progress * bar_w));
      DrawRect(renderer, bar_x, bar_y, bar_w, 16, SDL_Color{48, 52, 60, 255});
      DrawRect(renderer, bar_x, bar_y, fill_w, 16, SDL_Color{210, 210, 210, 255});
      DrawRect(renderer, bar_x, bar_y, bar_w, 16, SDL_Color{255, 255, 255, 220}, false);
#ifdef HAVE_SDL2_TTF
      SDL_Color boot_text_color{232, 236, 244, 255};
      if (!boot_status_text.empty()) {
        if (TextCacheEntry *te = get_text_texture(boot_status_text, boot_text_color); te && te->texture) {
          SDL_Rect td{std::max(0, (Layout().screen_w - te->w) / 2), bar_y + 28, te->w, te->h};
          SDL_RenderCopy(renderer, te->texture, nullptr, &td);
        }
      }
#endif
    } else {
      const NativeConfig &cfg = config.Get();
      const SDL_Color bg = (cfg.theme == 0) ? SDL_Color{22, 23, 29, 255} : SDL_Color{238, 237, 233, 255};
      DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, bg);

      std::function<void()> draw_volume_overlay = []() {};
      if (state == State::Shelf || state == State::Settings) {
        auto draw_native = [&](SDL_Texture *tex, int x, int y) {
          if (!tex) return;
          int tw = 0;
          int th = 0;
          get_texture_size(tex, tw, th);
          if (tw <= 0 || th <= 0) return;
          SDL_Rect dst{x, y, tw, th};
          SDL_RenderCopy(renderer, tex, nullptr, &dst);
        };
        draw_volume_overlay = [&]() {
#ifdef HAVE_SDL2_TTF
          if (now > volume_display_until) return;
          SDL_Color volume_text{238, 242, 250, 255};
          const std::string label = std::to_string(volume_display_percent);
          TextCacheEntry *te = get_text_texture(label, volume_text);
          if (!te || !te->texture) return;
          const int tx = 18;
          const int ty = Layout().top_bar_y + std::max(0, (Layout().top_bar_h - te->h) / 2);
          SDL_Rect td{tx, ty, te->w, te->h};
          SDL_RenderCopy(renderer, te->texture, nullptr, &td);
#endif
        };
        struct RenderEntry {
          int index = -1;
          bool focused = false;
          bool on_current_page = false;
        };
        std::vector<RenderEntry> render_items;
        render_items.reserve(kItemsPerPage * 2);

        struct PagePlan {
          int page = 0;
          float shift_y = 0.0f;
        };
        std::vector<PagePlan> pages_to_draw;
        if (page_animating) {
          const float slide = std::clamp(page_slide.Value(), 0.0f, 1.0f);
          pages_to_draw.push_back(PagePlan{page_anim_from, -static_cast<float>(page_anim_dir) * Layout().screen_h * slide});
          pages_to_draw.push_back(
              PagePlan{page_anim_to, static_cast<float>(page_anim_dir) * Layout().screen_h * (1.0f - slide)});
        } else {
          pages_to_draw.push_back(PagePlan{shelf_page, 0.0f});
        }

        for (const auto &pp : pages_to_draw) {
          const int start = pp.page * kGridCols;
          const int end = std::min<int>(start + kItemsPerPage, shelf_items.size());
          for (int i = start; i < end; ++i) {
            int local = i - start;
            int row = local / kGridCols;
            int col = local % kGridCols;
            float base_x = static_cast<float>(Layout().grid_start_x + col * (Layout().cover_w + Layout().grid_gap_x));
            float base_y = static_cast<float>(Layout().grid_start_y + row * (Layout().cover_h + Layout().grid_gap_y));
            float base_cx = base_x + static_cast<float>(Layout().cover_w) * 0.5f;
            float base_cy = base_y + static_cast<float>(Layout().cover_h) * 0.5f;
            bool focused = (i == focus_index);

            GridItemAnim &anim = grid_item_anims[i];
            anim.tcx = base_cx;
            anim.tcy = base_cy + pp.shift_y;
            anim.tw = focused ? static_cast<float>(FocusedCoverW()) : static_cast<float>(Layout().cover_w);
            anim.th = focused ? static_cast<float>(FocusedCoverH()) : static_cast<float>(Layout().cover_h);
            anim.t_alpha = focused ? 255.0f : static_cast<float>(kUnfocusedAlpha);
            // Keep page transition as a clean linear slide: while page is sliding,
            // do not add per-card interpolation on top.
            if (!animate_enabled || page_animating) {
              anim.SnapToTarget();
            } else {
              anim.Update(dt);
              any_grid_animating = any_grid_animating || anim.IsAnimating();
            }
            render_items.push_back(RenderEntry{i, focused, pp.page == shelf_page});
          }
        }

        auto draw_cover = [&](const BookItem &item, SDL_Rect dst, Uint8 alpha) {
          SDL_Texture *cover_tex = get_cover_texture(item);
          if (cover_tex) {
            int tw = 0;
            int th = 0;
            get_texture_size(cover_tex, tw, th);
            if (tw > 0 && th > 0) {
              const float src_aspect = static_cast<float>(tw) / static_cast<float>(th);
              const float dst_aspect = kCoverAspect;
              SDL_Rect src{0, 0, tw, th};
              if (src_aspect > dst_aspect) {
                src.w = static_cast<int>(std::round(th * dst_aspect));
                src.x = (tw - src.w) / 2;
              } else if (src_aspect < dst_aspect) {
                src.h = static_cast<int>(std::round(tw / dst_aspect));
                src.y = (th - src.h) / 2;
              }
              SDL_SetTextureAlphaMod(cover_tex, alpha);
              SDL_RenderCopy(renderer, cover_tex, &src, &dst);
              SDL_SetTextureAlphaMod(cover_tex, 255);
              return;
            }
          }
          SDL_Color c = item.is_dir ? SDL_Color{86, 121, 157, alpha} : SDL_Color{66, 81, 102, alpha};
          DrawRect(renderer, dst.x, dst.y, dst.w, dst.h, c);
        };

        auto make_outer_frame_rect = [&](const SDL_Rect &cover_rect) {
          const float sx = static_cast<float>(cover_rect.w) / static_cast<float>(Layout().cover_w);
          const float sy = static_cast<float>(cover_rect.h) / static_cast<float>(Layout().cover_h);
          const int outer_w = std::max(1, static_cast<int>(std::round(Layout().card_frame_w * sx)));
          const int outer_h = std::max(1, static_cast<int>(std::round(Layout().card_frame_h * sy)));
          const int cx = cover_rect.x + cover_rect.w / 2;
          const int cy = cover_rect.y + cover_rect.h / 2;
          return SDL_Rect{cx - outer_w / 2, cy - outer_h / 2, outer_w, outer_h};
        };

        auto draw_cover_under_shadow = [&](const SDL_Rect &outer_rect) {
          if (!ui_assets.book_under_shadow) return;
          SDL_RenderCopy(renderer, ui_assets.book_under_shadow, nullptr, &outer_rect);
        };

        auto draw_cover_select = [&](const SDL_Rect &outer_rect) {
          if (!ui_assets.book_select) return;
          SDL_RenderCopy(renderer, ui_assets.book_select, nullptr, &outer_rect);
        };

        auto draw_title_overlay = [&](const BookItem &item, const SDL_Rect &dst, bool focused) {
          if (ui_assets.book_title_shadow) {
            SDL_Rect od{dst.x, dst.y, dst.w, dst.h};
            SDL_RenderCopy(renderer, ui_assets.book_title_shadow, nullptr, &od);
          }

#ifdef HAVE_SDL2_TTF
          SDL_Color title_color = focused ? SDL_Color{248, 250, 255, 255} : SDL_Color{230, 236, 248, 244};
          const int text_area_x = dst.x + Layout().title_text_pad_x;
          const int text_area_w = std::max(8, dst.w - Layout().title_text_pad_x * 2);
          const int text_area_h = std::max(12, Layout().title_overlay_h - 2);
          const int text_area_y = dst.y + dst.h - text_area_h - Layout().title_text_pad_bottom;
          SDL_Rect clip{text_area_x, text_area_y, text_area_w, text_area_h};

          auto measure = [&](const std::string &s) -> int {
            TextCacheEntry *te = get_text_texture(s, title_color);
            return (te && te->texture) ? te->w : static_cast<int>(s.size()) * 8;
          };

          std::string display = shelf_title_text(item);
          if (!focused) {
            display = get_title_ellipsized(display, text_area_w, measure);
            TextCacheEntry *te = get_text_texture(display, title_color);
            if (te && te->texture) {
              SDL_RenderSetClipRect(renderer, &clip);
              const int centered_x = text_area_x + std::max(0, (text_area_w - te->w) / 2);
              const int text_y = text_area_y + std::max(0, (text_area_h - te->h) / 2);
              SDL_Rect td{centered_x, text_y, te->w, te->h};
              SDL_RenderCopy(renderer, te->texture, nullptr, &td);
              SDL_RenderSetClipRect(renderer, nullptr);
            }
          } else {
            TextCacheEntry *te = get_text_texture(display, title_color);
            if (te && te->texture) {
              SDL_RenderSetClipRect(renderer, &clip);
              const int text_y = text_area_y + std::max(0, (text_area_h - te->h) / 2);
              if (te->w > text_area_w) {
                const float span = static_cast<float>(te->w + Layout().title_marquee_gap_px);
                const float xoff = std::fmod(title_marquee_offset, span);
                SDL_Rect td1{text_area_x - static_cast<int>(std::round(xoff)), text_y, te->w, te->h};
                SDL_Rect td2{td1.x + static_cast<int>(span), text_y, te->w, te->h};
                SDL_RenderCopy(renderer, te->texture, nullptr, &td1);
                SDL_RenderCopy(renderer, te->texture, nullptr, &td2);
              } else {
                const int centered_x = text_area_x + std::max(0, (text_area_w - te->w) / 2);
                SDL_Rect td{centered_x, text_y, te->w, te->h};
                SDL_RenderCopy(renderer, te->texture, nullptr, &td);
              }
              SDL_RenderSetClipRect(renderer, nullptr);
            }
          }
#endif
        };

        auto render_shelf_static_layer = [&]() {
          if (ui_assets.background_main) {
            SDL_Rect bg_dst{0, 0, Layout().screen_w, Layout().screen_h};
            SDL_RenderCopy(renderer, ui_assets.background_main, nullptr, &bg_dst);
          } else {
            DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{26, 27, 31, 255});
          }

          for (const RenderEntry &e : render_items) {
            if (e.focused) continue;
            const BookItem &item = shelf_items[e.index];
            GridItemAnim &anim = grid_item_anims[e.index];
            SDL_Rect dst{
                static_cast<int>(std::round(anim.x)),
                static_cast<int>(std::round(anim.y)),
                static_cast<int>(std::round(anim.w)),
                static_cast<int>(std::round(anim.h)),
            };
            const SDL_Rect outer = make_outer_frame_rect(dst);
            const Uint8 alpha = static_cast<Uint8>(std::clamp(anim.alpha, 0.0f, 255.0f));
            draw_cover_under_shadow(outer);
            draw_cover(item, dst, alpha);
            if (!page_animating || e.on_current_page) {
              draw_title_overlay(item, dst, false);
            }
          }

          if (ui_assets.top_status_bar) {
            draw_native(ui_assets.top_status_bar, 0, 0);
          } else {
            DrawRect(renderer,
                     0,
                     Layout().top_bar_y,
                     Layout().screen_w,
                     Layout().top_bar_h,
                     SDL_Color{8, 10, 14, 255});
          }
          if (ui_assets.bottom_hint_bar) {
            int bw = 0, bh = 0;
            get_texture_size(ui_assets.bottom_hint_bar, bw, bh);
            draw_native(ui_assets.bottom_hint_bar, 0, Layout().screen_h - bh);
          } else {
            DrawRect(renderer,
                     0,
                     Layout().bottom_bar_y,
                     Layout().screen_w,
                     Layout().bottom_bar_h,
                     SDL_Color{8, 10, 14, 255});
          }
          if (ui_assets.nav_l1_icon) draw_native(ui_assets.nav_l1_icon, Layout().nav_l1_x, Layout().nav_l1_y);
          if (ui_assets.nav_r1_icon) draw_native(ui_assets.nav_r1_icon, Layout().nav_r1_x, Layout().nav_r1_y);
          {
            const int nav_start_x = Layout().nav_start_x;
            const int nav_slot_w = Layout().nav_slot_w;
            const int nav_y = Layout().nav_y;
            int nav_pill_h = 32;
            if (ui_assets.nav_selected_pill) {
              int pw = 0, ph = 0;
              get_texture_size(ui_assets.nav_selected_pill, pw, ph);
              if (ph > 0) nav_pill_h = ph;
              const int slot_center_x = nav_start_x + nav_selected_index * nav_slot_w + nav_slot_w / 2;
              draw_native(ui_assets.nav_selected_pill, slot_center_x - pw / 2, nav_y);
            }
#ifdef HAVE_SDL2_TTF
            const std::array<std::string, 4> nav_labels = {
                "ALL COMICS",
                "ALL BOOKS",
                "COLLECTIONS",
                "HISTORY",
            };
            SDL_Color nav_text = SDL_Color{238, 242, 250, 255};
            for (int i = 0; i < static_cast<int>(nav_labels.size()); ++i) {
              TextCacheEntry *te = get_text_texture(nav_labels[i], nav_text);
              if (!te || !te->texture) continue;
              const int slot_x = nav_start_x + i * nav_slot_w;
              const int tx = slot_x + std::max(0, (nav_slot_w - te->w) / 2);
              const int ty = nav_y + std::max(0, (nav_pill_h - te->h) / 2);
              SDL_Rect td{tx, ty, te->w, te->h};
              SDL_RenderCopy(renderer, te->texture, nullptr, &td);
            }
#endif
          }
        };

        const bool can_use_shelf_render_cache = false &&
            renderer_supports_target_textures && !page_animating && !any_grid_animating;
        const bool shelf_render_cache_matches =
            shelf_render_cache.texture &&
            shelf_render_cache.focus_index == focus_index &&
            shelf_render_cache.shelf_page == shelf_page &&
            shelf_render_cache.nav_selected_index == nav_selected_index &&
            shelf_render_cache.content_version == shelf_content_version;

        if (can_use_shelf_render_cache && shelf_render_cache_matches) {
          SDL_RenderCopy(renderer, shelf_render_cache.texture, nullptr, nullptr);
        } else if (can_use_shelf_render_cache) {
          invalidate_shelf_render_cache();
          SDL_Texture *cache_tex =
              SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, Layout().screen_w, Layout().screen_h);
          if (cache_tex) {
            SDL_SetTextureBlendMode(cache_tex, SDL_BLENDMODE_BLEND);
            remember_texture_size(cache_tex, Layout().screen_w, Layout().screen_h);
            if (SDL_SetRenderTarget(renderer, cache_tex) == 0) {
              SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
              SDL_RenderClear(renderer);
              render_shelf_static_layer();
              SDL_SetRenderTarget(renderer, nullptr);
              shelf_render_cache.texture = cache_tex;
              shelf_render_cache.focus_index = focus_index;
              shelf_render_cache.shelf_page = shelf_page;
              shelf_render_cache.nav_selected_index = nav_selected_index;
              shelf_render_cache.content_version = shelf_content_version;
              SDL_RenderCopy(renderer, shelf_render_cache.texture, nullptr, nullptr);
            } else {
              forget_texture_size(cache_tex);
              SDL_DestroyTexture(cache_tex);
              render_shelf_static_layer();
            }
          } else {
            invalidate_shelf_render_cache();
            render_shelf_static_layer();
          }
        } else {
          invalidate_shelf_render_cache();
          render_shelf_static_layer();
        }

        for (const RenderEntry &e : render_items) {
          if (!e.focused) continue;
          const BookItem &item = shelf_items[e.index];
          GridItemAnim &anim = grid_item_anims[e.index];
          SDL_Rect focus_rect{
              static_cast<int>(std::round(anim.x)),
              static_cast<int>(std::round(anim.y)),
              static_cast<int>(std::round(anim.w)),
              static_cast<int>(std::round(anim.h)),
          };
          const SDL_Rect outer = make_outer_frame_rect(focus_rect);
          draw_cover_under_shadow(outer);
          draw_cover(item, focus_rect, 255);
          draw_title_overlay(item, focus_rect, true);
          draw_cover_select(outer);
          break;
        }

        if (state != State::Settings) {
          draw_volume_overlay();
        }
      }

      if (state == State::Reader) {
        DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{12, 12, 12, 255});
        if (reader_mode == ReaderMode::Txt && txt_reader.open) {
          clamp_text_scroll();
#ifdef HAVE_SDL2_TTF
          const SDL_Rect clip{txt_reader.viewport_x, txt_reader.viewport_y, txt_reader.viewport_w, txt_reader.viewport_h};
          SDL_RenderSetClipRect(renderer, &clip);
          const SDL_Color color{242, 244, 248, 255};
          const int text_x = txt_reader.viewport_x + 2;
          const int start_line = std::max(0, txt_reader.scroll_px / std::max(1, txt_reader.line_h));
          int y = txt_reader.viewport_y - (txt_reader.scroll_px % std::max(1, txt_reader.line_h));
          for (int i = start_line; i < static_cast<int>(txt_reader.lines.size()); ++i) {
            if (y > txt_reader.viewport_y + txt_reader.viewport_h) break;
            TextCacheEntry *te = get_reader_text_texture(txt_reader.lines[i], color);
            if (te && te->texture) {
              SDL_Rect td{text_x, y, te->w, te->h};
              SDL_RenderCopy(renderer, te->texture, nullptr, &td);
            }
            y += txt_reader.line_h;
          }
          SDL_RenderSetClipRect(renderer, nullptr);
#endif
        } else if (reader_mode == ReaderMode::Pdf && pdf_runtime.IsOpen()) {
          pdf_runtime.UpdateViewport(Layout().screen_w, Layout().screen_h);
          pdf_runtime.Tick();
          pdf_runtime.Draw(renderer);
        } else {
          clamp_scroll();
          ensure_render();
          const ReaderViewState draw_state =
              display_state_valid ? display_state : target_state;
          const ReaderPageRenderMode draw_mode = reader_page_render_mode_for_state(draw_state);
          const float draw_scale =
              (display_state_valid && render_cache.texture
                   ? render_cache.scale
                   : reader_target_scale_for_state(draw_state));
          const bool showing_placeholder = (!display_state_valid || display_state != target_state);
          bool drew_reader_content = false;
          if (const ReaderRenderCache *cache =
                  visible_reader_render_cache_for_page(draw_state.page, draw_state.rotation, draw_scale)) {
            const bool cache_matches_display =
                cache->page == draw_state.page &&
                cache->rotation == draw_state.rotation &&
                std::abs(cache->scale - draw_scale) < 0.0005f;
            int draw_x =
                (cache->display_w <= Layout().screen_w)
                    ? ((Layout().screen_w - cache->display_w) / 2)
                    : (!showing_placeholder && cache_matches_display ? -reader.scroll_x : 0);
            int draw_y =
                (cache->display_h <= Layout().screen_h)
                    ? ((Layout().screen_h - cache->display_h) / 2)
                    : (!showing_placeholder && cache_matches_display ? -reader.scroll_y : 0);
            SDL_Rect dst{draw_x, draw_y, cache->display_w, cache->display_h};
            SDL_RenderCopy(renderer, cache->texture, nullptr, &dst);
            drew_reader_content = true;
#ifdef HAVE_SDL2_TTF
            if (showing_placeholder || !cache_matches_display) {
              DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{0, 0, 0, 96});
              if (!adaptive_render.pending_page_active) {
                const int panel_w = std::min(Layout().screen_w - 40, 280);
                const int panel_h = 64;
                const int panel_x = (Layout().screen_w - panel_w) / 2;
                const int panel_y = (Layout().screen_h - panel_h) / 2;
                DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{16, 16, 18, 200});
                DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{255, 255, 255, 40}, false);
                SDL_Color text_color{244, 246, 250, 255};
                if (TextCacheEntry *te = get_text_texture("Rendering...", text_color); te && te->texture) {
                  SDL_Rect td{
                      panel_x + std::max(0, (panel_w - te->w) / 2),
                      panel_y + std::max(0, (panel_h - te->h) / 2),
                      te->w,
                      te->h,
                  };
                  SDL_RenderCopy(renderer, te->texture, nullptr, &td);
                }
              }
            }
#endif
          }
#ifdef HAVE_SDL2_TTF
          if (!drew_reader_content && !adaptive_render.pending_page_active) {
            DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{0, 0, 0, 96});
            const int panel_w = std::min(Layout().screen_w - 40, 280);
            const int panel_h = 64;
            const int panel_x = (Layout().screen_w - panel_w) / 2;
            const int panel_y = (Layout().screen_h - panel_h) / 2;
            DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{16, 16, 18, 200});
            DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{255, 255, 255, 40}, false);
            SDL_Color text_color{244, 246, 250, 255};
            if (TextCacheEntry *te = get_text_texture("Rendering...", text_color); te && te->texture) {
              SDL_Rect td{
                  panel_x + std::max(0, (panel_w - te->w) / 2),
                  panel_y + std::max(0, (panel_h - te->h) / 2),
                  te->w,
                  te->h,
              };
              SDL_RenderCopy(renderer, te->texture, nullptr, &td);
            }
          }
#endif
#ifdef HAVE_SDL2_TTF
          if (adaptive_render.pending_page_active && adaptive_render.fast_flip_mode) {
            DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h, SDL_Color{0, 0, 0, 120});
            const int panel_w = std::min(Layout().screen_w - 40, 320);
            const int panel_h = 76;
            const int panel_x = (Layout().screen_w - panel_w) / 2;
            const int panel_y = (Layout().screen_h - panel_h) / 2;
            DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{16, 16, 18, 220});
            DrawRect(renderer, panel_x, panel_y, panel_w, panel_h, SDL_Color{255, 255, 255, 48}, false);
            const int pending_page = ClampInt(adaptive_render.pending_page, 0, std::max(0, reader_page_count() - 1));
            const std::string fast_flip_text =
                "Quick Jump: " + std::to_string(pending_page + 1) + " / " + std::to_string(std::max(1, reader_page_count()));
            SDL_Color text_color{244, 246, 250, 255};
            if (TextCacheEntry *te = get_text_texture(fast_flip_text, text_color); te && te->texture) {
              SDL_Rect td{
                  panel_x + std::max(0, (panel_w - te->w) / 2),
                  panel_y + std::max(0, (panel_h - te->h) / 2),
                  te->w,
                  te->h,
              };
              SDL_RenderCopy(renderer, te->texture, nullptr, &td);
            }
          }
#endif
        }
#ifdef HAVE_SDL2_TTF
        if (reader_progress_overlay_visible) {
          int pct = 0;
          if (reader_mode == ReaderMode::Txt && txt_reader.open) {
            const int max_scroll = std::max(0, txt_reader.content_h - txt_reader.viewport_h);
            pct = (max_scroll > 0)
                      ? ClampInt(static_cast<int>((static_cast<int64_t>(txt_reader.scroll_px) * 100) / max_scroll), 0, 100)
                      : 100;
          } else if (reader_is_open()) {
            const int page_count = std::max(1, reader_page_count());
            const int page_idx = ClampInt(
                (reader_mode == ReaderMode::Pdf)
                    ? pdf_runtime.Progress().page
                    : target_state.page,
                0, page_count - 1);
            pct = (page_count <= 1) ? 100 : ClampInt(static_cast<int>((static_cast<int64_t>(page_idx) * 100) / (page_count - 1)), 0, 100);
          }
          const int panel_h = 58;
          const int panel_y = Layout().screen_h - panel_h - Layout().reader_progress_panel_margin_bottom;
          DrawRect(renderer,
                   Layout().reader_progress_panel_margin_x,
                   panel_y,
                   Layout().screen_w - Layout().reader_progress_panel_margin_x * 2,
                   panel_h,
                   SDL_Color{0, 0, 0, 178});

          const int bar_x = Layout().reader_progress_bar_margin_x;
          const int bar_y = panel_y + 30;
          const int bar_w = Layout().screen_w - Layout().reader_progress_bar_margin_x * 2;
          const int bar_h = 12;
          DrawRect(renderer, bar_x, bar_y, bar_w, bar_h, SDL_Color{60, 60, 60, 220});
          const int fill_w = std::max(0, std::min(bar_w, (bar_w * pct) / 100));
          if (fill_w > 0) {
            DrawRect(renderer, bar_x, bar_y, fill_w, bar_h, SDL_Color{230, 230, 230, 235});
          }
          DrawRect(renderer, bar_x, bar_y, bar_w, bar_h, SDL_Color{255, 255, 255, 220}, false);

          SDL_Color tc{245, 245, 245, 255};
          const std::string percent = std::to_string(pct) + "%";
          if (TextCacheEntry *te = get_text_texture(percent, tc); te && te->texture) {
            SDL_Rect td{Layout().screen_w - Layout().reader_progress_percent_margin_x - te->w, panel_y + 8, te->w, te->h};
            SDL_RenderCopy(renderer, te->texture, nullptr, &td);
          }
        }
#endif
      }

      if (state == State::Settings) {
        const float anim_progress = menu_anim.Value();
        const float eased = cfg.animations ? animation::ApplyEase(animation::Ease::OutCubic, anim_progress) : anim_progress;
        const int menu_y = Layout().settings_y_offset;
        const int menu_h = std::max(0, Layout().screen_h - menu_y);
        int x = static_cast<int>(-menu_width + menu_width * eased);
        DrawRect(renderer, x, menu_y, menu_width, menu_h, SDL_Color{0, 0, 0, static_cast<Uint8>(eased * kSidebarMaskMaxAlpha)});
        const int preview_x = x + menu_width;
        const int preview_w = std::max(0, Layout().screen_w - preview_x);
        int preview_center_x = preview_x + preview_w / 2;
        if (preview_w > 0) {
          SDL_Texture *preview_tex = nullptr;
          const SettingId selected = menu_items[ClampInt(menu_selected, 0, static_cast<int>(menu_items.size()) - 1)];
          if (selected == SettingId::KeyGuide) preview_tex = ui_assets.settings_preview_keyguide;
          else if (selected == SettingId::ClearHistory) preview_tex = ui_assets.settings_preview_clean_history;
          else if (selected == SettingId::CleanCache) preview_tex = ui_assets.settings_preview_clean_cache;
          else if (selected == SettingId::TxtToUtf8) preview_tex = ui_assets.settings_preview_txt_to_utf8;
          else if (selected == SettingId::ContactMe) preview_tex = ui_assets.settings_preview_contact;
          else if (selected == SettingId::ExitApp) preview_tex = ui_assets.settings_preview_exit;

          if (preview_tex) {
            int pw = 0;
            int ph = 0;
            get_texture_size(preview_tex, pw, ph);
            SDL_Rect pd{preview_x, menu_y, pw, ph};
            SDL_RenderCopy(renderer, preview_tex, nullptr, &pd);
            preview_center_x = pd.x + pd.w / 2;
          }
        }
        DrawRect(renderer, x, menu_y, menu_width, menu_h, SDL_Color{24, 34, 46, 236});
        DrawRect(renderer, x + menu_width - 1, menu_y, 1, menu_h, SDL_Color{82, 125, 158, 255});
        int text_left = x + 32;
        int y = menu_y + 84 + Layout().settings_content_offset_y;
#ifdef HAVE_SDL2_TTF
        const std::string menu_title = std::string(u8"ROC\u5168\u80fd\u6f2b\u753b\u9605\u8bfb\u5668");
        SDL_Color title_color{240, 246, 255, 255};
        SDL_Color item_color{230, 236, 248, 255};
        TextCacheEntry *title_tex = get_title_text_texture(menu_title, title_color);
        int divider_y = menu_y + 68 + Layout().settings_content_offset_y;
        if (title_tex && title_tex->texture) {
          const int side_margin = std::max(0, (menu_width - title_tex->w) / 2);
          const int title_x = x + side_margin;
          const int title_y = menu_y + side_margin + Layout().settings_content_offset_y;
          const int title_gap = side_margin;
          divider_y = title_y + title_tex->h + title_gap;
          SDL_Rect td{title_x, title_y, title_tex->w, title_tex->h};
          SDL_RenderCopy(renderer, title_tex->texture, nullptr, &td);
        }
        DrawRect(renderer, x + 8, divider_y, menu_width - 16, 1, SDL_Color{66, 95, 124, 255});
        y = divider_y + 12;
        text_left = x + 32;
#else
        DrawRect(renderer,
                 x + 8,
                 72 + Layout().settings_content_offset_y,
                 menu_width - 16,
                 1,
                 SDL_Color{66, 95, 124, 255});
#endif
        for (size_t i = 0; i < menu_items.size(); ++i) {
          const bool sel = (static_cast<int>(i) == menu_selected);
          SDL_Color c = sel ? SDL_Color{63, 119, 158, 255} : SDL_Color{57, 73, 96, 214};
          DrawRect(renderer, x + 12, y, menu_width - 24, 30, c);
          if (sel) {
            DrawRect(renderer, x + 12, y, 3, 30, SDL_Color{139, 214, 255, 255});
            DrawRect(renderer, x + 11, y - 1, menu_width - 22, 32, SDL_Color{85, 152, 198, 208}, false);
          }
#ifdef HAVE_SDL2_TTF
          std::string label_text;
          switch (menu_items[i]) {
          case SettingId::KeyGuide: label_text = std::string(u8"\u6309\u952e\u8bf4\u660e"); break;
          case SettingId::ClearHistory: label_text = std::string(u8"\u6e05\u9664\u5386\u53f2"); break;
          case SettingId::CleanCache: label_text = std::string(u8"\u6e05\u9664\u7f13\u5b58"); break;
          case SettingId::TxtToUtf8: label_text = std::string(u8"TXT\u8f6c\u7801"); break;
          case SettingId::ContactMe: label_text = std::string(u8"\u8054\u7cfb\u6211"); break;
          case SettingId::ExitApp: label_text = std::string(u8"\u9000\u51fa"); break;
          }
          if (!label_text.empty()) {
            TextCacheEntry *label_tex = get_text_texture(label_text, item_color);
            if (label_tex && label_tex->texture) {
              const int ty = y + std::max(0, (30 - label_tex->h) / 2);
              SDL_Rect td{text_left, ty, label_tex->w, label_tex->h};
              SDL_RenderCopy(renderer, label_tex->texture, nullptr, &td);
            }
          }
#endif
          y += 42;
        }

        const SettingId selected = menu_items[ClampInt(menu_selected, 0, static_cast<int>(menu_items.size()) - 1)];
        if (selected == SettingId::TxtToUtf8 && txt_transcode_job.active) {
          const int bar_w = std::min(260, std::max(160, preview_w - 36));
          const int bar_h = 14;
          const int bar_x = ClampInt(preview_center_x - bar_w / 2, preview_x + 8, preview_x + std::max(8, preview_w - bar_w - 8));
          const int bar_y = 308;
          const float progress =
              (txt_transcode_job.total > 0)
                  ? static_cast<float>(txt_transcode_job.processed) / static_cast<float>(txt_transcode_job.total)
                  : 0.0f;
          const int fill_w = ClampInt(static_cast<int>(std::lround(bar_w * progress)), 0, bar_w);
          DrawRect(renderer, bar_x, bar_y, bar_w, bar_h, SDL_Color{46, 52, 62, 224});
          DrawRect(renderer, bar_x, bar_y, fill_w, bar_h, SDL_Color{63, 119, 158, 255});
          DrawRect(renderer, bar_x, bar_y, bar_w, bar_h, SDL_Color{255, 255, 255, 210}, false);
#ifdef HAVE_SDL2_TTF
          const int pct = ClampInt(static_cast<int>(std::lround(progress * 100.0f)), 0, 100);
          std::string progress_text = std::string(u8"\u8f6c\u7801\u4e2d ") + std::to_string(txt_transcode_job.processed) +
                                      "/" + std::to_string(txt_transcode_job.total) +
                                      "  (" + std::to_string(pct) + "%)";
          TextCacheEntry *progress_tex = get_text_texture(progress_text, SDL_Color{245, 248, 252, 255});
          if (progress_tex && progress_tex->texture) {
            SDL_Rect pd{preview_center_x - progress_tex->w / 2, bar_y + bar_h + 10, progress_tex->w, progress_tex->h};
            SDL_RenderCopy(renderer, progress_tex->texture, nullptr, &pd);
          }
          if (!txt_transcode_job.current_file.empty()) {
            const std::string file_text = Utf8Ellipsize(txt_transcode_job.current_file, 24);
            TextCacheEntry *file_tex = get_text_texture(file_text, SDL_Color{184, 197, 212, 255});
            if (file_tex && file_tex->texture) {
              SDL_Rect fd{preview_center_x - file_tex->w / 2, bar_y + bar_h + 32, file_tex->w, file_tex->h};
              SDL_RenderCopy(renderer, file_tex->texture, nullptr, &fd);
            }
          }
#endif
        }

        // Status bars must stay on top, even over settings menu.
        auto draw_native_topmost = [&](SDL_Texture *tex, int px, int py) {
          if (!tex) return;
          int tw = 0;
          int th = 0;
          get_texture_size(tex, tw, th);
          if (tw <= 0 || th <= 0) return;
          SDL_Rect dst{px, py, tw, th};
          SDL_RenderCopy(renderer, tex, nullptr, &dst);
        };
        if (ui_assets.top_status_bar) {
          draw_native_topmost(ui_assets.top_status_bar, 0, 0);
        } else {
          DrawRect(renderer,
                   0,
                   Layout().top_bar_y,
                   Layout().screen_w,
                   Layout().top_bar_h,
                   SDL_Color{8, 10, 14, 255});
        }
        draw_volume_overlay();
        if (ui_assets.bottom_hint_bar) {
          int bw = 0, bh = 0;
          get_texture_size(ui_assets.bottom_hint_bar, bw, bh);
          draw_native_topmost(ui_assets.bottom_hint_bar, 0, Layout().screen_h - bh);
        } else {
          DrawRect(renderer,
                   0,
                   Layout().bottom_bar_y,
                   Layout().screen_w,
                   Layout().bottom_bar_h,
                   SDL_Color{8, 10, 14, 255});
        }
      }
    }

    const float flash = scene_flash.Value();
    if (flash > 0.001f) {
      DrawRect(renderer, 0, 0, Layout().screen_w, Layout().screen_h,
               SDL_Color{0, 0, 0, static_cast<Uint8>(std::clamp(flash, 0.0f, 1.0f) * 255.0f)});
    }

    SDL_RenderPresent(renderer);

    uint32_t frame_budget_ms = 0;
    if (has_active_animation) frame_budget_ms = kActiveFrameBudgetMs;
    else if (needs_periodic_tick) frame_budget_ms = kPeriodicTickFrameBudgetMs;
    if (frame_budget_ms > 0) {
      const uint32_t frame_elapsed = SDL_GetTicks() - frame_begin_ticks;
      if (frame_elapsed < frame_budget_ms) SDL_Delay(frame_budget_ms - frame_elapsed);
    }
  }

  if (!current_book.empty()) {
    if (reader_mode == ReaderMode::Pdf && pdf_runtime.IsOpen()) {
      const PdfRuntimeProgress active_pdf = pdf_runtime.Progress();
      reader.page = active_pdf.page;
      reader.scroll_y = active_pdf.scroll_y;
      reader.zoom = active_pdf.zoom;
      reader.rotation = active_pdf.rotation;
    } else if (reader_mode == ReaderMode::Epub && epub_comic.IsOpen()) {
      reader.page = epub_comic.CurrentPage();
    } else if (reader_mode == ReaderMode::Txt && txt_reader.open) {
      reader.page = (txt_reader.line_h > 0) ? (txt_reader.scroll_px / txt_reader.line_h) : 0;
      reader.scroll_y = txt_reader.scroll_px;
      txt_reader.resume_cache_dirty = true;
      persist_current_txt_resume_snapshot(current_book, true);
    } else if (state != State::Reader) {
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
  clear_cover_cache();
  if (ui_assets.background_main) SDL_DestroyTexture(ui_assets.background_main);
  if (ui_assets.top_status_bar) SDL_DestroyTexture(ui_assets.top_status_bar);
  if (ui_assets.bottom_hint_bar) SDL_DestroyTexture(ui_assets.bottom_hint_bar);
  if (ui_assets.nav_l1_icon) SDL_DestroyTexture(ui_assets.nav_l1_icon);
  if (ui_assets.nav_r1_icon) SDL_DestroyTexture(ui_assets.nav_r1_icon);
  if (ui_assets.nav_selected_pill) SDL_DestroyTexture(ui_assets.nav_selected_pill);
  if (ui_assets.book_under_shadow) SDL_DestroyTexture(ui_assets.book_under_shadow);
  if (ui_assets.book_select) SDL_DestroyTexture(ui_assets.book_select);
  if (ui_assets.book_title_shadow) SDL_DestroyTexture(ui_assets.book_title_shadow);
  if (ui_assets.book_cover_txt) SDL_DestroyTexture(ui_assets.book_cover_txt);
  if (ui_assets.book_cover_pdf) SDL_DestroyTexture(ui_assets.book_cover_pdf);
  if (ui_assets.settings_preview_theme) SDL_DestroyTexture(ui_assets.settings_preview_theme);
  if (ui_assets.settings_preview_animations) SDL_DestroyTexture(ui_assets.settings_preview_animations);
  if (ui_assets.settings_preview_audio) SDL_DestroyTexture(ui_assets.settings_preview_audio);
  if (ui_assets.settings_preview_keyguide) SDL_DestroyTexture(ui_assets.settings_preview_keyguide);
  if (ui_assets.settings_preview_contact) SDL_DestroyTexture(ui_assets.settings_preview_contact);
  if (ui_assets.settings_preview_clean_history) SDL_DestroyTexture(ui_assets.settings_preview_clean_history);
  if (ui_assets.settings_preview_clean_cache) SDL_DestroyTexture(ui_assets.settings_preview_clean_cache);
  if (ui_assets.settings_preview_txt_to_utf8) SDL_DestroyTexture(ui_assets.settings_preview_txt_to_utf8);
  if (ui_assets.settings_preview_exit) SDL_DestroyTexture(ui_assets.settings_preview_exit);
#ifdef HAVE_SDL2_TTF
  clear_text_cache();
  if (ui_font_reader) {
    TTF_CloseFont(ui_font_reader);
    ui_font_reader = nullptr;
  }
  if (ui_font_title) {
    TTF_CloseFont(ui_font_title);
    ui_font_title = nullptr;
  }
  if (ui_font) {
    TTF_CloseFont(ui_font);
    ui_font = nullptr;
  }
#endif
  invalidate_all_render_cache();
  destroy_shelf_render_cache();
  for (auto &slot : reader_texture_pool) {
    if (slot.texture) {
      forget_texture_size(slot.texture);
      SDL_DestroyTexture(slot.texture);
      slot = ReaderTexturePoolEntry{};
    }
  }
  SDL_LockMutex(reader_async_mutex);
  reader_async_worker_running = false;
  SDL_CondSignal(reader_async_cond);
  SDL_UnlockMutex(reader_async_mutex);
  if (reader_async_thread) SDL_WaitThread(reader_async_thread, nullptr);
  reset_reader_async_state();
  if (reader_async_cond) SDL_DestroyCond(reader_async_cond);
  if (reader_async_mutex) SDL_DestroyMutex(reader_async_mutex);
  pdf_runtime.Close();
  epub_comic.Close();
  reader_page_size_cache.clear();
  for (SDL_GameController *gc : opened_controllers) {
    if (gc) SDL_GameControllerClose(gc);
  }
  for (SDL_Joystick *js : opened_joysticks) {
    if (js) SDL_JoystickClose(js);
  }
  sfx.Shutdown();
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

