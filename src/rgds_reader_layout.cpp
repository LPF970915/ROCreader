#include "rgds_reader_layout.h"

namespace rgds {
namespace {

int NormalizeRotation(int rotation) {
  int out = rotation % 360;
  if (out < 0) out += 360;
  return out;
}

} // namespace

bool IsImageReaderMode(ReaderMode mode, const IReaderModule *module) {
  if (mode == ReaderMode::Pdf || mode == ReaderMode::ZipImage) return true;
  if (mode != ReaderMode::Epub || !module) return false;
  return module->Capabilities().is_image_sequence;
}

bool IsHorizontalSpreadRotation(int rotation) {
  const int normalized = NormalizeRotation(rotation);
  return normalized == 90 || normalized == 270;
}

ReaderLayout ResolveReaderLayout(ReaderMode mode, const IReaderModule *module, int rotation) {
  ReaderLayout layout;
  const int screen_w = ScreenW();
  const int screen_h = ScreenH();
  layout.canvas_w = VirtualReaderW();
  layout.canvas_h = VirtualReaderH();
  layout.top_src = SDL_Rect{0, 0, screen_w, screen_h};
  layout.bottom_src = SDL_Rect{0, screen_h, screen_w, screen_h};
  layout.overlay_viewport = layout.bottom_src;
  if (IsImageReaderMode(mode, module) && IsHorizontalSpreadRotation(rotation)) {
    layout.mode = ReaderLayoutMode::HorizontalSpread;
    layout.canvas_w = screen_w * 2;
    layout.canvas_h = screen_h;
    layout.top_src = SDL_Rect{0, 0, screen_w, screen_h};
    layout.bottom_src = SDL_Rect{screen_w, 0, screen_w, screen_h};
    layout.overlay_viewport = layout.bottom_src;
  }
  return layout;
}

} // namespace rgds
