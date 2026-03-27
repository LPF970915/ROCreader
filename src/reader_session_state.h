#pragma once

#include "reader_core.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

struct TxtReaderState {
  bool open = false;
  std::vector<std::string> lines;
  int scroll_px = 0;
  int target_scroll_px = 0;
  int viewport_x = 0;
  int viewport_y = 0;
  int viewport_w = 0;
  int viewport_h = 0;
  int line_h = 28;
  int content_h = 0;
  std::string pending_raw;
  std::string pending_line;
  std::string cache_key;
  size_t parse_pos = 0;
  bool loading = false;
  bool truncated = false;
  bool limit_hit = false;
  bool truncation_notice_added = false;
  uint32_t last_resume_cache_save = 0;
  bool resume_cache_dirty = false;
};

struct TxtLayoutCacheEntry {
  std::vector<std::string> lines;
  int viewport_w = 0;
  int viewport_h = 0;
  int line_h = 0;
  int content_h = 0;
  bool truncated = false;
  bool limit_hit = false;
  uint32_t last_use = 0;
};

struct TxtResumeCacheEntry {
  std::vector<std::string> lines;
  std::string pending_raw;
  std::string pending_line;
  int viewport_w = 0;
  int viewport_h = 0;
  int line_h = 0;
  int content_h = 0;
  int scroll_px = 0;
  int target_scroll_px = 0;
  size_t parse_pos = 0;
  bool loading = false;
  bool truncated = false;
  bool limit_hit = false;
  bool truncation_notice_added = false;
};

struct TxtTranscodeJob {
  bool active = false;
  std::vector<std::string> files;
  size_t total = 0;
  size_t processed = 0;
  size_t converted = 0;
  size_t failed = 0;
  std::string current_file;
};

struct ReaderUiState {
  std::string current_book;
  ReaderProgress progress;
  ReaderMode mode = ReaderMode::None;
  TxtReaderState txt_reader;
  bool progress_overlay_visible = false;
  float hold_cooldown = 0.0f;
  std::array<float, kButtonCount> hold_speed{};
  std::array<bool, kButtonCount> long_fired{};
  bool warned_mock_pdf_backend = false;
  bool warned_epub_backend = false;
};

inline void ResetReaderInputState(ReaderUiState &state) {
  state.hold_cooldown = 0.0f;
  for (auto &value : state.hold_speed) value = 0.0f;
  for (auto &value : state.long_fired) value = false;
}
