#include "book_library_service.h"
#include "shelf_runtime.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace {
// Nested title clips must intersect, then restore, the shelf/caller clip.
class ScopedShelfClip {
 public:
  ScopedShelfClip(SDL_Renderer *renderer, SDL_Rect clip) : renderer_(renderer) {
    enabled_ = SDL_RenderIsClipEnabled(renderer_);
    SDL_RenderGetClipRect(renderer_, &previous_);
    if (enabled_) {
      SDL_Rect intersection{};
      SDL_IntersectRect(&previous_, &clip, &intersection);
      clip = intersection;
    }
    empty_ = SDL_RectEmpty(&clip);
    SDL_RenderSetClipRect(renderer_, &clip);
  }
  ~ScopedShelfClip() {
    SDL_RenderSetClipRect(renderer_, enabled_ ? &previous_ : nullptr);
  }
  ScopedShelfClip(const ScopedShelfClip &) = delete;
  ScopedShelfClip &operator=(const ScopedShelfClip &) = delete;
  bool Empty() const { return empty_; }

 private:
  SDL_Renderer *renderer_;
  SDL_bool enabled_;
  SDL_Rect previous_{};
  bool empty_ = false;
};
}  // namespace

float ShelfAnimationDelta(float dt) {
  // Idle waits and synchronous cover IO are not visible animation frames.
  return std::clamp(dt, 0.0f, 1.0f / 30.0f);
}

ShelfCategory ClampShelfCategory(int nav_selected_index) {
  if (nav_selected_index <= 0) return ShelfCategory::AllComics;
  if (nav_selected_index == 1) return ShelfCategory::AllBooks;
  if (nav_selected_index == 2) return ShelfCategory::Collections;
  return ShelfCategory::History;
}

std::string MakeShelfScanCacheKey(ShelfCategory category, const std::string &folder,
                                  const std::vector<std::string> &books_roots,
                                  const ShelfRuntimeDeps &deps) {
  return book_library_service::MakeScanCacheKey(category, folder, books_roots, deps);
}

void PruneShelfScanCache(ShelfRuntimeState &state, size_t max_cache_entries) {
  book_library_service::PruneScanCache(state, max_cache_entries);
}

bool ShelfMatchCategory(const BookItem &item, ShelfCategory category, const ShelfRuntimeDeps &deps) {
  return book_library_service::MatchCategory(item, category, deps);
}

std::vector<BookItem> ScanShelfBaseItems(ShelfRuntimeState &state, ShelfCategory category,
                                         const std::string &current_folder,
                                         const std::vector<std::string> &books_roots,
                                         const ShelfRuntimeDeps &deps) {
  return book_library_service::ScanBaseItems(state, category, current_folder, books_roots, deps);
}

void RebuildShelfItems(ShelfRuntimeState &state, ShelfCategory category, const std::string &current_folder,
                       const std::vector<std::string> &books_roots, const ShelfRuntimeDeps &deps) {
  book_library_service::RebuildItems(state, category, current_folder, books_roots, deps);
}

