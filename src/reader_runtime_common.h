#pragma once

#include "epub_comic_reader.h"
#include "pdf_runtime.h"
#include "reader_core.h"

#include <SDL.h>

#include <array>
#include <atomic>
#include <functional>
#include <unordered_map>

struct ReaderBackendState {
  ReaderMode &mode;
  PdfRuntime &pdf_runtime;
  EpubComicReader &epub_comic;
};

struct ReaderRenderState {
  ReaderRenderCache render_cache;
  ReaderRenderCache secondary_render_cache;
  ReaderRenderCache tertiary_render_cache;
  std::array<ReaderTexturePoolEntry, kReaderTexturePoolSize> texture_pool{};
  ReaderViewState display_state;
  ReaderViewState target_state;
  ReaderViewState ready_state;
  bool display_state_valid = false;
  bool ready_state_valid = false;
  ReaderAdaptiveRenderState adaptive_render;
  std::unordered_map<int, SDL_Point> page_size_cache;
};

struct ReaderAsyncState {
  ReaderAsyncRenderJob requested_job;
  ReaderAsyncRenderJob inflight_job;
  ReaderAsyncRenderResult result;
  uint64_t job_serial = 0;
  uint64_t latest_target_serial = 0;
  std::atomic<bool> cancel_requested{false};
  SDL_mutex *mutex = nullptr;
  SDL_cond *cond = nullptr;
  bool worker_running = false;
  Uint32 event_type = static_cast<Uint32>(-1);
  SDL_Thread *thread = nullptr;
};

using TextureSizeForgetFn = std::function<void(SDL_Texture *)>;
using TextureSizeRememberFn = std::function<void(SDL_Texture *, int, int)>;

bool ReaderHasRealRenderer(const ReaderBackendState &state);
bool ReaderIsOpen(const ReaderBackendState &state);
int ReaderPageCount(const ReaderBackendState &state);
int ReaderCurrentPage(const ReaderBackendState &state);
void ReaderSetPage(const ReaderBackendState &state, int page_index);
bool ReaderCurrentPageSize(const ReaderBackendState &state, int &w, int &h);
bool ReaderPageSize(const ReaderBackendState &state, int page_index, int &w, int &h);

void DestroyReaderRenderCache(ReaderRenderState &state, ReaderRenderCache &cache,
                              const TextureSizeForgetFn &forget_texture_size);
void InvalidateAllReaderRenderCaches(ReaderRenderState &state,
                                     const TextureSizeForgetFn &forget_texture_size);
SDL_Texture *AcquireReaderTexture(ReaderRenderState &state, SDL_Renderer *renderer, int w, int h,
                                  const TextureSizeRememberFn &remember_texture_size,
                                  const TextureSizeForgetFn &forget_texture_size);
bool ReaderPageSizeCached(const ReaderBackendState &backend, ReaderRenderState &state,
                          int page_index, int &w, int &h);
void ClearReaderPageSizeCache(ReaderRenderState &state);
void DestroyReaderTexturePool(ReaderRenderState &state,
                              const TextureSizeForgetFn &forget_texture_size);

bool InitReaderAsyncState(ReaderAsyncState &state);
void ShutdownReaderAsyncState(ReaderAsyncState &state);
void ResetReaderAsyncState(ReaderAsyncState &state);
bool TakeReadyReaderAsyncResult(ReaderAsyncState &state, ReaderAsyncRenderResult &out_result);
bool RequestReaderAsyncRender(ReaderAsyncState &state, ReaderAsyncRenderJob job,
                              bool allow_prefetch);
void BumpReaderAsyncTargetSerial(ReaderAsyncState &state);
