#pragma once

#include "reader_core.h"
#include "reader_session_state.h"

#include <SDL.h>

#include <functional>
#include <string>

struct EpubReaderInputDeps {
  const InputManager &input;
  ReaderUiState &ui;
  ReaderViewState &target_state;
  float dt = 0.0f;
  int tap_step_px = 0;
  std::function<void()> flush_pending_page_flip_now;
  std::function<void(const ReaderViewState &, bool, bool)> commit_target_view;
  std::function<void(int, int)> scroll_by_dir;
  std::function<void(int)> queue_page_flip;
  std::function<void()> clamp_scroll;
};

struct EpubRuntimeRenderDeps {
  SDL_Renderer *renderer = nullptr;
  int screen_w = 0;
  int screen_h = 0;
  ReaderUiState &ui;
  ReaderAdaptiveRenderState &adaptive_render;
  const ReaderRenderCache &render_cache;
  const ReaderViewState &display_state;
  const ReaderViewState &target_state;
  bool display_state_valid = false;
  std::function<void()> ensure_render;
  std::function<float(const ReaderViewState &)> reader_target_scale_for_state;
  std::function<const ReaderRenderCache *(int, int, float)> visible_reader_render_cache_for_page;
  std::function<int()> reader_page_count;
  std::function<void(const std::string &)> draw_loading_label;
  std::function<void(const std::string &)> draw_fast_flip_label;
};

void HandleEpubReaderInput(EpubReaderInputDeps &deps);
void DrawEpubRuntime(EpubRuntimeRenderDeps &deps);
