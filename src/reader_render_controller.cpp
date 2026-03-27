#include "reader_render_controller.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kReaderScaleEpsilon = 0.0005f;
}

const ReaderRenderCache *VisibleReaderRenderCache(ReaderRenderControllerDeps &deps) {
  const auto &render_cache = deps.render_state.render_cache;
  const auto &display_state = deps.render_state.display_state;
  if (render_cache.texture &&
      deps.render_state.display_state_valid &&
      render_cache.page == display_state.page &&
      render_cache.rotation == display_state.rotation) {
    return &render_cache;
  }
  return nullptr;
}

ReaderRenderCache *MatchingReaderRenderCache(ReaderRenderControllerDeps &deps, int page, int rotation,
                                             float target_scale, ReaderRenderQuality quality) {
  ReaderRenderCache *caches[3] = {
      &deps.render_state.render_cache,
      &deps.render_state.secondary_render_cache,
      &deps.render_state.tertiary_render_cache,
  };
  for (ReaderRenderCache *cache : caches) {
    if (!cache->texture) continue;
    if (cache->page != page || cache->rotation != rotation || cache->quality != quality) continue;
    if (std::abs(cache->scale - target_scale) >= kReaderScaleEpsilon) continue;
    return cache;
  }
  return nullptr;
}

const ReaderRenderCache *VisibleReaderRenderCacheForPage(ReaderRenderControllerDeps &deps, int page,
                                                         int rotation, float target_scale) {
  const auto &render_cache = deps.render_state.render_cache;
  if (render_cache.texture &&
      deps.render_state.display_state_valid &&
      render_cache.page == page &&
      render_cache.rotation == rotation &&
      std::abs(render_cache.scale - target_scale) < kReaderScaleEpsilon) {
    return &render_cache;
  }
  return nullptr;
}

bool EffectiveReaderDisplaySize(ReaderRenderControllerDeps &deps, int page, int rotation,
                                float target_scale, int &out_w, int &out_h) {
  int pw = 0;
  int ph = 0;
  if (!deps.reader_page_size_cached(page, pw, ph)) return false;
  const int base_w = std::max(1, static_cast<int>(std::round(static_cast<float>(pw) * target_scale)));
  const int base_h = std::max(1, static_cast<int>(std::round(static_cast<float>(ph) * target_scale)));
  if (rotation == 90 || rotation == 270) {
    out_w = base_h;
    out_h = base_w;
  } else {
    out_w = base_w;
    out_h = base_h;
  }
  return true;
}

float ReaderTargetScaleForState(ReaderRenderControllerDeps &deps, const ReaderViewState &state) {
  int pw = 0;
  int ph = 0;
  if (!deps.reader_page_size_cached(state.page, pw, ph) || pw <= 0 || ph <= 0) {
    return std::clamp(state.zoom, 0.1f, 6.0f);
  }
  float auto_scale = 1.0f;
  if (state.rotation == 0 || state.rotation == 180) {
    auto_scale = std::max(0.1f, static_cast<float>(deps.screen_w) / static_cast<float>(pw));
  } else {
    const float fit_h = static_cast<float>(deps.screen_h) / static_cast<float>(pw);
    const float need_overflow = static_cast<float>(deps.screen_w + 200) / static_cast<float>(ph);
    auto_scale = std::max(0.1f, std::max(fit_h, need_overflow));
  }
  return std::clamp(auto_scale * state.zoom, 0.1f, 6.0f);
}

bool EffectiveReaderDisplaySizeForState(ReaderRenderControllerDeps &deps,
                                        const ReaderViewState &state, int &out_w, int &out_h) {
  return EffectiveReaderDisplaySize(deps, state.page, state.rotation,
                                    ReaderTargetScaleForState(deps, state), out_w, out_h);
}

