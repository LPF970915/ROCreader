#include "reader_runtime_common.h"

#include <SDL.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {
struct ReaderAsyncWorkerCtx {
  SDL_mutex *mutex = nullptr;
  SDL_cond *cond = nullptr;
  bool *running = nullptr;
  ReaderAsyncRenderJob *requested = nullptr;
  ReaderAsyncRenderJob *inflight = nullptr;
  ReaderAsyncRenderResult *result = nullptr;
  uint64_t *latest_target_serial = nullptr;
  std::atomic<bool> *cancel_requested = nullptr;
  Uint32 event_type = 0;
};

int ReaderAsyncWorkerMain(void *userdata) {
  auto *ctx = static_cast<ReaderAsyncWorkerCtx *>(userdata);
  SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
  EpubComicReader worker_epub;
  ReaderMode open_mode = ReaderMode::None;
  std::string open_path;

  for (;;) {
    SDL_LockMutex(ctx->mutex);
    while (*ctx->running && (!ctx->requested->active || ctx->result->ready)) {
      SDL_CondWait(ctx->cond, ctx->mutex);
    }
    if (!*ctx->running) {
      SDL_UnlockMutex(ctx->mutex);
      break;
    }
    ReaderAsyncRenderJob job = *ctx->requested;
    ctx->requested->active = false;
    *ctx->inflight = job;
    if (!job.prefetch && ctx->cancel_requested) {
      ctx->cancel_requested->store(false);
    }
    SDL_UnlockMutex(ctx->mutex);

    bool success = false;
    int src_w = 0;
    int src_h = 0;
    std::vector<unsigned char> rgba;

    if (job.mode == ReaderMode::Epub) {
      if (open_mode != ReaderMode::Epub || open_path != job.path) {
        worker_epub.Close();
        open_mode = ReaderMode::None;
        open_path.clear();
        if (worker_epub.Open(job.path)) {
          open_mode = ReaderMode::Epub;
          open_path = job.path;
        }
      }
      if (open_mode == ReaderMode::Epub) {
        success = worker_epub.RenderPageRGBA(job.page, job.target_scale, rgba, src_w, src_h,
                                             ctx->cancel_requested);
      }
    }

    SDL_LockMutex(ctx->mutex);
    const bool requested_differs =
        ctx->requested->active &&
        (ctx->requested->mode != job.mode ||
         ctx->requested->path != job.path ||
         ctx->requested->page != job.page ||
         ctx->requested->rotation != job.rotation ||
         std::abs(ctx->requested->target_scale - job.target_scale) >= 0.0005f);
    const bool stale_job =
        requested_differs ||
        (!job.prefetch && job.serial != *ctx->latest_target_serial) ||
        (job.prefetch && job.serial < *ctx->latest_target_serial);
    if (*ctx->running && !stale_job) {
      ReaderAsyncRenderResult next_result;
      next_result.ready = true;
      next_result.success = success;
      next_result.mode = job.mode;
      next_result.path = job.path;
      next_result.state = job.state;
      next_result.page = job.page;
      next_result.target_scale = job.target_scale;
      next_result.rotation = job.rotation;
      next_result.display_w = job.display_w;
      next_result.display_h = job.display_h;
      next_result.src_w = src_w;
      next_result.src_h = src_h;
      next_result.rgba = std::move(rgba);
      next_result.serial = job.serial;
      *ctx->result = std::move(next_result);
    }
    *ctx->inflight = ReaderAsyncRenderJob{};
    SDL_UnlockMutex(ctx->mutex);

    if (!stale_job && ctx->event_type != static_cast<Uint32>(-1)) {
      SDL_Event ready_event{};
      ready_event.type = ctx->event_type;
      SDL_PushEvent(&ready_event);
    }
  }

  worker_epub.Close();
  delete ctx;
  return 0;
}
}

bool ReaderHasRealRenderer(const ReaderBackendState &state) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.HasRealRenderer();
  if (state.mode == ReaderMode::Epub) return state.epub_comic.HasRealRenderer();
  return false;
}

bool ReaderIsOpen(const ReaderBackendState &state) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.IsOpen();
  if (state.mode == ReaderMode::Epub) return state.epub_comic.IsOpen();
  return false;
}

int ReaderPageCount(const ReaderBackendState &state) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.PageCount();
  if (state.mode == ReaderMode::Epub) return state.epub_comic.PageCount();
  return 0;
}

int ReaderCurrentPage(const ReaderBackendState &state) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.CurrentPage();
  if (state.mode == ReaderMode::Epub) return state.epub_comic.CurrentPage();
  return 0;
}

