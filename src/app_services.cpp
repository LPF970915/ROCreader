#include "app_services.h"

#include "contributor_avatar_runtime.h"
#include "filesystem_compat.h"
#include "app_language.h"
#include "menu_scene.h"
#include "rgds_interaction.h"
#include "runtime_log.h"
#include "storage_paths.h"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <system_error>

AppPlatformEnv ResolveAppPlatformEnv(const std::string &device_model_token,
                                     const ScreenProfile &screen_profile,
                                     const rgds::Runtime &rgds_runtime) {
  AppPlatformEnv env;
#if defined(__arm__) || defined(__aarch64__)
  env.capabilities.use_h700_defaults = true;
#else
  env.capabilities.use_h700_defaults = false;
#endif
  env.capabilities.use_trimui_brick_keymap =
      device_model_token == "trimui-brick" || screen_profile.profile_name == "1024x768";
  const bool use_gkd350h_ultra_keymap = UsesGKD350HUltraKeymap(device_model_token, screen_profile);
  env.capabilities.input_profile =
      rgds::IsRgdsModelToken(device_model_token)
          ? InputProfile::RGDS
          : use_gkd350h_ultra_keymap
          ? InputProfile::GKD350HUltra
          : env.capabilities.use_trimui_brick_keymap
          ? InputProfile::TrimuiBrick
          : (env.capabilities.use_h700_defaults
                 ? (Uses34xxSpKeymap(device_model_token)
                        ? InputProfile::H70034xxSp
                        : (Uses35xxHKeymap(device_model_token) ? InputProfile::H70035xxH
                                                               : InputProfile::H700Default))
                 : InputProfile::DesktopDefault);
  env.rgds_dual_screen = rgds::IsActive(rgds_runtime);
  env.capabilities.has_dual_screen = env.rgds_dual_screen;
  env.capabilities.layout_mode =
      env.rgds_dual_screen
          ? (rgds_runtime.spanning ? PlatformLayoutMode::RgdsWestonSpanning
                                   : PlatformLayoutMode::RgdsStackedPreview)
          : PlatformLayoutMode::SingleScreen;
  env.capabilities.has_system_volume = env.capabilities.use_h700_defaults;
  env.capabilities.has_lid_control = env.capabilities.use_h700_defaults;
  return env;
}

std::filesystem::path AppRuntimePaths::ResolveRuntimeFile(const std::string &name) const {
  const std::vector<std::filesystem::path> candidates = {
      exe_path / name,
      exe_path / ".." / name,
      std::filesystem::current_path() / name,
  };
  for (const auto &p : candidates) {
    if (std::filesystem::exists(p)) return filesystem_compat::LexicallyNormal(p);
  }
  return filesystem_compat::LexicallyNormal((exe_path / name));
}

AppRuntimePaths ResolveAppRuntimePaths() {
  std::string exe_dir = ".";
  char *base = SDL_GetBasePath();
  if (base && *base) {
    exe_dir = base;
  }
  if (base) SDL_free(base);

  AppRuntimePaths paths;
  paths.exe_path = std::filesystem::path(exe_dir);
  paths.ui_path = paths.exe_path / "ui";
  return paths;
}

AppConfigPaths ResolveAppConfigPaths(const AppRuntimePaths &runtime_paths) {
  AppConfigPaths paths;
  paths.keymap_path = runtime_paths.ResolveRuntimeFile("native_keymap.ini");
  paths.config_path = runtime_paths.ResolveRuntimeFile("native_config.ini");
  paths.progress_path = runtime_paths.ResolveRuntimeFile("native_progress.tsv");
  paths.favorites_path = runtime_paths.ResolveRuntimeFile("native_favorites.txt");
  paths.history_path = runtime_paths.ResolveRuntimeFile("native_history.txt");
  const char *env_power_script = std::getenv("ROCREADER_PWR_SCRIPT");
  paths.power_script_path =
      (env_power_script && *env_power_script) ? std::filesystem::path(env_power_script)
                                              : std::filesystem::path("/mnt/mod/ctrl/pwr_new.sh");
  return paths;
}