void PruneReaderNeighborCaches(ReaderRenderControllerDeps &deps, int center_page, int rotation,
                               float target_scale) {
  ReaderRenderCache *neighbors[2] = {
      &deps.render_state.secondary_render_cache,
      &deps.render_state.tertiary_render_cache,
  };
  for (ReaderRenderCache *cache : neighbors) {
    if (!cache->texture) continue;
    const bool keep_page = cache->page == center_page || cache->page == center_page - 1 ||
                           cache->page == center_page + 1;
    const bool keep_rotation = cache->rotation == rotation;
    const bool keep_scale = std::abs(cache->scale - target_scale) < kReaderScaleEpsilon;
    const bool keep_quality = cache->quality == ReaderRenderQuality::Full;
    if (!keep_page || !keep_rotation || !keep_scale || !keep_quality) {
      deps.destroy_render_cache(*cache);
    }
  }
}

std::pair<int, int> CurrentReaderAxisSign(const ReaderRenderControllerDeps &deps) {
  const int rotation = deps.render_state.target_state.rotation;
  if (rotation == 0) return {1, 1};
  if (rotation == 90) return {0, -1};
  if (rotation == 180) return {1, -1};
  return {0, 1};
}

bool CurrentReaderDisplaySize(ReaderRenderControllerDeps &deps, int &out_w, int &out_h) {
  if (!deps.reader_is_open()) return false;
  if (EffectiveReaderDisplaySizeForState(deps, deps.render_state.target_state, out_w, out_h)) return true;
  if (deps.render_state.display_state_valid) {
    return EffectiveReaderDisplaySizeForState(deps, deps.render_state.display_state, out_w, out_h);
  }
  return false;
}

bool PromoteReadyTargetToDisplay(ReaderRenderControllerDeps &deps, float target_scale,
                                 ReaderRenderQuality quality) {
  auto &state = deps.render_state;
  if (!state.ready_state_valid || state.ready_state != state.target_state) return false;
  ReaderRenderCache *cached =
      MatchingReaderRenderCache(deps, state.target_state.page, state.target_state.rotation, target_scale, quality);
  if (!cached) return false;
  if (cached != &state.render_cache) {
    ReaderRenderCache previous_display = state.render_cache;
    state.render_cache = *cached;
    *cached = previous_display;
  }
  state.render_cache.last_use = SDL_GetTicks();
  state.display_state = state.target_state;
  state.display_state_valid = true;
  state.ready_state_valid = false;
  return true;
}

bool EnsureReaderRender(ReaderRenderControllerDeps &deps) {
  auto &state = deps.render_state;
  if (!deps.reader_is_open()) return false;
  deps.promote_async_render_result();
  const int page = state.target_state.page;
  const float target_scale = ReaderTargetScaleForState(deps, state.target_state);
  int display_w = 0;
  int display_h = 0;
  if (!EffectiveReaderDisplaySize(deps, page, state.target_state.rotation, target_scale, display_w, display_h)) {
    display_w = deps.screen_w;
    display_h = deps.screen_h;
  }
  constexpr ReaderRenderQuality quality = ReaderRenderQuality::Full;
  PruneReaderNeighborCaches(deps, page, state.target_state.rotation, target_scale);

  const bool display_matches_target =
      state.display_state_valid &&
      state.display_state.page == state.target_state.page &&
      state.display_state.rotation == state.target_state.rotation &&
      std::abs(state.display_state.zoom - state.target_state.zoom) < kReaderScaleEpsilon &&
      state.render_cache.texture &&
      state.render_cache.page == page &&
      state.render_cache.rotation == state.target_state.rotation &&
      state.render_cache.quality == quality &&
      std::abs(state.render_cache.scale - target_scale) < kReaderScaleEpsilon;

  if (!display_matches_target) {
    if (!PromoteReadyTargetToDisplay(deps, target_scale, quality)) {
      deps.request_reader_async_render(page, target_scale, display_w, display_h, false);
      return state.display_state_valid && state.render_cache.texture;
    }
  } else {
    state.render_cache.last_use = SDL_GetTicks();
  }

  if (!state.display_state_valid) {
    if (!PromoteReadyTargetToDisplay(deps, target_scale, quality)) {
      deps.request_reader_async_render(page, target_scale, display_w, display_h, false);
      return false;
    }
  }

  if (state.adaptive_render.pending_page_active) return true;

  const int page_count = std::max(1, deps.reader_page_count());
  const int next_page = page + 1;
  const int prev_page = page - 1;
  if (next_page < page_count &&
      !MatchingReaderRenderCache(deps, next_page, state.target_state.rotation, target_scale, quality)) {
    int next_display_w = 0;
    int next_display_h = 0;
    if (EffectiveReaderDisplaySize(deps, next_page, state.target_state.rotation, target_scale,
                                   next_display_w, next_display_h)) {
      deps.request_reader_async_render(next_page, target_scale, next_display_w, next_display_h, true);
    }
  } else if (prev_page >= 0 &&
             !MatchingReaderRenderCache(deps, prev_page, state.target_state.rotation, target_scale, quality)) {
    int prev_display_w = 0;
    int prev_display_h = 0;
    if (EffectiveReaderDisplaySize(deps, prev_page, state.target_state.rotation, target_scale,
                                   prev_display_w, prev_display_h)) {
      deps.request_reader_async_render(prev_page, target_scale, prev_display_w, prev_display_h, true);
    }
  }
  return true;
}

