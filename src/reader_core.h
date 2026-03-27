#pragma once

#include "input_manager.h"

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct ReaderProgress {
  int page = 0;
  int rotation = 0;
  float zoom = 1.0f;
  int scroll_x = 0;
  int scroll_y = 0;
};

enum class ReaderMode {
  None = 0,
  Pdf = 1,
  Txt = 2,
  Epub = 3,
};

enum class ReaderRenderQuality {
  Low = 0,
  Full = 1,
};

struct ReaderRenderCache {
  int page = -1;
  int rotation = 0;
  float scale = 1.0f;
  ReaderRenderQuality quality = ReaderRenderQuality::Full;
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  int display_w = 0;
  int display_h = 0;
  uint32_t last_use = 0;
};

struct ReaderViewState {
  int page = 0;
  float zoom = 1.0f;
  int rotation = 0;

  bool operator==(const ReaderViewState &other) const {
    return page == other.page &&
           rotation == other.rotation &&
           std::abs(zoom - other.zoom) < 0.0005f;
  }

  bool operator!=(const ReaderViewState &other) const { return !(*this == other); }
};

struct ReaderAdaptiveRenderState {
  uint32_t last_page_flip_tick = 0;
  bool pending_page_active = false;
  int pending_page = -1;
  bool pending_page_top = true;
  uint32_t pending_page_commit_tick = 0;
  bool fast_flip_mode = false;
  int last_scroll_dir = 1;
};

struct ReaderAsyncRenderJob {
  bool active = false;
  bool prefetch = false;
  ReaderMode mode = ReaderMode::None;
  std::string path;
  ReaderViewState state;
  int page = 0;
  float target_scale = 1.0f;
  int rotation = 0;
  int display_w = 0;
  int display_h = 0;
  uint64_t serial = 0;
};

struct ReaderAsyncRenderResult {
  bool ready = false;
  bool success = false;
  ReaderMode mode = ReaderMode::None;
  std::string path;
  ReaderViewState state;
  int page = 0;
  float target_scale = 1.0f;
  int rotation = 0;
  int display_w = 0;
  int display_h = 0;
  int src_w = 0;
  int src_h = 0;
  std::vector<unsigned char> rgba;
  uint64_t serial = 0;
};

struct ReaderTexturePoolEntry {
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  bool in_use = false;
  uint32_t last_use = 0;
};

int ReaderScrollDirForButton(int rotation, Button button);
int ReaderTapPageActionForButton(int rotation, Button button);
int PdfScrollDirForButton(int rotation, Button button);
int PdfTapPageActionForButton(int rotation, Button button);
