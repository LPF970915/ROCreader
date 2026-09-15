#include "app_bootstrap.h"

#include "app_layout.h"
#include "filesystem_compat.h"
#include "rgds_render.h"
#include "runtime_log.h"
#include "screen_profile.h"

#include <SDL.h>

#ifdef HAVE_SDL2_IMAGE
#include <SDL_image.h>
#endif
#ifdef HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include <cstdlib>
#include <iostream>
#include <string>

#include "rgds_runtime.h"

#include "runtime_log.h"

#include <csignal>
#include <cstdio>

namespace {
bool VerboseLogEnabled() {
  auto enabled = [](const char *value) {
    return value && *value && std::string(value) != "0";
  };
  return enabled(std::getenv("ROCREADER_VERBOSE_LOG")) || enabled(std::getenv("ROCREADER_DEBUG_LOG"));
}

}  // namespace

void InitializeProcessRuntime(const char *argv0, FatalSignalHandlerFn fatal_signal_handler) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  runtime_log::Init(argv0);
  if (fatal_signal_handler) {
    std::signal(SIGSEGV, fatal_signal_handler);
    std::signal(SIGABRT, fatal_signal_handler);
    std::signal(SIGFPE, fatal_signal_handler);
    std::signal(SIGILL, fatal_signal_handler);
  }
}

AppBootstrapResult BootstrapSdlApp() {
  AppBootstrapResult result;
  runtime_log::Line("main: SDL_Init begin");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
    runtime_log::Line(std::string("main: SDL_Init failed: ") + SDL_GetError());
    std::cerr << "[native_h700] SDL init failed: " << SDL_GetError() << "\n";
    result.exit_code = 1;
    return result;
  }
  runtime_log::Line("main: SDL_Init ok");
  SDL_JoystickEventState(SDL_ENABLE);
#ifdef HAVE_SDL2_TTF
  runtime_log::Line("main: TTF_Init begin");
  if (TTF_Init() != 0) {
    runtime_log::Line(std::string("main: TTF_Init warning: ") + TTF_GetError());
    std::cerr << "[native_h700] SDL2_ttf init warning: " << TTF_GetError() << "\n";
  } else {
    runtime_log::Line("main: TTF_Init ok");
  }
#endif
#ifdef HAVE_SDL2_IMAGE
  runtime_log::Line("main: IMG_Init begin");
  const int img_flags = IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP;
  const int img_ok = IMG_Init(img_flags);
  if ((img_ok & img_flags) == 0) {
    runtime_log::Line(std::string("main: IMG_Init warning: ") + IMG_GetError());
    std::cerr << "[native_h700] SDL2_image init warning: " << IMG_GetError() << "\n";
  } else {
    runtime_log::Line("main: IMG_Init ok");
  }
#endif

  const char *env_windowed = std::getenv("ROCREADER_WINDOWED");
  const char *env_fullscreen = std::getenv("ROCREADER_FULLSCREEN");
  const bool force_windowed = env_windowed && std::string(env_windowed) == "1";
  const bool force_fullscreen = env_fullscreen && std::string(env_fullscreen) == "1";

  result.device_model_token = DetectDeviceModelToken();
  const rgds::PlatformConfig rgds_platform = rgds::DetectPlatformConfig(result.device_model_token);
  result.rgds_runtime = rgds::Runtime{};

  runtime_log::Line("main: DetectScreenProfile begin");
  result.screen_profile = DetectScreenProfile();
  rgds::ConfigureScreenMetrics(result.screen_profile.screen_w, result.screen_profile.screen_h);
  result.verbose_log = VerboseLogEnabled();

  uint32_t win_flags = SDL_WINDOW_SHOWN;
#if defined(__arm__) || defined(__aarch64__)
  const bool default_fullscreen = true;
#else
  const bool default_fullscreen = false;
