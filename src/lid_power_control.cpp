#include "lid_power_control.h"

#include "runtime_log.h"

#include <cstdlib>
#include <initializer_list>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

std::string EscapeShellArg(const std::filesystem::path &path) {
  std::string text = path.string();
  std::string escaped = "'";
  for (char ch : text) {
    if (ch == '\'') escaped += "'\\''";
    else escaped += ch;
  }
  escaped += "'";
  return escaped;
}

std::string EscapeShellArg(const std::string &text) {
  std::string escaped = "'";
  for (char ch : text) {
    if (ch == '\'') escaped += "'\\''";
    else escaped += ch;
  }
  escaped += "'";
  return escaped;
}

std::string EnvOrDefault(const char *name, const char *fallback) {
  const char *value = std::getenv(name);
  return (value && *value) ? std::string(value) : std::string(fallback ? fallback : "");
}

bool RunCommandQuiet(const std::string &command) {
  if (command.empty()) return false;
  const int rc = std::system((command + " >/dev/null 2>&1").c_str());
  return rc == 0;
}

bool RunPowerScriptArg(const std::filesystem::path &script, const std::string &arg) {
  if (script.empty() || arg.empty()) return false;
  const std::string command = EscapeShellArg(script) + " " + EscapeShellArg(arg);
  const bool ok = RunCommandQuiet(command);
  runtime_log::Line(std::string("lid_power: script arg=") + arg +
                    " rc=" + (ok ? "0" : "nonzero") +
                    " path=" + script.string());
  return ok;
}

bool RunPowerScriptArgList(const std::filesystem::path &script,
                           std::initializer_list<const char *> args) {
  for (const char *arg : args) {
    if (arg && *arg && RunPowerScriptArg(script, arg)) return true;
  }
  return false;
}

bool RunPowerScriptArgListReverse(const std::filesystem::path &script,
                                  std::initializer_list<const char *> args) {
  for (const char *arg : args) {
    if (arg && *arg && RunPowerScriptArg(script, arg)) return true;
  }
  return false;
}

