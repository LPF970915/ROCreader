#include "epub_runtime.h"

#include "pdf_runtime.h"
#include "reader_render_controller.h"
#include "reader_render_runtime.h"
#include "reader_runtime_common.h"

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

}  // namespace

struct EpubRuntime::Impl {
  SDL_Renderer *renderer = nullptr;
  int screen_w = 720;
  int screen_h = 480;

  ReaderMode backend_mode = ReaderMode::Epub;
  PdfRuntime pdf_placeholder;
  EpubComicReader epub_comic;
  ReaderBackendState backend{backend_mode, pdf_placeholder, epub_comic};
  ReaderRenderState render_state;
  ReaderAsyncState async_state;

  std::string current_book;
  ReaderProgress progress;
  float hold_cooldown = 0.0f;
  std::array<float, kButtonCount> hold_speed{};
  std::array<bool, kButtonCount> long_fired{};

  Impl() { InitReaderAsyncState(async_state); }

  ~Impl() {
    InvalidateAllReaderRenderCaches(render_state, [](SDL_Texture *) {});
    DestroyReaderTexturePool(render_state, [](SDL_Texture *) {});
    ShutdownReaderAsyncState(async_state);
    ClearReaderPageSizeCache(render_state);
  }

  void ResetInputState() {
    hold_cooldown = 0.0f;
    for (auto &value : hold_speed) value = 0.0f;
    for (auto &value : long_fired) value = false;
  }

  void DestroyRenderCache(ReaderRenderCache &cache) {
    DestroyReaderRenderCache(render_state, cache, [](SDL_Texture *) {});
  }

  SDL_Texture *AcquireTexture(int w, int h) {
    return AcquireReaderTexture(render_state, renderer, w, h,
                                [](SDL_Texture *, int, int) {},
                                [](SDL_Texture *) {});
  }

  void ResetRuntimeState() {
    render_state.display_state = ReaderViewState{};
    render_state.target_state = ReaderViewState{};
    render_state.ready_state = ReaderViewState{};
    render_state.display_state_valid = false;
    render_state.ready_state_valid = false;
    render_state.adaptive_render = ReaderAdaptiveRenderState{};
    InvalidateAllReaderRenderCaches(render_state, [](SDL_Texture *) {});
    ClearReaderPageSizeCache(render_state);
    ResetReaderAsyncState(async_state);
  }

  ReaderRenderControllerDeps MakeControllerDeps() {
    return ReaderRenderControllerDeps{
        screen_w,
        screen_h,
        progress,
        hold_cooldown,
        render_state,
        async_state,
        [&]() -> bool { return ReaderIsOpen(backend); },
        [&]() -> int { return ReaderPageCount(backend); },
        [&](int page) { ReaderSetPage(backend, page); },
        [&](int page, int &w, int &h) { return ReaderPageSizeCached(backend, render_state, page, w, h); },
        [&](ReaderRenderCache &cache) { DestroyRenderCache(cache); },
        [&]() {
          ReaderRenderRuntimeDeps runtime_deps{
              backend_mode,
              current_book,
              render_state,
              async_state,
              render_state.display_state,
              render_state.target_state,
              render_state.ready_state,
              render_state.display_state_valid,
              render_state.ready_state_valid,
              [&]() -> bool { return ReaderIsOpen(backend); },
              [&](const ReaderViewState &state) {
                ReaderRenderControllerDeps deps = MakeControllerDeps();
                return ReaderTargetScaleForState(deps, state);
              },
              [&](int w, int h) { return AcquireTexture(w, h); },
              [&](ReaderRenderCache &cache) { DestroyRenderCache(cache); },
          };
          return PromoteAsyncReaderRenderResult(runtime_deps);
        },
        [&](int page, float target_scale, int display_w, int display_h, bool prefetch) {
          ReaderRenderRuntimeDeps runtime_deps{
              backend_mode,
              current_book,
              render_state,
              async_state,
              render_state.display_state,
              render_state.target_state,
              render_state.ready_state,
              render_state.display_state_valid,
              render_state.ready_state_valid,
              [&]() -> bool { return ReaderIsOpen(backend); },
              [&](const ReaderViewState &state) {
                ReaderRenderControllerDeps deps = MakeControllerDeps();
                return ReaderTargetScaleForState(deps, state);
              },
              [&](int w, int h) { return AcquireTexture(w, h); },
              [&](ReaderRenderCache &cache) { DestroyRenderCache(cache); },
          };
          return RequestAsyncReaderRender(runtime_deps, page, target_scale, display_w, display_h, prefetch);
        },
    };
  }
};

EpubRuntime::EpubRuntime() : impl_(new Impl()) {}

EpubRuntime::~EpubRuntime() {
  delete impl_;
  impl_ = nullptr;
}

