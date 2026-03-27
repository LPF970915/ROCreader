#pragma once

#include "input_manager.h"
#include "reader_session_state.h"

#include <SDL.h>

#include <functional>

struct TxtReaderInputDeps {
  const InputManager &input;
  ReaderUiState &ui;
  float dt = 0.0f;
  int tap_step_px = 0;
  std::function<void(int)> text_scroll_by;
  std::function<void(int)> text_page_by;
};

struct TxtReaderRenderDeps {
  SDL_Renderer *renderer = nullptr;
  ReaderUiState &ui;
  std::function<void()> clamp_text_scroll;
  std::function<void(const SDL_Rect &)> set_clip_rect;
  std::function<void()> clear_clip_rect;
  std::function<void(const std::string &, int, int)> draw_text_line;
};

void HandleTxtReaderInput(TxtReaderInputDeps &deps);
void DrawTxtReaderRuntime(TxtReaderRenderDeps &deps);
