#include "pdf_runtime.h"

#include "pdf_reader.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 6.0f;
constexpr float kZoomStep = 0.1f;
constexpr size_t kTextureCacheSize = 3;
constexpr Uint32 kVisualRenderThrottleMs = 75;
constexpr Uint32 kIdlePrefetchDelayMs = 220;

struct ViewState {
  float zoom = 1.0f;
  int rotation = 0;
};

struct LocationState {
  int page_num = 0;
  int y_offset = 0;
};

struct PdfState {
  ViewState view;
  LocationState location;

  bool SameVisualState(const PdfState &other) const {
    return location.page_num == other.location.page_num &&
           view.rotation == other.view.rotation &&
           std::abs(view.zoom - other.view.zoom) < 0.0005f;
  }
};

struct RenderResult {
  bool ready = false;
  bool success = false;
  bool prefetch = false;
  uint64_t serial = 0;
  PdfState state;
  int texture_w = 0;
  int texture_h = 0;
  std::vector<unsigned char> rgba;
};

struct VisibleContentSource {
  SDL_Texture *texture = nullptr;
  int texture_w = 0;
  int texture_h = 0;
  PdfState state;
  bool valid = false;
};

struct CachedTextureEntry {
  SDL_Texture *texture = nullptr;
  int texture_w = 0;
  int texture_h = 0;
  PdfState state;
  bool valid = false;
  uint64_t stamp = 0;
};

struct ViewportLayout {
  SDL_Rect src{0, 0, 0, 0};
  SDL_Rect dst{0, 0, 0, 0};
  bool valid = false;
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

  PdfState target_state;
  PdfState display_state;
  PdfState ready_state;
  bool display_valid = false;
  bool ready_valid = false;

  VisibleContentSource visible_source;
  std::array<CachedTextureEntry, kTextureCacheSize> texture_cache{};
  uint64_t cache_stamp = 0;

  SDL_mutex *mutex = SDL_CreateMutex();
  SDL_cond *cond = SDL_CreateCond();
  SDL_Thread *worker = nullptr;
  bool worker_running = false;
  bool request_active = false;
  bool prefetch_active = false;
  PdfState requested_state;
  PdfState prefetched_state;
  RenderResult result;
  std::atomic<bool> cancel_requested{false};
  uint64_t target_serial = 0;
  int preferred_prefetch_dir = 1;
  bool visual_render_delay_active = false;
  Uint32 visual_render_due_ms = 0;
  PdfState delayed_state;
  Uint32 last_interaction_ticks = 0;
  SDL_Texture *reusable_texture = nullptr;
  int reusable_texture_w = 0;
  int reusable_texture_h = 0;

  ~Impl() {
    if (worker) {
      SDL_LockMutex(mutex);
      worker_running = false;
      cancel_requested.store(true);
      SDL_CondSignal(cond);
      SDL_UnlockMutex(mutex);
      SDL_WaitThread(worker, nullptr);
      worker = nullptr;
    }
    DestroyTexture();
    ClearTextureCache();
    DestroyReusableTexture();
    if (cond) SDL_DestroyCond(cond);
    if (mutex) SDL_DestroyMutex(mutex);
  }

  void DestroyTexture() {
    if (visible_source.texture) {
      SDL_DestroyTexture(visible_source.texture);
      visible_source.texture = nullptr;
    }
    visible_source.texture_w = 0;
    visible_source.texture_h = 0;
    visible_source.valid = false;
  }

  void DestroyReusableTexture() {
    if (reusable_texture) {
      SDL_DestroyTexture(reusable_texture);
      reusable_texture = nullptr;
    }
    reusable_texture_w = 0;
    reusable_texture_h = 0;
  }

  void RecycleTexture(SDL_Texture *&texture, int &texture_w, int &texture_h) {
    if (!texture) return;
    DestroyReusableTexture();
    reusable_texture = texture;
    reusable_texture_w = texture_w;
    reusable_texture_h = texture_h;
    texture = nullptr;
    texture_w = 0;
    texture_h = 0;
  }

  void DestroyCachedTexture(CachedTextureEntry &entry) {
    if (entry.texture) {
      RecycleTexture(entry.texture, entry.texture_w, entry.texture_h);
    }
    entry.valid = false;
    entry.stamp = 0;
  }

