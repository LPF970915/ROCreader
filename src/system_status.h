#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct SystemStatusSnapshot {
  bool battery_available = false;
  int battery_percent = -1;
  bool charging_status_available = false;
  bool charging = false;
  std::string charging_text;
  std::string clock_text;
};

class SystemStatusMonitor {
public:
  SystemStatusMonitor();

  void Poll(uint32_t now);
  const SystemStatusSnapshot &Snapshot() const;
  std::string BatteryCapacityPath() const;
  std::string BatteryStatusPath() const;

private:
  static std::string ReadSmallTextFile(const std::filesystem::path &path);
  static std::string TrimAscii(std::string text);
  static std::string ToLowerAscii(std::string text);
  static bool ParseInt(const std::string &text, int &out_value);
  void DiscoverBatteryPaths();
  void UpdateClock();
  void UpdateBattery();

  SystemStatusSnapshot snapshot_;
  std::filesystem::path battery_capacity_path_;
  std::filesystem::path battery_status_path_;
  uint32_t last_poll_tick_ = 0;
};
