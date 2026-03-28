#include "progress_store.h"

#include <SDL.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>
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
    ReaderProgress rp;
    std::string_view row(line);
    size_t field_end = row.size();
    std::string fields[6];
    bool ok = true;
    for (int i = 5; i >= 1; --i) {
      const size_t tab = row.rfind('\t', field_end == row.size() ? std::string_view::npos : field_end - 1);
      if (tab == std::string_view::npos) {
        ok = false;
        break;
      }
      fields[i] = std::string(row.substr(tab + 1, field_end - tab - 1));
      field_end = tab;
    }
    if (!ok) continue;
    fields[0] = std::string(row.substr(0, field_end));
    if (fields[0].empty()) continue;

    try {
      rp.page = std::stoi(fields[1]);
      rp.rotation = std::stoi(fields[2]);
      rp.zoom = std::stof(fields[3]);
      rp.scroll_x = std::stoi(fields[4]);
      rp.scroll_y = std::stoi(fields[5]);
    } catch (...) {
      continue;
    }
    map_[fields[0]] = rp;
  }
  dirty_ = false;
  last_dirty_tick_ = 0;
}
