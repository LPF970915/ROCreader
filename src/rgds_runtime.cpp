#include "rgds_runtime.h"

#include "app_layout.h"
#include "runtime_log.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace rgds {
namespace {
int g_screen_w = kScreenW;
int g_screen_h = kScreenH;

SDL_Rect ValidDisplayBoundsOrFallback(int display_index, SDL_Rect fallback) {
  SDL_Rect bounds = fallback;
  if (SDL_GetDisplayBounds(display_index, &bounds) != 0 || bounds.w <= 0 || bounds.h <= 0) {
    bounds = fallback;
  }
  return bounds;
}

SDL_Rect UnionRect(SDL_Rect a, SDL_Rect b) {
  const int left = std::min(a.x, b.x);
  const int top = std::min(a.y, b.y);
  const int right = std::max(a.x + a.w, b.x + b.w);
  const int bottom = std::max(a.y + a.h, b.y + b.h);
  return SDL_Rect{left, top, right - left, bottom - top};
}

std::string RectText(SDL_Rect rect) {
  std::ostringstream out;
  out << rect.x << "," << rect.y << " " << rect.w << "x" << rect.h;
  return out.str();
}
} // namespace

int ScreenW() { return g_screen_w; }
int ScreenH() { return g_screen_h; }
int VirtualReaderW() { return g_screen_w; }
int VirtualReaderH() { return g_screen_h * 2; }
int ReaderCanvasMaxW() { return g_screen_w * 2; }
int ReaderCanvasMaxH() { return g_screen_h * 2; }

void ConfigureScreenMetrics(int screen_w, int screen_h) {
  if (screen_w <= 0 || screen_h <= 0) return;
  g_screen_w = screen_w;
  g_screen_h = screen_h;
}

bool IsRgdsModelToken(const std::string &device_model_token) {
  return device_model_token == "rgds" || device_model_token == "rgds-plus";
}

PlatformConfig DetectPlatformConfig(const std::string &device_model_token) {
  PlatformConfig config;
  config.is_model = IsRgdsModelToken(device_model_token);
  config.dual_screen_requested = config.is_model;
  config.stacked_preview = false;
  config.spanning = config.is_model;
  return config;
}

