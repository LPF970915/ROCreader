#pragma once

#include "book_scanner.h"
#include "input_manager.h"
#include "ui_assets.h"
#include "ui_text_cache.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ShelfCategory {
  AllComics = 0,
  AllBooks = 1,
  Collections = 2,
  History = 3,
};

struct ShelfScanCacheEntry {
  std::vector<BookItem> items;
  uint32_t last_scan_tick = 0;
};

struct ShelfRuntimeState {
  std::vector<BookItem> items;
  std::unordered_map<std::string, ShelfScanCacheEntry> scan_cache;
  uint64_t content_version = 1;
};

struct ShelfRenderCache {
  SDL_Texture *texture = nullptr;
  int focus_index = -1;
  int shelf_page = -1;
  int nav_selected_index = -1;
  uint64_t content_version = 0;
};

struct GridItemAnim {
  float x = 0.0f;
  float y = 0.0f;
  float cx = 0.0f;
  float cy = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  float alpha = 255.0f;

  float tcx = 0.0f;
  float tcy = 0.0f;
  float tw = 0.0f;
  float th = 0.0f;
  float t_alpha = 255.0f;

  bool initialized = false;

  void SnapToTarget() {
    cx = tcx;
    cy = tcy;
    w = tw;
    h = th;
    alpha = t_alpha;
    x = cx - w * 0.5f;
    y = cy - h * 0.5f;
    initialized = true;
  }

  void Update(float dt, float move_speed_x, float move_speed_y, float move_tail_ratio,
              float move_tail_min_mul, float scale_speed_w, float scale_speed_h,
              float scale_tail_ratio, float scale_tail_min_mul) {
    if (!initialized) {
      SnapToTarget();
      return;
    }
    auto move_linear_with_tail = [&](float current, float target, float speed_px_s, float base_span,
                                     float tail_ratio, float tail_min_mul) {
      const float delta = target - current;
      const float remain = std::abs(delta);
      const float span = std::max(1.0f, std::abs(base_span));
      const float tail_band = span * tail_ratio;
      float speed_mul = 1.0f;
      if (remain < tail_band) {
        const float u = remain / std::max(1.0f, tail_band);
        speed_mul = tail_min_mul + (1.0f - tail_min_mul) * u;
      }
      const float max_step = speed_px_s * speed_mul * dt;
      if (std::abs(delta) <= max_step) return target;
      return current + ((delta > 0.0f) ? max_step : -max_step);
    };
    cx = move_linear_with_tail(cx, tcx, move_speed_x, tw, move_tail_ratio, move_tail_min_mul);
    cy = move_linear_with_tail(cy, tcy, move_speed_y, th, move_tail_ratio, move_tail_min_mul);
    w = move_linear_with_tail(w, tw, scale_speed_w, tw, scale_tail_ratio, scale_tail_min_mul);
    h = move_linear_with_tail(h, th, scale_speed_h, th, scale_tail_ratio, scale_tail_min_mul);
    x = cx - w * 0.5f;
    y = cy - h * 0.5f;
    alpha = t_alpha;
  }

  bool IsAnimating() const {
    return std::abs(tcx - cx) > 0.25f || std::abs(tcy - cy) > 0.25f || std::abs(tw - w) > 0.25f ||
           std::abs(th - h) > 0.25f || std::abs(t_alpha - alpha) > 0.8f;
  }
};

struct ShelfRuntimeDeps {
  std::function<std::string(const std::string &)> normalize_path_key;
  std::function<std::string(const std::string &)> get_lower_ext;
  std::function<bool(const std::string &)> favorites_contains;
  std::function<bool(const std::string &)> history_contains;
  std::function<std::vector<std::string>()> favorites_ordered_paths;
  std::function<std::vector<std::string>()> history_ordered_paths;
  uint32_t cache_ttl_ms = 0;
  size_t max_cache_entries = 0;
};

struct ShelfRuntimeInputDeps {
  const InputManager &input;
  ShelfRuntimeState &shelf_runtime;
  std::unordered_map<std::string, int> &folder_focus;
  std::string &current_folder;
  int &focus_index;
  int &shelf_page;
  int &nav_selected_index;
  int k_grid_cols = 0;
  float dt = 0.0f;
  bool animations_enabled = false;
  bool &page_animating;
  int &page_anim_from;
  int &page_anim_to;
  int &page_anim_dir;
  int &title_focus_index;
  bool &title_marquee_active;
  float &title_marquee_wait;
  float &title_marquee_offset;
  float title_marquee_pause_sec = 0.0f;
  float title_marquee_speed_px = 0.0f;
  std::unordered_map<int, GridItemAnim> &grid_item_anims;
  std::function<bool()> focused_title_needs_marquee;
  std::function<void()> clear_cover_cache;
  std::function<void()> rebuild_shelf_items;
  std::function<void()> reset_page_slide;
  std::function<void()> animate_page_slide;
  std::function<void(const std::string &)> add_favorite;
  std::function<void(const std::string &)> remove_favorite;
  std::function<ShelfCategory()> current_category;
  std::function<bool(const BookItem &)> on_open_book;
};

