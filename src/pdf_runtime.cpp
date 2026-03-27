#include "pdf_runtime.h"

#include "pdf_reader.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 6.0f;
constexpr float kZoomStep = 0.1f;

struct ViewState {
  float zoom = 1.0f;
  int rotation = 0;
};

struct LocationState {
  int page_num = 0;
  int y_offset = 0;
};

int NormalizeRotation(int rotation) {
  rotation %= 360;
  if (rotation < 0) rotation += 360;
  return rotation;
}

std::vector<unsigned char> RotateRgba(const std::vector<unsigned char> &src,
                                      int src_w,
                                      int src_h,
                                      int rotation,
                                      int &dst_w,
                                      int &dst_h) {
  rotation = NormalizeRotation(rotation);
  if (rotation == 0) {
    dst_w = src_w;
    dst_h = src_h;
    return src;
  }

  if (rotation == 180) {
    dst_w = src_w;
    dst_h = src_h;
    std::vector<unsigned char> dst(static_cast<size_t>(dst_w * dst_h * 4), 255);
    for (int y = 0; y < src_h; ++y) {
      for (int x = 0; x < src_w; ++x) {
        const int si = (y * src_w + x) * 4;
        const int dx = src_w - 1 - x;
        const int dy = src_h - 1 - y;
        const int di = (dy * dst_w + dx) * 4;
        std::memcpy(dst.data() + di, src.data() + si, 4);
      }
    }
    return dst;
  }

  dst_w = src_h;
  dst_h = src_w;
  std::vector<unsigned char> dst(static_cast<size_t>(dst_w * dst_h * 4), 255);
  for (int y = 0; y < src_h; ++y) {
    for (int x = 0; x < src_w; ++x) {
      const int si = (y * src_w + x) * 4;
      int dx = 0;
      int dy = 0;
      if (rotation == 90) {
        dx = src_h - 1 - y;
        dy = x;
      } else {
        dx = y;
        dy = src_w - 1 - x;
      }
      const int di = (dy * dst_w + dx) * 4;
      std::memcpy(dst.data() + di, src.data() + si, 4);
    }
  }
  return dst;
}

}  // namespace

struct PdfRuntime::Impl {
  PdfReader reader;
  SDL_Renderer *renderer = nullptr;
  std::string path;
  int screen_w = 720;
  int screen_h = 480;

  ViewState view;
  LocationState location;

  SDL_Texture *page_texture = nullptr;
  int texture_w = 0;
  int texture_h = 0;

  ~Impl() { DestroyTexture(); }

  void DestroyTexture() {
    if (page_texture) {
      SDL_DestroyTexture(page_texture);
      page_texture = nullptr;
    }
    texture_w = 0;
    texture_h = 0;
  }

  bool ClampPage() {
    const int page_count = std::max(1, reader.PageCount());
    const int clamped = std::clamp(location.page_num, 0, page_count - 1);
    const bool changed = (clamped != location.page_num);
    location.page_num = clamped;
    return changed;
  }

