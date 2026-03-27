#pragma once

#include "reader_core.h"
#include "reader_runtime_common.h"

#include <functional>
#include <utility>

struct ReaderRenderControllerDeps {
  int screen_w = 0;
  int screen_h = 0;
  ReaderProgress &progress;
  float &hold_cooldown;
  ReaderRenderState &render_state;
  ReaderAsyncState &async_state;
  std::function<bool()> reader_is_open;
  std::function<int()> reader_page_count;
  std::function<void(int)> reader_set_page;
  std::function<bool(int, int &, int &)> reader_page_size_cached;
  std::function<void(ReaderRenderCache &)> destroy_render_cache;
  std::function<bool()> promote_async_render_result;
  std::function<bool(int, float, int, int, bool)> request_reader_async_render;
};

const ReaderRenderCache *VisibleReaderRenderCache(ReaderRenderControllerDeps &deps);
ReaderRenderCache *MatchingReaderRenderCache(ReaderRenderControllerDeps &deps, int page, int rotation,
                                             float target_scale, ReaderRenderQuality quality);
const ReaderRenderCache *VisibleReaderRenderCacheForPage(ReaderRenderControllerDeps &deps, int page,
                                                         int rotation, float target_scale);
bool EffectiveReaderDisplaySize(ReaderRenderControllerDeps &deps, int page, int rotation,
                                float target_scale, int &out_w, int &out_h);
float ReaderTargetScaleForState(ReaderRenderControllerDeps &deps, const ReaderViewState &state);
bool EffectiveReaderDisplaySizeForState(ReaderRenderControllerDeps &deps,
                                        const ReaderViewState &state, int &out_w, int &out_h);
void PruneReaderNeighborCaches(ReaderRenderControllerDeps &deps, int center_page, int rotation,
                               float target_scale);
std::pair<int, int> CurrentReaderAxisSign(const ReaderRenderControllerDeps &deps);
bool CurrentReaderDisplaySize(ReaderRenderControllerDeps &deps, int &out_w, int &out_h);
bool PromoteReadyTargetToDisplay(ReaderRenderControllerDeps &deps, float target_scale,
                                 ReaderRenderQuality quality);
bool EnsureReaderRender(ReaderRenderControllerDeps &deps);
void ClampReaderScroll(ReaderRenderControllerDeps &deps);
void SetReaderScrollEdge(ReaderRenderControllerDeps &deps, bool top);
void CommitReaderTargetView(ReaderRenderControllerDeps &deps, ReaderViewState next_state,
                            bool align_to_edge, bool edge_top);
void QueueReaderPageFlip(ReaderRenderControllerDeps &deps, int page_action,
                         uint32_t fast_flip_threshold_ms, uint32_t page_flip_debounce_ms);
void FlushPendingReaderPageFlip(ReaderRenderControllerDeps &deps);
void FlushPendingReaderPageFlipNow(ReaderRenderControllerDeps &deps);
void ScrollReaderByDir(ReaderRenderControllerDeps &deps, int dir, int step_px);