AppRuntimeStores InitializeAppRuntimeStores(const AppConfigPaths &app_config_paths,
                                            const std::function<std::string(const std::string &)> &normalize_path_key) {
  AppRuntimeStores stores{
      ConfigStore(app_config_paths.config_path.string()),
      ProgressStore(app_config_paths.progress_path.string()),
      RecentPathStore(app_config_paths.favorites_path.string(), normalize_path_key),
      RecentPathStore(app_config_paths.history_path.string(), normalize_path_key),
  };
  if (!stores.config.Get().audio) {
    stores.config.Mutable().audio = true;
    stores.config.MarkDirty();
    stores.config.Save();
  }
  return stores;
}

AppSystemServices InitializeAppSystemServices(bool use_h700_defaults,
                                               const std::filesystem::path &power_script_path) {
  return AppSystemServices{
      VolumeController(use_h700_defaults),
      SystemStatusMonitor(),
      SystemControlService(use_h700_defaults),
      LidPowerController(power_script_path),
  };
}

AppSettingsStates InitializeAppSettingsStates(ConfigStore &config,
                                              SystemControlService &system_control_service,
                                              bool use_h700_defaults,
                                              InputProfile input_profile) {
  AppSettingsStates states{};
  states.system_settings_state.auto_sleep_interval_index =
      ClampAutoSleepIntervalIndex(config.Get().auto_sleep_interval_index);
  states.system_settings_state.system_language_index =
      SystemLanguageIndexFromConfigValue(config.Get().system_language);
  states.txt_settings_state.background_color = ClampTxtColorIndex(config.Get().txt_background_color);
  states.txt_settings_state.font_color = ClampTxtColorIndex(config.Get().txt_font_color);
  states.txt_settings_state.font_size_level = ClampTxtFontSizeLevel(config.Get().txt_font_size_level);

  if (use_h700_defaults) {
    bool changed = false;
    if (input_profile == InputProfile::TrimuiBrick) {
      system_control_service.RefreshVolumeOnly(states.system_settings_state.levels.volume);
    } else if (system_control_service.ApplyVolumePercent(config.Get().system_volume_percent,
                                                         states.system_settings_state.levels.volume) &&
               states.system_settings_state.levels.volume.available) {
      const int applied_percent =
          std::clamp((states.system_settings_state.levels.volume.level * 100) /
                         std::max(1, states.system_settings_state.levels.volume.max_level),
                     0, 100);
      if (config.Mutable().system_volume_percent != applied_percent) {
        config.Mutable().system_volume_percent = applied_percent;
        changed = true;
      }
    } else {
      system_control_service.RefreshVolumeOnly(states.system_settings_state.levels.volume);
    }

    if (system_control_service.ApplyBrightnessLevel(config.Get().screen_brightness_level,
                                                    states.system_settings_state.levels.brightness) &&
        states.system_settings_state.levels.brightness.available) {
      const int applied_level =
          std::clamp(states.system_settings_state.levels.brightness.level, 0,
                     std::max(1, states.system_settings_state.levels.brightness.max_level));
      if (config.Mutable().screen_brightness_level != applied_level) {
        config.Mutable().screen_brightness_level = applied_level;
        changed = true;
      }
    } else {
      system_control_service.Refresh(states.system_settings_state.levels);
    }

    if (changed || config.IsDirty()) {
      config.MarkDirty();
      config.Save();
    }
  } else {
    system_control_service.Refresh(states.system_settings_state.levels);
  }

  return states;
}

void InitializeLidCloseScreenOffState(const ConfigStore &config,
                                      LidPowerController &lid_power_controller,
                                      SystemSettingsState &system_settings_state) {
  const bool enabled = config.Get().lid_close_screen_off;
  lid_power_controller.SetEnabled(enabled);
  system_settings_state.lid_close_screen_off_enabled = enabled;
}

void SnapRgdsMenuOpenState(MenuSceneState &menu_state, bool close_armed) {
  menu_state.anim.Snap(1.0f);
  menu_state.closing = false;
  menu_state.close_armed = close_armed;
}

