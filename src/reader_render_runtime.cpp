#include "reader_render_runtime.h"

#include <cmath>
#include <vector>

namespace {
ReaderRenderCache *ResultNeighborCacheSlot(ReaderRenderRuntimeDeps &deps, int page) {
  return (page < deps.target_state.page) ? &deps.reader_render.secondary_render_cache
                                         : &deps.reader_render.tertiary_render_cache;
}
}

bool PromoteAsyncReaderRenderResult(ReaderRenderRuntimeDeps &deps) {
  ReaderAsyncRenderResult ready_result;
  if (!TakeReadyReaderAsyncResult(deps.reader_async, ready_result)) return false;
  if (!ready_result.success) return false;
  if (ready_result.mode != deps.reader_mode || ready_result.path != deps.current_book) return false;
  if (!deps.reader_is_open()) return false;

  const int focus_page = deps.target_state.page;
  const bool is_target_page = ready_result.page == focus_page;
  const bool is_neighbor_page = ready_result.page == focus_page - 1 || ready_result.page == focus_page + 1;
  if (!is_target_page && !is_neighbor_page &&
      (!deps.display_state_valid || ready_result.page != deps.display_state.page)) {
    return false;
  }
  if (ready_result.rotation != deps.target_state.rotation) return false;

  const float target_scale = deps.reader_target_scale_for_state(deps.target_state);
  if (std::abs(ready_result.target_scale - target_scale) >= 0.0005f) return false;

  ReaderViewState result_state;
  result_state.page = ready_result.page;
  result_state.rotation = ready_result.rotation;
  if (ready_result.page == deps.target_state.page && ready_result.rotation == deps.target_state.rotation) {
    result_state.zoom = deps.target_state.zoom;
  } else if (deps.display_state_valid && ready_result.page == deps.display_state.page &&
             ready_result.rotation == deps.display_state.rotation) {
    result_state.zoom = deps.display_state.zoom;
  } else {
    result_state.zoom = deps.target_state.zoom;
  }
  if (result_state == deps.target_state) {
    deps.ready_state = result_state;
    deps.ready_state_valid = true;
  }

  int render_w = ready_result.src_w;
  int render_h = ready_result.src_h;
  std::vector<unsigned char> rotated;
  const std::vector<unsigned char> &rgba = ready_result.rgba;
  if (ready_result.rotation == 90 || ready_result.rotation == 270) {
    render_w = ready_result.src_h;
    render_h = ready_result.src_w;
    rotated.assign(static_cast<size_t>(render_w * render_h * 4), 0);
    for (int y = 0; y < ready_result.src_h; ++y) {
      for (int x = 0; x < ready_result.src_w; ++x) {
        const int src = (y * ready_result.src_w + x) * 4;
        int dx = 0;
        int dy = 0;
        if (ready_result.rotation == 90) {
          dx = ready_result.src_h - 1 - y;
          dy = x;
        } else {
          dx = y;
          dy = ready_result.src_w - 1 - x;
        }
        const int dst = (dy * render_w + dx) * 4;
        rotated[dst + 0] = rgba[src + 0];
        rotated[dst + 1] = rgba[src + 1];
        rotated[dst + 2] = rgba[src + 2];
        rotated[dst + 3] = rgba[src + 3];
      }
    }
  } else if (ready_result.rotation == 180) {
    rotated.assign(static_cast<size_t>(render_w * render_h * 4), 0);
    for (int y = 0; y < ready_result.src_h; ++y) {
      for (int x = 0; x < ready_result.src_w; ++x) {
        const int src = (y * ready_result.src_w + x) * 4;
        const int dx = ready_result.src_w - 1 - x;
        const int dy = ready_result.src_h - 1 - y;
        const int dst = (dy * render_w + dx) * 4;
        rotated[dst + 0] = rgba[src + 0];
        rotated[dst + 1] = rgba[src + 1];
        rotated[dst + 2] = rgba[src + 2];
        rotated[dst + 3] = rgba[src + 3];
      }
    }
  }

  const unsigned char *pixels = (ready_result.rotation == 0) ? rgba.data() : rotated.data();
  SDL_Texture *texture = deps.acquire_reader_texture(render_w, render_h);
  if (!texture) return false;
  if (SDL_UpdateTexture(texture, nullptr, pixels, render_w * 4) != 0) {
    for (auto &slot : deps.reader_render.texture_pool) {
      if (slot.texture == texture) {
        slot.in_use = false;
        slot.last_use = SDL_GetTicks();
        break;
      }
    }
    return false;
  }

  ReaderRenderCache fresh_cache;
  fresh_cache.texture = texture;
  fresh_cache.page = ready_result.page;
  fresh_cache.rotation = ready_result.rotation;
  fresh_cache.scale = ready_result.target_scale;
  fresh_cache.quality = ReaderRenderQuality::Full;
  fresh_cache.w = render_w;
  fresh_cache.h = render_h;
  fresh_cache.display_w = ready_result.display_w;
  fresh_cache.display_h = ready_result.display_h;
  fresh_cache.last_use = SDL_GetTicks();

  if (deps.display_state_valid && result_state == deps.display_state &&
      std::abs(ready_result.target_scale - deps.reader_render.render_cache.scale) < 0.0005f) {
    deps.destroy_render_cache(deps.reader_render.render_cache);
    deps.reader_render.render_cache = fresh_cache;
  } else {
    ReaderRenderCache *target_cache = ResultNeighborCacheSlot(deps, ready_result.page);
    deps.destroy_render_cache(*target_cache);
    *target_cache = fresh_cache;
  }
  return true;
}

bool RequestAsyncReaderRender(ReaderRenderRuntimeDeps &deps, int page, float target_scale, int display_w,
                              int display_h, bool prefetch) {
  ReaderAsyncRenderJob next_job;
  next_job.active = true;
  next_job.prefetch = prefetch;
  next_job.mode = deps.reader_mode;
  next_job.path = deps.current_book;
  next_job.state = deps.target_state;
  next_job.page = page;
  next_job.target_scale = target_scale;
  next_job.rotation = deps.target_state.rotation;
  next_job.display_w = display_w;
  next_job.display_h = display_h;
  const bool allow_prefetch = prefetch && deps.display_state_valid &&
                              deps.display_state == deps.target_state &&
                              !deps.ready_state_valid;
  return RequestReaderAsyncRender(deps.reader_async, std::move(next_job), allow_prefetch);
}
