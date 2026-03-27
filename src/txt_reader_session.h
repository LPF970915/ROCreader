#pragma once

#include "reader_session_state.h"

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

struct TxtReaderSessionDeps {
  ReaderUiState &ui;
  std::unordered_map<std::string, TxtLayoutCacheEntry> &layout_cache;
  std::function<void()> open_ui_font;
  std::function<bool()> has_reader_font;
  std::function<int()> reader_font_height;
  std::function<SDL_Rect()> get_text_viewport_bounds;
  std::function<std::string(const std::string &, const SDL_Rect &, int, uintmax_t, long long)> make_layout_cache_key;
  std::function<bool(const std::string &, TxtLayoutCacheEntry &)> load_layout_cache_from_disk;
  std::function<void(const std::string &, const TxtLayoutCacheEntry &)> save_layout_cache_to_disk;
  std::function<bool(const std::string &, TxtResumeCacheEntry &)> load_resume_cache_from_disk;
  std::function<void(const std::string &, const TxtReaderState &)> save_resume_cache_to_disk;
  std::function<void()> prune_layout_cache;
  std::function<bool(const std::string &, std::string &)> decode_text_bytes_to_utf8;
  std::function<bool(TxtReaderState &, const std::string &)> append_wrapped_text_line;
  std::function<void()> invalidate_all_render_cache;
  std::function<void()> clamp_text_scroll;
  int txt_line_spacing = 0;
  size_t txt_max_bytes = 0;
  uint32_t txt_resume_save_delay_ms = 0;
};

bool OpenTextBookSession(const std::string &path, TxtReaderSessionDeps &deps);
void FinalizeTextReaderLoading(TxtReaderState &state, const std::string *cache_key, TxtReaderSessionDeps &deps);
void ProcessTextLayoutChunk(TxtReaderState &state, uint32_t budget_ms, size_t byte_budget,
                            const std::string *cache_key, TxtReaderSessionDeps &deps);
void WarmTextReaderToTarget(TxtReaderState &state, const std::string *cache_key, TxtReaderSessionDeps &deps);
void TickTextBookSession(const std::string &book_path, TxtReaderSessionDeps &deps,
                         uint32_t budget_ms, size_t byte_budget);
void PersistCurrentTxtResumeSnapshot(const std::string &book_path, bool force, TxtReaderSessionDeps &deps);
void TextScrollBy(int delta_px, const std::string &book_path, TxtReaderSessionDeps &deps);
void TextPageBy(int dir, const std::string &book_path, TxtReaderSessionDeps &deps);
int TxtReaderProgressPercent(const TxtReaderState &state);