void InitializeRgdsStartupState(bool is_rgds_runtime,
                                MenuSceneState &menu_state,
                                rgds::InteractionState &rgds_interaction,
                                bool close_armed) {
  if (!is_rgds_runtime) return;
  SnapRgdsMenuOpenState(menu_state, close_armed);
  menu_state.toggle_guard = 0.0f;
  rgds::EnterShelf(rgds_interaction);
}

void InitializeRgdsReaderState(bool is_rgds_runtime,
                               MenuSceneState &menu_state,
                               rgds::InteractionState &rgds_interaction) {
  if (!is_rgds_runtime) return;
  rgds::EnterReader(rgds_interaction);
  SnapRgdsMenuOpenState(menu_state, true);
  menu_state.toggle_guard = 0.0f;
}

void ApplyOnlineShelfInputTickResult(const OnlineShelfControllerTickResult &result,
                                     const OnlineShelfInputTickHandlers &handlers) {
  if (result.online_shelf_needs_reset) {
    if (handlers.reset_to_category_root) handlers.reset_to_category_root();
  }
  if (result.cover_cache_changed) {
    if (handlers.clear_cover_cache) handlers.clear_cover_cache();
  }
  if (result.shelf_items_changed) {
    if (handlers.rebuild_shelf_items) handlers.rebuild_shelf_items();
  }
  if (result.online_shelf_needs_reset || result.shelf_items_changed) {
    if (handlers.reset_shelf_cover_stream_preload) handlers.reset_shelf_cover_stream_preload();
    if (handlers.enter_shelf) handlers.enter_shelf();
  }
}

void ApplyOnlineShelfPresentTickResult(const OnlineShelfControllerTickResult &result,
                                       const OnlineShelfPresentTickHandlers &handlers) {
  if (result.cover_cache_changed) {
    if (handlers.clear_cover_cache) handlers.clear_cover_cache();
  }
  if (result.shelf_items_changed) {
    if (handlers.rebuild_shelf_items) handlers.rebuild_shelf_items();
  }
  if (result.online_shelf_needs_reset) {
    if (handlers.reset_shelf_cover_stream_preload) handlers.reset_shelf_cover_stream_preload();
  }
}

bool HandleOnlineShelfDeferredConnect(OnlineShelfController &online_shelf_controller,
                                      ShelfRuntimeState &shelf_runtime,
                                      const OnlineShelfInputTickHandlers &handlers) {
  if (!online_shelf_controller.HandleDeferredConnect()) return false;
  const OnlineShelfControllerTickResult online_after_connect =
      online_shelf_controller.TickAfterInput(shelf_runtime);
  ApplyOnlineShelfInputTickResult(online_after_connect, handlers);
  return true;
}

void HandleOnlineShelfDeferredDisconnect(OnlineShelfController &online_shelf_controller,
                                         std::vector<std::string> &books_roots,
                                         std::vector<std::string> &cover_roots,
                                         const OnlineShelfDeferredDisconnectHandlers &handlers) {
  online_shelf_controller.HandleDeferredDisconnect(books_roots, cover_roots);
  books_roots = storage_paths::DetectBooksRoots();
  cover_roots = storage_paths::DetectCoverRoots();
  if (handlers.reset_nav_to_first) handlers.reset_nav_to_first();
  if (handlers.reset_to_category_root) handlers.reset_to_category_root();
  if (handlers.clear_cover_cache) handlers.clear_cover_cache();
  if (handlers.rebuild_shelf_items) handlers.rebuild_shelf_items();
  if (handlers.reset_shelf_cover_stream_preload) handlers.reset_shelf_cover_stream_preload();
  if (handlers.enter_shelf) handlers.enter_shelf();
}

MenuSceneInputContext MakeMenuSceneInputContext(const InputManager &input,
                                                const NativeConfig &ui_cfg,
                                                InputProfile input_profile,
                                                float dt,
                                                MenuSceneState &menu_state,
                                                SystemSettingsState &system_settings_state,
                                                TxtSettingsState &txt_settings_state,
                                                ContributorAvatarState &contributor_avatar_state,
                                                size_t contributor_avatar_count,
                                                KeyCalibrationState &key_calibration_state,
                                                VersionUpdateState &version_update_state,
                                                OnlineSourceState &online_source_state,
                                                MenuSceneInputServices services) {
  return MenuSceneInputContext{input,
                               ui_cfg,
                               input_profile,
                               dt,
                               menu_state,
                               system_settings_state,
                               txt_settings_state,
                               contributor_avatar_state,
                               contributor_avatar_count,
                               key_calibration_state,
                               version_update_state,
                               online_source_state,
                               std::move(services)};
}