struct ShelfLayoutMetrics {
  int screen_w = 0;
  int screen_h = 0;
  int top_bar_y = 0;
  int top_bar_h = 0;
  int bottom_bar_y = 0;
  int bottom_bar_h = 0;
  int cover_w = 0;
  int cover_h = 0;
  int card_frame_w = 0;
  int card_frame_h = 0;
  int grid_gap_x = 0;
  int grid_gap_y = 0;
  int grid_start_x = 0;
  int grid_start_y = 0;
  int title_overlay_h = 0;
  int title_text_pad_x = 0;
  int title_text_pad_bottom = 0;
  int title_marquee_gap_px = 0;
  int nav_l1_x = 0;
  int nav_l1_y = 0;
  int nav_r1_x = 0;
  int nav_r1_y = 0;
  int nav_start_x = 0;
  int nav_slot_w = 0;
  int nav_y = 0;
};

struct ShelfRuntimeRenderDeps {
  SDL_Renderer *renderer = nullptr;
  UiAssets &ui_assets;
  UiTextCacheState *ui_text_cache = nullptr;
  ShelfRuntimeState &shelf_runtime;
  ShelfRenderCache &render_cache;
  std::unordered_map<int, GridItemAnim> &grid_item_anims;
  ShelfLayoutMetrics layout;
  int k_grid_cols = 0;
  int k_items_per_page = 0;
  float dt = 0.0f;
  bool animate_enabled = false;
  bool &any_grid_animating;
  bool page_animating = false;
  int page_anim_from = 0;
  int page_anim_to = 0;
  int page_anim_dir = 0;
  float page_slide_value = 0.0f;
  int shelf_page = 0;
  int focus_index = 0;
  int nav_selected_index = 0;
  float title_marquee_offset = 0.0f;
  bool renderer_supports_target_textures = false;
  uint64_t shelf_content_version = 0;
  float unfocused_alpha = 255.0f;
  float focus_cover_w = 0.0f;
  float focus_cover_h = 0.0f;
  float cover_aspect = 0.0f;
  float card_move_linear_speed_x = 0.0f;
  float card_move_linear_speed_y = 0.0f;
  float card_move_tail_ratio = 0.0f;
  float card_move_tail_min_mul = 0.0f;
  float card_scale_linear_speed_w = 0.0f;
  float card_scale_linear_speed_h = 0.0f;
  float card_scale_tail_ratio = 0.0f;
  float card_scale_tail_min_mul = 0.0f;
  std::function<void(int, int, int, int, SDL_Color, bool)> draw_rect;
  std::function<void(SDL_Texture *, int &, int &)> get_texture_size;
  std::function<SDL_Texture *(const BookItem &)> get_cover_texture;
  std::function<void(const std::string &, SDL_Color, int &, int &, SDL_Texture *&)> get_text_texture;
  std::function<std::string(const std::string &, int, const std::function<int(const std::string &)> &)>
      get_title_ellipsized;
  std::function<std::string(const BookItem &)> shelf_title_text;
  std::function<void(SDL_Texture *)> forget_texture_size;
};

ShelfCategory ClampShelfCategory(int nav_selected_index);
std::string MakeShelfScanCacheKey(ShelfCategory category, const std::string &folder,
                                  const std::vector<std::string> &books_roots,
                                  const ShelfRuntimeDeps &deps);
void PruneShelfScanCache(ShelfRuntimeState &state, size_t max_cache_entries);
bool ShelfMatchCategory(const BookItem &item, ShelfCategory category, const ShelfRuntimeDeps &deps);
std::vector<BookItem> ScanShelfBaseItems(ShelfRuntimeState &state, ShelfCategory category,
                                         const std::string &current_folder,
                                         const std::vector<std::string> &books_roots,
                                         const ShelfRuntimeDeps &deps);
void RebuildShelfItems(ShelfRuntimeState &state, ShelfCategory category, const std::string &current_folder,
                       const std::vector<std::string> &books_roots, const ShelfRuntimeDeps &deps);
void HandleShelfInput(ShelfRuntimeInputDeps &deps);
void DestroyShelfRenderCache(ShelfRenderCache &cache,
                             const std::function<void(SDL_Texture *)> &forget_texture_size);
void InvalidateShelfRenderCache(ShelfRenderCache &cache,
                                const std::function<void(SDL_Texture *)> &forget_texture_size);
void DrawShelfRuntime(ShelfRuntimeRenderDeps &deps);
