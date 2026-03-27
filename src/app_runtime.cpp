#include "app_runtime.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

VolumeController::VolumeController(bool prefer_system) : prefer_system_(prefer_system) {}

bool VolumeController::UsesSystemVolume() const { return prefer_system_; }

bool VolumeController::AdjustUp() { return AdjustByPercent(+5); }

bool VolumeController::AdjustDown() { return AdjustByPercent(-5); }

bool VolumeController::AdjustByPercent(int delta_percent) {
  if (!prefer_system_) return false;
  const std::string delta =
      (delta_percent >= 0) ? (std::to_string(delta_percent) + "%+") : (std::to_string(-delta_percent) + "%-");
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
      std::cout << "[native_h700] volume command try: control=" << control << " cmd=" << cmd << "\n";
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

bool VolumeController::Run(const std::string &command) { return std::system(command.c_str()) == 0; }

void TickAppUiState(AppUiState &state, float dt) {
  state.menu_toggle_cooldown = std::max(0.0f, state.menu_toggle_cooldown - dt);
}

void HandleVolumeControls(AppUiState &state, const InputManager &input, uint32_t now,
                          VolumeController &volume_controller, ConfigStore &config,
                          const std::function<void(int)> &apply_sfx_volume,
                          const std::function<void()> &play_change_sfx) {
  const bool vol_up_pressed = input.IsJustPressed(Button::VolUp) || input.IsRepeated(Button::VolUp);
  const bool vol_down_pressed = input.IsJustPressed(Button::VolDown) || input.IsRepeated(Button::VolDown);
  if (!vol_up_pressed && !vol_down_pressed) return;

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
                << (vol_up_pressed && vol_down_pressed ? "unchanged-step" : (vol_up_pressed ? "up" : "down")) << "\n";
    } else if (!state.warned_system_volume_fallback) {
      state.warned_system_volume_fallback = true;
      std::cout << "[native_h700] system volume control unavailable, fallback to app sfx volume\n";
    }
  }

  if (!system_volume_changed) {
    NativeConfig &cfg = config.Mutable();
    const int old_volume = cfg.sfx_volume;
    if (vol_up_pressed) cfg.sfx_volume = std::min(SDL_MIX_MAXVOLUME, cfg.sfx_volume + 8);
    if (vol_down_pressed) cfg.sfx_volume = std::max(0, cfg.sfx_volume - 8);
    if (cfg.sfx_volume != old_volume) {
      if (apply_sfx_volume) apply_sfx_volume(cfg.sfx_volume);
      config.MarkDirty();
      std::cout << "[native_h700] sound volume: " << cfg.sfx_volume << "\n";
      if (cfg.audio && cfg.sfx_volume > 0 && play_change_sfx) {
        play_change_sfx();
      }
    }
    state.volume_display_percent =
        std::clamp((cfg.sfx_volume * 100) / std::max(1, SDL_MIX_MAXVOLUME), 0, 100);
  } else {
    if (vol_up_pressed) state.volume_display_percent = std::clamp(state.volume_display_percent + 5, 0, 100);
    if (vol_down_pressed) state.volume_display_percent = std::clamp(state.volume_display_percent - 5, 0, 100);
  }

  state.volume_display_until = now + 1500;
}

MenuToggleAction HandleMenuToggleInput(AppUiState &state, const InputManager &input, bool is_settings, bool is_shelf,
                                       bool is_reader, bool settings_close_armed, float settings_toggle_guard,
                                       bool menu_closing, float debounce_sec) {
  const bool start_just_pressed = input.IsJustPressed(Button::Start);
  const bool select_just_pressed = input.IsJustPressed(Button::Select);
  const bool menu_toggle_pressed = input.IsPressed(Button::Start) || input.IsPressed(Button::Select);
  if (!menu_toggle_pressed && state.menu_toggle_cooldown <= 0.0f) {
    state.menu_toggle_armed = true;
  }

  const bool menu_toggle_request = start_just_pressed || select_just_pressed;
  if (!menu_toggle_request || !state.menu_toggle_armed || state.menu_toggle_cooldown > 0.0f) {
    return MenuToggleAction::None;
  }

  state.menu_toggle_armed = false;
  state.menu_toggle_cooldown = debounce_sec;

  if (is_settings) {
    if (settings_close_armed && settings_toggle_guard <= 0.0f && !menu_closing) {
      return MenuToggleAction::CloseSettings;
    }
    return MenuToggleAction::None;
  }
  if (is_shelf) return MenuToggleAction::OpenFromShelf;
  if (is_reader) return MenuToggleAction::OpenFromReader;
  return MenuToggleAction::None;
}
