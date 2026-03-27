#include "epub_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {
void FillOverlay(SDL_Renderer *renderer, int w, int h, Uint8 alpha) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
  SDL_Rect rect{0, 0, w, h};
  SDL_RenderFillRect(renderer, &rect);
}
}

void HandleEpubReaderInput(EpubReaderInputDeps &deps) {
  if (deps.ui.mode != ReaderMode::Epub) return;

  if (deps.input.IsJustPressed(Button::L2) || deps.input.IsJustPressed(Button::R2) ||
      deps.input.IsJustPressed(Button::L1) || deps.input.IsJustPressed(Button::R1) ||
      deps.input.IsJustPressed(Button::A)) {
    deps.flush_pending_page_flip_now();
  }
  if (deps.input.IsJustPressed(Button::L2)) {
    ReaderViewState next_view = deps.target_state;
    next_view.rotation = (next_view.rotation + 270) % 360;
    deps.commit_target_view(next_view, true, true);
  }
  if (deps.input.IsJustPressed(Button::R2)) {
    ReaderViewState next_view = deps.target_state;
    next_view.rotation = (next_view.rotation + 90) % 360;
    deps.commit_target_view(next_view, true, true);
  }
  if (deps.input.IsJustPressed(Button::L1)) {
    ReaderViewState next_view = deps.target_state;
    next_view.zoom = std::max(0.25f, next_view.zoom / 1.1f);
    deps.commit_target_view(next_view, false, true);
  }
  if (deps.input.IsJustPressed(Button::R1)) {
    ReaderViewState next_view = deps.target_state;
    next_view.zoom = std::min(6.0f, next_view.zoom * 1.1f);
    deps.commit_target_view(next_view, false, true);
  }
  if (deps.input.IsJustPressed(Button::A)) {
    ReaderViewState next_view = deps.target_state;
    next_view.zoom = 1.0f;
    deps.commit_target_view(next_view, false, true);
    deps.ui.progress.scroll_x = deps.ui.progress.scroll_y = 0;
    deps.clamp_scroll();
  }

  std::array<Button, 4> dirs = {Button::Up, Button::Down, Button::Left, Button::Right};
  for (Button b : dirs) {
    int bi = static_cast<int>(b);
    int long_dir = ReaderScrollDirForButton(deps.target_state.rotation, b);
    if (long_dir == 0) {
      deps.ui.hold_speed[bi] = 0.0f;
      continue;
    }
    if (deps.input.IsPressed(b)) {
      const float hold = deps.input.HoldTime(b);
      float delay = (deps.target_state.rotation == 0) ? 0.33f : 0.28f;
      float speed_min = (deps.target_state.rotation == 0) ? 95.0f : 120.0f;
      float speed_max = (deps.target_state.rotation == 0) ? 500.0f : 680.0f;
      float speed_accel = (deps.target_state.rotation == 0) ? 620.0f : 920.0f;
      if (hold >= delay) {
        deps.flush_pending_page_flip_now();
        deps.ui.long_fired[bi] = true;
        deps.ui.hold_speed[bi] = (deps.ui.hold_speed[bi] <= 0.0f)
                                     ? speed_min
                                     : std::min(speed_max, deps.ui.hold_speed[bi] + speed_accel * deps.dt);
        const int step_px = std::max(1, static_cast<int>(deps.ui.hold_speed[bi] * deps.dt));
        deps.scroll_by_dir(long_dir, step_px);
      } else {
        deps.ui.hold_speed[bi] = 0.0f;
      }
    } else {
      deps.ui.hold_speed[bi] = 0.0f;
    }
  }

  for (Button b : dirs) {
    int bi = static_cast<int>(b);
    if (!deps.input.IsJustReleased(b)) continue;
    deps.ui.hold_speed[bi] = 0.0f;
    if (deps.ui.long_fired[bi]) {
      deps.ui.long_fired[bi] = false;
      continue;
    }
    const int tap_dir = ReaderScrollDirForButton(deps.target_state.rotation, b);
    if (tap_dir != 0) {
      deps.flush_pending_page_flip_now();
      deps.scroll_by_dir(tap_dir, deps.tap_step_px);
    } else {
      const int page_action = ReaderTapPageActionForButton(deps.target_state.rotation, b);
      if (page_action > 0) {
        deps.queue_page_flip(1);
      } else if (page_action < 0) {
        deps.queue_page_flip(-1);
      }
    }
  }
}

void DrawEpubRuntime(EpubRuntimeRenderDeps &deps) {
  deps.ensure_render();
  const ReaderViewState draw_state = deps.display_state_valid ? deps.display_state : deps.target_state;
  const float draw_scale =
      (deps.display_state_valid && deps.render_cache.texture
           ? deps.render_cache.scale
           : deps.reader_target_scale_for_state(draw_state));
  const bool showing_placeholder = (!deps.display_state_valid || deps.display_state != deps.target_state);
  bool drew_reader_content = false;

  if (const ReaderRenderCache *cache =
          deps.visible_reader_render_cache_for_page(draw_state.page, draw_state.rotation, draw_scale)) {
    const bool cache_matches_display =
        cache->page == draw_state.page &&
        cache->rotation == draw_state.rotation &&
        std::abs(cache->scale - draw_scale) < 0.0005f;
    int draw_x =
        (cache->display_w <= deps.screen_w)
            ? ((deps.screen_w - cache->display_w) / 2)
            : (!showing_placeholder && cache_matches_display ? -deps.ui.progress.scroll_x : 0);
    int draw_y =
        (cache->display_h <= deps.screen_h)
            ? ((deps.screen_h - cache->display_h) / 2)
            : (!showing_placeholder && cache_matches_display ? -deps.ui.progress.scroll_y : 0);
    SDL_Rect dst{draw_x, draw_y, cache->display_w, cache->display_h};
    SDL_RenderCopy(deps.renderer, cache->texture, nullptr, &dst);
    drew_reader_content = true;

    if (showing_placeholder || !cache_matches_display) {
      FillOverlay(deps.renderer, deps.screen_w, deps.screen_h, 96);
      if (!deps.adaptive_render.pending_page_active) {
        deps.draw_loading_label("Rendering...");
      }
    }
  }

  if (!drew_reader_content && !deps.adaptive_render.pending_page_active) {
    FillOverlay(deps.renderer, deps.screen_w, deps.screen_h, 96);
    deps.draw_loading_label("Rendering...");
  }

  if (deps.adaptive_render.pending_page_active && deps.adaptive_render.fast_flip_mode) {
    FillOverlay(deps.renderer, deps.screen_w, deps.screen_h, 120);
    const int pending_page = std::clamp(deps.adaptive_render.pending_page, 0, std::max(0, deps.reader_page_count() - 1));
    const std::string fast_flip_text =
        "Quick Jump: " + std::to_string(pending_page + 1) + " / " + std::to_string(std::max(1, deps.reader_page_count()));
    deps.draw_fast_flip_label(fast_flip_text);
  }
}
