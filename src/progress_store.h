#pragma once

#include "reader_core.h"

#include <string>
#include <unordered_map>

class ProgressStore {
public:
  explicit ProgressStore(std::string path);

  ReaderProgress Get(const std::string &book) const;
  void Set(const std::string &book, const ReaderProgress &p);
  bool IsDirty() const;
  bool ShouldFlush(uint32_t now, uint32_t delay_ms) const;
  void MarkDirty();
  void Save();

private:
  void Load();

  std::string path_;
  std::unordered_map<std::string, ReaderProgress> map_;
  bool dirty_ = false;
  uint32_t last_dirty_tick_ = 0;
};