void HandleShelfInput(ShelfRuntimeInputDeps &deps) {
  auto sync_focus_with_page = [&]() {
    if (deps.shelf_runtime.items.empty()) {
      deps.focus_index = 0;
      deps.shelf_page = 0;
      return;
    }
    const int max_index = static_cast<int>(deps.shelf_runtime.items.size()) - 1;
    const int max_row_page = max_index / deps.k_grid_cols;
    deps.shelf_page = std::clamp(deps.shelf_page, 0, max_row_page);
    const int col = std::clamp(deps.focus_index % deps.k_grid_cols, 0, deps.k_grid_cols - 1);
    deps.focus_index = std::min(max_index, deps.shelf_page * deps.k_grid_cols + col);
  };

  const int prev_page = deps.shelf_page;
  sync_focus_with_page();

  const bool marquee_needed = deps.focused_title_needs_marquee ? deps.focused_title_needs_marquee() : false;
  if (deps.focus_index != deps.title_focus_index || marquee_needed != deps.title_marquee_active) {
    deps.title_focus_index = deps.focus_index;
    deps.title_marquee_active = marquee_needed;
    deps.title_marquee_wait = deps.title_marquee_pause_sec;
    deps.title_marquee_offset = 0.0f;
  } else if (deps.title_marquee_active) {
    if (deps.title_marquee_wait > 0.0f) {
      deps.title_marquee_wait = std::max(0.0f, deps.title_marquee_wait - deps.dt);
    } else {
      deps.title_marquee_offset += deps.title_marquee_speed_px * deps.dt;
      if (deps.title_marquee_offset > 8192.0f) {
        deps.title_marquee_offset = std::fmod(deps.title_marquee_offset, 8192.0f);
      }
    }
  }

  auto reset_grid_page_state = [&]() {
    deps.page_animating = false;
    if (deps.reset_page_slide) deps.reset_page_slide();
    deps.grid_item_anims.clear();
  };

  const int nav_count = std::max(1, deps.nav_item_count ? deps.nav_item_count() : 4);

  if (deps.input.IsJustPressed(Button::L1)) {
    deps.nav_selected_index = (deps.nav_selected_index + nav_count - 1) % nav_count;
    deps.current_folder.clear();
    if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
    deps.focus_index = 0;
    deps.shelf_page = 0;
    reset_grid_page_state();
    sync_focus_with_page();
  } else if (deps.input.IsJustPressed(Button::R1)) {
    deps.nav_selected_index = (deps.nav_selected_index + 1) % nav_count;
    deps.current_folder.clear();
    if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
    deps.focus_index = 0;
    deps.shelf_page = 0;
    reset_grid_page_state();
    sync_focus_with_page();
  } else if (deps.input.IsJustPressed(Button::B)) {
    if (!deps.current_folder.empty()) {
      deps.current_folder.clear();
      if (deps.clear_cover_cache) deps.clear_cover_cache();
      if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
      deps.focus_index = deps.folder_focus[""];
      deps.shelf_page = deps.shelf_runtime.items.empty() ? 0 : (deps.focus_index / deps.k_grid_cols);
      reset_grid_page_state();
      sync_focus_with_page();
    }
  } else if (deps.input.IsJustPressed(Button::Left) || deps.input.IsRepeated(Button::Left)) {
    if (!deps.shelf_runtime.items.empty()) {
      const int max_index = static_cast<int>(deps.shelf_runtime.items.size()) - 1;
      const int max_row_page = max_index / deps.k_grid_cols;
      int col = deps.focus_index % deps.k_grid_cols;
      if (col > 0) {
        --col;
      } else if (deps.shelf_page > 0) {
        --deps.shelf_page;
        col = deps.k_grid_cols - 1;
      }
      deps.shelf_page = std::clamp(deps.shelf_page, 0, max_row_page);
      deps.focus_index = std::min(max_index, deps.shelf_page * deps.k_grid_cols + col);
    }
  } else if (deps.input.IsJustPressed(Button::Right) || deps.input.IsRepeated(Button::Right)) {
    if (!deps.shelf_runtime.items.empty()) {
      const int max_index = static_cast<int>(deps.shelf_runtime.items.size()) - 1;
      const int max_row_page = max_index / deps.k_grid_cols;
      int col = deps.focus_index % deps.k_grid_cols;
      if (col < deps.k_grid_cols - 1) {
        ++col;
      } else if (deps.shelf_page < max_row_page) {
        ++deps.shelf_page;
        col = 0;
      }
      deps.shelf_page = std::clamp(deps.shelf_page, 0, max_row_page);
      deps.focus_index = std::min(max_index, deps.shelf_page * deps.k_grid_cols + col);
    }
  } else if (deps.input.IsJustPressed(Button::Up) || deps.input.IsRepeated(Button::Up)) {
    if (!deps.shelf_runtime.items.empty()) {
      const int max_index = static_cast<int>(deps.shelf_runtime.items.size()) - 1;
      const int max_row_page = max_index / deps.k_grid_cols;
      const int col = deps.focus_index % deps.k_grid_cols;
      if (deps.shelf_page > 0) --deps.shelf_page;
      deps.shelf_page = std::clamp(deps.shelf_page, 0, max_row_page);
      deps.focus_index = std::min(max_index, deps.shelf_page * deps.k_grid_cols + col);
    }
  } else if (deps.input.IsJustPressed(Button::Down) || deps.input.IsRepeated(Button::Down)) {
    if (!deps.shelf_runtime.items.empty()) {
      const int max_index = static_cast<int>(deps.shelf_runtime.items.size()) - 1;
      const int max_row_page = max_index / deps.k_grid_cols;
      const int col = deps.focus_index % deps.k_grid_cols;
      if (deps.shelf_page < max_row_page) ++deps.shelf_page;
      deps.shelf_page = std::clamp(deps.shelf_page, 0, max_row_page);
      deps.focus_index = std::min(max_index, deps.shelf_page * deps.k_grid_cols + col);
    }
  } else if (deps.input.IsJustPressed(Button::X) && !deps.shelf_runtime.items.empty()) {
    const BookItem &item = deps.shelf_runtime.items[deps.focus_index];
    if (!item.is_dir) {
      if (item.is_remote) {
        if (deps.mark_remote_for_local && deps.mark_remote_for_local(item)) {
          if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
          reset_grid_page_state();
          sync_focus_with_page();
        }
      } else {
        if (deps.add_favorite) deps.add_favorite(item.real_path.empty() ? item.path : item.real_path);
      }
      if (!item.is_remote && deps.current_category && deps.current_category() == ShelfCategory::Collections) {
        if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
        reset_grid_page_state();
        sync_focus_with_page();
      }
    }
  } else if (deps.input.IsJustPressed(Button::Y) && !deps.shelf_runtime.items.empty()) {
    const BookItem &item = deps.shelf_runtime.items[deps.focus_index];
    if (!item.is_dir) {
      if (item.is_remote) {
        if (deps.unmark_remote_for_local && deps.unmark_remote_for_local(item)) {
          if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
          reset_grid_page_state();
          sync_focus_with_page();
        }
      } else {
        if (deps.remove_favorite) deps.remove_favorite(item.real_path.empty() ? item.path : item.real_path);
      }
      if (!item.is_remote && deps.current_category && deps.current_category() == ShelfCategory::Collections) {
        if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
        reset_grid_page_state();
        sync_focus_with_page();
      }
    }
  } else if (deps.input.IsJustPressed(Button::A) && !deps.shelf_runtime.items.empty()) {
    const BookItem &item = deps.shelf_runtime.items[deps.focus_index];
    if (item.is_dir && deps.current_folder.empty()) {
      deps.folder_focus[deps.current_folder] = deps.focus_index;
      deps.current_folder = item.real_path.empty() ? item.path : item.real_path;
      if (deps.clear_cover_cache) deps.clear_cover_cache();
      if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
      deps.focus_index = 0;
      deps.shelf_page = 0;
      reset_grid_page_state();
    } else if (!item.is_dir && deps.on_open_book) {
      deps.on_open_book(item);
    }
  }

  if (deps.shelf_page != prev_page) {
    if (deps.animations_enabled) {
      deps.page_anim_from = prev_page;
      deps.page_anim_to = deps.shelf_page;
      deps.page_anim_dir = (deps.shelf_page > prev_page) ? 1 : -1;
      deps.page_animating = true;
      if (deps.reset_page_slide) deps.reset_page_slide();
      if (deps.animate_page_slide) deps.animate_page_slide();
    } else {
      deps.page_animating = false;
      if (deps.reset_page_slide) deps.reset_page_slide();
    }
  }
}