  float RenderScaleForCurrentPage() const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return std::max(kMinZoom, std::min(kMaxZoom, view.zoom));
    }

    const int rotation = NormalizeRotation(view.rotation);
    float fit_scale = 1.0f;
    if (rotation == 90 || rotation == 270) {
      // In portrait reading mode, keep the original page short edge
      // fitted to the physical screen height.
      fit_scale = std::max(0.1f, static_cast<float>(screen_h) /
                                    static_cast<float>(std::max(1, page_w)));
    } else {
      fit_scale = std::max(0.1f, static_cast<float>(screen_w) /
                                    static_cast<float>(std::max(1, page_w)));
    }
    return std::max(kMinZoom, std::min(kMaxZoom, fit_scale * view.zoom));
  }

  int RenderedFlowExtent() const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return screen_h;
    }
    return std::max(1, static_cast<int>(std::lround(RenderScaleForCurrentPage() *
                                                    static_cast<float>(page_h))));
  }

  int ViewportFlowExtent() const {
    const int rotation = NormalizeRotation(view.rotation);
    return (rotation == 90 || rotation == 270) ? screen_w : screen_h;
  }

  int MaxYOffset() const {
    return std::max(0, RenderedFlowExtent() - ViewportFlowExtent());
  }

  void ClampYOffsetCurrentPage() {
    ClampPage();
    location.y_offset = std::clamp(location.y_offset, 0, MaxYOffset());
  }

  void NormalizeLocation() {
    ClampPage();
    const int page_count = std::max(1, reader.PageCount());

    while (location.y_offset < 0 && location.page_num > 0) {
      --location.page_num;
      location.y_offset = MaxYOffset();
    }

    while (location.page_num + 1 < page_count) {
      const int current_max = MaxYOffset();
      if (location.y_offset <= current_max) break;
      ++location.page_num;
      location.y_offset = 0;
    }

    location.y_offset = std::clamp(location.y_offset, 0, MaxYOffset());
  }

  void RecenterAfterVisualChange(const ViewState &old_view, int old_y_offset) {
    const int locked_page = location.page_num;
    const int old_rotation = NormalizeRotation(old_view.rotation);
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      ClampYOffsetCurrentPage();
      return;
    }

    float old_fit_scale = 1.0f;
    if (old_rotation == 90 || old_rotation == 270) {
      old_fit_scale = std::max(0.1f, static_cast<float>(screen_h) /
                                        static_cast<float>(std::max(1, page_w)));
    } else {
      old_fit_scale = std::max(0.1f, static_cast<float>(screen_w) /
                                        static_cast<float>(std::max(1, page_w)));
    }
    const int old_rendered_h = std::max(1, static_cast<int>(std::lround(
                                              old_fit_scale * old_view.zoom * static_cast<float>(page_h))));
    const int old_view_extent = (old_rotation == 90 || old_rotation == 270) ? screen_w : screen_h;

    const float old_center_y = static_cast<float>(old_y_offset) + static_cast<float>(old_view_extent) * 0.5f;
    const float old_anchor =
        (old_rendered_h > 0) ? (old_center_y / static_cast<float>(old_rendered_h)) : 0.5f;

    location.page_num = locked_page;
    ClampPage();
    const int new_rendered_h = RenderedFlowExtent();
    const int new_view_extent = ViewportFlowExtent();
    const float new_center_y = old_anchor * static_cast<float>(new_rendered_h);
    location.y_offset = static_cast<int>(std::lround(new_center_y - static_cast<float>(new_view_extent) * 0.5f));
    ClampYOffsetCurrentPage();
  }

  bool RenderFullPageSync() {
    if (!renderer || !reader.IsOpen()) return false;

    std::vector<unsigned char> rgba;
    int raw_w = 0;
    int raw_h = 0;
    if (!reader.RenderPageRGBA(location.page_num, RenderScaleForCurrentPage(), rgba, raw_w, raw_h, nullptr)) {
      return false;
    }

    int rotated_w = 0;
    int rotated_h = 0;
    std::vector<unsigned char> rotated = RotateRgba(rgba, raw_w, raw_h, view.rotation, rotated_w, rotated_h);

    SDL_Texture *next =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, rotated_w, rotated_h);
    if (!next) return false;
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
    if (SDL_UpdateTexture(next, nullptr, rotated.data(), rotated_w * 4) != 0) {
      SDL_DestroyTexture(next);
      return false;
    }

    DestroyTexture();
    page_texture = next;
    texture_w = rotated_w;
    texture_h = rotated_h;
    return true;
  }
};

PdfRuntime::PdfRuntime() : impl_(new Impl()) {}

PdfRuntime::~PdfRuntime() {
  Close();
  delete impl_;
  impl_ = nullptr;
}

bool PdfRuntime::Open(SDL_Renderer *renderer,
                      const std::string &path,
                      int screen_w,
                      int screen_h,
                      const PdfRuntimeProgress &initial_progress) {
  Close();
  impl_->renderer = renderer;
  impl_->screen_w = std::max(1, screen_w);
  impl_->screen_h = std::max(1, screen_h);

  if (!impl_->reader.Open(path)) return false;
  impl_->path = path;

  impl_->view.zoom = std::max(kMinZoom, std::min(kMaxZoom, initial_progress.zoom));
  impl_->view.rotation = NormalizeRotation(initial_progress.rotation);
  impl_->location.page_num = std::max(0, initial_progress.page);
  impl_->location.y_offset = std::max(0, initial_progress.scroll_y);
  impl_->NormalizeLocation();

  if (!impl_->RenderFullPageSync()) {
    impl_->reader.Close();
    impl_->path.clear();
    return false;
  }
  return true;
}

void PdfRuntime::Close() {
  if (!impl_) return;
  impl_->DestroyTexture();
  impl_->reader.Close();
  impl_->path.clear();
  impl_->view = ViewState{};
  impl_->location = LocationState{};
}

bool PdfRuntime::IsOpen() const { return impl_ && impl_->reader.IsOpen(); }

bool PdfRuntime::HasRealRenderer() const { return impl_ && impl_->reader.HasRealRenderer(); }

void PdfRuntime::UpdateViewport(int screen_w, int screen_h) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  screen_w = std::max(1, screen_w);
  screen_h = std::max(1, screen_h);
  if (screen_w == impl_->screen_w && screen_h == impl_->screen_h) return;
  impl_->screen_w = screen_w;
  impl_->screen_h = screen_h;
  impl_->NormalizeLocation();
  impl_->RenderFullPageSync();
}

void PdfRuntime::Tick() {}

