#include "app_stores.h"
#include "app_language.h"
#include "system_settings_runtime.h"
#include "txt_settings_runtime.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <utility>

namespace {
constexpr int kSdlMixMaxVolume = 128;
constexpr int kBrightnessPercentMax = 100;
constexpr int kLegacyBrightnessMaxLevel = 8;
constexpr int kBrightnessSchema = 3;

int LegacyBrightnessLevelToPercent(int level) {
  const int clamped = std::clamp(level, 0, kLegacyBrightnessMaxLevel);
  return std::clamp((clamped * kBrightnessPercentMax + kLegacyBrightnessMaxLevel / 2) / kLegacyBrightnessMaxLevel,
                    0, kBrightnessPercentMax);
}

int PercentBrightnessToLevel(int percent) {
  const int clamped = std::clamp(percent, 0, kBrightnessPercentMax);
  return std::clamp((clamped * kLegacyBrightnessMaxLevel + kBrightnessPercentMax / 2) / kBrightnessPercentMax,
                    0, kLegacyBrightnessMaxLevel);
}
}

ConfigStore::ConfigStore(std::string path) : path_(std::move(path)) {
  Load();
}

const NativeConfig &ConfigStore::Get() const { return cfg_; }
NativeConfig &ConfigStore::Mutable() { return cfg_; }
bool ConfigStore::IsDirty() const { return dirty_; }

bool ConfigStore::ShouldFlush(uint32_t now, uint32_t delay_ms) const {
  return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
}

void ConfigStore::MarkDirty() {
  dirty_ = true;
  last_dirty_tick_ = SDL_GetTicks();
}

void ConfigStore::Save() {
  std::ofstream out(path_, std::ios::trunc);
  if (!out) {
    std::cout << "[native_h700][config] save failed path=" << path_ << "\n";
    return;
  }
  out << "theme=" << cfg_.theme << "\n";
  out << "animations=" << (cfg_.animations ? 1 : 0) << "\n";
  out << "audio=" << (cfg_.audio ? 1 : 0) << "\n";
  out << "sfx_volume=" << cfg_.sfx_volume << "\n";
  if (!cfg_.screen_profile.empty()) out << "screen_profile=" << cfg_.screen_profile << "\n";
  if (!cfg_.system_language.empty()) out << "system_language=" << cfg_.system_language << "\n";
  out << "system_volume_percent=" << cfg_.system_volume_percent << "\n";
  out << "screen_brightness_level=" << cfg_.screen_brightness_level << "\n";
  out << "screen_brightness_schema=" << cfg_.screen_brightness_schema << "\n";
  out << "lid_close_screen_off=" << (cfg_.lid_close_screen_off ? 1 : 0) << "\n";
  out << "auto_sleep_interval_schema=" << cfg_.auto_sleep_interval_schema << "\n";
  out << "auto_sleep_interval_index=" << cfg_.auto_sleep_interval_index << "\n";
  if (!cfg_.selected_contributor_avatar_label.empty()) {
    out << "selected_contributor_avatar_label=" << cfg_.selected_contributor_avatar_label << "\n";
  }
  out << "txt_background_color=" << cfg_.txt_background_color << "\n";
  out << "txt_font_color=" << cfg_.txt_font_color << "\n";
  out << "txt_font_size_level=" << cfg_.txt_font_size_level << "\n";
  dirty_ = false;
  last_dirty_tick_ = 0;
}