std::string TrimAscii(std::string text) {
  auto is_space = [](unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
  };
  size_t start = 0;
  while (start < text.size() && is_space(static_cast<unsigned char>(text[start]))) ++start;
  size_t end = text.size();
  while (end > start && is_space(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(start, end - start);
}

std::filesystem::path RgdsPowerStatePath(const std::filesystem::path &script) {
  if (const char *env = std::getenv("ROCREADER_RGDS_POWER_STATE_FILE"); env && *env) {
    return std::filesystem::path(env);
  }
  std::filesystem::path state = script;
  state.replace_filename("rgds_power_state.txt");
  return state;
}

bool TryReadTextFile(const std::filesystem::path &path, std::string &out) {
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream oss;
  oss << in.rdbuf();
  out = TrimAscii(oss.str());
  return !out.empty();
}

bool RgdsPowerStateMatches(const std::filesystem::path &script, const std::string &expected, bool &state_available) {
  state_available = false;
  const std::filesystem::path state_path = RgdsPowerStatePath(script);
  std::string state;
  if (!TryReadTextFile(state_path, state)) return false;
  state_available = true;
  const bool match = state == expected;
  if (!match) {
    runtime_log::Line("lid_power: rgds power state mismatch expected=" + expected +
                      " got=" + state + " path=" + state_path.string());
  }
  return match;
}

bool IsH700InputProfile(InputProfile input_profile) {
  return input_profile == InputProfile::H700Default ||
         input_profile == InputProfile::H70034xxSp ||
         input_profile == InputProfile::H70035xxH;
}

bool SetGkd350HUltraDisplayPower(bool on) {
  const char *env_name = on ? "ROCREADER_GKD350H_SCREEN_ON_COMMAND"
                            : "ROCREADER_GKD350H_SCREEN_OFF_COMMAND";
  if (const char *command = std::getenv(env_name); command && *command) {
    const bool ok = RunCommandQuiet(command);
    runtime_log::Line(std::string("lid_power: GKD350HUltra display ") +
                      (on ? "on" : "off") + " via env command rc=" +
                      (ok ? "0" : "nonzero"));
    return ok;
  }

  // ROCKNIX drives the built-in DSI panel through Sway/DRM. fb0/blank is
  // present for compatibility but does not power down this panel. Use the
  // compositor's idempotent output power command so a hardware power-key
  // wake cannot accidentally toggle the display off again.
  const std::string output = EnvOrDefault("ROCREADER_GKD350H_OUTPUT", "DSI-1");
  const std::string command =
      "env XDG_RUNTIME_DIR=/var/run/0-runtime-dir WAYLAND_DISPLAY=wayland-1 "
      "SWAYSOCK=/var/run/0-runtime-dir/sway-ipc.0.sock swaymsg output " +
      EscapeShellArg(output) + " power " + (on ? "on" : "off");
  const bool ok = RunCommandQuiet(command);
  runtime_log::Line(std::string("lid_power: GKD350HUltra display ") +
                    (on ? "on" : "off") + " via sway output=" + output +
                    " rc=" + (ok ? "0" : "nonzero"));
  return ok;
}

LidPowerController::LidPowerController(std::filesystem::path power_script_path)
    : power_script_path_(std::move(power_script_path)) {}

bool LidPowerController::Enabled() const { return enabled_; }

void LidPowerController::SetEnabled(bool enabled) { enabled_ = enabled; }

bool LidPowerController::ScriptAvailable() const {
  std::error_code ec;
  return !power_script_path_.empty() && std::filesystem::exists(power_script_path_, ec) && !ec;
}

bool LidPowerController::TriggerAutoIfEnabled() const {
  if (!enabled_ || !ScriptAvailable()) return false;
  if (const char *command = std::getenv("ROCREADER_RGDS_AUTO_SLEEP_COMMAND"); command && *command) {
    if (RunCommandQuiet(command)) {
      bool state_available = false;
      return !RgdsPowerStateMatches(std::filesystem::path(command), "off", state_available) ? !state_available : true;
    }
  }
  const bool script_ok =
      RunPowerScriptArgList(power_script_path_, {"auto", "powerkey", "manual", "off"}) ||
      RunPowerScriptArgListReverse(power_script_path_, {"off", "manual", "powerkey", "auto"});
  if (!script_ok) return false;
  bool state_available = false;
  return RgdsPowerStateMatches(power_script_path_, "off", state_available) || !state_available;
}

bool LidPowerController::TriggerPowerKeyScreenOff(InputProfile input_profile) const {
  if (input_profile == InputProfile::GKD350HUltra) {
    return SetGkd350HUltraDisplayPower(false);
  }

  if (input_profile == InputProfile::TrimuiBrick) {
    if (const char *command = std::getenv("ROCREADER_TRIMUI_POWER_KEY_SCREEN_OFF_COMMAND");
        command && *command) {
      return RunCommandQuiet(command);
    }
    const std::string shmvar_path =
        EnvOrDefault("ROCREADER_TRIMUI_SHMVAR_PATH", "/usr/trimui/bin/shmvar");
    if (RunCommandQuiet(EscapeShellArg(shmvar_path) + " lcd 0")) return true;
    return RunCommandQuiet("sh -c 'echo 1 > /sys/class/graphics/fb0/blank'");
  }

  if (!ScriptAvailable()) return false;
  const bool h700_profile = IsH700InputProfile(input_profile);
  const char *env_name = h700_profile ? "ROCREADER_H700_POWER_KEY_ARG" : "ROCREADER_POWER_KEY_ARG";
  if (const char *arg = std::getenv(env_name); arg && *arg) {
    return RunPowerScriptArg(power_script_path_, arg);
  }
  if (h700_profile) {
    return RunPowerScriptArgList(power_script_path_, {"off", "manual", "powerkey"});
  }
  return RunPowerScriptArgList(power_script_path_, {"powerkey", "manual", "off"});
}

bool LidPowerController::TriggerScreenOn(InputProfile input_profile) const {
  if (input_profile == InputProfile::GKD350HUltra) {
    return SetGkd350HUltraDisplayPower(true);
  }

  if (input_profile == InputProfile::TrimuiBrick) {
    if (const char *command = std::getenv("ROCREADER_TRIMUI_SCREEN_ON_COMMAND"); command && *command) {
      return RunCommandQuiet(command);
    }
    const std::string shmvar_path =
        EnvOrDefault("ROCREADER_TRIMUI_SHMVAR_PATH", "/usr/trimui/bin/shmvar");
    if (RunCommandQuiet(EscapeShellArg(shmvar_path) + " lcd 1")) return true;
    return RunCommandQuiet("sh -c 'echo 0 > /sys/class/graphics/fb0/blank'");
  }

  if (IsH700InputProfile(input_profile)) {
    if (const char *command = std::getenv("ROCREADER_H700_SCREEN_ON_COMMAND"); command && *command) {
      return RunCommandQuiet(command);
    }
    if (const char *arg = std::getenv("ROCREADER_H700_SCREEN_ON_ARG"); arg && *arg) {
      if (!ScriptAvailable()) return false;
      return RunPowerScriptArg(power_script_path_, arg);
    }
    // H700 stock pwr_new.sh treats unknown args like a power-key sleep request.
    // Physical power-key wake already turns the display back on, so skip the
    // script here and only let the app resync input state.
    runtime_log::Line("lid_power: h700 screen-on handled by hardware power key; skip pwr script");
    return true;
  }

  if (!ScriptAvailable()) return false;
  if (const char *arg = std::getenv("ROCREADER_SCREEN_ON_ARG"); arg && *arg) {
    if (!RunPowerScriptArg(power_script_path_, arg)) return false;
    bool state_available = false;
    return RgdsPowerStateMatches(power_script_path_, "on", state_available) || !state_available;
  }
  if (const char *command = std::getenv("ROCREADER_RGDS_SCREEN_ON_COMMAND"); command && *command) {
    if (RunCommandQuiet(command)) {
      bool state_available = false;
      return RgdsPowerStateMatches(power_script_path_, "on", state_available) || !state_available;
    }
  }
  if (!(RunPowerScriptArgList(power_script_path_, {"on", "wake", "resume"}))) return false;
  bool state_available = false;
  return RgdsPowerStateMatches(power_script_path_, "on", state_available) || !state_available;
}

std::string LidPowerController::PowerScriptPath() const { return power_script_path_.string(); }

bool LidPowerController::SuspendRgdsPlus(bool lid_closed) const {
  if (!ScriptAvailable() || (lid_closed && !enabled_)) return false;
  return RunPowerScriptArg(power_script_path_, lid_closed ? "lid" : "suspend");
}

bool RgdsPlusLidMonitor::Poll(uint32_t now, bool enabled) {
  if (!enabled) return Observe(-1, now, false);
  if (polled_ && now - last_poll_ < 100) return false;
  polled_ = true;
  last_poll_ = now;
  std::ifstream in(EnvOrDefault("ROCREADER_RGDS_HALL_PATH",
                               "/sys/class/anbernic_misc/hallkey"));
  int value = -1;
  if (!(in >> value)) value = -1;
  in >> std::ws;
  if (!in.eof()) value = -1;
  return Observe(value, now, true);
}

bool RgdsPlusLidMonitor::Observe(int hall_value, uint32_t now, bool enabled) {
  if (!enabled) {
    close_pending_ = false;
    close_handled_ = false;
    polled_ = false;
    return false;
  }
  if (hall_value < 0 || hall_value > 3) {
    close_pending_ = false;
    return false;
  }
  // Firmware dmenu uses bit 0 (0 = closed), not the entire two-bit value.
  if ((hall_value & 1) != 0) {
    close_pending_ = false;
    close_handled_ = false;
    return false;
  }
  if (close_handled_) return false;
  if (!close_pending_) {
    close_pending_ = true;
    closed_since_ = now;
  }
  if (now - closed_since_ < 3000) return false;
  close_handled_ = true;
  return true;
}

void RgdsPlusLidMonitor::SuspendCompleted(bool success, uint32_t now) {
  // An RTC/power-key wake while still closed must not immediately suspend again.
  close_handled_ = success;
  close_pending_ = !success;
  closed_since_ = now;
}
