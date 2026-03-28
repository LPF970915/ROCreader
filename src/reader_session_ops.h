#pragma once

#include "epub_runtime.h"
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
  EpubRuntime &epub_runtime;
  std::function<bool(const std::string &)> open_text_book;
  std::function<void()> close_text_reader;
};

struct ReaderCloseDeps {
  ReaderUiState &ui;
  ProgressStore &progress_store;
  PdfRuntime &pdf_runtime;
  EpubRuntime &epub_runtime;
  std::function<void()> close_text_reader;
  std::function<void(const std::string &, bool)> persist_current_txt_resume_snapshot;
};

bool OpenReaderSession(const std::string &book_path, const std::string &ext, ReaderOpenDeps &deps);
void CloseReaderSession(ReaderCloseDeps &deps);