void ReaderSetPage(const ReaderBackendState &state, int page_index) {
  if (state.mode == ReaderMode::Pdf) return;
  if (state.mode == ReaderMode::Epub) state.epub_comic.SetPage(page_index);
}

bool ReaderCurrentPageSize(const ReaderBackendState &state, int &w, int &h) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.PageSize(state.pdf_runtime.CurrentPage(), w, h);
  if (state.mode == ReaderMode::Epub) return state.epub_comic.CurrentPageSize(w, h);
  return false;
}

bool ReaderPageSize(const ReaderBackendState &state, int page_index, int &w, int &h) {
  if (state.mode == ReaderMode::Pdf) return state.pdf_runtime.PageSize(page_index, w, h);
  if (state.mode == ReaderMode::Epub) return state.epub_comic.PageSize(page_index, w, h);
  return false;
}

void DestroyReaderRenderCache(ReaderRenderState &state, ReaderRenderCache &cache,
                              const TextureSizeForgetFn &forget_texture_size) {
  if (cache.texture) {
    bool pooled = false;
    for (auto &slot : state.texture_pool) {
      if (slot.texture == cache.texture) {
        slot.in_use = false;
        slot.last_use = SDL_GetTicks();
        pooled = true;
        break;
      }
    }
    if (!pooled) {
      forget_texture_size(cache.texture);
      SDL_DestroyTexture(cache.texture);
    }
  }
  cache = ReaderRenderCache{};
}

void InvalidateAllReaderRenderCaches(ReaderRenderState &state,
                                     const TextureSizeForgetFn &forget_texture_size) {
  DestroyReaderRenderCache(state, state.render_cache, forget_texture_size);
  DestroyReaderRenderCache(state, state.secondary_render_cache, forget_texture_size);
  DestroyReaderRenderCache(state, state.tertiary_render_cache, forget_texture_size);
}

SDL_Texture *AcquireReaderTexture(ReaderRenderState &state, SDL_Renderer *renderer, int w, int h,
                                  const TextureSizeRememberFn &remember_texture_size,
                                  const TextureSizeForgetFn &forget_texture_size) {
  const uint32_t now = SDL_GetTicks();
  for (auto &slot : state.texture_pool) {
    if (slot.texture && !slot.in_use && slot.w == w && slot.h == h) {
      slot.in_use = true;
      slot.last_use = now;
      return slot.texture;
    }
  }

  for (auto &slot : state.texture_pool) {
    if (!slot.texture) {
      SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
      if (!tex) return nullptr;
      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      remember_texture_size(tex, w, h);
      slot.texture = tex;
      slot.w = w;
      slot.h = h;
      slot.in_use = true;
      slot.last_use = now;
      return tex;
    }
  }

  ReaderTexturePoolEntry *evict = nullptr;
  for (auto &slot : state.texture_pool) {
    if (slot.in_use) continue;
    if (!evict || slot.last_use < evict->last_use) evict = &slot;
  }
  if (!evict) return nullptr;
  if (evict->texture) {
    forget_texture_size(evict->texture);
    SDL_DestroyTexture(evict->texture);
  }
  SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!tex) {
    evict->texture = nullptr;
    evict->w = 0;
    evict->h = 0;
    evict->in_use = false;
    evict->last_use = 0;
    return nullptr;
  }
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  remember_texture_size(tex, w, h);
  evict->texture = tex;
  evict->w = w;
  evict->h = h;
  evict->in_use = true;
  evict->last_use = now;
  return tex;
}

bool ReaderPageSizeCached(const ReaderBackendState &backend, ReaderRenderState &state,
                          int page_index, int &w, int &h) {
  auto it = state.page_size_cache.find(page_index);
  if (it != state.page_size_cache.end() && it->second.x > 0 && it->second.y > 0) {
    w = it->second.x;
    h = it->second.y;
    return true;
  }
  if (!ReaderPageSize(backend, page_index, w, h) || w <= 0 || h <= 0) return false;
  state.page_size_cache[page_index] = SDL_Point{w, h};
  return true;
}

void ClearReaderPageSizeCache(ReaderRenderState &state) {
  state.page_size_cache.clear();
}

void DestroyReaderTexturePool(ReaderRenderState &state,
                              const TextureSizeForgetFn &forget_texture_size) {
  for (auto &slot : state.texture_pool) {
    if (!slot.texture) continue;
    forget_texture_size(slot.texture);
    SDL_DestroyTexture(slot.texture);
    slot = ReaderTexturePoolEntry{};
  }
}