void PrepareMenuSceneInputState(SystemSettingsState &system_settings_state,
                                bool refresh_system_controls,
                                SystemControlService &system_control_service,
                                ContributorAvatarState &contributor_avatar_state,
                                size_t contributor_avatar_count) {
  if (refresh_system_controls) {
    system_control_service.Refresh(system_settings_state.levels);
  }
  SyncContributorAvatarState(contributor_avatar_state, contributor_avatar_count);
}

AppInputDevices OpenAppInputDevices(bool verbose_log) {
  AppInputDevices devices;
  const int joystick_count = SDL_NumJoysticks();
  if (verbose_log) {
    std::cout << "[native_h700] joysticks: " << joystick_count << "\n";
  }
  for (int i = 0; i < joystick_count; ++i) {
    const char *joy_name = SDL_JoystickNameForIndex(i);
    if (verbose_log) {
      std::cout << "[native_h700] joystick info: idx=" << i
                << " name=" << (joy_name ? joy_name : "unknown")
                << " is_gamecontroller=" << (SDL_IsGameController(i) ? "1" : "0") << "\n";
    }
    if (SDL_IsGameController(i)) {
      SDL_GameController *gc = SDL_GameControllerOpen(i);
      if (gc) {
        devices.opened_controllers.push_back(gc);
        SDL_Joystick *js = SDL_GameControllerGetJoystick(gc);
        if (verbose_log) {
          std::cout << "[native_h700] opened gamecontroller idx=" << i
                    << " name=" << (SDL_GameControllerName(gc) ? SDL_GameControllerName(gc) : "unknown")
                    << " joystick_name=" << (js && SDL_JoystickName(js) ? SDL_JoystickName(js) : "unknown")
                    << " instance=" << (js ? SDL_JoystickInstanceID(js) : -1)
                    << " axes=" << (js ? SDL_JoystickNumAxes(js) : -1)
                    << " buttons=" << (js ? SDL_JoystickNumButtons(js) : -1)
                    << " hats=" << (js ? SDL_JoystickNumHats(js) : -1)
                    << " balls=" << (js ? SDL_JoystickNumBalls(js) : -1) << "\n";
        }
        continue;
      }
      if (verbose_log) {
        std::cout << "[native_h700] open gamecontroller failed idx=" << i
                  << " err=" << SDL_GetError() << "\n";
      }
    }
    SDL_Joystick *js = SDL_JoystickOpen(i);
    if (js) {
      devices.opened_joysticks.push_back(js);
      if (verbose_log) {
        std::cout << "[native_h700] opened joystick idx=" << i
                  << " name=" << (SDL_JoystickName(js) ? SDL_JoystickName(js) : "unknown")
                  << " instance=" << SDL_JoystickInstanceID(js)
                  << " axes=" << SDL_JoystickNumAxes(js)
                  << " buttons=" << SDL_JoystickNumButtons(js)
                  << " hats=" << SDL_JoystickNumHats(js)
                  << " balls=" << SDL_JoystickNumBalls(js) << "\n";
      }
    } else if (verbose_log) {
      std::cout << "[native_h700] open joystick failed idx=" << i
                << " err=" << SDL_GetError() << "\n";
    }
  }
  return devices;
}

void CloseAppInputDevices(AppInputDevices &devices) {
  for (SDL_GameController *gc : devices.opened_controllers) {
    if (gc) SDL_GameControllerClose(gc);
  }
  devices.opened_controllers.clear();

  for (SDL_Joystick *js : devices.opened_joysticks) {
    if (js) SDL_JoystickClose(js);
  }
  devices.opened_joysticks.clear();
}

void RefreshAppInputDevices(AppInputDevices &devices, bool verbose_log) {
  (void)devices;
  SDL_GameControllerUpdate();
  SDL_JoystickUpdate();
  if (verbose_log) {
    std::cout << "[native_h700] refreshed SDL input state without reopening handles\n";
  }
}

