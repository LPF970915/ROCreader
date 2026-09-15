#pragma once

#include "reader_core.h"
#include "reader_module.h"
#include "rgds_runtime.h"

#include <SDL.h>

namespace rgds {

enum class ReaderLayoutMode {
  VerticalStack,
  HorizontalSpread,
};

struct ReaderLayout {
  ReaderLayoutMode mode = ReaderLayoutMode::VerticalStack;
  int canvas_w = kVirtualReaderW;
  int canvas_h = kVirtualReaderH;
  SDL_Rect top_src{0, 0, kScreenW, kScreenH};
  SDL_Rect bottom_src{0, kScreenH, kScreenW, kScreenH};
  SDL_Rect overlay_viewport{0, kScreenH, kScreenW, kScreenH};
};

bool IsImageReaderMode(ReaderMode mode, const IReaderModule *module);
bool IsHorizontalSpreadRotation(int rotation);
ReaderLayout ResolveReaderLayout(ReaderMode mode, const IReaderModule *module, int rotation);

} // namespace rgds
