#pragma once

#include "filesystem_compat.h"
#include "input_manager.h"
#include <cstdint>
#include <string>

class RgdsPlusLidMonitor {
public:
  bool Poll(uint32_t now, bool enabled);
  bool Observe(int hall_value, uint32_t now, bool enabled);
  void SuspendCompleted(bool success, uint32_t now);

private:
  bool polled_ = false;
  bool close_pending_ = false;
  bool close_handled_ = false;
  uint32_t last_poll_ = 0;
  uint32_t closed_since_ = 0;
};

class LidPowerController {
public:
  explicit LidPowerController(std::filesystem::path power_script_path);

  bool Enabled() const;
  void SetEnabled(bool enabled);
  bool ScriptAvailable() const;
  bool TriggerAutoIfEnabled() const;
  bool TriggerPowerKeyScreenOff(InputProfile input_profile) const;
  bool TriggerScreenOn(InputProfile input_profile) const;
  // The Plus firmware's blocking suspend call returns after hardware wake.
  bool SuspendRgdsPlus(bool lid_closed) const;
  std::string PowerScriptPath() const;

private:
  std::filesystem::path power_script_path_;
  bool enabled_ = true;
};