bool InitReaderAsyncState(ReaderAsyncState &state) {
  state.mutex = SDL_CreateMutex();
  state.cond = SDL_CreateCond();
  state.worker_running = (state.mutex != nullptr && state.cond != nullptr);
  state.event_type = SDL_RegisterEvents(1);
  if (state.event_type == static_cast<Uint32>(-1)) {
    state.event_type = static_cast<Uint32>(-1);
  }
  if (!state.worker_running) return false;
  auto *ctx = new ReaderAsyncWorkerCtx{
      state.mutex,
      state.cond,
      &state.worker_running,
      &state.requested_job,
      &state.inflight_job,
      &state.result,
      &state.latest_target_serial,
      &state.cancel_requested,
      state.event_type,
  };
  state.thread = SDL_CreateThread(ReaderAsyncWorkerMain, "reader_async", ctx);
  if (!state.thread) {
    state.worker_running = false;
    delete ctx;
    return false;
  }
  return true;
}

void ShutdownReaderAsyncState(ReaderAsyncState &state) {
  if (state.mutex) {
    SDL_LockMutex(state.mutex);
    state.worker_running = false;
    if (state.cond) SDL_CondSignal(state.cond);
    SDL_UnlockMutex(state.mutex);
  }
  if (state.thread) SDL_WaitThread(state.thread, nullptr);
  state.thread = nullptr;
  ResetReaderAsyncState(state);
  if (state.cond) SDL_DestroyCond(state.cond);
  if (state.mutex) SDL_DestroyMutex(state.mutex);
  state.cond = nullptr;
  state.mutex = nullptr;
}

void ResetReaderAsyncState(ReaderAsyncState &state) {
  if (state.mutex) SDL_LockMutex(state.mutex);
  state.requested_job = ReaderAsyncRenderJob{};
  state.inflight_job = ReaderAsyncRenderJob{};
  state.result = ReaderAsyncRenderResult{};
  state.latest_target_serial = 0;
  state.cancel_requested.store(false);
  if (state.mutex) SDL_UnlockMutex(state.mutex);
}

bool TakeReadyReaderAsyncResult(ReaderAsyncState &state, ReaderAsyncRenderResult &out_result) {
  bool has_result = false;
  if (!state.mutex) return false;
  SDL_LockMutex(state.mutex);
  if (state.result.ready) {
    out_result = std::move(state.result);
    state.result = ReaderAsyncRenderResult{};
    has_result = true;
    if (state.cond) SDL_CondSignal(state.cond);
  }
  SDL_UnlockMutex(state.mutex);
  return has_result;
}

bool RequestReaderAsyncRender(ReaderAsyncState &state, ReaderAsyncRenderJob job,
                              bool allow_prefetch) {
  job.serial = ++state.job_serial;
  if (job.serial == 0) job.serial = ++state.job_serial;
  if (!state.mutex) return false;

  SDL_LockMutex(state.mutex);
  if (!job.prefetch) {
    state.latest_target_serial = job.serial;
    state.cancel_requested.store(true);
  }
  const bool inflight_same =
      state.inflight_job.active &&
      state.inflight_job.mode == job.mode &&
      state.inflight_job.path == job.path &&
      state.inflight_job.page == job.page &&
      state.inflight_job.rotation == job.rotation &&
      std::abs(state.inflight_job.target_scale - job.target_scale) < 0.0005f;
  const bool requested_same =
      state.requested_job.active &&
      state.requested_job.mode == job.mode &&
      state.requested_job.path == job.path &&
      state.requested_job.page == job.page &&
      state.requested_job.rotation == job.rotation &&
      std::abs(state.requested_job.target_scale - job.target_scale) < 0.0005f;
  const bool busy_with_target =
      state.requested_job.active || (state.inflight_job.active && !state.inflight_job.prefetch);
  const bool keep_requested = job.prefetch && state.requested_job.active;
  const bool allow_request =
      job.prefetch ? (allow_prefetch && !busy_with_target && !state.inflight_job.active) : true;
  bool accepted = false;
  if (allow_request && !inflight_same && !requested_same && !keep_requested) {
    state.requested_job = std::move(job);
    if (state.cond) SDL_CondSignal(state.cond);
    accepted = true;
  }
  SDL_UnlockMutex(state.mutex);
  return accepted;
}

void BumpReaderAsyncTargetSerial(ReaderAsyncState &state) {
  if (!state.mutex) return;
  SDL_LockMutex(state.mutex);
  state.latest_target_serial = ++state.job_serial;
  state.cancel_requested.store(true);
  SDL_UnlockMutex(state.mutex);
}
