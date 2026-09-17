#pragma once

#include <SDL.h>
#include <algorithm>

namespace image_flow {
constexpr float kMinRenderScale = 0.05f;

// Offset is relative to this page, and is negative for pages ahead of the viewport.
inline bool ClipSlice(SDL_Rect &src, SDL_Rect &dst, int page_extent, int viewport_extent,
                      int offset, bool horizontal, bool positive) {
  const int source_start = std::max(0, offset);
  const int destination_start = std::max(0, -offset);
  const int visible = std::min(page_extent - source_start, viewport_extent - destination_start);
  if (visible <= 0) return false;
  const int source = positive ? source_start : page_extent - source_start - visible;
  const int destination = positive ? destination_start : viewport_extent - destination_start - visible;
  if (horizontal) {
    src.x = source;
    src.w = visible;
    dst.x = destination;
    dst.w = visible;
  } else {
    src.y = source;
    src.h = visible;
    dst.y = destination;
    dst.h = visible;
  }
  return src.w > 0 && src.h > 0;
}
}  // namespace image_flow