bool EpubRuntime::Open(SDL_Renderer *renderer, const std::string &path, int screen_w, int screen_h,
                       const EpubRuntimeProgress &initial_progress) {
  Close();
  impl_->renderer = renderer;
  impl_->screen_w = screen_w;
  impl_->screen_h = screen_h;
  if (!impl_->epub_comic.Open(path)) return false;
  impl_->current_book = path;
  impl_->ResetRuntimeState();
  impl_->ResetInputState();
  impl_->epub_comic.SetPage(initial_progress.page);
  impl_->progress.page = impl_->epub_comic.CurrentPage();
  impl_->progress.rotation = initial_progress.rotation;
  impl_->progress.zoom = initial_progress.zoom;
  impl_->progress.scroll_x = initial_progress.scroll_x;
  impl_->progress.scroll_y = initial_progress.scroll_y;
  impl_->render_state.target_state =
      ReaderViewState{impl_->epub_comic.CurrentPage(), initial_progress.zoom, initial_progress.rotation};
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  ClampReaderScroll(deps);
  return true;
}

void EpubRuntime::Close() {
  if (!impl_) return;
  impl_->epub_comic.Close();
  impl_->current_book.clear();
  impl_->ResetRuntimeState();
  impl_->ResetInputState();
  impl_->progress = ReaderProgress{};
}

bool EpubRuntime::IsOpen() const {
  return impl_ && impl_->epub_comic.IsOpen();
}

bool EpubRuntime::HasRealRenderer() const {
  return impl_ && impl_->epub_comic.HasRealRenderer();
}

const char *EpubRuntime::BackendName() const {
  return impl_ ? impl_->epub_comic.BackendName() : "none";
}

bool EpubRuntime::IsRenderPending() const {
  return impl_ && impl_->render_state.adaptive_render.pending_page_active;
}

void EpubRuntime::UpdateViewport(int screen_w, int screen_h) {
  if (!impl_) return;
  impl_->screen_w = screen_w;
  impl_->screen_h = screen_h;
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  ClampReaderScroll(deps);
}

void EpubRuntime::Tick() {
  if (!impl_ || !impl_->epub_comic.IsOpen()) return;
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  FlushPendingReaderPageFlip(deps);
}

void EpubRuntime::CommitPendingNavigation() {
  if (!impl_ || !impl_->epub_comic.IsOpen()) return;
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  FlushPendingReaderPageFlipNow(deps);
}

void EpubRuntime::HandleInput(const InputManager &input, float dt, int tap_step_px) {
  if (!impl_ || !impl_->epub_comic.IsOpen()) return;
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  ReaderViewState &target_state = impl_->render_state.target_state;

  if (input.IsJustPressed(Button::L2) || input.IsJustPressed(Button::R2) ||
      input.IsJustPressed(Button::L1) || input.IsJustPressed(Button::R1) ||
      input.IsJustPressed(Button::A)) {
    FlushPendingReaderPageFlipNow(deps);
  }
  if (input.IsJustPressed(Button::L2)) {
    ReaderViewState next_view = target_state;
    next_view.rotation = (next_view.rotation + 270) % 360;
    CommitReaderTargetView(deps, next_view, true, true);
  }
  if (input.IsJustPressed(Button::R2)) {
    ReaderViewState next_view = target_state;
    next_view.rotation = (next_view.rotation + 90) % 360;
    CommitReaderTargetView(deps, next_view, true, true);
  }
  if (input.IsJustPressed(Button::L1)) {
    ReaderViewState next_view = target_state;
    next_view.zoom = std::max(0.25f, next_view.zoom / 1.1f);
    CommitReaderTargetView(deps, next_view, false, true);
  }
  if (input.IsJustPressed(Button::R1)) {
    ReaderViewState next_view = target_state;
    next_view.zoom = std::min(6.0f, next_view.zoom * 1.1f);
    CommitReaderTargetView(deps, next_view, false, true);
  }
  if (input.IsJustPressed(Button::A)) {
    ReaderViewState next_view = target_state;
    next_view.zoom = 1.0f;
    CommitReaderTargetView(deps, next_view, false, true);
    impl_->progress.scroll_x = 0;
    impl_->progress.scroll_y = 0;
    ClampReaderScroll(deps);
  }

  std::array<Button, 4> dirs = {Button::Up, Button::Down, Button::Left, Button::Right};
  for (Button b : dirs) {
    int bi = static_cast<int>(b);
    int long_dir = ReaderScrollDirForButton(target_state.rotation, b);
    if (long_dir == 0) {
      impl_->hold_speed[bi] = 0.0f;
      continue;
    }
    if (input.IsPressed(b)) {
      const float hold = input.HoldTime(b);
      float delay = (target_state.rotation == 0) ? 0.33f : 0.28f;
      float speed_min = (target_state.rotation == 0) ? 95.0f : 120.0f;
      float speed_max = (target_state.rotation == 0) ? 500.0f : 680.0f;
      float speed_accel = (target_state.rotation == 0) ? 620.0f : 920.0f;
      if (hold >= delay) {
        FlushPendingReaderPageFlipNow(deps);
        impl_->long_fired[bi] = true;
        impl_->hold_speed[bi] = (impl_->hold_speed[bi] <= 0.0f)
                                    ? speed_min
                                    : std::min(speed_max, impl_->hold_speed[bi] + speed_accel * dt);
        const int step_px = std::max(1, static_cast<int>(impl_->hold_speed[bi] * dt));
        ScrollReaderByDir(deps, long_dir, step_px);
      } else {
        impl_->hold_speed[bi] = 0.0f;
      }
    } else {
      impl_->hold_speed[bi] = 0.0f;
    }
  }

  for (Button b : dirs) {
    int bi = static_cast<int>(b);
    if (!input.IsJustReleased(b)) continue;
    impl_->hold_speed[bi] = 0.0f;
    if (impl_->long_fired[bi]) {
      impl_->long_fired[bi] = false;
      continue;
    }
    const int tap_dir = ReaderScrollDirForButton(target_state.rotation, b);
    if (tap_dir != 0) {
      FlushPendingReaderPageFlipNow(deps);
      ScrollReaderByDir(deps, tap_dir, tap_step_px);
    } else {
      const int page_action = ReaderTapPageActionForButton(target_state.rotation, b);
      if (page_action > 0) QueueReaderPageFlip(deps, 1, 200, 150);
      else if (page_action < 0) QueueReaderPageFlip(deps, -1, 200, 150);
    }
  }
}

