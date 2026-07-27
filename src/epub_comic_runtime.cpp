#include "epub_comic_runtime.h"

#include "async_image_render_queue.h"
#include "epub_comic_reader.h"
#include "image_runtime_tuning.h"
#include "runtime_log.h"

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
constexpr size_t kTextureCacheSize = 12;
constexpr size_t kMaxPrefetchLanes = 4;
constexpr Uint32 kVisualRenderThrottleMs = 75;
constexpr Uint32 kIdlePrefetchDelayMs = 220;

void ApplyImageTextureFiltering(SDL_Texture *texture) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
  if (texture) SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#else
  (void)texture;
#endif
}

struct ViewState {
  float zoom = 1.0f;
  int rotation = 0;
  int viewport_w = 0;
  int viewport_h = 0;
};

struct LocationState {
  int page_num = 0;
  int x_offset = 0;
  int y_offset = 0;
};

struct EpubState {
  ViewState view;
  LocationState location;

  bool SameVisualState(const EpubState &other) const {
    return location.page_num == other.location.page_num &&
           view.rotation == other.view.rotation &&
           std::abs(view.zoom - other.view.zoom) < 0.0005f &&
           view.viewport_w == other.view.viewport_w &&
           view.viewport_h == other.view.viewport_h;
  }
};

AsyncImageRenderJob MakeAsyncJobForState(const std::string &path,
                                         const EpubState &state,
                                         float render_scale,
                                         bool prefetch) {
  AsyncImageRenderJob job;
  job.active = true;
  job.prefetch = prefetch;
  job.source_key = path;
  job.page = state.location.page_num;
  job.scale = render_scale;
  job.rotation = state.view.rotation;
  job.zoom = state.view.zoom;
  job.scroll_x = state.location.x_offset;
  job.scroll_y = state.location.y_offset;
  job.viewport_w = state.view.viewport_w;
  job.viewport_h = state.view.viewport_h;
  return job;
}

EpubState StateFromAsyncJob(const AsyncImageRenderJob &job) {
  EpubState state;
  state.location.page_num = job.page;
  state.location.x_offset = job.scroll_x;
  state.location.y_offset = job.scroll_y;
  state.view.rotation = job.rotation;
  state.view.zoom = job.zoom;
  state.view.viewport_w = job.viewport_w;
  state.view.viewport_h = job.viewport_h;
  return state;
}

struct RenderResult {
  bool ready = false;
  bool success = false;
  bool prefetch = false;
  uint64_t serial = 0;
  EpubState state;
  int texture_w = 0;
  int texture_h = 0;
  std::vector<unsigned char> rgba;
};

struct VisibleContentSource {
  SDL_Texture *texture = nullptr;
  int texture_w = 0;
  int texture_h = 0;
  EpubState state;
  bool valid = false;
};

struct CachedTextureEntry {
  SDL_Texture *texture = nullptr;
  int texture_w = 0;
  int texture_h = 0;
  EpubState state;
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

struct EpubComicRuntime::Impl {
  EpubComicReader reader;
  SDL_Renderer *renderer = nullptr;
  std::string path;
  int screen_w = 720;
  int screen_h = 480;
  int max_texture_w = 0;
  int max_texture_h = 0;

  EpubState target_state;
  EpubState display_state;
  EpubState ready_state;
  bool display_valid = false;
  bool ready_valid = false;

  VisibleContentSource visible_source;
  std::array<CachedTextureEntry, kTextureCacheSize> texture_cache{};
  uint64_t cache_stamp = 0;

  SDL_mutex *mutex = SDL_CreateMutex();
  bool request_active = false;
  bool prefetch_active = false;
  EpubState requested_state;
  EpubState prefetched_state;
  AsyncImageRenderQueue render_queue;
  std::array<AsyncImageRenderQueue, kMaxPrefetchLanes> prefetch_queues{};
  int active_prefetch_lanes = 0;
  bool dedicated_prefetch_queue = false;
  int preferred_prefetch_dir = 1;
  bool visual_render_delay_active = false;
  Uint32 visual_render_due_ms = 0;
  EpubState delayed_state;
  Uint32 last_interaction_ticks = 0;
  SDL_Texture *reusable_texture = nullptr;
  int reusable_texture_w = 0;
  int reusable_texture_h = 0;

  ~Impl() {
    render_queue.Shutdown();
    ShutdownPrefetchQueues();
    DestroyTexture();
    ClearTextureCache();
    DestroyReusableTexture();
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

  size_t ActiveTextureCacheSlots() const {
    return static_cast<size_t>(image_runtime_tuning::TextureCacheSlots(
        3, static_cast<int>(texture_cache.size())));
  }

  void ShutdownPrefetchQueues() {
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      prefetch_queues[static_cast<size_t>(i)].Shutdown();
    }
    active_prefetch_lanes = 0;
    dedicated_prefetch_queue = false;
  }

  bool AnyPrefetchQueueBusyOrReady() const {
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      if (prefetch_queues[static_cast<size_t>(i)].IsBusyOrReady()) return true;
    }
    return false;
  }