void ClampReaderScroll(ReaderRenderControllerDeps &deps) {
  int display_w = 0;
  int display_h = 0;
  if (!CurrentReaderDisplaySize(deps, display_w, display_h)) {
    if (const ReaderRenderCache *cache = VisibleReaderRenderCache(deps)) {
      display_w = cache->display_w;
      display_h = cache->display_h;
    } else {
      deps.progress.scroll_x = 0;
      deps.progress.scroll_y = 0;
      return;
    }
  }
  const int max_x = std::max(0, display_w - deps.screen_w);
  const int max_y = std::max(0, display_h - deps.screen_h);
  deps.progress.scroll_x = std::clamp(deps.progress.scroll_x, 0, max_x);
  deps.progress.scroll_y = std::clamp(deps.progress.scroll_y, 0, max_y);
}

void SetReaderScrollEdge(ReaderRenderControllerDeps &deps, bool top) {
  ClampReaderScroll(deps);
  int display_w = 0;
  int display_h = 0;
  if (!CurrentReaderDisplaySize(deps, display_w, display_h)) {
    if (const ReaderRenderCache *cache = VisibleReaderRenderCache(deps)) {
      display_w = cache->display_w;
      display_h = cache->display_h;
    }
  }
  const int max_x = std::max(0, display_w - deps.screen_w);
  const int max_y = std::max(0, display_h - deps.screen_h);
  const auto [axis, sign] = CurrentReaderAxisSign(deps);
  if (axis == 1) {
    deps.progress.scroll_x = 0;
    deps.progress.scroll_y = top ? (sign > 0 ? 0 : max_y) : (sign > 0 ? max_y : 0);
  } else {
    deps.progress.scroll_y = 0;
    deps.progress.scroll_x = top ? (sign > 0 ? 0 : max_x) : (sign > 0 ? max_x : 0);
  }
}

void CommitReaderTargetView(ReaderRenderControllerDeps &deps, ReaderViewState next_state,
                            bool align_to_edge, bool edge_top) {
  auto &state = deps.render_state;
  if (!deps.reader_is_open()) return;
  const int page_count = std::max(1, deps.reader_page_count());
  next_state.page = std::clamp(next_state.page, 0, page_count - 1);
  next_state.zoom = std::clamp(next_state.zoom, 0.25f, 6.0f);
  next_state.rotation %= 360;
  if (next_state.rotation < 0) next_state.rotation += 360;
  next_state.rotation = ((next_state.rotation + 45) / 90) * 90;
  next_state.rotation %= 360;

  const ReaderViewState previous_state = state.target_state;
  const bool target_changed = next_state != previous_state;
  const bool page_changed = next_state.page != previous_state.page;
  if (target_changed) {
    BumpReaderAsyncTargetSerial(deps.async_state);
    state.ready_state = ReaderViewState{};
    state.ready_state_valid = false;
  }
  if (page_changed) {
    deps.reader_set_page(next_state.page);
    deps.progress.scroll_x = 0;
    deps.progress.scroll_y = 0;
  }
  deps.progress.rotation = next_state.rotation;
  deps.progress.zoom = next_state.zoom;
  state.target_state = next_state;

  if (align_to_edge) SetReaderScrollEdge(deps, edge_top);
  else ClampReaderScroll(deps);

  if (page_changed) {
    state.adaptive_render.pending_page_active = false;
    state.adaptive_render.fast_flip_mode = false;
  }
}