uint32_t ApplyWindowFlags(uint32_t current_flags, const PlatformConfig &config) {
  if (!config.dual_screen_requested) return current_flags;
  if (config.stacked_preview) return SDL_WINDOW_SHOWN;
  if (config.spanning) return SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
  return SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void ProbeDisplayBounds(Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout, bool verbose_log) {
  runtime.top_bounds = SDL_Rect{0, 0, ScreenW(), ScreenH()};
  runtime.bottom_bounds = SDL_Rect{ScreenW(), 0, ScreenW(), ScreenH()};
  if (!config.dual_screen_requested) return;
  if (config.stacked_preview) {
    runtime.top_bounds = SDL_Rect{0, 0, ScreenW(), VirtualReaderH()};
    runtime.bottom_bounds = SDL_Rect{0, ScreenH(), ScreenW(), ScreenH()};
    runtime.stacked_preview = true;
    runtime_log::Line("main: RGDS stacked preview route enabled");
    return;
  }

  runtime.top_bounds = ValidDisplayBoundsOrFallback(0, SDL_Rect{0, 0, ScreenW(), ScreenH()});
  runtime.bottom_bounds = ValidDisplayBoundsOrFallback(1, SDL_Rect{ScreenW(), 0, ScreenW(), ScreenH()});
  runtime.spanning = config.spanning;
  if (config.spanning) {
    runtime.dual_screen_active = true;
    runtime_log::Line("main: RGDS display route locked to Weston spanning");
  }
  runtime_log::Line(config.spanning ? "main: RGDS locked Weston spanning route enabled"
                                    : "main: RGDS legacy dual-window route enabled");
  const int display_count = SDL_GetNumVideoDisplays();
  runtime_log::Line(std::string("main: RGDS SDL display count=") + std::to_string(display_count));
  if (config.spanning) {
    const SDL_Rect spanning_bounds = UnionRect(runtime.top_bounds, runtime.bottom_bounds);
    runtime_log::Line(std::string("main: RGDS spanning union=") + RectText(spanning_bounds) +
                      " top=" + RectText(runtime.top_bounds) +
                      " bottom=" + RectText(runtime.bottom_bounds));
  }
  if (!verbose_log) return;
  std::cout << "[native_h700] RGDS locked Weston spanning route enabled, displays=" << display_count << "\n";
  for (int i = 0; i < display_count; ++i) {
    SDL_Rect bounds{};
    SDL_DisplayMode mode{};
    SDL_GetDisplayBounds(i, &bounds);
    SDL_GetCurrentDisplayMode(i, &mode);
    std::cout << "[native_h700] RGDS display " << i
              << " bounds=" << bounds.x << "," << bounds.y << " " << bounds.w << "x" << bounds.h
              << " mode=" << mode.w << "x" << mode.h << "@" << mode.refresh_rate << "\n";
  }
}

int TopWindowX(const Runtime &runtime, const PlatformConfig &config) {
  if (config.spanning) return UnionRect(runtime.top_bounds, runtime.bottom_bounds).x;
  return config.dual_screen_requested ? runtime.top_bounds.x + 16 : SDL_WINDOWPOS_CENTERED;
}

int TopWindowY(const Runtime &runtime, const PlatformConfig &config) {
  if (config.spanning) return UnionRect(runtime.top_bounds, runtime.bottom_bounds).y;
  return config.dual_screen_requested ? runtime.top_bounds.y + 16 : SDL_WINDOWPOS_CENTERED;
}

int TopWindowW(const Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout) {
  if (config.spanning) {
    return std::max(ScreenW() * 2, UnionRect(runtime.top_bounds, runtime.bottom_bounds).w);
  }
  return config.stacked_preview ? ScreenW() : layout.screen_w;
}

int TopWindowH(const Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout) {
  if (config.spanning) {
    return std::max(ScreenH(), UnionRect(runtime.top_bounds, runtime.bottom_bounds).h);
  }
  return config.stacked_preview ? VirtualReaderH() : layout.screen_h;
}

void ConfigureMainWindow(Runtime &runtime, const PlatformConfig &config, SDL_Window *window, bool verbose_log) {
  if (!window || !config.dual_screen_requested || !config.spanning) return;
  const SDL_Rect spanning_bounds = UnionRect(runtime.top_bounds, runtime.bottom_bounds);
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowPosition(window, spanning_bounds.x, spanning_bounds.y);
  SDL_SetWindowSize(window, std::max(ScreenW() * 2, spanning_bounds.w), std::max(ScreenH(), spanning_bounds.h));
  SDL_RaiseWindow(window);

  int actual_x = 0;
  int actual_y = 0;
  int actual_w = 0;
  int actual_h = 0;
  SDL_GetWindowPosition(window, &actual_x, &actual_y);
  SDL_GetWindowSize(window, &actual_w, &actual_h);
  const Uint32 flags = SDL_GetWindowFlags(window);
  runtime_log::Line(std::string("main: RGDS spanning window actual=") +
                    std::to_string(actual_x) + "," + std::to_string(actual_y) + " " +
                    std::to_string(actual_w) + "x" + std::to_string(actual_h) +
                    " display_index=" + std::to_string(SDL_GetWindowDisplayIndex(window)) +
                    " flags=0x" + [&flags]() {
                      std::ostringstream out;
                      out << std::hex << flags;
                      return out.str();
                    }());
  if (verbose_log) {
    std::cout << "[native_h700] RGDS spanning window actual="
              << actual_x << "," << actual_y << " " << actual_w << "x" << actual_h
              << " display_index=" << SDL_GetWindowDisplayIndex(window)
              << " flags=0x" << std::hex << flags << std::dec << "\n";
  }
}

void CreateBottomSurface(Runtime &runtime, const PlatformConfig &config, const LayoutMetrics &layout,
                         uint32_t window_flags, bool verbose_log) {
  if (!config.dual_screen_requested) return;
  if (config.stacked_preview) return;
  if (config.spanning) {
    runtime.bottom_window = nullptr;
    runtime.dual_screen_active = true;
    runtime.spanning = true;
    runtime_log::Line(runtime.bottom_renderer
                          ? "main: RGDS spanning surface ready on shared renderer"
                          : "main: RGDS spanning surface pending shared renderer");
    return;
  }
  runtime_log::Line("main: RGDS bottom SDL_CreateWindow begin");
  runtime.bottom_window = SDL_CreateWindow("ROCreader RGDS Bottom",
                                           runtime.bottom_bounds.x + 16,
                                           runtime.bottom_bounds.y + 16,
                                           layout.screen_w,
                                           layout.screen_h,
                                           window_flags);
  if (runtime.bottom_window) {
    runtime.bottom_renderer =
        SDL_CreateRenderer(runtime.bottom_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!runtime.bottom_renderer) {
      runtime.bottom_renderer = SDL_CreateRenderer(runtime.bottom_window, -1, SDL_RENDERER_SOFTWARE);
    }
  }
  if (!runtime.bottom_window || !runtime.bottom_renderer) {
    runtime_log::Line(std::string("main: RGDS bottom surface disabled: ") + SDL_GetError());
    if (runtime.bottom_renderer) SDL_DestroyRenderer(runtime.bottom_renderer);
    if (runtime.bottom_window) SDL_DestroyWindow(runtime.bottom_window);
    runtime.bottom_renderer = nullptr;
    runtime.bottom_window = nullptr;
    return;
  }

  runtime.dual_screen_active = true;
  runtime_log::Line("main: RGDS dual-screen surfaces ready");
  if (verbose_log) {
    SDL_RendererInfo bottom_info{};
    SDL_GetRendererInfo(runtime.bottom_renderer, &bottom_info);
    std::cout << "[native_h700] RGDS bottom renderer: "
              << (bottom_info.name ? bottom_info.name : "unknown")
              << " flags=0x" << std::hex << bottom_info.flags << std::dec << "\n";
  }
}

void AttachStackedPreviewSurface(Runtime &runtime, const PlatformConfig &config, SDL_Renderer *top_renderer) {
  if (config.dual_screen_requested && config.spanning && top_renderer) {
    runtime.bottom_renderer = top_renderer;
    runtime.bottom_window = nullptr;
    runtime.dual_screen_active = true;
    runtime.spanning = true;
    runtime_log::Line("main: RGDS spanning renderer attached");
    return;
  }
  if (!config.dual_screen_requested || !config.stacked_preview || !top_renderer) return;
  runtime.bottom_renderer = top_renderer;
  runtime.bottom_window = nullptr;
  runtime.dual_screen_active = true;
  runtime.stacked_preview = true;
  runtime_log::Line("main: RGDS stacked preview surface ready");
}

void CreateReaderCanvas(Runtime &runtime, SDL_Renderer *top_renderer, bool top_renderer_supports_target_textures) {
  if (!runtime.dual_screen_active || !top_renderer_supports_target_textures || !top_renderer) return;
  runtime.reader_canvas_content_w = 0;
  runtime.reader_canvas_content_h = 0;
  runtime.reader_canvas_content_valid = false;
  runtime.reader_canvas = SDL_CreateTexture(top_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                            ReaderCanvasMaxW(), ReaderCanvasMaxH());
  if (runtime.reader_canvas) {
    SDL_SetTextureBlendMode(runtime.reader_canvas, SDL_BLENDMODE_BLEND);
    runtime_log::Line("main: RGDS reader canvas ready");
  } else {
    runtime_log::Line(std::string("main: RGDS reader canvas disabled: ") + SDL_GetError());
  }
  if (!runtime.stacked_preview && runtime.bottom_renderer) {
    const Uint32 access = runtime.spanning ? SDL_TEXTUREACCESS_TARGET : SDL_TEXTUREACCESS_STREAMING;
    runtime.bottom_reader_canvas =
        SDL_CreateTexture(runtime.bottom_renderer, SDL_PIXELFORMAT_RGBA8888, access, ReaderCanvasMaxW(), ReaderCanvasMaxH());
    if (runtime.bottom_reader_canvas) {
      SDL_SetTextureBlendMode(runtime.bottom_reader_canvas, SDL_BLENDMODE_BLEND);
      runtime_log::Line("main: RGDS bottom reader canvas ready");
    } else {
      runtime_log::Line(std::string("main: RGDS bottom reader canvas disabled: ") + SDL_GetError());
    }
  } else if (runtime.stacked_preview) {
    runtime.bottom_reader_canvas = runtime.reader_canvas;
  }
}

bool IsActive(const Runtime &runtime) {
  return runtime.dual_screen_active && (runtime.bottom_renderer || runtime.spanning);
}

void Destroy(Runtime &runtime) {
  if (runtime.bottom_reader_canvas && runtime.bottom_reader_canvas != runtime.reader_canvas) {
    SDL_DestroyTexture(runtime.bottom_reader_canvas);
  }
  if (runtime.reader_canvas) SDL_DestroyTexture(runtime.reader_canvas);
  if (runtime.bottom_renderer && !runtime.stacked_preview) SDL_DestroyRenderer(runtime.bottom_renderer);
  if (runtime.bottom_window) SDL_DestroyWindow(runtime.bottom_window);
  runtime = Runtime{};
}

} // namespace rgds
