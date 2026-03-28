#include "system_status.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string ReadSmallTextFileImpl(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::string TrimAsciiImpl(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.erase(text.begin());
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  return text;
}

std::string ToLowerAsciiImpl(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}
} // namespace

SystemStatusMonitor::SystemStatusMonitor() {
  DiscoverBatteryPaths();
  UpdateClock();
  UpdateBattery();
}

void SystemStatusMonitor::Poll(uint32_t now) {
  if (last_poll_tick_ != 0 && now - last_poll_tick_ < 500) return;
  last_poll_tick_ = now;
  UpdateClock();
  UpdateBattery();
}

const SystemStatusSnapshot &SystemStatusMonitor::Snapshot() const { return snapshot_; }

std::string SystemStatusMonitor::BatteryCapacityPath() const { return battery_capacity_path_.string(); }

std::string SystemStatusMonitor::BatteryStatusPath() const { return battery_status_path_.string(); }

std::string SystemStatusMonitor::ReadSmallTextFile(const std::filesystem::path &path) {
  return ReadSmallTextFileImpl(path);
}

std::string SystemStatusMonitor::TrimAscii(std::string text) {
  return TrimAsciiImpl(std::move(text));
}

std::string SystemStatusMonitor::ToLowerAscii(std::string text) {
  return ToLowerAsciiImpl(std::move(text));
}

bool SystemStatusMonitor::ParseInt(const std::string &text, int &out_value) {
  try {
    size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed == 0) return false;
    out_value = value;
    return true;
  } catch (...) {
    return false;
  }
}

void SystemStatusMonitor::DiscoverBatteryPaths() {
  const char *env_capacity = std::getenv("ROCREADER_BATTERY_CAPACITY_PATH");
  const char *env_status = std::getenv("ROCREADER_BATTERY_STATUS_PATH");
  if (env_capacity && *env_capacity) battery_capacity_path_ = env_capacity;
  if (env_status && *env_status) battery_status_path_ = env_status;
  if (!battery_capacity_path_.empty() || !battery_status_path_.empty()) {
    std::cout << "[native_h700] battery discover: env capacity=" << battery_capacity_path_.string()
              << " status=" << battery_status_path_.string() << "\n";
    return;
  }

  const std::filesystem::path root("/sys/class/power_supply");
  std::error_code ec;
  if (!std::filesystem::exists(root, ec) || ec) {
    std::cout << "[native_h700] battery discover: power_supply root unavailable\n";
    return;
  }

  for (std::filesystem::directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const std::filesystem::path dir = it->path();
    const std::string type = ToLowerAscii(TrimAscii(ReadSmallTextFile(dir / "type")));
    const std::string lower_name = ToLowerAscii(dir.filename().string());
    const bool looks_like_battery =
        type == "battery" ||
        lower_name.find("battery") != std::string::npos ||
        lower_name.find("bat") != std::string::npos;
    if (!looks_like_battery) continue;

    const std::filesystem::path capacity = dir / "capacity";
    const std::filesystem::path status = dir / "status";
    if (battery_capacity_path_.empty() && std::filesystem::exists(capacity, ec) && !ec) {
      battery_capacity_path_ = capacity;
    }
    ec.clear();
    if (battery_status_path_.empty() && std::filesystem::exists(status, ec) && !ec) {
      battery_status_path_ = status;
    }
    if (!battery_capacity_path_.empty() || !battery_status_path_.empty()) break;
  }

  std::cout << "[native_h700] battery discover: capacity=" << battery_capacity_path_.string()
            << " status=" << battery_status_path_.string() << "\n";
}

void SystemStatusMonitor::UpdateClock() {
  const std::time_t now = std::time(nullptr);
  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &now);
#else
  localtime_r(&now, &local_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%H:%M");
  snapshot_.clock_text = oss.str();
}

void SystemStatusMonitor::UpdateBattery() {
  snapshot_.battery_available = false;
  snapshot_.battery_percent = -1;
  snapshot_.charging_status_available = false;
  snapshot_.charging = false;
  snapshot_.charging_text.clear();

  if (!battery_capacity_path_.empty()) {
    int percent = -1;
    if (ParseInt(TrimAscii(ReadSmallTextFile(battery_capacity_path_)), percent)) {
      snapshot_.battery_available = true;
      snapshot_.battery_percent = std::clamp(percent, 0, 100);
    }
  }

  if (!battery_status_path_.empty()) {
    const std::string status = TrimAscii(ReadSmallTextFile(battery_status_path_));
    if (!status.empty()) {
      const std::string lower = ToLowerAscii(status);
      snapshot_.charging_status_available = true;
      snapshot_.charging_text = status;
      snapshot_.charging =
          lower.find("charging") != std::string::npos && lower.find("discharging") == std::string::npos;
    }
  }
}