void QueueReaderPageFlip(ReaderRenderControllerDeps &deps, int page_action,
                         uint32_t fast_flip_threshold_ms, uint32_t page_flip_debounce_ms) {
  auto &adaptive_render = deps.render_state.adaptive_render;
  const auto &target_state = deps.render_state.target_state;
  if (page_action == 0 || !deps.reader_is_open()) return;
  const uint32_t now = SDL_GetTicks();
  const bool rapid_flip = adaptive_render.pending_page_active ||
                          (adaptive_render.last_page_flip_tick > 0 &&
                           now - adaptive_render.last_page_flip_tick <= fast_flip_threshold_ms);
  const int page_count = std::max(1, deps.reader_page_count());
  const int base_page = adaptive_render.pending_page_active ? adaptive_render.pending_page : target_state.page;
  const int target_page = std::clamp(base_page + page_action, 0, page_count - 1);
  if (target_page == base_page) return;
  adaptive_render.pending_page_active = true;
  adaptive_render.pending_page = target_page;
  adaptive_render.pending_page_top = page_action > 0;
  adaptive_render.pending_page_commit_tick = now + page_flip_debounce_ms;
  adaptive_render.fast_flip_mode = rapid_flip;
  adaptive_render.last_page_flip_tick = now;
}

void FlushPendingReaderPageFlip(ReaderRenderControllerDeps &deps) {
  auto &adaptive_render = deps.render_state.adaptive_render;
  if (!adaptive_render.pending_page_active || !deps.reader_is_open()) return;
  const uint32_t now = SDL_GetTicks();
  if (now < adaptive_render.pending_page_commit_tick) return;
  adaptive_render.pending_page_active = false;
  adaptive_render.fast_flip_mode = false;
  const ReaderViewState current_view = deps.render_state.target_state;
  const int current_page = current_view.page;
  const int target_page = std::clamp(adaptive_render.pending_page, 0,
                                     std::max(0, deps.reader_page_count() - 1));
  if (target_page == current_page) return;
  ReaderViewState next_view = current_view;
  next_view.page = target_page;
  CommitReaderTargetView(deps, next_view, true, adaptive_render.pending_page_top);
}

void FlushPendingReaderPageFlipNow(ReaderRenderControllerDeps &deps) {
  if (!deps.render_state.adaptive_render.pending_page_active) return;
  deps.render_state.adaptive_render.pending_page_commit_tick = 0;
  FlushPendingReaderPageFlip(deps);
}

void ScrollReaderByDir(ReaderRenderControllerDeps &deps, int dir, int step_px) {
  auto &state = deps.render_state;
  if (!EnsureReaderRender(deps)) return;
  state.adaptive_render.last_scroll_dir = (dir >= 0) ? 1 : -1;
  const auto [axis, sign] = CurrentReaderAxisSign(deps);
  int display_w = 0;
  int display_h = 0;
  if (!CurrentReaderDisplaySize(deps, display_w, display_h)) {
    if (const ReaderRenderCache *cache = VisibleReaderRenderCache(deps)) {
      display_w = cache->display_w;
      display_h = cache->display_h;
    } else {
      return;
    }
  }
  const int max_x = std::max(0, display_w - deps.screen_w);
  const int max_y = std::max(0, display_h - deps.screen_h);
  int *pos = (axis == 1) ? &deps.progress.scroll_y : &deps.progress.scroll_x;
  const int max_pos = (axis == 1) ? max_y : max_x;
  const int old = *pos;
  const int delta = step_px * dir * sign;
  *pos = std::clamp(*pos + delta, 0, max_pos);
  if (*pos != old) return;
  if (deps.hold_cooldown > 0.0f) return;

  const ReaderViewState current_view = state.target_state;
  const int target_page = std::clamp(current_view.page + (dir > 0 ? 1 : -1), 0,
                                     std::max(0, deps.reader_page_count() - 1));
  if (target_page == current_view.page) return;
  ReaderViewState next_view = current_view;
  next_view.page = target_page;
  CommitReaderTargetView(deps, next_view, true, dir > 0);
  deps.hold_cooldown = 0.16f;
}