  void ClearTextureCache() {
    for (auto &entry : texture_cache) {
      DestroyCachedTexture(entry);
    }
  }

  bool ClampPage(LocationState &location) const {
    const int page_count = std::max(1, reader.PageCount());
    const int clamped = std::clamp(location.page_num, 0, page_count - 1);
    const bool changed = (clamped != location.page_num);
    location.page_num = clamped;
    return changed;
  }

  float RenderScaleForState(const PdfState &state) const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(state.location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return std::max(kMinZoom, std::min(kMaxZoom, state.view.zoom));
    }

    const int rotation = NormalizeRotation(state.view.rotation);
    float fit_scale = 1.0f;
    if (rotation == 90 || rotation == 270) {
      fit_scale = std::max(0.1f, static_cast<float>(screen_h) /
                                    static_cast<float>(std::max(1, page_w)));
    } else {
      fit_scale = std::max(0.1f, static_cast<float>(screen_w) /
                                    static_cast<float>(std::max(1, page_w)));
    }
    return std::max(kMinZoom, std::min(kMaxZoom, fit_scale * state.view.zoom));
  }

  int RenderedFlowExtent(const PdfState &state) const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(state.location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return screen_h;
    }
    return std::max(1, static_cast<int>(std::lround(RenderScaleForState(state) *
                                                    static_cast<float>(page_h))));
  }

  int ViewportFlowExtent(const PdfState &state) const {
    const int rotation = NormalizeRotation(state.view.rotation);
    return (rotation == 90 || rotation == 270) ? screen_w : screen_h;
  }

  int MaxYOffset(const PdfState &state) const {
    return std::max(0, RenderedFlowExtent(state) - ViewportFlowExtent(state));
  }

  void ClampYOffsetCurrentPage(PdfState &state) const {
    ClampPage(state.location);
    state.location.y_offset = std::clamp(state.location.y_offset, 0, MaxYOffset(state));
  }

  void NormalizeState(PdfState &state) const {
    ClampPage(state.location);
    const int page_count = std::max(1, reader.PageCount());

    while (state.location.y_offset < 0 && state.location.page_num > 0) {
      --state.location.page_num;
      state.location.y_offset = MaxYOffset(state);
    }

    while (state.location.page_num + 1 < page_count) {
      const int current_max = MaxYOffset(state);
      if (state.location.y_offset <= current_max) break;
      ++state.location.page_num;
      state.location.y_offset = 0;
    }

    state.location.y_offset = std::clamp(state.location.y_offset, 0, MaxYOffset(state));
  }

  void RecenterAfterVisualChange(PdfState &state, const ViewState &old_view, int old_y_offset) const {
    const int locked_page = state.location.page_num;
    const int old_rotation = NormalizeRotation(old_view.rotation);
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(locked_page, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      ClampYOffsetCurrentPage(state);
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

    state.location.page_num = locked_page;
    ClampPage(state.location);
    const int new_rendered_h = RenderedFlowExtent(state);
    const int new_view_extent = ViewportFlowExtent(state);
    const float new_center_y = old_anchor * static_cast<float>(new_rendered_h);
    state.location.y_offset =
        static_cast<int>(std::lround(new_center_y - static_cast<float>(new_view_extent) * 0.5f));
    ClampYOffsetCurrentPage(state);
  }

  bool RenderPixelsForState(const PdfState &state, RenderResult &out, const std::atomic<bool> *cancel) {
    std::vector<unsigned char> rgba;
    int raw_w = 0;
    int raw_h = 0;
    if (!reader.RenderPageRGBA(state.location.page_num, RenderScaleForState(state), rgba, raw_w, raw_h, cancel)) {
      return false;
    }
    int rotated_w = 0;
    int rotated_h = 0;
    std::vector<unsigned char> rotated = RotateRgba(rgba, raw_w, raw_h, state.view.rotation, rotated_w, rotated_h);
    out.ready = true;
    out.success = true;
    out.state = state;
    out.texture_w = rotated_w;
    out.texture_h = rotated_h;
    out.rgba = std::move(rotated);
    return true;
  }

  SDL_Texture *CreateTextureFromResult(const RenderResult &ready) {
    SDL_Texture *next = nullptr;
    if (reusable_texture && reusable_texture_w == ready.texture_w &&
        reusable_texture_h == ready.texture_h) {
      next = reusable_texture;
      reusable_texture = nullptr;
      reusable_texture_w = 0;
      reusable_texture_h = 0;
    } else {
      next = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                               ready.texture_w, ready.texture_h);
    }
    if (!next) return nullptr;
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
    if (SDL_UpdateTexture(next, nullptr, ready.rgba.data(), ready.texture_w * 4) != 0) {
      SDL_DestroyTexture(next);
      return nullptr;
    }
    return next;
  }

  int FindCacheEntryForVisual(const PdfState &state) const {
    for (size_t i = 0; i < texture_cache.size(); ++i) {
      if (texture_cache[i].valid && texture_cache[i].state.SameVisualState(state)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  bool HasVisualTextureForState(const PdfState &state) const {
    if (visible_source.valid && visible_source.state.SameVisualState(state)) return true;
    return FindCacheEntryForVisual(state) >= 0;
  }

  bool ShouldCacheState(const PdfState &state) const {
    return NormalizeRotation(state.view.rotation) == NormalizeRotation(target_state.view.rotation) &&
           std::abs(state.view.zoom - target_state.view.zoom) < 0.0005f;
  }

  bool WantsIdlePrefetch(Uint32 now) const {
    if (!reader.IsOpen()) return false;
    if (!display_valid || !visible_source.valid) return false;
    if (!display_state.SameVisualState(target_state)) return false;
    if (request_active || prefetch_active || visual_render_delay_active || result.ready) return false;
    if (!SDL_TICKS_PASSED(now, last_interaction_ticks + kIdlePrefetchDelayMs)) return false;

    PdfState candidate = target_state;
    candidate.location.page_num += (preferred_prefetch_dir >= 0) ? 1 : -1;
    candidate.location.y_offset = 0;
    if (candidate.location.page_num < 0 || candidate.location.page_num >= reader.PageCount()) return false;
    return !HasVisualTextureForState(candidate);
  }

  int SelectCacheVictim() const {
    int victim = 0;
    uint64_t best_stamp = UINT64_MAX;
    for (size_t i = 0; i < texture_cache.size(); ++i) {
      if (!texture_cache[i].valid) return static_cast<int>(i);
      if (texture_cache[i].stamp < best_stamp) {
        best_stamp = texture_cache[i].stamp;
        victim = static_cast<int>(i);
      }
    }
    return victim;
  }

  void StoreTextureInCache(SDL_Texture *texture, int texture_w, int texture_h, const PdfState &state) {
    if (!texture) return;
    const int existing = FindCacheEntryForVisual(state);
    const int slot_index = (existing >= 0) ? existing : SelectCacheVictim();
    CachedTextureEntry &slot = texture_cache[slot_index];
    DestroyCachedTexture(slot);
    slot.texture = texture;
    slot.texture_w = texture_w;
    slot.texture_h = texture_h;
    slot.state = state;
    slot.valid = true;
    slot.stamp = ++cache_stamp;
  }

  void MaybeMoveVisibleToCache() {
    if (!visible_source.valid || !visible_source.texture) return;
    if (!ShouldCacheState(visible_source.state)) {
      RecycleTexture(visible_source.texture, visible_source.texture_w, visible_source.texture_h);
      visible_source.valid = false;
      return;
    }
    StoreTextureInCache(visible_source.texture, visible_source.texture_w, visible_source.texture_h,
                        visible_source.state);
    visible_source.texture = nullptr;
    visible_source.texture_w = 0;
    visible_source.texture_h = 0;
    visible_source.valid = false;
  }

  bool InstallTexture(const RenderResult &ready) {
    SDL_Texture *next = CreateTextureFromResult(ready);
    if (!next) return false;

    MaybeMoveVisibleToCache();
    visible_source.texture = next;
    visible_source.texture_w = ready.texture_w;
    visible_source.texture_h = ready.texture_h;
    visible_source.state = ready.state;
    visible_source.valid = true;
    display_state = ready.state;
    display_valid = true;
    return true;
  }

  bool ActivateCachedTexture(const PdfState &state) {
    const int cache_index = FindCacheEntryForVisual(state);
    if (cache_index < 0) return false;
    CachedTextureEntry &entry = texture_cache[cache_index];
    if (!entry.valid || !entry.texture) return false;

    MaybeMoveVisibleToCache();
    visible_source.texture = entry.texture;
    visible_source.texture_w = entry.texture_w;
    visible_source.texture_h = entry.texture_h;
    visible_source.state = state;
    visible_source.valid = true;
    display_state = state;
    display_valid = true;

    entry.texture = nullptr;
    entry.texture_w = 0;
    entry.texture_h = 0;
    entry.valid = false;
    entry.stamp = 0;
    return true;
  }

  bool TryUseCachedTarget() {
    if (!ActivateCachedTexture(target_state)) return false;
    SDL_LockMutex(mutex);
    SchedulePrefetchLocked();
    SDL_UnlockMutex(mutex);
    return true;
  }

  void MarkInteraction() { last_interaction_ticks = SDL_GetTicks(); }

  void MarkTargetChangedLocked() {
    ++target_serial;
    request_active = false;
    prefetch_active = false;
    visual_render_delay_active = false;
    cancel_requested.store(true);
  }

  void RequestRenderLocked() {
    if (request_active && requested_state.SameVisualState(target_state)) return;
    if (visual_render_delay_active && delayed_state.SameVisualState(target_state)) return;
    MarkTargetChangedLocked();
    requested_state = target_state;
    request_active = true;
    SDL_CondSignal(cond);
  }

  void DelayVisualRenderLocked() {
    if (request_active && requested_state.SameVisualState(target_state)) return;
    if (visual_render_delay_active && delayed_state.SameVisualState(target_state)) {
      visual_render_due_ms = SDL_GetTicks() + kVisualRenderThrottleMs;
      return;
    }
    MarkTargetChangedLocked();
    delayed_state = target_state;
    visual_render_delay_active = true;
    visual_render_due_ms = SDL_GetTicks() + kVisualRenderThrottleMs;
  }

  void FlushDelayedRenderLocked(Uint32 now) {
    if (!visual_render_delay_active) return;
    if (SDL_TICKS_PASSED(now, visual_render_due_ms)) {
      requested_state = delayed_state;
      request_active = true;
      visual_render_delay_active = false;
      SDL_CondSignal(cond);
    }
  }

  void SchedulePrefetchLocked() {
    const Uint32 now = SDL_GetTicks();
    if (!WantsIdlePrefetch(now)) {
      prefetch_active = false;
      return;
    }

    PdfState candidate = target_state;
    candidate.location.page_num += (preferred_prefetch_dir >= 0) ? 1 : -1;
    candidate.location.y_offset = 0;
    if (candidate.location.page_num < 0 || candidate.location.page_num >= reader.PageCount()) {
      prefetch_active = false;
      return;
    }
    ClampPage(candidate.location);
    if (HasVisualTextureForState(candidate)) {
      prefetch_active = false;
      return;
    }
    prefetched_state = candidate;
    prefetch_active = true;
    SDL_CondSignal(cond);
  }

  static int WorkerMain(void *userdata) {
    auto *impl = static_cast<Impl *>(userdata);
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);

    PdfReader worker_reader;
    if (!worker_reader.Open(impl->path)) {
      return 0;
    }

    while (true) {
      SDL_LockMutex(impl->mutex);
      while (impl->worker_running &&
             ((!impl->request_active && !impl->prefetch_active) || impl->result.ready)) {
        SDL_CondWait(impl->cond, impl->mutex);
      }
      if (!impl->worker_running) {
        SDL_UnlockMutex(impl->mutex);
        break;
      }

      const bool prefetch = !impl->request_active && impl->prefetch_active;
      const PdfState task_state = prefetch ? impl->prefetched_state : impl->requested_state;
      const uint64_t task_serial = impl->target_serial;
      if (prefetch) {
        impl->prefetch_active = false;
      } else {
        impl->request_active = false;
      }
      impl->cancel_requested.store(false);
      SDL_UnlockMutex(impl->mutex);

      RenderResult ready;
      ready.serial = task_serial;
      ready.state = task_state;
      ready.prefetch = prefetch;

      if (task_serial == 0) continue;
      if (impl->cancel_requested.load()) continue;

      int page_w = 0;
      int page_h = 0;
      if (!worker_reader.PageSize(task_state.location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
        page_w = 720;
        page_h = 480;
      }
      const int rotation = NormalizeRotation(task_state.view.rotation);
      float fit_scale = 1.0f;
      if (rotation == 90 || rotation == 270) {
        fit_scale = std::max(0.1f, static_cast<float>(impl->screen_h) /
                                      static_cast<float>(std::max(1, page_w)));
      } else {
        fit_scale = std::max(0.1f, static_cast<float>(impl->screen_w) /
                                      static_cast<float>(std::max(1, page_w)));
      }
      const float render_scale =
          std::max(kMinZoom, std::min(kMaxZoom, fit_scale * task_state.view.zoom));

      std::vector<unsigned char> rgba;
      int raw_w = 0;
      int raw_h = 0;
      if (!worker_reader.RenderPageRGBA(task_state.location.page_num,
                                        render_scale,
                                        rgba,
                                        raw_w,
                                        raw_h,
                                        &impl->cancel_requested)) {
        SDL_LockMutex(impl->mutex);
        if (!prefetch && impl->target_serial == task_serial) {
          ready.ready = true;
          ready.success = false;
          impl->result = std::move(ready);
        }
        SDL_UnlockMutex(impl->mutex);
        continue;
      }

      int rotated_w = 0;
      int rotated_h = 0;
      std::vector<unsigned char> rotated =
          RotateRgba(rgba, raw_w, raw_h, task_state.view.rotation, rotated_w, rotated_h);

      SDL_LockMutex(impl->mutex);
      const bool obsolete = !impl->worker_running || impl->cancel_requested.load() ||
                            task_serial != impl->target_serial;
      if (!obsolete) {
        ready.ready = true;
        ready.success = true;
        ready.texture_w = rotated_w;
        ready.texture_h = rotated_h;
        ready.rgba = std::move(rotated);
        impl->result = std::move(ready);
      }
      SDL_UnlockMutex(impl->mutex);
    }

    worker_reader.Close();
    return 0;
  }

  ViewportLayout ComputeViewportLayout(const PdfState &state, int content_w, int content_h) const {
    const int rotation = NormalizeRotation(state.view.rotation);
    ViewportLayout layout;
    if (content_w <= 0 || content_h <= 0) return layout;

    SDL_Rect src{0, 0, content_w, content_h};
    SDL_Rect dst{0, 0, screen_w, screen_h};

    if (rotation == 90 || rotation == 270) {
      if (content_h > screen_h) {
        src.h = screen_h;
        src.y = (content_h - screen_h) / 2;
      } else {
        dst.y = (screen_h - content_h) / 2;
        dst.h = content_h;
      }

      if (content_w > screen_w) {
        src.w = screen_w;
        const int max_x = std::max(0, content_w - screen_w);
        if (rotation == 90) {
          src.x = std::clamp(max_x - state.location.y_offset, 0, max_x);
        } else {
          src.x = std::clamp(state.location.y_offset, 0, max_x);
        }
      } else {
        dst.x = (screen_w - content_w) / 2;
        dst.w = content_w;
      }
    } else {
      if (content_w > screen_w) {
        src.x = (content_w - screen_w) / 2;
        src.w = screen_w;
      } else {
        dst.x = (screen_w - content_w) / 2;
        dst.w = content_w;
      }

      if (content_h > screen_h) {
        src.h = screen_h;
        const int max_y = std::max(0, content_h - screen_h);
        if (rotation == 180) {
          src.y = std::clamp(max_y - state.location.y_offset, 0, max_y);
        } else {
          src.y = std::clamp(state.location.y_offset, 0, max_y);
        }
      } else {
        dst.y = (screen_h - content_h) / 2;
        dst.h = content_h;
      }
    }

    layout.src = src;
    layout.dst = dst;
    layout.valid = true;
    return layout;
  }

  void DrawVisibleSource(SDL_Renderer *renderer, const VisibleContentSource &source, const PdfState &state) const {
    if (!renderer || !source.texture || !source.valid) return;
    const ViewportLayout layout = ComputeViewportLayout(state, source.texture_w, source.texture_h);
    if (!layout.valid) return;
    SDL_RenderCopy(renderer, source.texture, &layout.src, &layout.dst);
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

  impl_->target_state.view.zoom = std::max(kMinZoom, std::min(kMaxZoom, initial_progress.zoom));
  impl_->target_state.view.rotation = NormalizeRotation(initial_progress.rotation);
  impl_->target_state.location.page_num = std::max(0, initial_progress.page);
  impl_->target_state.location.y_offset = std::max(0, initial_progress.scroll_y);
  impl_->NormalizeState(impl_->target_state);

  RenderResult seed;
  if (!impl_->RenderPixelsForState(impl_->target_state, seed, nullptr)) {
    impl_->reader.Close();
    impl_->path.clear();
    return false;
  }
  if (!impl_->InstallTexture(seed)) {
    impl_->reader.Close();
    impl_->path.clear();
    return false;
  }

  impl_->display_state = impl_->target_state;
  impl_->ready_state = impl_->target_state;
  impl_->display_valid = true;
  impl_->ready_valid = true;
  impl_->result = RenderResult{};
  impl_->request_active = false;
  impl_->prefetch_active = false;
  impl_->cancel_requested.store(false);
  impl_->target_serial = 0;
  impl_->last_interaction_ticks = SDL_GetTicks();
  impl_->worker_running = true;
  impl_->worker = SDL_CreateThread(Impl::WorkerMain, "pdf_runtime_worker", impl_);
  if (!impl_->worker) {
    impl_->worker_running = false;
  } else {
    SDL_LockMutex(impl_->mutex);
    impl_->SchedulePrefetchLocked();
    SDL_UnlockMutex(impl_->mutex);
  }
  return true;
}

void PdfRuntime::Close() {
  if (!impl_) return;
  if (impl_->worker) {
    SDL_LockMutex(impl_->mutex);
    impl_->worker_running = false;
    impl_->cancel_requested.store(true);
    SDL_CondSignal(impl_->cond);
    SDL_UnlockMutex(impl_->mutex);
    SDL_WaitThread(impl_->worker, nullptr);
    impl_->worker = nullptr;
  }
  impl_->DestroyTexture();
  impl_->reader.Close();
  impl_->path.clear();
  impl_->target_state = PdfState{};
  impl_->display_state = PdfState{};
  impl_->ready_state = PdfState{};
  impl_->display_valid = false;
  impl_->ready_valid = false;
  impl_->request_active = false;
  impl_->prefetch_active = false;
  impl_->visual_render_delay_active = false;
  impl_->result = RenderResult{};
  impl_->cancel_requested.store(false);
  impl_->target_serial = 0;
  impl_->ClearTextureCache();
  impl_->DestroyReusableTexture();
}

bool PdfRuntime::IsOpen() const { return impl_ && impl_->reader.IsOpen(); }

bool PdfRuntime::HasRealRenderer() const { return impl_ && impl_->reader.HasRealRenderer(); }

bool PdfRuntime::IsRenderPending() const {
  if (!impl_ || !impl_->reader.IsOpen()) return false;
  if (!impl_->display_valid || !impl_->visible_source.valid) return true;
  if (!impl_->display_state.SameVisualState(impl_->target_state)) return true;
  SDL_LockMutex(impl_->mutex);
  const bool pending = impl_->request_active || impl_->prefetch_active || impl_->visual_render_delay_active ||
                       impl_->result.ready || impl_->WantsIdlePrefetch(SDL_GetTicks());
  SDL_UnlockMutex(impl_->mutex);
  return pending;
}

void PdfRuntime::UpdateViewport(int screen_w, int screen_h) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  screen_w = std::max(1, screen_w);
  screen_h = std::max(1, screen_h);
  if (screen_w == impl_->screen_w && screen_h == impl_->screen_h) return;
  impl_->screen_w = screen_w;
  impl_->screen_h = screen_h;
  impl_->MarkInteraction();
  impl_->NormalizeState(impl_->target_state);
  if (impl_->display_valid) {
    impl_->NormalizeState(impl_->display_state);
  }
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->RequestRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::Tick() {
  if (!impl_ || !impl_->reader.IsOpen()) return;

  SDL_LockMutex(impl_->mutex);
  impl_->FlushDelayedRenderLocked(SDL_GetTicks());
  if (!impl_->request_active && !impl_->result.ready) {
    impl_->SchedulePrefetchLocked();
  }
  SDL_UnlockMutex(impl_->mutex);

  RenderResult ready;
  bool have_ready = false;
  SDL_LockMutex(impl_->mutex);
  if (impl_->result.ready) {
    ready = std::move(impl_->result);
    impl_->result = RenderResult{};
    have_ready = true;
    SDL_CondSignal(impl_->cond);
  }
  SDL_UnlockMutex(impl_->mutex);

  if (!have_ready || !ready.success) return;
  if (ready.serial != impl_->target_serial) return;

  if (ready.prefetch) {
    SDL_Texture *texture = impl_->CreateTextureFromResult(ready);
    if (texture) {
      impl_->StoreTextureInCache(texture, ready.texture_w, ready.texture_h, ready.state);
    }
    SDL_LockMutex(impl_->mutex);
    impl_->SchedulePrefetchLocked();
    SDL_UnlockMutex(impl_->mutex);
    return;
  }

  impl_->ready_state = ready.state;
  impl_->ready_valid = true;
  if (impl_->InstallTexture(ready)) {
    impl_->display_state = impl_->ready_state;
    impl_->display_valid = true;
  }
  SDL_LockMutex(impl_->mutex);
  impl_->SchedulePrefetchLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::Draw(SDL_Renderer *renderer) const {
  if (!impl_ || !renderer || !impl_->visible_source.texture || !impl_->display_valid || !impl_->visible_source.valid) return;

  const bool exact = impl_->display_state.SameVisualState(impl_->target_state);
  if (exact) {
    PdfState draw_state = impl_->display_state;
    draw_state.location.y_offset = impl_->target_state.location.y_offset;
    impl_->DrawVisibleSource(renderer, impl_->visible_source, draw_state);
    return;
  }

  impl_->DrawVisibleSource(renderer, impl_->visible_source, impl_->display_state);
}

void PdfRuntime::RotateLeft() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.rotation = NormalizeRotation(impl_->target_state.view.rotation + 270);
  if (impl_->target_state.view.rotation == old_view.rotation) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::RotateRight() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.rotation = NormalizeRotation(impl_->target_state.view.rotation + 90);
  if (impl_->target_state.view.rotation == old_view.rotation) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::ZoomOut() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = std::max(kMinZoom, impl_->target_state.view.zoom - kZoomStep);
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::ZoomIn() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = std::min(kMaxZoom, impl_->target_state.view.zoom + kZoomStep);
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::ResetView() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = 1.0f;
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void PdfRuntime::ScrollByPixels(int delta_px) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const int old_page = impl_->target_state.location.page_num;
  if (delta_px > 0) impl_->preferred_prefetch_dir = 1;
  if (delta_px < 0) impl_->preferred_prefetch_dir = -1;
  impl_->target_state.location.y_offset += delta_px;
  impl_->NormalizeState(impl_->target_state);
  if (impl_->target_state.location.page_num != old_page) {
    if (impl_->TryUseCachedTarget()) return;
    SDL_LockMutex(impl_->mutex);
    impl_->RequestRenderLocked();
    SDL_UnlockMutex(impl_->mutex);
  }
}

void PdfRuntime::JumpByScreen(int direction) {
  if (!impl_ || !impl_->reader.IsOpen() || direction == 0) return;
  impl_->MarkInteraction();
  const int old_page = impl_->target_state.location.page_num;
  impl_->preferred_prefetch_dir = (direction > 0) ? 1 : -1;
  impl_->target_state.location.y_offset += direction * impl_->screen_h;
  impl_->NormalizeState(impl_->target_state);
  if (impl_->target_state.location.page_num != old_page) {
    if (impl_->TryUseCachedTarget()) return;
    SDL_LockMutex(impl_->mutex);
    impl_->RequestRenderLocked();
    SDL_UnlockMutex(impl_->mutex);
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
  return impl_->target_state.location.page_num;
}

PdfRuntimeProgress PdfRuntime::Progress() const {
  PdfRuntimeProgress out;
  if (!impl_) return out;
  out.page = impl_->target_state.location.page_num;
  out.rotation = impl_->target_state.view.rotation;
  out.zoom = impl_->target_state.view.zoom;
  out.scroll_y = impl_->target_state.location.y_offset;
  return out;
}