void DestroyShelfRenderCache(ShelfRenderCache &cache,
                             const std::function<void(SDL_Texture *)> &forget_texture_size) {
  if (cache.texture) {
    if (forget_texture_size) forget_texture_size(cache.texture);
    SDL_DestroyTexture(cache.texture);
  }
  for (auto &kv : cache.static_page_textures) {
    if (kv.second) {
      if (forget_texture_size) forget_texture_size(kv.second);
      SDL_DestroyTexture(kv.second);
    }
  }
  if (cache.online_nav_pill_texture) {
    if (forget_texture_size) forget_texture_size(cache.online_nav_pill_texture);
    SDL_DestroyTexture(cache.online_nav_pill_texture);
  }
  cache = ShelfRenderCache{};
}

void InvalidateShelfRenderCache(ShelfRenderCache &cache,
                                const std::function<void(SDL_Texture *)> &forget_texture_size) {
  DestroyShelfRenderCache(cache, forget_texture_size);
}

void DrawShelfRuntime(ShelfRuntimeRenderDeps &deps) {
  auto draw_native = [&](SDL_Texture *tex, int x, int y) {
    if (!tex) return;
    int tw = 0;
    int th = 0;
    deps.get_texture_size(tex, tw, th);
    if (tw <= 0 || th <= 0) return;
    SDL_Rect dst{x, y, tw, th};
    SDL_RenderCopy(deps.renderer, tex, nullptr, &dst);
  };

  auto draw_filled_round_rect = [&](SDL_Rect rect, int radius, SDL_Color color) {
    radius = std::max(0, std::min(radius, std::min(rect.w, rect.h) / 2));
    if (radius <= 0) {
      deps.draw_rect(rect.x, rect.y, rect.w, rect.h, color, true);
      return;
    }
    for (int y = 0; y < rect.h; ++y) {
      int inset = 0;
      if (y < radius) {
        const int dy = radius - y - 1;
        inset = radius - static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
      } else if (y >= rect.h - radius) {
        const int dy = y - (rect.h - radius);
        inset = radius - static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
      }
      const int line_w = std::max(0, rect.w - inset * 2);
      if (line_w > 0) deps.draw_rect(rect.x + inset, rect.y + y, line_w, 1, color, true);
    }
  };

  auto get_online_nav_pill_texture = [&](int w, int h) -> SDL_Texture * {
    w = std::max(1, w);
    h = std::max(1, h);
    if (deps.render_cache.online_nav_pill_texture &&
        deps.render_cache.online_nav_pill_h == h &&
        deps.render_cache.online_nav_pill_w == w) {
      return deps.render_cache.online_nav_pill_texture;
    }
    if (deps.render_cache.online_nav_pill_texture) {
      if (deps.forget_texture_size) deps.forget_texture_size(deps.render_cache.online_nav_pill_texture);
      SDL_DestroyTexture(deps.render_cache.online_nav_pill_texture);
      deps.render_cache.online_nav_pill_texture = nullptr;
    }
    SDL_Texture *texture = SDL_CreateTexture(deps.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!texture) return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_Texture *old_target = SDL_GetRenderTarget(deps.renderer);
    if (SDL_SetRenderTarget(deps.renderer, texture) != 0) {
      SDL_DestroyTexture(texture);
      return nullptr;
    }
    SDL_SetRenderDrawColor(deps.renderer, 0, 0, 0, 0);
    SDL_RenderClear(deps.renderer);
    for (int y = 0; y < h; ++y) {
      const float cy = static_cast<float>(h - 1) * 0.5f;
      const float radius = static_cast<float>(h) * 0.5f;
      float inset = 0.0f;
      if (y < h / 2) {
        const float dy = cy - static_cast<float>(y);
        inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
      } else {
        const float dy = static_cast<float>(y) - cy;
        inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
      }
      const int x = std::max(0, static_cast<int>(std::floor(inset)));
      const int line_w = std::max(0, w - x * 2);
      const Uint8 edge_alpha = static_cast<Uint8>(std::clamp(255.0f * (1.0f - (inset - std::floor(inset))), 160.0f, 255.0f));
      SDL_SetRenderDrawColor(deps.renderer, 105, 113, 130, 255);
      SDL_RenderDrawLine(deps.renderer, x, y, x + line_w - 1, y);
      if (x > 0) {
        SDL_SetRenderDrawColor(deps.renderer, 105, 113, 130, edge_alpha);
        SDL_RenderDrawPoint(deps.renderer, x - 1, y);
        SDL_RenderDrawPoint(deps.renderer, w - x, y);
      }
    }
    SDL_SetRenderTarget(deps.renderer, old_target);
    deps.render_cache.online_nav_pill_texture = texture;
    deps.render_cache.online_nav_pill_w = w;
    deps.render_cache.online_nav_pill_h = h;
    if (deps.get_texture_size) {
      int tw = 0;
      int th = 0;
      deps.get_texture_size(texture, tw, th);
    }
    return texture;
  };

  struct RenderEntry {
    int index = -1;
    bool focused = false;
    bool on_current_page = false;
  };
  std::vector<RenderEntry> render_items;
  render_items.reserve(static_cast<size_t>(deps.k_items_per_page * 2));

  // A shelf page advances by one row, so the source/destination windows overlap.
  // Update and draw their union once, with scrolling separate from focus scaling.
  const int first_page = deps.page_animating ? std::min(deps.page_anim_from, deps.page_anim_to) : deps.shelf_page;
  const int last_page = deps.page_animating ? std::max(deps.page_anim_from, deps.page_anim_to) : deps.shelf_page;
  const int start = first_page * deps.k_grid_cols;
  const int end = std::min<int>(last_page * deps.k_grid_cols + deps.k_items_per_page,
                                deps.shelf_runtime.items.size());
  const float row_pitch = static_cast<float>(deps.layout.cover_h + deps.layout.grid_gap_y);
  const float shift_y = deps.page_animating
                            ? (deps.page_anim_to - deps.page_anim_from) * row_pitch *
                                  (1.0f - std::clamp(deps.page_slide_value, 0.0f, 1.0f))
                            : 0.0f;
  // Preserve GKD's current timing rather than speeding up smaller covers.
  const float scale_speed_w = deps.card_scale_linear_speed_w * deps.layout.cover_w / 314.0f;
  const float scale_speed_h = deps.card_scale_linear_speed_h * deps.layout.cover_h / 471.0f;
  for (int i = start; i < end; ++i) {
    const int local = i - deps.shelf_page * deps.k_grid_cols;
    const int row = i / deps.k_grid_cols - deps.shelf_page;
    const int col = i % deps.k_grid_cols;
    const float base_x = static_cast<float>(deps.layout.grid_start_x + col * (deps.layout.cover_w + deps.layout.grid_gap_x));
    const float base_y = static_cast<float>(deps.layout.grid_start_y + row * (deps.layout.cover_h + deps.layout.grid_gap_y));
    const float base_cx = base_x + static_cast<float>(deps.layout.cover_w) * 0.5f;
    const float base_cy = base_y + static_cast<float>(deps.layout.cover_h) * 0.5f;
    const bool focused = (i == deps.focus_index);

    GridItemAnim &anim = deps.grid_item_anims[i];
    anim.tcx = base_cx;
    anim.tcy = base_cy + shift_y;
    if (deps.animate_enabled && deps.page_animating && !anim.initialized) {
      anim.tw = static_cast<float>(deps.layout.cover_w);
      anim.th = static_cast<float>(deps.layout.cover_h);
      anim.SnapToTarget();
    }
    anim.tw = focused ? deps.focus_cover_w : static_cast<float>(deps.layout.cover_w);
    anim.th = focused ? deps.focus_cover_h : static_cast<float>(deps.layout.cover_h);
    anim.t_alpha = focused ? 255.0f : deps.unfocused_alpha;
    if (!deps.animate_enabled) {
      anim.SnapToTarget();
    } else {
      if (deps.page_animating) {
        anim.cx = anim.tcx;
        anim.cy = anim.tcy;
      }
      anim.Update(ShelfAnimationDelta(deps.dt), deps.card_move_linear_speed_x, deps.card_move_linear_speed_y,
                  deps.card_move_tail_ratio, deps.card_move_tail_min_mul,
                  scale_speed_w, scale_speed_h,
                  deps.card_scale_tail_ratio, deps.card_scale_tail_min_mul);
      deps.any_grid_animating = deps.any_grid_animating || anim.IsAnimating();
    }
    render_items.push_back(RenderEntry{i, focused, local >= 0 && local < deps.k_items_per_page});
  }

  auto static_page_key = [&](int page) {
    return std::to_string(page) + "|" + std::to_string(deps.nav_selected_index) + "|" +
           std::to_string(deps.shelf_content_version) + "|" + std::to_string(deps.layout.screen_w) + "x" +
           std::to_string(deps.layout.screen_h);
  };

  auto clear_static_page_cache_if_needed = [&]() {
    const bool matches = deps.render_cache.static_nav_selected_index == deps.nav_selected_index &&
                         deps.render_cache.static_content_version == deps.shelf_content_version &&
                         deps.render_cache.static_screen_w == deps.layout.screen_w &&
                         deps.render_cache.static_screen_h == deps.layout.screen_h;
    if (matches) return;
    for (auto &kv : deps.render_cache.static_page_textures) {
      if (kv.second) {
        if (deps.forget_texture_size) deps.forget_texture_size(kv.second);
        SDL_DestroyTexture(kv.second);
      }
    }
    deps.render_cache.static_page_textures.clear();
    deps.render_cache.static_nav_selected_index = deps.nav_selected_index;
    deps.render_cache.static_content_version = deps.shelf_content_version;
    deps.render_cache.static_screen_w = deps.layout.screen_w;
    deps.render_cache.static_screen_h = deps.layout.screen_h;
  };

  clear_static_page_cache_if_needed();

  auto draw_cover = [&](const BookItem &item, SDL_Rect dst, Uint8 alpha) {
    bool drew_cover = false;
    SDL_Texture *cover_tex = deps.get_cover_texture(item);
    if (cover_tex) {
      int tw = 0;
      int th = 0;
      deps.get_texture_size(cover_tex, tw, th);
      if (tw > 0 && th > 0) {
        const float src_aspect = static_cast<float>(tw) / static_cast<float>(th);
        SDL_Rect src{0, 0, tw, th};
        if (src_aspect > deps.cover_aspect) {
          src.w = static_cast<int>(std::round(th * deps.cover_aspect));
          src.x = (tw - src.w) / 2;
        } else if (src_aspect < deps.cover_aspect) {
          src.h = static_cast<int>(std::round(tw / deps.cover_aspect));
          src.y = (th - src.h) / 2;
        }
        SDL_SetTextureAlphaMod(cover_tex, alpha);
        SDL_RenderCopy(deps.renderer, cover_tex, &src, &dst);
        SDL_SetTextureAlphaMod(cover_tex, 255);
        drew_cover = true;
      }
    }
    if (!drew_cover && item.is_remote && deps.ui_assets.book_cover_pdf) {
      SDL_SetTextureAlphaMod(deps.ui_assets.book_cover_pdf, alpha);
      SDL_RenderCopy(deps.renderer, deps.ui_assets.book_cover_pdf, nullptr, &dst);
      SDL_SetTextureAlphaMod(deps.ui_assets.book_cover_pdf, 255);
      drew_cover = true;
    } else if (!drew_cover) {
      SDL_Color c = item.is_dir ? SDL_Color{86, 121, 157, alpha} : SDL_Color{66, 81, 102, alpha};
      deps.draw_rect(dst.x, dst.y, dst.w, dst.h, c, true);
    }

    std::string status_text;
    if (item.is_remote && deps.remote_book_status_text) status_text = deps.remote_book_status_text(item);
    if (status_text.empty() && item.is_remote && deps.remote_cover_loading && deps.remote_cover_loading(item)) {
      status_text = u8"\u52a0\u8f7d\u4e2d";
    }
    if (!status_text.empty()) {
      const bool gkd350h_ultra_online_status_bar =
          item.is_remote &&
          deps.layout.screen_w == 1600 &&
          deps.layout.screen_h == 1440 &&
          deps.online_shelf_active &&
          deps.online_shelf_active();
      const int bar_h = gkd350h_ultra_online_status_bar
                            ? std::max(40, std::min(56, dst.h / 8))
                            : std::max(20, std::min(28, dst.h / 6));
      deps.draw_rect(dst.x, dst.y, dst.w, bar_h,
                     SDL_Color{0, 0, 0, static_cast<Uint8>(std::min<int>(170, alpha))}, true);
      if (status_text == u8"\u52a0\u8f7d\u4e2d" || status_text == u8"\u4e0b\u8f7d\u4e2d") {
        const int progress_h = gkd350h_ultra_online_status_bar
                                   ? std::max(4, std::min(8, bar_h / 7))
                                   : std::max(2, std::min(4, bar_h / 6));
        const int progress_y = dst.y + bar_h - progress_h;
        deps.draw_rect(dst.x, progress_y, dst.w, progress_h,
                       SDL_Color{36, 58, 76, static_cast<Uint8>(std::min<int>(150, alpha))}, true);
        float progress = -1.0f;
        if (deps.remote_book_status_progress) progress = deps.remote_book_status_progress(item);
        if (progress >= 0.0f) {
          const int fill_w = std::clamp(static_cast<int>(std::round(static_cast<float>(dst.w) * progress)), 0, dst.w);
          if (fill_w > 0) {
            deps.draw_rect(dst.x, progress_y, fill_w, progress_h,
                           SDL_Color{139, 214, 255, static_cast<Uint8>(std::min<int>(230, alpha))}, true);
          }
        } else {
          const int fill_w = std::max(10, dst.w / 3);
          const uint32_t ticks = SDL_GetTicks();
          const int travel = std::max(1, dst.w + fill_w);
          const int x = dst.x - fill_w + static_cast<int>((ticks / 8) % static_cast<uint32_t>(travel));
          const int clipped_x = std::max(dst.x, x);
          const int clipped_w = std::min(dst.x + dst.w, x + fill_w) - clipped_x;
          if (clipped_w > 0) {
            deps.draw_rect(clipped_x, progress_y, clipped_w, progress_h,
                           SDL_Color{139, 214, 255, static_cast<Uint8>(std::min<int>(230, alpha))}, true);
          }
        }
      }
      int tw = 0;
      int th = 0;
      SDL_Texture *text_tex = nullptr;
      deps.get_text_texture(status_text, SDL_Color{246, 248, 252, alpha}, tw, th, text_tex);
      if (text_tex) {
        SDL_Rect td{dst.x + std::max(0, (dst.w - tw) / 2), dst.y + std::max(0, (bar_h - th) / 2), tw, th};
        SDL_RenderCopy(deps.renderer, text_tex, nullptr, &td);
      }
    }
  };

  auto draw_cached_cover = [&](const BookItem &item, SDL_Rect dst, Uint8 alpha) {
    SDL_Texture *cover_tex = deps.get_cached_cover_texture ? deps.get_cached_cover_texture(item) : nullptr;
    if (cover_tex) {
      int tw = 0;
      int th = 0;
      deps.get_texture_size(cover_tex, tw, th);
      if (tw > 0 && th > 0) {
        const float src_aspect = static_cast<float>(tw) / static_cast<float>(th);
        SDL_Rect src{0, 0, tw, th};
        if (src_aspect > deps.cover_aspect) {
          src.w = static_cast<int>(std::round(th * deps.cover_aspect));
          src.x = (tw - src.w) / 2;
        } else if (src_aspect < deps.cover_aspect) {
          src.h = static_cast<int>(std::round(tw / deps.cover_aspect));
          src.y = (th - src.h) / 2;
        }
        SDL_SetTextureAlphaMod(cover_tex, alpha);
        SDL_RenderCopy(deps.renderer, cover_tex, &src, &dst);
        SDL_SetTextureAlphaMod(cover_tex, 255);
        return true;
      }
    }
    return false;
  };

  auto make_outer_frame_rect = [&](const SDL_Rect &cover_rect) {
    const float sx = static_cast<float>(cover_rect.w) / static_cast<float>(deps.layout.cover_w);
    const float sy = static_cast<float>(cover_rect.h) / static_cast<float>(deps.layout.cover_h);
    const int outer_w = std::max(1, static_cast<int>(std::round(deps.layout.card_frame_w * sx)));
    const int outer_h = std::max(1, static_cast<int>(std::round(deps.layout.card_frame_h * sy)));
    const int cx = cover_rect.x + cover_rect.w / 2;
    const int cy = cover_rect.y + cover_rect.h / 2;
    return SDL_Rect{cx - outer_w / 2, cy - outer_h / 2, outer_w, outer_h};
  };

  auto draw_cover_under_shadow = [&](const SDL_Rect &outer_rect) {
    if (!deps.ui_assets.book_under_shadow) return;
    SDL_RenderCopy(deps.renderer, deps.ui_assets.book_under_shadow, nullptr, &outer_rect);
  };

  auto draw_cover_select = [&](const SDL_Rect &outer_rect) {
    if (!deps.ui_assets.book_select) return;
    SDL_RenderCopy(deps.renderer, deps.ui_assets.book_select, nullptr, &outer_rect);
  };

  auto draw_title_overlay = [&](const BookItem &item, const SDL_Rect &dst, bool focused) {
    if (deps.ui_assets.book_title_shadow) {
      SDL_Rect od{dst.x, dst.y, dst.w, dst.h};
      SDL_RenderCopy(deps.renderer, deps.ui_assets.book_title_shadow, nullptr, &od);
    }
    SDL_Color title_color = focused ? SDL_Color{248, 250, 255, 255} : SDL_Color{230, 236, 248, 244};
    const int text_area_x = dst.x + deps.layout.title_text_pad_x;
    const int text_area_w = std::max(8, dst.w - deps.layout.title_text_pad_x * 2);
    const int text_area_h = std::max(12, deps.layout.title_overlay_h - 2);
    const int text_area_y = dst.y + dst.h - text_area_h - deps.layout.title_text_pad_bottom;
    SDL_Rect clip{text_area_x, text_area_y, text_area_w, text_area_h};

    auto measure = [&](const std::string &s) -> int {
      int tw = 0;
      int th = 0;
      SDL_Texture *tex = nullptr;
      deps.get_text_texture(s, title_color, tw, th, tex);
      return tex ? tw : static_cast<int>(s.size()) * 8;
    };

    std::string display = deps.shelf_title_text(item);
    if (!focused) display = deps.get_title_ellipsized(display, text_area_w, measure);
    int tw = 0;
    int th = 0;
    SDL_Texture *text_tex = nullptr;
    deps.get_text_texture(display, title_color, tw, th, text_tex);
    if (!text_tex) return;

    ScopedShelfClip title_clip(deps.renderer, clip);
    if (title_clip.Empty()) return;
    const int text_y = text_area_y + std::max(0, (text_area_h - th) / 2);
    if (focused && tw > text_area_w) {
      const float span = static_cast<float>(tw + deps.layout.title_marquee_gap_px);
      const float xoff = std::fmod(deps.title_marquee_offset, span);
      SDL_Rect td1{text_area_x - static_cast<int>(std::round(xoff)), text_y, tw, th};
      SDL_Rect td2{td1.x + static_cast<int>(span), text_y, tw, th};
      SDL_RenderCopy(deps.renderer, text_tex, nullptr, &td1);
      SDL_RenderCopy(deps.renderer, text_tex, nullptr, &td2);
    } else {
      const int centered_x = text_area_x + std::max(0, (text_area_w - tw) / 2);
      SDL_Rect td{centered_x, text_y, tw, th};
      SDL_RenderCopy(deps.renderer, text_tex, nullptr, &td);
    }
  };

  auto draw_nav_chrome = [&]() {
    if (deps.ui_assets.nav_l1_icon) draw_native(deps.ui_assets.nav_l1_icon, deps.layout.nav_l1_x, deps.layout.nav_l1_y);
    if (deps.ui_assets.nav_r1_icon) draw_native(deps.ui_assets.nav_r1_icon, deps.layout.nav_r1_x, deps.layout.nav_r1_y);
    const std::array<std::string, 4> default_nav_labels = {"ALL COMICS", "ALL BOOKS", "COLLECTIONS", "HISTORY"};
    const int nav_count = std::max(1, deps.nav_item_count ? deps.nav_item_count() : 4);
    const int base_total_w = deps.layout.nav_slot_w * 4;
    const int nav_slot_w = std::max(1, base_total_w / nav_count);
    const bool online = deps.online_shelf_active && deps.online_shelf_active();
    const int nav_pill_h = std::max(1, deps.layout.nav_pill_h);
    int nav_text_center_h = nav_pill_h;
    SDL_Color nav_text{238, 242, 250, 255};
    std::vector<int> text_widths(static_cast<size_t>(nav_count), 0);
    std::vector<int> text_heights(static_cast<size_t>(nav_count), 0);
    std::vector<SDL_Texture *> text_textures(static_cast<size_t>(nav_count), nullptr);
    for (int i = 0; i < nav_count; ++i) {
      std::string label = deps.nav_label_text ? deps.nav_label_text(i) : default_nav_labels[i];
      const int max_label_w = std::max(8, nav_slot_w - 6);
      auto measure_nav_label = [&](const std::string &text) -> int {
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = nullptr;
        deps.get_text_texture(text, nav_text, tw, th, tex);
        return tex ? tw : static_cast<int>(text.size()) * 8;
      };
      if (deps.get_title_ellipsized && measure_nav_label(label) > max_label_w) {
        label = deps.get_title_ellipsized(label, max_label_w, measure_nav_label);
      }
      deps.get_text_texture(label, nav_text, text_widths[static_cast<size_t>(i)],
                            text_heights[static_cast<size_t>(i)], text_textures[static_cast<size_t>(i)]);
    }
    if (online) {
      const int selected = std::clamp(deps.nav_selected_index, 0, nav_count - 1);
      const int selected_center = deps.layout.nav_start_x + selected * nav_slot_w + nav_slot_w / 2;
      const int pill_w = std::max(1, nav_slot_w - 6);
      SDL_Rect pill{selected_center - pill_w / 2, deps.layout.nav_y, pill_w, nav_pill_h};
      if (SDL_Texture *pill_texture = get_online_nav_pill_texture(pill_w, nav_pill_h)) {
        SDL_RenderCopy(deps.renderer, pill_texture, nullptr, &pill);
      } else {
        draw_filled_round_rect(pill, nav_pill_h / 2, SDL_Color{105, 113, 130, 255});
      }
    } else if (deps.ui_assets.nav_selected_pill) {
      int pw = 0;
      int ph = 0;
      deps.get_texture_size(deps.ui_assets.nav_selected_pill, pw, ph);
      if (ph > 0) nav_text_center_h = ph;
      const int slot_center_x =
          deps.layout.nav_start_x + deps.nav_selected_index * deps.layout.nav_slot_w + deps.layout.nav_slot_w / 2;
      draw_native(deps.ui_assets.nav_selected_pill, slot_center_x - pw / 2, deps.layout.nav_y);
    }
    const int nav_text_y_offset = (deps.layout.screen_w == 1024 && deps.layout.screen_h == 768) ? 1 : 0;
    for (int i = 0; i < nav_count; ++i) {
      SDL_Texture *tex = text_textures[static_cast<size_t>(i)];
      if (!tex) continue;
      const int slot_x = deps.layout.nav_start_x + i * nav_slot_w;
      const int tx = slot_x + std::max(0, (nav_slot_w - text_widths[static_cast<size_t>(i)]) / 2);
      const int ty = deps.layout.nav_y + std::max(0, (nav_text_center_h - text_heights[static_cast<size_t>(i)]) / 2) +
                     nav_text_y_offset;
      SDL_Rect td{tx, ty, text_widths[static_cast<size_t>(i)], text_heights[static_cast<size_t>(i)]};
      SDL_RenderCopy(deps.renderer, tex, nullptr, &td);
    }
  };

  const int shelf_clip_top = std::clamp(deps.layout.nav_bar_bottom, 0, deps.layout.screen_h);
  const SDL_Rect shelf_clip{0, shelf_clip_top, deps.layout.screen_w, deps.layout.screen_h - shelf_clip_top};

  auto render_shelf_static_layer = [&]() {
    if (deps.ui_assets.background_main) {
      SDL_Rect bg_dst{0, 0, deps.layout.screen_w, deps.layout.screen_h};
      SDL_RenderCopy(deps.renderer, deps.ui_assets.background_main, nullptr, &bg_dst);
    }

    {
      ScopedShelfClip cards_clip(deps.renderer, shelf_clip);
      for (const RenderEntry &e : render_items) {
        if (cards_clip.Empty()) break;
        if (e.focused) continue;
        const BookItem &item = deps.shelf_runtime.items[e.index];
        GridItemAnim &anim = deps.grid_item_anims[e.index];
        SDL_Rect dst{
            static_cast<int>(std::round(anim.x)),
            static_cast<int>(std::round(anim.y)),
            static_cast<int>(std::round(anim.w)),
            static_cast<int>(std::round(anim.h)),
        };
        const SDL_Rect outer = make_outer_frame_rect(dst);
        const Uint8 alpha = static_cast<Uint8>(std::clamp(anim.alpha, 0.0f, 255.0f));
        draw_cover_under_shadow(outer);
        draw_cover(item, dst, alpha);
        if (!deps.page_animating || e.on_current_page) draw_title_overlay(item, dst, false);
      }
    }

    if (deps.ui_assets.top_status_bar) draw_native(deps.ui_assets.top_status_bar, 0, 0);
    if (deps.ui_assets.bottom_hint_bar) {
      int bw = 0;
      int bh = 0;
      deps.get_texture_size(deps.ui_assets.bottom_hint_bar, bw, bh);
      draw_native(deps.ui_assets.bottom_hint_bar, 0, deps.layout.screen_h - bh);
    }
    draw_nav_chrome();
  };

  auto page_covers_are_cached = [&](int page) {
    if (!deps.get_cached_cover_texture) return false;
    const int start = page * deps.k_grid_cols;
    const int end = std::min<int>(start + deps.k_items_per_page, deps.shelf_runtime.items.size());
    for (int i = start; i < end; ++i) {
      if (!deps.get_cached_cover_texture(deps.shelf_runtime.items[i])) return false;
    }
    return true;
  };

  auto render_static_page_cards = [&](int page, float shift_y, bool skip_focused, bool cached_only) {
    ScopedShelfClip cards_clip(deps.renderer, shelf_clip);
    if (cards_clip.Empty()) return;
    const int start = page * deps.k_grid_cols;
    const int end = std::min<int>(start + deps.k_items_per_page, deps.shelf_runtime.items.size());
    for (int i = start; i < end; ++i) {
      if (skip_focused && i == deps.focus_index) continue;
      const int local = i - start;
      const int row = local / deps.k_grid_cols;
      const int col = local % deps.k_grid_cols;
      SDL_Rect dst{
          deps.layout.grid_start_x + col * (deps.layout.cover_w + deps.layout.grid_gap_x),
          static_cast<int>(std::round(static_cast<float>(deps.layout.grid_start_y +
                                                         row * (deps.layout.cover_h + deps.layout.grid_gap_y)) +
                                      shift_y)),
          deps.layout.cover_w,
          deps.layout.cover_h,
      };
      const SDL_Rect outer = make_outer_frame_rect(dst);
      const BookItem &item = deps.shelf_runtime.items[i];
      draw_cover_under_shadow(outer);
      const Uint8 alpha = static_cast<Uint8>(std::clamp(deps.unfocused_alpha, 0.0f, 255.0f));
      if (cached_only) {
        if (draw_cached_cover(item, dst, alpha)) draw_title_overlay(item, dst, false);
      } else {
        draw_cover(item, dst, alpha);
        draw_title_overlay(item, dst, false);
      }
    }
  };

  auto get_or_create_static_page_texture = [&](int page) -> SDL_Texture * {
    if (!deps.renderer_supports_target_textures || deps.layout.screen_w <= 0 || deps.layout.screen_h <= 0) return nullptr;
    const std::string key = static_page_key(page);
    auto it = deps.render_cache.static_page_textures.find(key);
    if (it != deps.render_cache.static_page_textures.end()) return it->second;
    if (!page_covers_are_cached(page) && deps.ensure_page_cover_textures) {
      deps.ensure_page_cover_textures(page);
    }
    if (!page_covers_are_cached(page)) return nullptr;

    SDL_Texture *texture = SDL_CreateTexture(deps.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                             deps.layout.screen_w, deps.layout.screen_h);
    if (!texture) return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_Texture *previous_target = SDL_GetRenderTarget(deps.renderer);
    const SDL_bool previous_clip_enabled = SDL_RenderIsClipEnabled(deps.renderer);
    SDL_Rect previous_clip{};
    SDL_RenderGetClipRect(deps.renderer, &previous_clip);
    if (SDL_SetRenderTarget(deps.renderer, texture) != 0) {
      SDL_DestroyTexture(texture);
      return nullptr;
    }
    // Bake the whole shelf cache independently of the caller's temporary clip.
    SDL_RenderSetClipRect(deps.renderer, nullptr);
    SDL_SetRenderDrawColor(deps.renderer, 0, 0, 0, 0);
    SDL_RenderClear(deps.renderer);
    render_static_page_cards(page, 0.0f, false, true);
    SDL_SetRenderTarget(deps.renderer, previous_target);
    SDL_RenderSetClipRect(deps.renderer, previous_clip_enabled ? &previous_clip : nullptr);
    deps.render_cache.static_page_textures[key] = texture;
    return texture;
  };

  auto draw_shelf_background = [&]() {
    if (deps.ui_assets.background_main) {
      SDL_Rect bg_dst{0, 0, deps.layout.screen_w, deps.layout.screen_h};
      SDL_RenderCopy(deps.renderer, deps.ui_assets.background_main, nullptr, &bg_dst);
    }
  };

  auto draw_shelf_chrome = [&]() {
    if (deps.ui_assets.top_status_bar) draw_native(deps.ui_assets.top_status_bar, 0, 0);
    if (deps.ui_assets.bottom_hint_bar) {
      int bw = 0;
      int bh = 0;
      deps.get_texture_size(deps.ui_assets.bottom_hint_bar, bw, bh);
      draw_native(deps.ui_assets.bottom_hint_bar, 0, deps.layout.screen_h - bh);
    }
    draw_nav_chrome();
  };

  const bool can_use_static_page_cache = !(deps.online_shelf_active && deps.online_shelf_active()) &&
                                         deps.renderer_supports_target_textures && !deps.page_animating &&
                                         !deps.any_grid_animating;
  const bool can_use_shelf_render_cache = false && deps.renderer_supports_target_textures &&
                                          !deps.page_animating && !deps.any_grid_animating;
  const bool cache_matches = deps.render_cache.texture &&
                             deps.render_cache.focus_index == deps.focus_index &&
                             deps.render_cache.shelf_page == deps.shelf_page &&
                             deps.render_cache.nav_selected_index == deps.nav_selected_index &&
                             deps.render_cache.content_version == deps.shelf_content_version;
  bool chrome_deferred = false;
  if (can_use_static_page_cache) {
    draw_shelf_background();
    SDL_Texture *static_page = get_or_create_static_page_texture(deps.shelf_page);
    if (static_page) {
      ScopedShelfClip cards_clip(deps.renderer, shelf_clip);
      if (!cards_clip.Empty()) SDL_RenderCopy(deps.renderer, static_page, nullptr, nullptr);
    } else {
      render_static_page_cards(deps.shelf_page, 0.0f, true, true);
    }
    chrome_deferred = true;
  } else if (can_use_shelf_render_cache && cache_matches) {
    SDL_RenderCopy(deps.renderer, deps.render_cache.texture, nullptr, nullptr);
  } else {
    render_shelf_static_layer();
  }

  for (const RenderEntry &e : render_items) {
    if (!e.focused) continue;
    ScopedShelfClip cards_clip(deps.renderer, shelf_clip);
    if (cards_clip.Empty()) break;
    const BookItem &item = deps.shelf_runtime.items[e.index];
    GridItemAnim &anim = deps.grid_item_anims[e.index];
    SDL_Rect focus_rect{
        static_cast<int>(std::round(anim.x)),
        static_cast<int>(std::round(anim.y)),
        static_cast<int>(std::round(anim.w)),
        static_cast<int>(std::round(anim.h)),
    };
    const SDL_Rect outer = make_outer_frame_rect(focus_rect);
    draw_cover_under_shadow(outer);
    draw_cover(item, focus_rect, 255);
    draw_title_overlay(item, focus_rect, true);
    draw_cover_select(outer);
    break;
  }
  if (chrome_deferred) draw_shelf_chrome();
}