void ConfigStore::Load() {
  std::ifstream in(path_);
  if (!in) return;
  bool saw_system_volume_percent = false;
  bool saw_screen_brightness_level = false;
  bool saw_screen_brightness_schema = false;
  bool saw_auto_sleep_interval_schema = false;
  bool saw_auto_sleep_interval_index = false;
  bool saw_txt_background_color = false;
  bool saw_txt_font_color = false;
  bool saw_txt_font_size_level = false;
  std::string line;
  while (std::getline(in, line)) {
    // Windows-edited config files retain CR on Linux; do not turn "1\r" into false.
    const size_t last = line.find_last_not_of(" \t\r");
    if (last == std::string::npos) continue;
    line.erase(last + 1);
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = line.substr(0, eq);
    const std::string v = line.substr(eq + 1);
    if (k == "theme") cfg_.theme = std::stoi(v);
    else if (k == "animations") cfg_.animations = (v == "1");
    else if (k == "audio") cfg_.audio = (v == "1");
    else if (k == "sfx_volume") cfg_.sfx_volume = std::stoi(v);
    else if (k == "screen_profile") cfg_.screen_profile = v;
    else if (k == "system_language") cfg_.system_language = v;
    else if (k == "system_volume_percent") {
      cfg_.system_volume_percent = std::stoi(v);
      saw_system_volume_percent = true;
    } else if (k == "screen_brightness_level") {
      cfg_.screen_brightness_level = std::stoi(v);
      saw_screen_brightness_level = true;
    } else if (k == "screen_brightness_schema") {
      cfg_.screen_brightness_schema = std::stoi(v);
      saw_screen_brightness_schema = true;
    } else if (k == "lid_close_screen_off") {
      cfg_.lid_close_screen_off = (v == "1");
    } else if (k == "auto_sleep_interval_schema") {
      cfg_.auto_sleep_interval_schema = std::stoi(v);
      saw_auto_sleep_interval_schema = true;
    } else if (k == "auto_sleep_interval_index") {
      cfg_.auto_sleep_interval_index = std::stoi(v);
      saw_auto_sleep_interval_index = true;
    } else if (k == "selected_contributor_avatar_label") {
      cfg_.selected_contributor_avatar_label = v;
    } else if (k == "txt_background_color") {
      cfg_.txt_background_color = std::stoi(v);
      saw_txt_background_color = true;
    } else if (k == "txt_font_color") {
      cfg_.txt_font_color = std::stoi(v);
      saw_txt_font_color = true;
    } else if (k == "txt_font_size_level") {
      cfg_.txt_font_size_level = std::stoi(v);
      saw_txt_font_size_level = true;
    }
  }
  cfg_.sfx_volume = std::clamp(cfg_.sfx_volume, 0, kSdlMixMaxVolume);
  cfg_.system_language = NormalizeSystemLanguageConfigValue(cfg_.system_language);
  cfg_.system_volume_percent = std::clamp(cfg_.system_volume_percent, 0, 100);
  if (!saw_screen_brightness_schema || cfg_.screen_brightness_schema < 2) {
    cfg_.screen_brightness_level = LegacyBrightnessLevelToPercent(cfg_.screen_brightness_level);
    cfg_.screen_brightness_schema = 2;
    dirty_ = true;
  }
  if (cfg_.screen_brightness_schema < kBrightnessSchema) {
    cfg_.screen_brightness_level = PercentBrightnessToLevel(cfg_.screen_brightness_level);
    cfg_.screen_brightness_schema = kBrightnessSchema;
    dirty_ = true;
  }
  cfg_.screen_brightness_level = std::clamp(cfg_.screen_brightness_level, 0, kLegacyBrightnessMaxLevel);
  cfg_.screen_brightness_schema = kBrightnessSchema;
  if (!saw_auto_sleep_interval_schema && saw_auto_sleep_interval_index && cfg_.auto_sleep_interval_index >= 2) {
    ++cfg_.auto_sleep_interval_index;
  }
  cfg_.auto_sleep_interval_schema = 2;
  cfg_.auto_sleep_interval_index = ClampAutoSleepIntervalIndex(cfg_.auto_sleep_interval_index);
  cfg_.txt_background_color = std::clamp(cfg_.txt_background_color, 0, 4);
  cfg_.txt_font_color = std::clamp(cfg_.txt_font_color, 0, 4);
  cfg_.txt_font_size_level = ClampTxtFontSizeLevel(cfg_.txt_font_size_level);
  if (!saw_system_volume_percent || !saw_screen_brightness_level || !saw_screen_brightness_schema ||
      !saw_auto_sleep_interval_schema || !saw_auto_sleep_interval_index || !saw_txt_background_color ||
      !saw_txt_font_color || !saw_txt_font_size_level) {
    dirty_ = true;
  }
}

PathSetStore::PathSetStore(std::string path, std::function<std::string(const std::string &)> normalize_path_key)
    : path_(std::move(path)), normalize_path_key_(std::move(normalize_path_key)) {
  Load();
}

bool PathSetStore::Contains(const std::string &book_path) const {
  return set_.find(normalize_path_key_(book_path)) != set_.end();
}

void PathSetStore::Add(const std::string &book_path) {
  if (set_.insert(normalize_path_key_(book_path)).second) MarkDirty();
}

void PathSetStore::Remove(const std::string &book_path) {
  if (set_.erase(normalize_path_key_(book_path)) > 0) MarkDirty();
}

bool PathSetStore::Toggle(const std::string &book_path) {
  const std::string key = normalize_path_key_(book_path);
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

void PathSetStore::Clear() {
  if (set_.empty()) return;
  set_.clear();
  MarkDirty();
}

bool PathSetStore::IsDirty() const { return dirty_; }

bool PathSetStore::ShouldFlush(uint32_t now, uint32_t delay_ms) const {
  return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
}

void PathSetStore::MarkDirty() {
  dirty_ = true;
  last_dirty_tick_ = SDL_GetTicks();
}

void PathSetStore::Save() {
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return;
  for (const auto &v : set_) out << v << "\n";
  dirty_ = false;
  last_dirty_tick_ = 0;
}

void PathSetStore::Load() {
  std::ifstream in(path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    set_.insert(normalize_path_key_(line));
  }
}

RecentPathStore::RecentPathStore(std::string path, std::function<std::string(const std::string &)> normalize_path_key)
    : path_(std::move(path)), normalize_path_key_(std::move(normalize_path_key)) {
  Load();
}

bool RecentPathStore::Contains(const std::string &book_path) const {
  return set_.find(normalize_path_key_(book_path)) != set_.end();
}

void RecentPathStore::Add(const std::string &book_path) {
  const std::string key = normalize_path_key_(book_path);
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

void RecentPathStore::Remove(const std::string &book_path) {
  const std::string key = normalize_path_key_(book_path);
  if (set_.erase(key) == 0) return;
  order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
  MarkDirty();
}

void RecentPathStore::Clear() {
  if (order_.empty()) return;
  set_.clear();
  order_.clear();
  MarkDirty();
}

const std::vector<std::string> &RecentPathStore::OrderedPaths() const { return order_; }
bool RecentPathStore::IsDirty() const { return dirty_; }

bool RecentPathStore::ShouldFlush(uint32_t now, uint32_t delay_ms) const {
  return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
}

void RecentPathStore::MarkDirty() {
  dirty_ = true;
  last_dirty_tick_ = SDL_GetTicks();
}

void RecentPathStore::Save() {
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return;
  for (const auto &v : order_) out << v << "\n";
  dirty_ = false;
  last_dirty_tick_ = 0;
}

void RecentPathStore::Load() {
  std::ifstream in(path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const std::string key = normalize_path_key_(line);
    if (key.empty() || !set_.insert(key).second) continue;
    order_.push_back(key);
  }
}