#endif
  if ((default_fullscreen && !force_windowed) || force_fullscreen) {
    win_flags |= SDL_WINDOW_FULLSCREEN;
  }
  SetLayoutProfile(SelectLayoutProfile(result.screen_profile.screen_w, result.screen_profile.screen_h));
  win_flags = rgds::ApplyWindowFlags(win_flags, rgds_platform);
  if (result.verbose_log) {
    std::cout << "[native_h700] screen detect: source=" << result.screen_profile.detection_source
              << " detected=" << result.screen_profile.detected_w << "x" << result.screen_profile.detected_h
              << " profile=" << result.screen_profile.profile_name
              << " layout=" << Layout().screen_w << "x" << Layout().screen_h << "\n";
  }

  runtime_log::Line(std::string("main: screen source=") + result.screen_profile.detection_source +
                    " detected=" + std::to_string(result.screen_profile.detected_w) + "x" +
                    std::to_string(result.screen_profile.detected_h) + " profile=" +
                    result.screen_profile.profile_name);
  rgds::ProbeDisplayBounds(result.rgds_runtime, rgds_platform, Layout(), result.verbose_log);
  runtime_log::Line("main: SDL_CreateWindow begin");
  const int window_x = rgds::TopWindowX(result.rgds_runtime, rgds_platform);
  const int window_y = rgds::TopWindowY(result.rgds_runtime, rgds_platform);
  result.window =
      SDL_CreateWindow("ROCreader Native H700",
                       window_x,
                       window_y,
                       rgds::TopWindowW(result.rgds_runtime, rgds_platform, Layout()),
                       rgds::TopWindowH(result.rgds_runtime, rgds_platform, Layout()),
                       win_flags);
  if (!result.window) {
    runtime_log::Line(std::string("main: SDL_CreateWindow failed: ") + SDL_GetError());
    std::cerr << "[native_h700] window failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    result.exit_code = 2;
    return result;
  }
  runtime_log::Line("main: SDL_CreateWindow ok");
  rgds::ConfigureMainWindow(result.rgds_runtime, rgds_platform, result.window, result.verbose_log);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  runtime_log::Line("main: SDL_CreateRenderer begin");
  result.renderer = SDL_CreateRenderer(result.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!result.renderer) result.renderer = SDL_CreateRenderer(result.window, -1, SDL_RENDERER_SOFTWARE);
  if (!result.renderer) {
    runtime_log::Line(std::string("main: SDL_CreateRenderer failed: ") + SDL_GetError());
    std::cerr << "[native_h700] renderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(result.window);
    SDL_Quit();
    result.window = nullptr;
    result.exit_code = 3;
    return result;
  }
  SDL_RendererInfo renderer_info{};
  if (SDL_GetRendererInfo(result.renderer, &renderer_info) == 0) {
    runtime_log::Line(std::string("main: renderer name=") + (renderer_info.name ? renderer_info.name : "unknown") +
                      " flags=" + std::to_string(renderer_info.flags) +
                      " accelerated=" + ((renderer_info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no") +
                      " vsync=" + ((renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no") +
                      " target=" + ((renderer_info.flags & SDL_RENDERER_TARGETTEXTURE) ? "yes" : "no"));
    if (result.verbose_log) {
      std::cout << "[native_h700] renderer: " << (renderer_info.name ? renderer_info.name : "unknown")
                << " flags=0x" << std::hex << renderer_info.flags << std::dec
                << " accelerated=" << ((renderer_info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no")
                << " vsync=" << ((renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no") << "\n";
    }
  }
  runtime_log::Line("main: SDL_CreateRenderer ok");
  result.renderer_supports_target_textures =
      (renderer_info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
  rgds::AttachStackedPreviewSurface(result.rgds_runtime, rgds_platform, result.renderer);
  rgds::CreateBottomSurface(result.rgds_runtime, rgds_platform, Layout(), win_flags, result.verbose_log);
  rgds::CreateReaderCanvas(result.rgds_runtime, result.renderer, result.renderer_supports_target_textures);
  result.ok = true;
  return result;
}
