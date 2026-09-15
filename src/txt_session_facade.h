#pragma once

#include "reader_session_state.h"
#include "txt_reader_module.h"
#include "txt_reader_session.h"
#include "txt_text_service.h"

#include <SDL.h>

#include <filesystem>
#include <functional>
#include <string>

struct TxtSessionFacadeDeps {
  ReaderUiState &ui;
  TxtTextServiceState &text_service;
  std::function<void()> open_ui_font;
  std::function<bool()> has_reader_font;
  std::function<int()> reader_font_height;
  std::function<SDL_Rect()> get_text_viewport_bounds;
  std::function<std::string(const std::string &)> normalize_path_key;
  std::function<bool(const std::string &, std::string &)> decode_text_bytes_to_utf8;
  std::function<bool(TxtReaderState &, const std::string &, size_t)> append_wrapped_text_line;
  std::function<void()> invalidate_all_render_cache;
  std::function<void()> clamp_text_scroll;
  std::filesystem::path epub_asset_cache_dir;
  int txt_line_spacing = 0;
  size_t txt_max_bytes = 0;
  size_t txt_max_wrapped_lines = 0;
  uint32_t txt_resume_save_delay_ms = 0;
  int txt_layout_cache_version = kTxtLayoutCacheVersion;
};

class TxtSessionFacade {
public:
  explicit TxtSessionFacade(TxtSessionFacadeDeps deps);

  TxtReaderSessionDeps MakeDeps() const;
  TxtReaderModuleCallbacks ModuleCallbacks(const std::string &current_book) const;

  void Close() const;
  bool OpenTextBook(const std::string &path) const;
  bool OpenEpubTextFallback(const std::string &path) const;
  void TickLoading(const std::string &book_path, uint32_t budget_ms, size_t byte_budget) const;
  void PersistResumeSnapshot(const std::string &book_path, bool force) const;
  void ScrollBy(const std::string &book_path, int delta_px) const;
  void PageBy(const std::string &book_path, int dir) const;
  void JumpToPercent(const std::string &book_path, int pct) const;
  void JumpToSourceOffset(const std::string &book_path, size_t source_offset) const;

private:
  TxtSessionFacadeDeps deps_;
};
