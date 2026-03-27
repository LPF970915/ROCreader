#pragma once

#include "reader_runtime_common.h"

#include <SDL.h>

#include <functional>
#include <string>

struct ReaderRenderRuntimeDeps {
  ReaderMode &reader_mode;
  std::string &current_book;
  ReaderRenderState &reader_render;
  ReaderAsyncState &reader_async;
  ReaderViewState &display_state;
  ReaderViewState &target_state;
  ReaderViewState &ready_state;
  bool &display_state_valid;
  bool &ready_state_valid;
  std::function<bool()> reader_is_open;
  std::function<float(const ReaderViewState &)> reader_target_scale_for_state;
  std::function<SDL_Texture *(int, int)> acquire_reader_texture;
  std::function<void(ReaderRenderCache &)> destroy_render_cache;
};

bool PromoteAsyncReaderRenderResult(ReaderRenderRuntimeDeps &deps);
bool RequestAsyncReaderRender(ReaderRenderRuntimeDeps &deps, int page, float target_scale,
                              int display_w, int display_h, bool prefetch);