void EpubRuntime::Draw(SDL_Renderer *renderer,
                       const std::function<void(const std::string &)> &draw_loading_label,
                       const std::function<void(const std::string &)> &draw_fast_flip_label) {
  if (!impl_ || !impl_->epub_comic.IsOpen()) return;
  impl_->renderer = renderer;
  ReaderRenderControllerDeps deps = impl_->MakeControllerDeps();
  EnsureReaderRender(deps);
  const ReaderViewState draw_state =
      impl_->render_state.display_state_valid ? impl_->render_state.display_state : impl_->render_state.target_state;
  const float draw_scale =
      (impl_->render_state.display_state_valid && impl_->render_state.render_cache.texture
           ? impl_->render_state.render_cache.scale
           : ReaderTargetScaleForState(deps, draw_state));
  const bool showing_placeholder =
      (!impl_->render_state.display_state_valid || impl_->render_state.display_state != impl_->render_state.target_state);
  bool drew_reader_content = false;

  if (const ReaderRenderCache *cache =
          VisibleReaderRenderCacheForPage(deps, draw_state.page, draw_state.rotation, draw_scale)) {
    const bool cache_matches_display =
        cache->page == draw_state.page &&
        cache->rotation == draw_state.rotation &&
        std::abs(cache->scale - draw_scale) < 0.0005f;
    int draw_x =
        (cache->display_w <= impl_->screen_w)
            ? ((impl_->screen_w - cache->display_w) / 2)
            : (!showing_placeholder && cache_matches_display ? -impl_->progress.scroll_x : 0);
    int draw_y =
        (cache->display_h <= impl_->screen_h)
            ? ((impl_->screen_h - cache->display_h) / 2)
            : (!showing_placeholder && cache_matches_display ? -impl_->progress.scroll_y : 0);
    SDL_Rect dst{draw_x, draw_y, cache->display_w, cache->display_h};
    SDL_RenderCopy(renderer, cache->texture, nullptr, &dst);
    drew_reader_content = true;

    if (showing_placeholder || !cache_matches_display) {
      FillOverlay(renderer, impl_->screen_w, impl_->screen_h, 96);
      if (!impl_->render_state.adaptive_render.pending_page_active) draw_loading_label("Rendering...");
    }
  }

  if (!drew_reader_content && !impl_->render_state.adaptive_render.pending_page_active) {
    FillOverlay(renderer, impl_->screen_w, impl_->screen_h, 96);
    draw_loading_label("Rendering...");
  }

  if (impl_->render_state.adaptive_render.pending_page_active &&
      impl_->render_state.adaptive_render.fast_flip_mode) {
    FillOverlay(renderer, impl_->screen_w, impl_->screen_h, 120);
    const int pending_page = std::clamp(impl_->render_state.adaptive_render.pending_page, 0,
                                        std::max(0, PageCount() - 1));
    const std::string fast_flip_text =
        "Quick Jump: " + std::to_string(pending_page + 1) + " / " + std::to_string(std::max(1, PageCount()));
    draw_fast_flip_label(fast_flip_text);
  }
}

int EpubRuntime::PageCount() const {
  return impl_ ? impl_->epub_comic.PageCount() : 0;
}

bool EpubRuntime::PageSize(int page_index, int &w, int &h) const {
  return impl_ ? ReaderPageSize(impl_->backend, page_index, w, h) : false;
}

int EpubRuntime::CurrentPage() const {
  return impl_ ? impl_->render_state.target_state.page : 0;
}

EpubRuntimeProgress EpubRuntime::Progress() const {
  EpubRuntimeProgress progress;
  if (!impl_) return progress;
  progress.page = impl_->render_state.target_state.page;
  progress.rotation = impl_->render_state.target_state.rotation;
  progress.zoom = impl_->render_state.target_state.zoom;
  progress.scroll_x = impl_->progress.scroll_x;
  progress.scroll_y = impl_->progress.scroll_y;
  return progress;
}