  bool HasIdlePrefetchLane() const {
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      if (!prefetch_queues[static_cast<size_t>(i)].IsBusyOrReady()) return true;
    }
    return false;
  }

  bool HasQueuedPrefetchTarget(const AsyncImageRenderJob &job) const {
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      if (prefetch_queues[static_cast<size_t>(i)].HasVisualTarget(job)) return true;
    }
    return false;
  }

  bool TakePrefetchReady(AsyncImageRenderResult &out_result) {
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      if (prefetch_queues[static_cast<size_t>(i)].TakeReady(out_result)) return true;
    }
    return false;
  }

  void StartPrefetchQueues() {
    ShutdownPrefetchQueues();
    if (!image_runtime_tuning::DedicatedPrefetchThreadEnabled()) return;
    const int requested_lanes = std::min(
        static_cast<int>(kMaxPrefetchLanes),
        std::max(1, image_runtime_tuning::DedicatedPrefetchLaneCount(1)));
    for (int i = 0; i < requested_lanes; ++i) {
      const std::string thread_name = "epub_comic_prefetch_worker_" + std::to_string(i + 1);
      const bool started = prefetch_queues[static_cast<size_t>(i)].Start(
          thread_name,
          [impl = this](const AsyncImageRenderJob &job,
                        std::atomic<bool> &cancel,
                        AsyncImageRenderResult &out) {
            return impl->RenderAsyncJob(job, cancel, out);
          });
      if (!started) break;
      ++active_prefetch_lanes;
    }
    dedicated_prefetch_queue = active_prefetch_lanes > 0;
  }

  bool ClampPage(LocationState &location) const {
    const int page_count = std::max(1, reader.PageCount());
    const int clamped = std::clamp(location.page_num, 0, page_count - 1);
    const bool changed = (clamped != location.page_num);
    location.page_num = clamped;
    return changed;
  }

  float RenderScaleForState(const EpubState &state) const {
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
    float render_scale = std::max(kMinZoom, std::min(kMaxZoom, fit_scale * state.view.zoom));
    const int max_w = max_texture_w > 0 ? max_texture_w : 0;
    const int max_h = max_texture_h > 0 ? max_texture_h : 0;
    if (max_w > 0 && max_h > 0) {
      if (rotation == 90 || rotation == 270) {
        render_scale = std::min(render_scale, static_cast<float>(max_w) / static_cast<float>(std::max(1, page_h)));
        render_scale = std::min(render_scale, static_cast<float>(max_h) / static_cast<float>(std::max(1, page_w)));
      } else {
        render_scale = std::min(render_scale, static_cast<float>(max_w) / static_cast<float>(std::max(1, page_w)));
        render_scale = std::min(render_scale, static_cast<float>(max_h) / static_cast<float>(std::max(1, page_h)));
      }
    }
    return std::max(0.05f, render_scale);
  }

  int RenderedFlowExtent(const EpubState &state) const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(state.location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return screen_h;
    }
    return std::max(1, static_cast<int>(std::lround(RenderScaleForState(state) *
                                                    static_cast<float>(page_h))));
  }

  int ViewportFlowExtent(const EpubState &state) const {
    const int rotation = NormalizeRotation(state.view.rotation);
    return (rotation == 90 || rotation == 270) ? screen_w : screen_h;
  }

  bool HasPrevPage(const EpubState &state) const { return state.location.page_num > 0; }

  bool HasNextPage(const EpubState &state) const {
    return state.location.page_num + 1 < std::max(1, reader.PageCount());
  }

  int MaxYOffset(const EpubState &state) const {
    return std::max(0, RenderedFlowExtent(state) - ViewportFlowExtent(state));
  }

  int RenderedCrossExtent(const EpubState &state) const {
    int page_w = 0;
    int page_h = 0;
    if (!reader.PageSize(state.location.page_num, page_w, page_h) || page_w <= 0 || page_h <= 0) {
      return screen_w;
    }
    return std::max(1, static_cast<int>(std::lround(RenderScaleForState(state) *
                                                    static_cast<float>(page_w))));
  }

  int ViewportCrossExtent(const EpubState &state) const {
    const int rotation = NormalizeRotation(state.view.rotation);
    return (rotation == 90 || rotation == 270) ? screen_h : screen_w;
  }

  int MaxXOffset(const EpubState &state) const {
    return std::max(0, RenderedCrossExtent(state) - ViewportCrossExtent(state));
  }

  void ClampXOffset(EpubState &state) const {
    state.location.x_offset = std::clamp(state.location.x_offset, 0, MaxXOffset(state));
  }

  int TransitionStartYOffset(const EpubState &state) const { return MaxYOffset(state); }

  int NormalizeCarryPrev(const EpubState &state) const {
    EpubState prev = state;
    --prev.location.page_num;
    prev.location.y_offset = 0;
    return RenderedFlowExtent(prev);
  }

  void ClampYOffsetCurrentPage(EpubState &state) const {
    ClampPage(state.location);
    ClampXOffset(state);
    state.location.y_offset = std::clamp(state.location.y_offset, 0, MaxYOffset(state));
  }

  void NormalizeState(EpubState &state) const {
    ClampPage(state.location);
    while (state.location.y_offset < 0 && HasPrevPage(state)) {
      state.location.y_offset += NormalizeCarryPrev(state);
      --state.location.page_num;
    }

    while (HasNextPage(state)) {
      const int current_extent = RenderedFlowExtent(state);
      if (state.location.y_offset < current_extent) break;
      state.location.y_offset -= current_extent;
      ++state.location.page_num;
    }

    if (!HasPrevPage(state) && state.location.y_offset < 0) {
      state.location.y_offset = 0;
    }
    if (!HasNextPage(state)) {
      state.location.y_offset = std::clamp(state.location.y_offset, 0, MaxYOffset(state));
    }
    ClampXOffset(state);
  }

  void RecenterAfterVisualChange(EpubState &state, const ViewState &old_view,
                                 int old_x_offset, int old_y_offset) const {
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
    const int old_rendered_cross = std::max(1, static_cast<int>(std::lround(
                                                 old_fit_scale * old_view.zoom * static_cast<float>(page_w))));
    const int old_cross_extent = (old_rotation == 90 || old_rotation == 270) ? screen_h : screen_w;
    const int old_max_x = std::max(0, old_rendered_cross - old_cross_extent);
    const float old_cross_anchor =
        (old_max_x > 0) ? ((static_cast<float>(old_x_offset) + static_cast<float>(old_cross_extent) * 0.5f) /
                           static_cast<float>(old_rendered_cross))
                        : 0.5f;

    state.location.page_num = locked_page;
    ClampPage(state.location);
    const int new_rendered_h = RenderedFlowExtent(state);
    const int new_view_extent = ViewportFlowExtent(state);
    const float new_center_y = old_anchor * static_cast<float>(new_rendered_h);
    state.location.y_offset =
        static_cast<int>(std::lround(new_center_y - static_cast<float>(new_view_extent) * 0.5f));
    const int new_rendered_cross = RenderedCrossExtent(state);
    const int new_cross_extent = ViewportCrossExtent(state);
    const float new_center_x = old_cross_anchor * static_cast<float>(new_rendered_cross);
    state.location.x_offset =
        static_cast<int>(std::lround(new_center_x - static_cast<float>(new_cross_extent) * 0.5f));
    ClampYOffsetCurrentPage(state);
  }

  bool PanHorizontalByPixels(int delta_px) {
    if (!reader.IsOpen() || delta_px == 0) return false;
    const int old_x = target_state.location.x_offset;
    target_state.location.x_offset = std::clamp(target_state.location.x_offset + delta_px, 0, MaxXOffset(target_state));
    return target_state.location.x_offset != old_x;
  }

  bool PanVerticalByPixels(int delta_px) {
    if (!reader.IsOpen() || delta_px == 0) return false;
    const int old_y = target_state.location.y_offset;
    target_state.location.y_offset = std::clamp(target_state.location.y_offset + delta_px, 0, MaxYOffset(target_state));
    return target_state.location.y_offset != old_y;
  }

  bool RenderPixelsForState(const EpubState &state, RenderResult &out, const std::atomic<bool> *cancel) {
    std::vector<unsigned char> rgba;
    int raw_w = 0;
    int raw_h = 0;
    if (!reader.RenderPageRGBA(state.location.page_num, RenderScaleForState(state), rgba, raw_w, raw_h, cancel)) {
      runtime_log::Line("[epub_runtime] render pixels failed page=" + std::to_string(state.location.page_num));
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

  bool RenderAsyncJob(const AsyncImageRenderJob &job,
                      std::atomic<bool> &cancel,
                      AsyncImageRenderResult &out) {
    EpubComicReader worker_reader;
    if (!worker_reader.Open(job.source_key)) return false;

    std::vector<unsigned char> rgba;
    int raw_w = 0;
    int raw_h = 0;
    const bool ok = worker_reader.RenderPageRGBA(job.page, job.scale, rgba, raw_w, raw_h, &cancel);
    worker_reader.Close();
    if (!ok) {
      runtime_log::Line("[epub_comic_runtime] async render failed page=" + std::to_string(job.page));
      return false;
    }

    int rotated_w = 0;
    int rotated_h = 0;
    out.rgba = RotateRgba(rgba, raw_w, raw_h, job.rotation, rotated_w, rotated_h);
    out.width = rotated_w;
    out.height = rotated_h;
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
    if (!next) {
      runtime_log::Line("[epub_runtime] SDL_CreateTexture failed size=" + std::to_string(ready.texture_w) + "x" +
                        std::to_string(ready.texture_h) + " err=" + SDL_GetError());
      return nullptr;
    }
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
    ApplyImageTextureFiltering(next);
    if (SDL_UpdateTexture(next, nullptr, ready.rgba.data(), ready.texture_w * 4) != 0) {
      runtime_log::Line("[epub_runtime] SDL_UpdateTexture failed size=" + std::to_string(ready.texture_w) + "x" +
                        std::to_string(ready.texture_h) + " err=" + SDL_GetError());
      SDL_DestroyTexture(next);
      return nullptr;
    }
    return next;
  }

  int FindCacheEntryForVisual(const EpubState &state) const {
    const size_t active_slots = ActiveTextureCacheSlots();
    for (size_t i = 0; i < active_slots; ++i) {
      if (texture_cache[i].valid && texture_cache[i].state.SameVisualState(state)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  bool HasVisualTextureForState(const EpubState &state) const {
    if (visible_source.valid && visible_source.state.SameVisualState(state)) return true;
    return FindCacheEntryForVisual(state) >= 0;
  }

  VisibleContentSource LookupSourceForState(const EpubState &state) const {
    if (visible_source.valid && visible_source.texture && visible_source.state.SameVisualState(state)) {
      return visible_source;
    }
    const int cache_index = FindCacheEntryForVisual(state);
    if (cache_index < 0) return {};
    const CachedTextureEntry &entry = texture_cache[cache_index];
    if (!entry.valid || !entry.texture) return {};
    VisibleContentSource source;
    source.texture = entry.texture;
    source.texture_w = entry.texture_w;
    source.texture_h = entry.texture_h;
    source.state = entry.state;
    source.valid = true;
    return source;
  }

  bool ShouldCacheState(const EpubState &state) const {
    return NormalizeRotation(state.view.rotation) == NormalizeRotation(target_state.view.rotation) &&
           std::abs(state.view.zoom - target_state.view.zoom) < 0.0005f;
  }

  bool WantsIdlePrefetch(Uint32 now) const {
    if (!reader.IsOpen()) return false;
    if (!display_valid || !visible_source.valid) return false;
    const bool can_prefetch_during_target =
        dedicated_prefetch_queue && image_runtime_tuning::DedicatedPrefetchThreadEnabled();
    if (can_prefetch_during_target) {
      if (!HasIdlePrefetchLane()) return false;
    } else if (prefetch_active) {
      return false;
    }
    if (!can_prefetch_during_target && !display_state.SameVisualState(target_state)) return false;
    if (!can_prefetch_during_target && (request_active || visual_render_delay_active)) return false;
    if (!can_prefetch_during_target &&
        !SDL_TICKS_PASSED(now, last_interaction_ticks +
                                   image_runtime_tuning::IdlePrefetchDelayMs(kIdlePrefetchDelayMs))) {
      return false;
    }

    return FindPrefetchPageOffset(preferred_prefetch_dir) != 0;
  }

  int SelectCacheVictim() const {
    int victim = 0;
    uint64_t best_stamp = UINT64_MAX;
    const size_t active_slots = ActiveTextureCacheSlots();
    for (size_t i = 0; i < active_slots; ++i) {
      if (!texture_cache[i].valid) return static_cast<int>(i);
      if (texture_cache[i].stamp < best_stamp) {
        best_stamp = texture_cache[i].stamp;
        victim = static_cast<int>(i);
      }
    }
    return victim;
  }

  void StoreTextureInCache(SDL_Texture *texture, int texture_w, int texture_h, const EpubState &state) {
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

  void InstallReadyPrefetchResults() {
    const int prefetch_install_budget = image_runtime_tuning::ReadyResultQueueDepth(1);
    for (int i = 0; i < prefetch_install_budget; ++i) {
      AsyncImageRenderResult prefetch_ready;
      if (!TakePrefetchReady(prefetch_ready)) break;
      if (!prefetch_ready.ready || !prefetch_ready.success || !prefetch_ready.job.prefetch) continue;

      RenderResult ready;
      ready.ready = prefetch_ready.ready;
      ready.success = prefetch_ready.success;
      ready.prefetch = true;
      ready.serial = prefetch_ready.job.serial;
      ready.state = StateFromAsyncJob(prefetch_ready.job);
      ready.texture_w = prefetch_ready.width;
      ready.texture_h = prefetch_ready.height;
      ready.rgba = std::move(prefetch_ready.rgba);

      SDL_Texture *texture = CreateTextureFromResult(ready);
      if (texture && ShouldCacheState(ready.state)) {
        StoreTextureInCache(texture, ready.texture_w, ready.texture_h, ready.state);
      } else if (texture) {
        SDL_DestroyTexture(texture);
      }
    }
  }

  bool ActivateCachedTexture(const EpubState &state) {
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

  bool ShouldPrimeAdjacentPage(const EpubState &state, int dir) const {
    const int warmup_extent =
        ViewportFlowExtent(state) * image_runtime_tuning::PrefetchViewportScreens(1);
    if (dir >= 0) {
      if (!HasNextPage(state)) return false;
      const int warmup_start = std::max(0, TransitionStartYOffset(state) - warmup_extent);
      return state.location.y_offset >= warmup_start;
    }
    if (!HasPrevPage(state)) return false;
    return state.location.y_offset <= warmup_extent;
  }

  bool ShouldHoldForwardScrollForNextPage(const EpubState &before, const EpubState &candidate) const {
    if (!image_runtime_tuning::RgdsFastMode()) return false;
    if (!HasNextPage(before)) return false;
    if (candidate.location.page_num < before.location.page_num) return false;
    const int transition_start = TransitionStartYOffset(before);
    const bool entering_next_page =
        candidate.location.page_num > before.location.page_num ||
        candidate.location.y_offset > transition_start;
    if (!entering_next_page) return false;

    EpubState next = before;
    ++next.location.page_num;
    next.location.x_offset = 0;
    next.location.y_offset = 0;
    return !HasVisualTextureForState(next);
  }

  bool QueuePrefetchPageOffsetLocked(int page_offset) {
    if (page_offset == 0) return false;
    EpubState candidate = target_state;
    candidate.location.page_num += page_offset;
    candidate.location.y_offset = 0;
    if (candidate.location.page_num < 0 || candidate.location.page_num >= reader.PageCount()) return false;
    ClampPage(candidate.location);
    if (HasVisualTextureForState(candidate)) return false;
    prefetched_state = candidate;
    AsyncImageRenderJob job = MakeAsyncJobForState(path, candidate, RenderScaleForState(candidate), true);
    bool accepted = false;
    if (dedicated_prefetch_queue) {
      if (render_queue.HasVisualTarget(job) || HasQueuedPrefetchTarget(job)) return false;
      for (int i = 0; i < active_prefetch_lanes; ++i) {
        AsyncImageRenderQueue &queue = prefetch_queues[static_cast<size_t>(i)];
        if (queue.IsBusyOrReady()) continue;
        accepted = queue.Request(job, true);
        break;
      }
    } else {
      accepted = render_queue.Request(job, true);
    }
    prefetch_active = prefetch_active || accepted || AnyPrefetchQueueBusyOrReady();
    return accepted;
  }

  int FindPrefetchPageOffset(int dir) const {
    const int direction = (dir >= 0) ? 1 : -1;
    const int ahead_pages = image_runtime_tuning::PrefetchAheadPages(1);
    for (int step = 1; step <= ahead_pages; ++step) {
      EpubState candidate = target_state;
      candidate.location.page_num += direction * step;
      candidate.location.y_offset = 0;
      if (candidate.location.page_num >= 0 && candidate.location.page_num < reader.PageCount() &&
          !HasVisualTextureForState(candidate)) {
        return direction * step;
      }
    }
    if (image_runtime_tuning::BidirectionalPrefetchEnabled()) {
      EpubState candidate = target_state;
      candidate.location.page_num -= direction;
      candidate.location.y_offset = 0;
      if (candidate.location.page_num >= 0 && candidate.location.page_num < reader.PageCount() &&
          !HasVisualTextureForState(candidate)) {
        return -direction;
      }
    }
    return 0;
  }

  void QueueAdjacentPrefetchLocked(int dir) {
    QueuePrefetchPageOffsetLocked(FindPrefetchPageOffset(dir));
  }

  void MarkInteraction() { last_interaction_ticks = SDL_GetTicks(); }

  void MarkTargetChangedLocked() {
    request_active = false;
    prefetch_active = false;
    visual_render_delay_active = false;
    render_queue.CancelTarget();
    for (int i = 0; i < active_prefetch_lanes; ++i) {
      prefetch_queues[static_cast<size_t>(i)].CancelTarget();
    }
  }

  void QueueTargetRenderLocked(const EpubState &state) {
    requested_state = state;
    request_active =
        render_queue.Request(MakeAsyncJobForState(path, state, RenderScaleForState(state), false), false);
    if (dedicated_prefetch_queue) {
      QueueAdjacentPrefetchLocked(preferred_prefetch_dir);
    }
  }

  void RequestRenderLocked() {
    if (request_active && requested_state.SameVisualState(target_state)) return;
    if (visual_render_delay_active && delayed_state.SameVisualState(target_state)) return;
    MarkTargetChangedLocked();
    QueueTargetRenderLocked(target_state);
  }

  void DelayVisualRenderLocked() {
    if (request_active && requested_state.SameVisualState(target_state)) return;
    const Uint32 delay_ms = image_runtime_tuning::VisualRenderThrottleMs(kVisualRenderThrottleMs);
    if (delay_ms == 0) {
      MarkTargetChangedLocked();
      QueueTargetRenderLocked(target_state);
      return;
    }
    if (visual_render_delay_active && delayed_state.SameVisualState(target_state)) {
      visual_render_due_ms = SDL_GetTicks() + delay_ms;
      return;
    }
    MarkTargetChangedLocked();
    delayed_state = target_state;
    visual_render_delay_active = true;
    visual_render_due_ms = SDL_GetTicks() + delay_ms;
  }

  void FlushDelayedRenderLocked(Uint32 now) {
    if (!visual_render_delay_active) return;
    if (SDL_TICKS_PASSED(now, visual_render_due_ms)) {
      visual_render_delay_active = false;
      QueueTargetRenderLocked(delayed_state);
    }
  }

  void SchedulePrefetchLocked() {
    const Uint32 now = SDL_GetTicks();
    if (!WantsIdlePrefetch(now)) {
      prefetch_active = AnyPrefetchQueueBusyOrReady();
      return;
    }

    if (!dedicated_prefetch_queue) {
      if (!QueuePrefetchPageOffsetLocked(FindPrefetchPageOffset(preferred_prefetch_dir))) {
        prefetch_active = false;
      }
      return;
    }

    bool queued_any = false;
    const int direction = (preferred_prefetch_dir >= 0) ? 1 : -1;
    const int ahead_pages = image_runtime_tuning::PrefetchAheadPages(1);
    for (int step = 1; step <= ahead_pages && HasIdlePrefetchLane(); ++step) {
      if (QueuePrefetchPageOffsetLocked(direction * step)) queued_any = true;
    }
    if (image_runtime_tuning::BidirectionalPrefetchEnabled()) {
      for (int step = 1; step <= 1 && HasIdlePrefetchLane(); ++step) {
        if (QueuePrefetchPageOffsetLocked(-direction * step)) queued_any = true;
      }
    }
    prefetch_active = queued_any || AnyPrefetchQueueBusyOrReady();
  }

  ViewportLayout ComputeViewportLayout(const EpubState &state, int content_w, int content_h) const {
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
      const int max_cross_y = std::max(0, content_h - src.h);
      if (max_cross_y > 0) {
        src.y = std::clamp(state.location.x_offset, 0, max_cross_y);
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
      const int max_cross_x = std::max(0, content_w - src.w);
      if (max_cross_x > 0) {
        src.x = std::clamp(state.location.x_offset, 0, max_cross_x);
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

  void DrawVisibleSource(SDL_Renderer *renderer, const VisibleContentSource &source, const EpubState &state) const {
    if (!renderer || !source.texture || !source.valid) return;
    const ViewportLayout layout = ComputeViewportLayout(state, source.texture_w, source.texture_h);
    if (!layout.valid) return;
    SDL_RenderCopy(renderer, source.texture, &layout.src, &layout.dst);
  }

  bool DrawPageAt(SDL_Renderer *renderer, int page_index, const SDL_Rect &dst_rect) const {
    if (!renderer || page_index < 0 || page_index >= reader.PageCount()) return false;
    EpubState page_state = target_state;
    page_state.location.page_num = page_index;
    page_state.location.x_offset = 0;
    page_state.location.y_offset = 0;
    const VisibleContentSource source = LookupSourceForState(page_state);
    if (!source.valid || !source.texture) return false;
    ViewportLayout layout = ComputeViewportLayout(page_state, source.texture_w, source.texture_h);
    if (!layout.valid) return false;
    layout.dst.x += dst_rect.x;
    layout.dst.y += dst_rect.y;
    SDL_RenderCopy(renderer, source.texture, &layout.src, &layout.dst);
    return true;
  }

  bool CanDrawPageAt(int page_index) const {
    if (page_index < 0 || page_index >= reader.PageCount()) return false;
    EpubState page_state = target_state;
    page_state.location.page_num = page_index;
    page_state.location.x_offset = 0;
    page_state.location.y_offset = 0;
    const VisibleContentSource source = LookupSourceForState(page_state);
    return source.valid && source.texture;
  }

  void PrefetchPageAt(int page_index) {
    if (page_index < 0 || page_index >= reader.PageCount()) return;
    const int page_offset = page_index - target_state.location.page_num;
    if (page_offset == 0) return;
    SDL_LockMutex(mutex);
    QueuePrefetchPageOffsetLocked(page_offset);
    SDL_UnlockMutex(mutex);
  }

  bool DrawContinuousNextPage(SDL_Renderer *renderer, const EpubState &state) const {
    if (!renderer) return false;
    if (!HasNextPage(state)) return false;

    const int rotation = NormalizeRotation(state.view.rotation);
    const int transition_start = TransitionStartYOffset(state);
    if (state.location.y_offset <= transition_start) return false;

    const VisibleContentSource current_source = LookupSourceForState(state);
    if (!current_source.valid || !current_source.texture) return false;

    EpubState next_state = state;
    ++next_state.location.page_num;
    next_state.location.y_offset = 0;
    const VisibleContentSource next_source = LookupSourceForState(next_state);
    if (!next_source.valid || !next_source.texture) return false;

    const bool horizontal_flow = (rotation == 90 || rotation == 270);
    const bool positive_flow = (rotation == 0 || rotation == 270);
    const int viewport_extent = ViewportFlowExtent(state);
    const int overflow = std::clamp(state.location.y_offset - transition_start, 0, viewport_extent);
    if (overflow <= 0) return false;
    const int current_visible = std::max(0, viewport_extent - overflow);

    if (current_visible > 0) {
      ViewportLayout current_layout =
          ComputeViewportLayout(state, current_source.texture_w, current_source.texture_h);
      if (current_layout.valid) {
        if (horizontal_flow) {
          const int src_start = positive_flow
                                    ? std::clamp(state.location.y_offset, 0,
                                                 std::max(0, current_source.texture_w - current_visible))
                                    : std::clamp(current_source.texture_w - state.location.y_offset - current_visible,
                                                 0, std::max(0, current_source.texture_w - current_visible));
          current_layout.src.x = src_start;
          current_layout.src.w = std::min(current_visible, current_source.texture_w - current_layout.src.x);
          current_layout.dst.x = positive_flow ? 0 : overflow;
          current_layout.dst.w = current_layout.src.w;
        } else {
          const int src_start = positive_flow
                                    ? std::clamp(state.location.y_offset, 0,
                                                 std::max(0, current_source.texture_h - current_visible))
                                    : std::clamp(current_source.texture_h - state.location.y_offset - current_visible,
                                                 0, std::max(0, current_source.texture_h - current_visible));
          current_layout.src.y = src_start;
          current_layout.src.h = std::min(current_visible, current_source.texture_h - current_layout.src.y);
          current_layout.dst.y = positive_flow ? 0 : overflow;
          current_layout.dst.h = current_layout.src.h;
        }
        if (current_layout.src.w > 0 && current_layout.src.h > 0 &&
            current_layout.dst.w > 0 && current_layout.dst.h > 0) {
          SDL_RenderCopy(renderer, current_source.texture, &current_layout.src, &current_layout.dst);
        }
      }
    }

    ViewportLayout next_layout = ComputeViewportLayout(next_state, next_source.texture_w, next_source.texture_h);
    if (!next_layout.valid) return true;
    if (horizontal_flow) {
      const int src_start = positive_flow ? 0 : std::max(0, next_source.texture_w - overflow);
      next_layout.src.x = src_start;
      next_layout.src.w = std::min(overflow, next_source.texture_w - next_layout.src.x);
      next_layout.dst.x = positive_flow ? current_visible : 0;
      next_layout.dst.w = next_layout.src.w;
    } else {
      const int src_start = positive_flow ? 0 : std::max(0, next_source.texture_h - overflow);
      next_layout.src.y = src_start;
      next_layout.src.h = std::min(overflow, next_source.texture_h - next_layout.src.y);
      next_layout.dst.y = positive_flow ? current_visible : 0;
      next_layout.dst.h = next_layout.src.h;
    }
    if (next_layout.src.w > 0 && next_layout.src.h > 0 &&
        next_layout.dst.w > 0 && next_layout.dst.h > 0) {
      SDL_RenderCopy(renderer, next_source.texture, &next_layout.src, &next_layout.dst);
    }
    return true;
  }
};

EpubComicRuntime::EpubComicRuntime() : impl_(new Impl()) {}

EpubComicRuntime::~EpubComicRuntime() {
  Close();
  delete impl_;
  impl_ = nullptr;
}

bool EpubComicRuntime::Open(SDL_Renderer *renderer,
                       const std::string &path,
                       int screen_w,
                       int screen_h,
                       const EpubRuntimeProgress &initial_progress) {
  Close();
  impl_->renderer = renderer;
  impl_->screen_w = std::max(1, screen_w);
  impl_->screen_h = std::max(1, screen_h);
  SDL_RendererInfo renderer_info{};
  if (renderer && SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
    impl_->max_texture_w = renderer_info.max_texture_width;
    impl_->max_texture_h = renderer_info.max_texture_height;
  } else {
    impl_->max_texture_w = 0;
    impl_->max_texture_h = 0;
  }
  runtime_log::Line("[epub_runtime] open path=" + path + " screen=" + std::to_string(impl_->screen_w) + "x" +
                    std::to_string(impl_->screen_h) + " max_texture=" +
                    std::to_string(impl_->max_texture_w) + "x" + std::to_string(impl_->max_texture_h));

  if (!impl_->reader.Open(path)) {
    runtime_log::Line("[epub_comic_runtime] reader open failed path=" + path);
    return false;
  }
  runtime_log::Line("[epub_comic_runtime] reader open ok pages=" + std::to_string(impl_->reader.PageCount()));
  impl_->path = path;

  impl_->target_state.view.zoom = std::max(kMinZoom, std::min(kMaxZoom, initial_progress.zoom));
  impl_->target_state.view.rotation = NormalizeRotation(initial_progress.rotation);
  impl_->target_state.view.viewport_w = impl_->screen_w;
  impl_->target_state.view.viewport_h = impl_->screen_h;
  impl_->target_state.location.page_num = std::max(0, initial_progress.page);
  impl_->target_state.location.x_offset = std::max(0, initial_progress.scroll_x);
  impl_->target_state.location.y_offset = std::max(0, initial_progress.scroll_y);
  impl_->NormalizeState(impl_->target_state);

  RenderResult seed;
  if (!impl_->RenderPixelsForState(impl_->target_state, seed, nullptr)) {
    runtime_log::Line("[epub_comic_runtime] seed render failed path=" + path);
    impl_->reader.Close();
    impl_->path.clear();
    return false;
  }
  if (!impl_->InstallTexture(seed)) {
    runtime_log::Line("[epub_comic_runtime] seed texture install failed path=" + path + " size=" +
                      std::to_string(seed.texture_w) + "x" + std::to_string(seed.texture_h));
    impl_->reader.Close();
    impl_->path.clear();
    return false;
  }

  impl_->display_state = impl_->target_state;
  impl_->ready_state = impl_->target_state;
  impl_->display_valid = true;
  impl_->ready_valid = true;
  impl_->request_active = false;
  impl_->prefetch_active = false;
  impl_->last_interaction_ticks = SDL_GetTicks();
  const bool queue_started = impl_->render_queue.Start(
      "epub_comic_runtime_worker",
      [impl = impl_](const AsyncImageRenderJob &job,
                     std::atomic<bool> &cancel,
                     AsyncImageRenderResult &out) {
        return impl->RenderAsyncJob(job, cancel, out);
      });
  if (queue_started) impl_->StartPrefetchQueues();
  if (queue_started) {
    SDL_LockMutex(impl_->mutex);
    impl_->SchedulePrefetchLocked();
    SDL_UnlockMutex(impl_->mutex);
  }
  return true;
}

void EpubComicRuntime::Close() {
  if (!impl_) return;
  impl_->render_queue.Shutdown();
  impl_->ShutdownPrefetchQueues();
  impl_->DestroyTexture();
  impl_->reader.Close();
  impl_->path.clear();
  impl_->target_state = EpubState{};
  impl_->display_state = EpubState{};
  impl_->ready_state = EpubState{};
  impl_->display_valid = false;
  impl_->ready_valid = false;
  impl_->request_active = false;
  impl_->prefetch_active = false;
  impl_->visual_render_delay_active = false;
  impl_->ClearTextureCache();
  impl_->DestroyReusableTexture();
}

bool EpubComicRuntime::IsOpen() const {
  if (!impl_) return false;
  return impl_->reader.IsOpen();
}

bool EpubComicRuntime::HasRealRenderer() const {
  if (!impl_) return false;
  return impl_->reader.HasRealRenderer();
}

const char *EpubComicRuntime::BackendName() const {
  if (!impl_) return "none";
  return impl_->reader.BackendName();
}

bool EpubComicRuntime::IsRenderPending() const {
  if (!impl_ || !impl_->reader.IsOpen()) return false;
  if (!impl_->display_valid || !impl_->visible_source.valid) return true;
  if (!impl_->display_state.SameVisualState(impl_->target_state)) return true;
  SDL_LockMutex(impl_->mutex);
  const bool pending = impl_->request_active || impl_->prefetch_active || impl_->visual_render_delay_active ||
                       impl_->render_queue.IsBusyOrReady() || impl_->AnyPrefetchQueueBusyOrReady() ||
                       impl_->WantsIdlePrefetch(SDL_GetTicks());
  SDL_UnlockMutex(impl_->mutex);
  return pending;
}

void EpubComicRuntime::UpdateViewport(int screen_w, int screen_h) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  screen_w = std::max(1, screen_w);
  screen_h = std::max(1, screen_h);
  if (screen_w == impl_->screen_w && screen_h == impl_->screen_h) return;
  impl_->screen_w = screen_w;
  impl_->screen_h = screen_h;
  impl_->MarkInteraction();
  impl_->target_state.view.viewport_w = impl_->screen_w;
  impl_->target_state.view.viewport_h = impl_->screen_h;
  impl_->NormalizeState(impl_->target_state);
  if (impl_->display_valid) {
    impl_->NormalizeState(impl_->display_state);
  }
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->RequestRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::Tick() {
  if (!impl_ || !impl_->reader.IsOpen()) return;

  SDL_LockMutex(impl_->mutex);
  impl_->FlushDelayedRenderLocked(SDL_GetTicks());
  if (!impl_->request_active) {
    impl_->SchedulePrefetchLocked();
  }
  SDL_UnlockMutex(impl_->mutex);

  RenderResult ready;
  bool have_ready = false;
  AsyncImageRenderResult async_ready;
  if (impl_->render_queue.TakeReady(async_ready) || impl_->TakePrefetchReady(async_ready)) {
    ready.ready = async_ready.ready;
    ready.success = async_ready.success;
    ready.prefetch = async_ready.job.prefetch;
    ready.serial = async_ready.job.serial;
    ready.state = StateFromAsyncJob(async_ready.job);
    ready.texture_w = async_ready.width;
    ready.texture_h = async_ready.height;
    ready.rgba = std::move(async_ready.rgba);
    have_ready = true;
    if (ready.prefetch) {
      impl_->prefetch_active = impl_->AnyPrefetchQueueBusyOrReady();
    } else {
      impl_->request_active = false;
    }
  }

  if (!have_ready || !ready.success) return;

  if (ready.prefetch) {
    SDL_Texture *texture = impl_->CreateTextureFromResult(ready);
    if (texture && impl_->ShouldCacheState(ready.state)) {
      impl_->StoreTextureInCache(texture, ready.texture_w, ready.texture_h, ready.state);
    } else if (texture) {
      SDL_DestroyTexture(texture);
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

  if (!impl_->request_active) {
    impl_->InstallReadyPrefetchResults();
  }

  SDL_LockMutex(impl_->mutex);
  impl_->SchedulePrefetchLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::Draw(SDL_Renderer *renderer) const {
  if (!impl_ || !renderer || !impl_->visible_source.texture || !impl_->display_valid || !impl_->visible_source.valid) return;
  if (impl_->visible_source.state.view.viewport_w != impl_->screen_w ||
      impl_->visible_source.state.view.viewport_h != impl_->screen_h) {
    return;
  }

  const bool exact = impl_->display_state.SameVisualState(impl_->target_state);
  if (exact) {
    EpubState draw_state = impl_->display_state;
    draw_state.location.x_offset = impl_->target_state.location.x_offset;
    draw_state.location.y_offset = impl_->target_state.location.y_offset;
    if (impl_->DrawContinuousNextPage(renderer, draw_state)) return;
    impl_->DrawVisibleSource(renderer, impl_->visible_source, draw_state);
    return;
  }

  if (image_runtime_tuning::RgdsFastMode() &&
      impl_->display_state.location.page_num != impl_->target_state.location.page_num) {
    EpubState draw_state = impl_->display_state;
    draw_state.location.x_offset = impl_->target_state.location.x_offset;
    const int rotation = NormalizeRotation(draw_state.view.rotation);
    const int flow_extent =
        (rotation == 90 || rotation == 270) ? impl_->visible_source.texture_w : impl_->visible_source.texture_h;
    const int viewport_extent = (rotation == 90 || rotation == 270) ? impl_->screen_w : impl_->screen_h;
    const int max_y = std::max(0, flow_extent - viewport_extent);
    draw_state.location.y_offset =
        (impl_->target_state.location.page_num > impl_->display_state.location.page_num) ? max_y : 0;
    impl_->DrawVisibleSource(renderer, impl_->visible_source, draw_state);
    return;
  }

  impl_->DrawVisibleSource(renderer, impl_->visible_source, impl_->display_state);
}

bool EpubComicRuntime::DrawPageAt(SDL_Renderer *renderer, int page_index, const SDL_Rect &dst_rect) const {
  return impl_ ? impl_->DrawPageAt(renderer, page_index, dst_rect) : false;
}

bool EpubComicRuntime::CanDrawPageAt(int page_index) const {
  return impl_ ? impl_->CanDrawPageAt(page_index) : false;
}

void EpubComicRuntime::PrefetchPageAt(int page_index) {
  if (impl_) impl_->PrefetchPageAt(page_index);
}

void EpubComicRuntime::RotateLeft() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_x_offset = impl_->target_state.location.x_offset;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.rotation = NormalizeRotation(impl_->target_state.view.rotation + 270);
  if (impl_->target_state.view.rotation == old_view.rotation) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_x_offset, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::RotateRight() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_x_offset = impl_->target_state.location.x_offset;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.rotation = NormalizeRotation(impl_->target_state.view.rotation + 90);
  if (impl_->target_state.view.rotation == old_view.rotation) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_x_offset, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::ZoomOut() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_x_offset = impl_->target_state.location.x_offset;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = std::max(kMinZoom, impl_->target_state.view.zoom - kZoomStep);
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_x_offset, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::ZoomIn() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_x_offset = impl_->target_state.location.x_offset;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = std::min(kMaxZoom, impl_->target_state.view.zoom + kZoomStep);
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_x_offset, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

void EpubComicRuntime::ResetView() {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const ViewState old_view = impl_->target_state.view;
  const int old_x_offset = impl_->target_state.location.x_offset;
  const int old_y_offset = impl_->target_state.location.y_offset;
  impl_->target_state.view.zoom = 1.0f;
  impl_->target_state.location.x_offset = 0;
  if (std::abs(impl_->target_state.view.zoom - old_view.zoom) < 0.0005f) return;
  impl_->RecenterAfterVisualChange(impl_->target_state, old_view, old_x_offset, old_y_offset);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->DelayVisualRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

bool EpubComicRuntime::PanHorizontalByPixels(int delta_px) {
  if (!impl_ || !impl_->reader.IsOpen()) return false;
  impl_->MarkInteraction();
  return impl_->PanHorizontalByPixels(delta_px);
}

bool EpubComicRuntime::PanVerticalByPixels(int delta_px) {
  if (!impl_ || !impl_->reader.IsOpen()) return false;
  impl_->MarkInteraction();
  return impl_->PanVerticalByPixels(delta_px);
}

void EpubComicRuntime::ScrollByPixels(int delta_px) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  if (image_runtime_tuning::RgdsFastMode()) {
    impl_->InstallReadyPrefetchResults();
  }
  const int old_page = impl_->target_state.location.page_num;
  if (delta_px > 0) impl_->preferred_prefetch_dir = 1;
  if (delta_px < 0) impl_->preferred_prefetch_dir = -1;
  EpubState next_state = impl_->target_state;
  next_state.location.y_offset += delta_px;
  impl_->NormalizeState(next_state);
  if (delta_px > 0 && impl_->ShouldHoldForwardScrollForNextPage(impl_->target_state, next_state)) {
    impl_->target_state.location.y_offset = impl_->TransitionStartYOffset(impl_->target_state);
    SDL_LockMutex(impl_->mutex);
    impl_->QueuePrefetchPageOffsetLocked(1);
    SDL_UnlockMutex(impl_->mutex);
    return;
  }
  impl_->target_state = next_state;
  if (impl_->target_state.location.page_num != old_page) {
    impl_->target_state.location.x_offset = 0;
    if (impl_->TryUseCachedTarget()) return;
    SDL_LockMutex(impl_->mutex);
    impl_->RequestRenderLocked();
    SDL_UnlockMutex(impl_->mutex);
    return;
  }

  if (impl_->ShouldPrimeAdjacentPage(impl_->target_state, impl_->preferred_prefetch_dir)) {
    SDL_LockMutex(impl_->mutex);
    impl_->QueueAdjacentPrefetchLocked(impl_->preferred_prefetch_dir);
    SDL_UnlockMutex(impl_->mutex);
  }
}

void EpubComicRuntime::JumpByScreen(int direction) {
  if (!impl_ || !impl_->reader.IsOpen() || direction == 0) return;
  impl_->MarkInteraction();
  const int old_page = impl_->target_state.location.page_num;
  impl_->preferred_prefetch_dir = (direction > 0) ? 1 : -1;
  impl_->target_state.location.y_offset += direction * impl_->screen_h;
  impl_->NormalizeState(impl_->target_state);
  if (impl_->target_state.location.page_num != old_page) {
    impl_->target_state.location.x_offset = 0;
    if (impl_->TryUseCachedTarget()) return;
    SDL_LockMutex(impl_->mutex);
    impl_->RequestRenderLocked();
    SDL_UnlockMutex(impl_->mutex);
    return;
  }

  if (impl_->ShouldPrimeAdjacentPage(impl_->target_state, impl_->preferred_prefetch_dir)) {
    SDL_LockMutex(impl_->mutex);
    impl_->QueueAdjacentPrefetchLocked(impl_->preferred_prefetch_dir);
    SDL_UnlockMutex(impl_->mutex);
  }
}

void EpubComicRuntime::SetPage(int page_index) {
  if (!impl_ || !impl_->reader.IsOpen()) return;
  impl_->MarkInteraction();
  const int page_count = std::max(1, impl_->reader.PageCount());
  const int clamped_page = std::clamp(page_index, 0, page_count - 1);
  const int old_page = impl_->target_state.location.page_num;
  impl_->preferred_prefetch_dir = (clamped_page >= old_page) ? 1 : -1;
  impl_->target_state.location.page_num = clamped_page;
  impl_->target_state.location.x_offset = 0;
  impl_->target_state.location.y_offset = 0;
  impl_->NormalizeState(impl_->target_state);
  if (impl_->TryUseCachedTarget()) return;
  SDL_LockMutex(impl_->mutex);
  impl_->RequestRenderLocked();
  SDL_UnlockMutex(impl_->mutex);
}

int EpubComicRuntime::PageCount() const {
  if (!impl_) return 0;
  return impl_->reader.PageCount();
}

bool EpubComicRuntime::PageSize(int page_index, int &w, int &h) const {
  if (!impl_) return false;
  return impl_->reader.PageSize(page_index, w, h);
}

int EpubComicRuntime::CurrentPage() const {
  if (!impl_) return 0;
  return impl_->target_state.location.page_num;
}

EpubRuntimeProgress EpubComicRuntime::Progress() const {
  EpubRuntimeProgress out;
  if (!impl_) return out;
  out.page = impl_->target_state.location.page_num;
  out.rotation = impl_->target_state.view.rotation;
  out.zoom = impl_->target_state.view.zoom;
  out.scroll_x = impl_->target_state.location.x_offset;
  out.scroll_y = impl_->target_state.location.y_offset;
  return out;
}

