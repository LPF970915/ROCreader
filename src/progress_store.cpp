#include "progress_store.h"

#include <SDL.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

ProgressStore::ProgressStore(std::string path) : path_(std::move(path)) { Load(); }

ReaderProgress ProgressStore::Get(const std::string &book) const {
  auto it = map_.find(book);
  return it == map_.end() ? ReaderProgress{} : it->second;
}

void ProgressStore::Set(const std::string &book, const ReaderProgress &p) {
  auto it = map_.find(book);
  if (it != map_.end() &&
      it->second.page == p.page &&
      it->second.rotation == p.rotation &&
      std::abs(it->second.zoom - p.zoom) < 0.0001f &&
      it->second.scroll_x == p.scroll_x &&
      it->second.scroll_y == p.scroll_y) {
    return;
  }
  map_[book] = p;
  MarkDirty();
}

bool ProgressStore::IsDirty() const { return dirty_; }

bool ProgressStore::ShouldFlush(uint32_t now, uint32_t delay_ms) const {
  return dirty_ && (last_dirty_tick_ == 0 || now - last_dirty_tick_ >= delay_ms);
}

void ProgressStore::MarkDirty() {
  dirty_ = true;
  last_dirty_tick_ = SDL_GetTicks();
}

void ProgressStore::Save() {
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return;
  for (const auto &kv : map_) {
    out << kv.first << "\t" << kv.second.page << "\t" << kv.second.rotation << "\t" << kv.second.zoom << "\t"
        << kv.second.scroll_x << "\t" << kv.second.scroll_y << "\n";
  }
  dirty_ = false;
  last_dirty_tick_ = 0;
}

void ProgressStore::Load() {
  map_.clear();
  std::ifstream in(path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    ReaderProgress rp;
    std::string book;
    if (!(iss >> book >> rp.page >> rp.rotation >> rp.zoom >> rp.scroll_x >> rp.scroll_y)) continue;
    map_[book] = rp;
  }
  dirty_ = false;
  last_dirty_tick_ = 0;
}
