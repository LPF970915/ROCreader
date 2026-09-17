#include "shelf_scene.h"

#include "app_layout.h"

ShelfLayoutMetrics MakeShelfSceneLayoutMetrics(const LayoutMetrics &layout) {
  return ShelfLayoutMetrics{
      layout.screen_w,
      layout.screen_h,
      layout.top_bar_y,
      layout.top_bar_h,
      layout.bottom_bar_y,
      layout.bottom_bar_h,
      layout.cover_w,
      layout.cover_h,
      layout.card_frame_w,
      layout.card_frame_h,
      layout.grid_gap_x,
      layout.grid_gap_y,
      layout.grid_start_x,
      layout.grid_start_y,
      layout.title_overlay_h,
      layout.title_text_pad_x,
      layout.title_text_pad_bottom,
      layout.title_marquee_gap_px,
      layout.nav_l1_x,
      layout.nav_l1_y,
      layout.nav_r1_x,
      layout.nav_r1_y,
      layout.nav_start_x,
      layout.nav_slot_w,
      layout.nav_y,
      layout.nav_pill_h,
      layout.nav_bar_y + layout.nav_bar_h,
  };
}

void ShelfScene::ResetToCategoryRoot(ShelfSceneState &state) const {
  state.current_folder.clear();
  state.focus_index = 0;
  state.shelf_page = 0;
  state.page_animating = false;
  state.page_slide.Snap(0.0f);
  state.grid_item_anims.clear();
}

void ShelfScene::HandleInput(const ShelfSceneInputContext &context) const {
  ShelfRuntimeInputDeps deps{
      context.input,
      context.shelf_runtime,
      context.scene_state.folder_focus,
      context.scene_state.current_folder,
      context.scene_state.focus_index,
      context.scene_state.shelf_page,
      context.scene_state.nav_selected_index,
      context.grid_cols,
      context.dt,
      context.animations_enabled,
      context.scene_state.page_animating,
      context.scene_state.page_anim_from,
      context.scene_state.page_anim_to,
      context.scene_state.page_anim_dir,
      context.scene_state.title_focus_index,
      context.scene_state.title_marquee_active,
      context.scene_state.title_marquee_wait,
      context.scene_state.title_marquee_offset,
      context.title_marquee_pause_sec,
      context.title_marquee_speed_px,
      context.scene_state.grid_item_anims,
      context.services.focused_title_needs_marquee,
      context.services.clear_cover_cache,
      context.services.rebuild_shelf_items,
      [&]() { context.scene_state.page_slide.Snap(0.0f); },
      [&]() { context.scene_state.page_slide.AnimateTo(1.0f, context.page_slide_duration_sec, animation::Ease::OutCubic); },
      context.services.add_favorite,
      context.services.remove_favorite,
      context.services.current_category,
      context.services.on_open_book,
      context.services.mark_remote_for_local,
      context.services.unmark_remote_for_local,
      context.services.nav_item_count,
  };
  HandleShelfInput(deps);
}

ShelfSceneRenderServices MakeShelfSceneRenderServices(ShelfSceneRenderServiceCallbacks callbacks) {
  return ShelfSceneRenderServices{
      std::move(callbacks.draw_rect),
      std::move(callbacks.get_texture_size),
      std::move(callbacks.get_cover_texture),
      std::move(callbacks.get_cached_cover_texture),
      std::move(callbacks.ensure_page_cover_textures),
      [get_text_texture = std::move(callbacks.get_text_texture)](
          const std::string &text, SDL_Color color, int &w, int &h, SDL_Texture *&tex) {
        TextCacheEntry *entry = get_text_texture ? get_text_texture(text, color) : nullptr;
        tex = entry ? entry->texture : nullptr;
        w = entry ? entry->w : 0;
        h = entry ? entry->h : 0;
      },
      std::move(callbacks.get_title_ellipsized),
      std::move(callbacks.shelf_title_text),
      std::move(callbacks.nav_label_text),
      std::move(callbacks.nav_item_count),
      std::move(callbacks.online_shelf_active),
      std::move(callbacks.remote_cover_loading),
      std::move(callbacks.remote_book_status_text),
      std::move(callbacks.remote_book_status_progress),
      std::move(callbacks.forget_texture_size),
  };
}

void ShelfScene::TickAnimations(ShelfSceneState &state, float dt, bool enabled) const {
  state.any_grid_animating = false;
  if (enabled) state.page_slide.Update(ShelfAnimationDelta(dt));
  if (!enabled || (state.page_animating && !state.page_slide.IsAnimating() &&
                   state.page_slide.Value() >= 0.999f)) {
    state.page_animating = false;
    state.page_slide.Snap(0.0f);
  }
}

void ShelfScene::Draw(const ShelfSceneRenderContext &context) const {
  ShelfRuntimeRenderDeps deps{
      context.renderer,
      context.ui_assets,
      context.ui_text_cache,
      context.shelf_runtime,
      context.render_cache,
      context.scene_state.grid_item_anims,
      context.layout,
      context.grid_cols,
      context.items_per_page,
      context.dt,
      context.animate_enabled,
      context.scene_state.any_grid_animating,
      context.scene_state.page_animating,
      context.scene_state.page_anim_from,
      context.scene_state.page_anim_to,
      context.scene_state.page_anim_dir,
      context.scene_state.page_slide.Value(),
      context.scene_state.shelf_page,
      context.scene_state.focus_index,
      context.scene_state.nav_selected_index,
      context.scene_state.title_marquee_offset,
      context.renderer_supports_target_textures,
      context.shelf_content_version,
      context.unfocused_alpha,
      context.focus_cover_w,
      context.focus_cover_h,
      context.cover_aspect,
      context.card_move_linear_speed_x,
      context.card_move_linear_speed_y,
      context.card_move_tail_ratio,
      context.card_move_tail_min_mul,
      context.card_scale_linear_speed_w,
      context.card_scale_linear_speed_h,
      context.card_scale_tail_ratio,
      context.card_scale_tail_min_mul,
      context.services.draw_rect,
      context.services.get_texture_size,
      context.services.get_cover_texture,
      context.services.get_cached_cover_texture,
      context.services.ensure_page_cover_textures,
      context.services.get_text_texture,
      context.services.get_title_ellipsized,
      context.services.shelf_title_text,
      context.services.nav_label_text,
      context.services.nav_item_count,
      context.services.online_shelf_active,
      context.services.remote_cover_loading,
      context.services.remote_book_status_text,
      context.services.remote_book_status_progress,
      context.services.forget_texture_size,
  };
  DrawShelfRuntime(deps);
}