void ReopenAppInputDevices(AppInputDevices &devices, bool verbose_log) {
  CloseAppInputDevices(devices);
  SDL_GameControllerUpdate();
  SDL_JoystickUpdate();
  devices = OpenAppInputDevices(verbose_log);
  if (verbose_log) {
    std::cout << "[native_h700] reopened SDL input devices after wake\n";
  }
}

AppStoragePaths InitializeAppStoragePaths(bool verbose_log) {
  AppStoragePaths paths;
  paths.books_roots = storage_paths::DetectBooksRoots();
  runtime_log::Line(std::string("main: DetectBooksRoots count=") + std::to_string(paths.books_roots.size()));
  for (const auto &r : paths.books_roots) runtime_log::Line(std::string("main: books root: ") + r);
  if (paths.books_roots.empty()) {
    runtime_log::Line("main: WARNING no books roots found; expected folder name is lowercase books");
  }

  runtime_log::Line("main: DetectCoverRoots begin");
  paths.cover_roots = storage_paths::DetectCoverRoots();
  runtime_log::Line(std::string("main: DetectCoverRoots count=") + std::to_string(paths.cover_roots.size()));
  for (const auto &r : paths.cover_roots) runtime_log::Line(std::string("main: cover root: ") + r);

  paths.txt_layout_cache_dir = std::filesystem::path("/mnt/mmc/cache/txt_layouts");
  paths.removable_txt_layout_cache_dir = std::filesystem::path("/mnt/sdcard/cache/txt_layouts");
  paths.cover_thumb_cache_dir = std::filesystem::path("/mnt/mmc/cache/cover_thumbs");
  paths.removable_cover_thumb_cache_dir = std::filesystem::path("/mnt/sdcard/cache/cover_thumbs");
  if (const char *env_cache = std::getenv("ROCREADER_CACHE_ROOT"); env_cache && *env_cache) {
    const std::filesystem::path cache_root(env_cache);
    paths.txt_layout_cache_dir = cache_root / "txt_layouts";
    paths.removable_txt_layout_cache_dir = cache_root / "txt_layouts";
    paths.cover_thumb_cache_dir = cache_root / "cover_thumbs";
    paths.removable_cover_thumb_cache_dir = cache_root / "cover_thumbs";
  }
  runtime_log::Line("main: txt layout cache: " + paths.txt_layout_cache_dir.string());
  runtime_log::Line("main: cover thumb cache: " + paths.cover_thumb_cache_dir.string());
  {
    std::error_code ec;
    std::filesystem::create_directories(paths.txt_layout_cache_dir, ec);
  }
  {
    std::error_code ec;
    std::filesystem::create_directories(paths.removable_txt_layout_cache_dir, ec);
  }
  {
    std::error_code ec;
    std::filesystem::create_directories(paths.cover_thumb_cache_dir, ec);
  }
  {
    std::error_code ec;
    std::filesystem::create_directories(paths.removable_cover_thumb_cache_dir, ec);
  }

  if (verbose_log) {
    std::cout << "[native_h700] books roots:";
    for (const auto &r : paths.books_roots) std::cout << " " << r;
    std::cout << "\n";
    std::cout << "[native_h700] cover roots:";
    for (const auto &r : paths.cover_roots) std::cout << " " << r;
    std::cout << "\n";
    std::cout << "[native_h700] cover thumb cache dir: "
              << filesystem_compat::LexicallyNormal(paths.cover_thumb_cache_dir).string() << "\n";
    std::cout << "[native_h700] removable cover thumb cache dir: "
              << filesystem_compat::LexicallyNormal(paths.removable_cover_thumb_cache_dir).string() << "\n";
    std::cout << "[native_h700] txt layout cache dir: "
              << filesystem_compat::LexicallyNormal(paths.txt_layout_cache_dir).string() << "\n";
    std::cout << "[native_h700] removable txt layout cache dir: "
              << filesystem_compat::LexicallyNormal(paths.removable_txt_layout_cache_dir).string() << "\n";
  }

  return paths;
}
