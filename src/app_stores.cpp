#include "app_stores.h"

#include <algorithm>
#include <fstream>
#include <utility>

namespace {
constexpr int kSdlMixMaxVolume = 128;
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
  if (!out) return;
  out << "theme=" << cfg_.theme << "\n";
  out << "animations=" << (cfg_.animations ? 1 : 0) << "\n";
  out << "audio=" << (cfg_.audio ? 1 : 0) << "\n";
  out << "sfx_volume=" << cfg_.sfx_volume << "\n";
  dirty_ = false;
  last_dirty_tick_ = 0;
}

void ConfigStore::Load() {
  std::ifstream in(path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = line.substr(0, eq);
    const std::string v = line.substr(eq + 1);
    if (k == "theme") cfg_.theme = std::stoi(v);
    else if (k == "animations") cfg_.animations = (v == "1");
    else if (k == "audio") cfg_.audio = (v == "1");
    else if (k == "sfx_volume") cfg_.sfx_volume = std::stoi(v);
  }
  cfg_.sfx_volume = std::clamp(cfg_.sfx_volume, 0, kSdlMixMaxVolume);
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
