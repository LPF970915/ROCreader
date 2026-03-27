#pragma once

#include "epub_comic_reader.h"
#include "pdf_runtime.h"
#include "progress_store.h"
#include "reader_session_state.h"

#include <SDL.h>

#include <functional>
#include <string>

struct ReaderOpenDeps {
  SDL_Renderer *renderer = nullptr;
  int screen_w = 0;
  int screen_h = 0;
  ReaderUiState &ui;
  PdfRuntime &pdf_runtime;
  EpubComicReader &epub_comic;
  ReaderAdaptiveRenderState &adaptive_render;
  ReaderViewState &display_state;
  ReaderViewState &ready_state;
  ReaderViewState &target_state;
  bool &display_state_valid;
  bool &ready_state_valid;
  std::function<bool(const std::string &)> open_text_book;
  std::function<void()> close_text_reader;
  std::function<void()> reset_reader_async_state;
  std::function<void()> invalidate_all_render_cache;
  std::function<void()> clear_reader_page_size_cache;
  std::function<void()> clamp_scroll;
  std::function<int()> reader_current_page;
};

struct ReaderCloseDeps {
  ReaderUiState &ui;
  ProgressStore &progress_store;
  PdfRuntime &pdf_runtime;
  EpubComicReader &epub_comic;
  ReaderViewState &display_state;
  ReaderViewState &ready_state;
  ReaderViewState &target_state;
  bool &display_state_valid;
  bool &ready_state_valid;
  std::function<void()> close_text_reader;
  std::function<void()> reset_reader_async_state;
  std::function<void()> invalidate_all_render_cache;
  std::function<void()> clear_reader_page_size_cache;
  std::function<void(const std::string &, bool)> persist_current_txt_resume_snapshot;
};

bool OpenReaderSession(const std::string &book_path, const std::string &ext, ReaderOpenDeps &deps);
void CloseReaderSession(ReaderCloseDeps &deps);