void PdfRuntime::Draw(SDL_Renderer *renderer) const {
  if (!impl_ || !renderer || !impl_->page_texture) return;

  const int rotation = NormalizeRotation(impl_->view.rotation);
  SDL_Rect src{0, 0, impl_->texture_w, impl_->texture_h};
  SDL_Rect dst{0, 0, impl_->screen_w, impl_->screen_h};

  if (rotation == 90 || rotation == 270) {
    if (impl_->texture_h > impl_->screen_h) {
      src.h = impl_->screen_h;
      src.y = (impl_->texture_h - impl_->screen_h) / 2;
    } else {
      dst.y = (impl_->screen_h - impl_->texture_h) / 2;
      dst.h = impl_->texture_h;
    }

    if (impl_->texture_w > impl_->screen_w) {
      src.w = impl_->screen_w;
      const int max_x = std::max(0, impl_->texture_w - impl_->screen_w);
      if (rotation == 90) {
        src.x = std::clamp(max_x - impl_->location.y_offset, 0, max_x);
      } else {
        src.x = std::clamp(impl_->location.y_offset, 0, max_x);
      }
    } else {
      dst.x = (impl_->screen_w - impl_->texture_w) / 2;
      dst.w = impl_->texture_w;
    }
  } else {
    if (impl_->texture_w > impl_->screen_w) {
      src.x = (impl_->texture_w - impl_->screen_w) / 2;
      src.w = impl_->screen_w;
    } else {
      dst.x = (impl_->screen_w - impl_->texture_w) / 2;
      dst.w = impl_->texture_w;
    }

    if (impl_->texture_h > impl_->screen_h) {
      src.h = impl_->screen_h;
      const int max_y = std::max(0, impl_->texture_h - impl_->screen_h);
      if (rotation == 180) {
        src.y = std::clamp(max_y - impl_->location.y_offset, 0, max_y);
      } else {
        src.y = std::clamp(impl_->location.y_offset, 0, max_y);
      }
    } else {
      dst.y = (impl_->screen_h - impl_->texture_h) / 2;
      dst.h = impl_->texture_h;
    }
  }

  SDL_RenderCopy(renderer, impl_->page_texture, &src, &dst);
}

void PdfRuntime::RotateLeft() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const ViewState old_view = impl_->view;
  const int old_y_offset = impl_->location.y_offset;
  impl_->view.rotation = NormalizeRotation(impl_->view.rotation + 270);
  impl_->RecenterAfterVisualChange(old_view, old_y_offset);
  impl_->RenderFullPageSync();
}

void PdfRuntime::RotateRight() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const ViewState old_view = impl_->view;
  const int old_y_offset = impl_->location.y_offset;
  impl_->view.rotation = NormalizeRotation(impl_->view.rotation + 90);
  impl_->RecenterAfterVisualChange(old_view, old_y_offset);
  impl_->RenderFullPageSync();
}

void PdfRuntime::ZoomOut() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const ViewState old_view = impl_->view;
  const int old_y_offset = impl_->location.y_offset;
  impl_->view.zoom = std::max(kMinZoom, impl_->view.zoom - kZoomStep);
  impl_->RecenterAfterVisualChange(old_view, old_y_offset);
  impl_->RenderFullPageSync();
}

void PdfRuntime::ZoomIn() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const ViewState old_view = impl_->view;
  const int old_y_offset = impl_->location.y_offset;
  impl_->view.zoom = std::min(kMaxZoom, impl_->view.zoom + kZoomStep);
  impl_->RecenterAfterVisualChange(old_view, old_y_offset);
  impl_->RenderFullPageSync();
}

void PdfRuntime::ResetView() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const ViewState old_view = impl_->view;
  const int old_y_offset = impl_->location.y_offset;
  impl_->view.zoom = 1.0f;
  impl_->RecenterAfterVisualChange(old_view, old_y_offset);
  impl_->RenderFullPageSync();
}

void PdfRuntime::ScrollByPixels(int delta_px) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  const int old_page = impl_->location.page_num;
  impl_->location.y_offset += delta_px;
  impl_->NormalizeLocation();
  if (impl_->location.page_num != old_page) {
    impl_->RenderFullPageSync();
  }
}

void PdfRuntime::JumpByScreen(int direction) {
  if (!impl_ || !impl_->reader.IsOpen() || direction == 0) return;
  const int old_page = impl_->location.page_num;
  impl_->location.y_offset += direction * impl_->screen_h;
  impl_->NormalizeLocation();
  if (impl_->location.page_num != old_page) {
    impl_->RenderFullPageSync();
  }
}

int PdfRuntime::PageCount() const {
  if (!impl_) return 0;
  return impl_->reader.PageCount();
}

bool PdfRuntime::PageSize(int page_index, int &w, int &h) const {
  if (!impl_) return false;
  return impl_->reader.PageSize(page_index, w, h);
}

int PdfRuntime::CurrentPage() const {
  if (!impl_) return 0;
  return impl_->location.page_num;
}

PdfRuntimeProgress PdfRuntime::Progress() const {
  PdfRuntimeProgress out;
  if (!impl_) return out;
  out.page = impl_->location.page_num;
  out.rotation = impl_->view.rotation;
  out.zoom = impl_->view.zoom;
  out.scroll_y = impl_->location.y_offset;
  return out;
}
