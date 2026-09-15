#pragma once

#include "screen_profile.h"

#include <SDL.h>

#include <cstdint>
#include <string>

struct LayoutMetrics;

namespace rgds {

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr int kVirtualReaderW = 640;
constexpr int kVirtualReaderH = 960;
constexpr int kReaderCanvasMaxW = 1280;
constexpr int kReaderCanvasMaxH = 960;

// RGDS and RGDS plus share the runtime, but use different physical panel sizes.
// Keep the historical 640x480 constants as defaults for existing RGDS builds and
// resolve the active metrics from the detected screen profile at startup.
int ScreenW();
int ScreenH();
int VirtualReaderW();
int VirtualReaderH();
int ReaderCanvasMaxW();
int ReaderCanvasMaxH();
void ConfigureScreenMetrics(int screen_w, int screen_h);
bool IsRgdsModelToken(const std::string &device_model_token);

struct PlatformConfig {
  bool is_model = false;
  bool dual_screen_requested = false;
  bool stacked_preview = false;
  bool spanning = false;
};

struct Runtime {
  SDL_Rect top_bounds{0, 0, kScreenW, kScreenH};
  SDL_Rect bottom_bounds{kScreenW, 0, kScreenW, kScreenH};
  SDL_Window *bottom_window = nullptr;
  SDL_Renderer *bottom_renderer = nullptr;
  SDL_Texture *reader_canvas = nullptr;
  SDL_Texture *bottom_reader_canvas = nullptr;
  int reader_canvas_content_w = 0;
  int reader_canvas_content_h = 0;
  bool reader_canvas_content_valid = false;
  bool dual_screen_active = false;
  bool stacked_preview = false;
  bool spanning = false;
};

PlatformConfig DetectPlatformConfig(const std::string &device_model_token);
uint32_t ApplyWindowFlags(uint32_t current_flags, const PlatformConfig &config);
void ProbeDisplayBounds(Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout, bool verbose_log);
int TopWindowX(const Runtime &runtime, const PlatformConfig &config);
int TopWindowY(const Runtime &runtime, const PlatformConfig &config);
int TopWindowW(const Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout);
int TopWindowH(const Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout);
void ConfigureMainWindow(Runtime &runtime, const PlatformConfig &config, SDL_Window *window, bool verbose_log);
void CreateBottomSurface(Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout,
                         uint32_t window_flags, bool verbose_log);
void AttachStackedPreviewSurface(Runtime &runtime, const PlatformConfig &config, SDL_Renderer *top_renderer);
void CreateReaderCanvas(Runtime &runtime, SDL_Renderer *top_renderer, bool top_renderer_supports_target_textures);
bool IsActive(const Runtime &runtime);
void Destroy(Runtime &runtime);

} // namespace rgds
