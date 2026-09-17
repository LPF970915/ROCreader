#pragma once

#include "animation.h"
#include "shelf_runtime.h"

struct LayoutMetrics;

struct ShelfSceneState {
  std::string current_folder;
  std::unordered_map<std::string, int> folder_focus;
  int focus_index = 0;
  int shelf_page = 0;
  int nav_selected_index = 0;
  std::unordered_map<int, GridItemAnim> grid_item_anims;

  int page_anim_from = 0;
  int page_anim_to = 0;
  int page_anim_dir = 0;
  bool page_animating = false;
  animation::TweenFloat page_slide{0.0f};
  bool any_grid_animating = false;

  int title_focus_index = -1;
  float title_marquee_wait = 0.0f;
  float title_marquee_offset = 0.0f;
  bool title_marquee_active = false;
};

struct ShelfSceneInputServices {
  std::function<bool()> focused_title_needs_marquee;
  std::function<void()> clear_cover_cache;
  std::function<void()> rebuild_shelf_items;
  std::function<void(const std::string &)> add_favorite;
  std::function<void(const std::string &)> remove_favorite;
  std::function<ShelfCategory()> current_category;
  std::function<bool(const BookItem &)> on_open_book;
  std::function<bool(const BookItem &)> mark_remote_for_local;
  std::function<bool(const BookItem &)> unmark_remote_for_local;
  std::function<int()> nav_item_count;
};

struct ShelfSceneInputContext {
  const InputManager &input;
  ShelfRuntimeState &shelf_runtime;
  ShelfSceneState &scene_state;
  int grid_cols = 0;
  float dt = 0.0f;
  bool animations_enabled = false;
  float page_slide_duration_sec = 0.0f;
  float title_marquee_pause_sec = 0.0f;
  float title_marquee_speed_px = 0.0f;
  ShelfSceneInputServices services;
};

struct ShelfSceneRenderServices {
  std::function<void(int, int, int, int, SDL_Color, bool)> draw_rect;
  std::function<void(SDL_Texture *, int &, int &)> get_texture_size;
  std::function<SDL_Texture *(const BookItem &)> get_cover_texture;
  std::function<SDL_Texture *(const BookItem &)> get_cached_cover_texture;
  std::function<void(int)> ensure_page_cover_textures;
  std::function<void(const std::string &, SDL_Color, int &, int &, SDL_Texture *&)> get_text_texture;
  std::function<std::string(const std::string &, int, const std::function<int(const std::string &)> &)>
      get_title_ellipsized;
  std::function<std::string(const BookItem &)> shelf_title_text;
  std::function<std::string(int)> nav_label_text;
  std::function<int()> nav_item_count;
  std::function<bool()> online_shelf_active;
  std::function<bool(const BookItem &)> remote_cover_loading;
  std::function<std::string(const BookItem &)> remote_book_status_text;
  std::function<float(const BookItem &)> remote_book_status_progress;
  std::function<void(SDL_Texture *)> forget_texture_size;
};

struct ShelfSceneRenderServiceCallbacks {
  std::function<void(int, int, int, int, SDL_Color, bool)> draw_rect;
  std::function<void(SDL_Texture *, int &, int &)> get_texture_size;
  std::function<SDL_Texture *(const BookItem &)> get_cover_texture;
  std::function<SDL_Texture *(const BookItem &)> get_cached_cover_texture;
  std::function<void(int)> ensure_page_cover_textures;
  std::function<TextCacheEntry *(const std::string &, SDL_Color)> get_text_texture;
  std::function<std::string(const std::string &, int, const std::function<int(const std::string &)> &)>
      get_title_ellipsized;
  std::function<std::string(const BookItem &)> shelf_title_text;
  std::function<std::string(int)> nav_label_text;
  std::function<int()> nav_item_count;
  std::function<bool()> online_shelf_active;
  std::function<bool(const BookItem &)> remote_cover_loading;
  std::function<std::string(const BookItem &)> remote_book_status_text;
  std::function<float(const BookItem &)> remote_book_status_progress;
  std::function<void(SDL_Texture *)> forget_texture_size;
};

ShelfSceneRenderServices MakeShelfSceneRenderServices(ShelfSceneRenderServiceCallbacks callbacks);

struct ShelfSceneRenderContext {
  SDL_Renderer *renderer = nullptr;
  UiAssets &ui_assets;
  UiTextCacheState *ui_text_cache = nullptr;
  ShelfRuntimeState &shelf_runtime;
  ShelfSceneState &scene_state;
  ShelfRenderCache &render_cache;
  ShelfLayoutMetrics layout;
  int grid_cols = 0;
  int items_per_page = 0;
  float dt = 0.0f;
  bool animate_enabled = false;
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
  ShelfSceneRenderServices services;
};

ShelfLayoutMetrics MakeShelfSceneLayoutMetrics(const LayoutMetrics &layout);

class ShelfScene {
public:
  void ResetToCategoryRoot(ShelfSceneState &state) const;
  void HandleInput(const ShelfSceneInputContext &context) const;
  void TickAnimations(ShelfSceneState &state, float dt, bool enabled) const;
  void Draw(const ShelfSceneRenderContext &context) const;
};
