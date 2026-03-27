#include "shelf_runtime.h"

#include <algorithm>
#include <filesystem>
#include <array>
#include <sstream>
#include <unordered_map>

namespace {
bool IsPureExtFolder(const std::string &folder_path, const std::string &wanted_ext,
                     const ShelfRuntimeDeps &deps) {
  std::error_code ec;
  const std::filesystem::path root(folder_path);
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return false;
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  bool has_match = false;
  for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
    if (ec) continue;
    const auto &entry = *it;
    if (!entry.is_regular_file(ec)) continue;
    const std::string ext = deps.get_lower_ext(entry.path().string());
    if (ext == wanted_ext) {
      has_match = true;
      continue;
    }
    return false;
  }
  return has_match;
}

void SortShelfItems(std::vector<BookItem> &items) {
  std::sort(items.begin(), items.end(), [](const BookItem &a, const BookItem &b) {
    if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
    return a.name < b.name;
  });
}
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
  std::ostringstream oss;
  oss << static_cast<int>(category) << "|";
  if (folder.empty()) {
    oss << "<root>";
    for (const auto &root : books_roots) oss << "|" << deps.normalize_path_key(root);
  } else {
    oss << deps.normalize_path_key(folder);
  }
  return oss.str();
}

void PruneShelfScanCache(ShelfRuntimeState &state, size_t max_cache_entries) {
  while (state.scan_cache.size() > max_cache_entries) {
    auto oldest = state.scan_cache.end();
    for (auto it = state.scan_cache.begin(); it != state.scan_cache.end(); ++it) {
      if (oldest == state.scan_cache.end() || it->second.last_scan_tick < oldest->second.last_scan_tick) {
        oldest = it;
      }
    }
    if (oldest == state.scan_cache.end()) break;
    state.scan_cache.erase(oldest);
  }
}

bool ShelfMatchCategory(const BookItem &item, ShelfCategory category, const ShelfRuntimeDeps &deps) {
  if (item.is_dir) return category == ShelfCategory::AllComics || category == ShelfCategory::AllBooks;
  const std::string ext = deps.get_lower_ext(item.path);
  if (category == ShelfCategory::AllComics) return ext == ".pdf" || ext == ".epub";
  if (category == ShelfCategory::AllBooks) return ext == ".txt";
  if (category == ShelfCategory::Collections) return deps.favorites_contains(item.path);
  if (category == ShelfCategory::History) return deps.history_contains(item.path);
  return true;
}

std::vector<BookItem> ScanShelfBaseItems(ShelfRuntimeState &state, ShelfCategory category,
                                         const std::string &current_folder,
                                         const std::vector<std::string> &books_roots,
                                         const ShelfRuntimeDeps &deps) {
  const std::string cache_key = MakeShelfScanCacheKey(category, current_folder, books_roots, deps);
  const uint32_t now = SDL_GetTicks();
  auto cache_it = state.scan_cache.find(cache_key);
  if (cache_it != state.scan_cache.end() && now - cache_it->second.last_scan_tick < deps.cache_ttl_ms) {
    return cache_it->second.items;
  }

  auto save_and_return = [&](std::vector<BookItem> out) {
    state.scan_cache[cache_key] = ShelfScanCacheEntry{out, now};
    PruneShelfScanCache(state, deps.max_cache_entries);
    return out;
  };

  if (category == ShelfCategory::AllComics || category == ShelfCategory::AllBooks) {
    const std::string wanted_ext = (category == ShelfCategory::AllBooks) ? ".txt" : "";
    std::vector<BookItem> out;
    if (current_folder.empty()) {
      for (const auto &root : books_roots) {
        std::error_code ec;
        const std::filesystem::path root_path(root);
        if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
          if (ec) continue;
          const auto &entry = *it;
          if (entry.is_regular_file(ec)) {
            const std::string ext = deps.get_lower_ext(entry.path().string());
            const bool matched = (category == ShelfCategory::AllBooks) ? (ext == ".txt")
                                                                       : (ext == ".pdf" || ext == ".epub");
            if (!matched) continue;
            out.push_back(BookItem{entry.path().filename().string(), entry.path().string(), false});
          } else if (entry.is_directory(ec)) {
            bool matched = false;
            if (category == ShelfCategory::AllBooks) {
              matched = IsPureExtFolder(entry.path().string(), wanted_ext, deps);
            } else {
              matched = IsPureExtFolder(entry.path().string(), ".pdf", deps) ||
                        IsPureExtFolder(entry.path().string(), ".epub", deps);
            }
            if (!matched) continue;
            out.push_back(BookItem{entry.path().filename().string(), entry.path().string(), true});
          }
        }
      }
    } else {
      std::error_code ec;
      const std::filesystem::path root_path(current_folder);
      if (std::filesystem::exists(root_path, ec) && std::filesystem::is_directory(root_path, ec)) {
        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
          if (ec) continue;
          const auto &entry = *it;
          if (!entry.is_regular_file(ec)) continue;
          const std::string ext = deps.get_lower_ext(entry.path().string());
          const bool matched = (category == ShelfCategory::AllBooks) ? (ext == ".txt")
                                                                     : (ext == ".pdf" || ext == ".epub");
          if (!matched) continue;
          out.push_back(BookItem{entry.path().filename().string(), entry.path().string(), false});
        }
      }
    }
    SortShelfItems(out);
    return save_and_return(std::move(out));
  }

  if (current_folder.empty() && category == ShelfCategory::Collections) {
    std::unordered_map<std::string, BookItem> found;
    for (const auto &root : books_roots) {
      std::error_code ec;
      const std::filesystem::path root_path(root);
      if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
      const auto opts = std::filesystem::directory_options::skip_permission_denied;
      for (std::filesystem::recursive_directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) continue;
        const auto &entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = deps.get_lower_ext(entry.path().string());
        if (ext != ".pdf" && ext != ".txt" && ext != ".epub") continue;
        found.emplace(deps.normalize_path_key(entry.path().string()),
                      BookItem{entry.path().filename().string(), entry.path().string(), false});
      }
    }
    std::vector<BookItem> out;
    for (const auto &path_key : deps.favorites_ordered_paths()) {
      auto it = found.find(path_key);
      if (it != found.end()) out.push_back(it->second);
    }
    return save_and_return(std::move(out));
  }

  if (current_folder.empty() && category == ShelfCategory::History) {
    std::unordered_map<std::string, BookItem> found;
    for (const auto &root : books_roots) {
      std::error_code ec;
      const std::filesystem::path root_path(root);
      if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
      const auto opts = std::filesystem::directory_options::skip_permission_denied;
      for (std::filesystem::recursive_directory_iterator it(root_path, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) continue;
        const auto &entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = deps.get_lower_ext(entry.path().string());
        if (ext != ".pdf" && ext != ".txt" && ext != ".epub") continue;
        found.emplace(deps.normalize_path_key(entry.path().string()),
                      BookItem{entry.path().filename().string(), entry.path().string(), false});
      }
    }
    std::vector<BookItem> out;
    for (const auto &path_key : deps.history_ordered_paths()) {
      auto it = found.find(path_key);
      if (it != found.end()) out.push_back(it->second);
    }
    return save_and_return(std::move(out));
  }

  std::vector<BookItem> out =
      current_folder.empty() ? BookScanner::ScanRoots(books_roots) : BookScanner::ScanPath(current_folder, false);
  return save_and_return(std::move(out));
}

void RebuildShelfItems(ShelfRuntimeState &state, ShelfCategory category, const std::string &current_folder,
                       const std::vector<std::string> &books_roots, const ShelfRuntimeDeps &deps) {
  const std::vector<BookItem> base = ScanShelfBaseItems(state, category, current_folder, books_roots, deps);
  state.items.clear();
  state.items.reserve(base.size());
  for (const auto &item : base) {
    if (ShelfMatchCategory(item, category, deps)) state.items.push_back(item);
  }
  ++state.content_version;
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

  if (deps.input.IsJustPressed(Button::L1)) {
    deps.nav_selected_index = (deps.nav_selected_index + 3) % 4;
    deps.current_folder.clear();
    if (deps.clear_cover_cache) deps.clear_cover_cache();
    if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
    deps.focus_index = 0;
    deps.shelf_page = 0;
    reset_grid_page_state();
    sync_focus_with_page();
  } else if (deps.input.IsJustPressed(Button::R1)) {
    deps.nav_selected_index = (deps.nav_selected_index + 1) % 4;
    deps.current_folder.clear();
    if (deps.clear_cover_cache) deps.clear_cover_cache();
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
      if (deps.add_favorite) deps.add_favorite(item.path);
      if (deps.current_category && deps.current_category() == ShelfCategory::Collections) {
        if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
        reset_grid_page_state();
        sync_focus_with_page();
      }
    }
  } else if (deps.input.IsJustPressed(Button::Y) && !deps.shelf_runtime.items.empty()) {
    const BookItem &item = deps.shelf_runtime.items[deps.focus_index];
    if (!item.is_dir) {
      if (deps.remove_favorite) deps.remove_favorite(item.path);
      if (deps.current_category && deps.current_category() == ShelfCategory::Collections) {
        if (deps.rebuild_shelf_items) deps.rebuild_shelf_items();
        reset_grid_page_state();
        sync_focus_with_page();
      }
    }
  } else if (deps.input.IsJustPressed(Button::A) && !deps.shelf_runtime.items.empty()) {
    const BookItem &item = deps.shelf_runtime.items[deps.focus_index];
    if (item.is_dir && deps.current_folder.empty()) {
      deps.folder_focus[deps.current_folder] = deps.focus_index;
      deps.current_folder = item.path;
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

  struct RenderEntry {
    int index = -1;
    bool focused = false;
    bool on_current_page = false;
  };
  std::vector<RenderEntry> render_items;
  render_items.reserve(static_cast<size_t>(deps.k_items_per_page * 2));

  struct PagePlan {
    int page = 0;
    float shift_y = 0.0f;
  };
  std::vector<PagePlan> pages_to_draw;
  if (deps.page_animating) {
    const float slide = std::clamp(deps.page_slide_value, 0.0f, 1.0f);
    pages_to_draw.push_back(PagePlan{deps.page_anim_from, -static_cast<float>(deps.page_anim_dir) * deps.layout.screen_h * slide});
    pages_to_draw.push_back(
        PagePlan{deps.page_anim_to, static_cast<float>(deps.page_anim_dir) * deps.layout.screen_h * (1.0f - slide)});
  } else {
    pages_to_draw.push_back(PagePlan{deps.shelf_page, 0.0f});
  }

  for (const auto &pp : pages_to_draw) {
    const int start = pp.page * deps.k_grid_cols;
    const int end = std::min<int>(start + deps.k_items_per_page, deps.shelf_runtime.items.size());
    for (int i = start; i < end; ++i) {
      int local = i - start;
      int row = local / deps.k_grid_cols;
      int col = local % deps.k_grid_cols;
      float base_x = static_cast<float>(deps.layout.grid_start_x + col * (deps.layout.cover_w + deps.layout.grid_gap_x));
      float base_y = static_cast<float>(deps.layout.grid_start_y + row * (deps.layout.cover_h + deps.layout.grid_gap_y));
      float base_cx = base_x + static_cast<float>(deps.layout.cover_w) * 0.5f;
      float base_cy = base_y + static_cast<float>(deps.layout.cover_h) * 0.5f;
      bool focused = (i == deps.focus_index);

      GridItemAnim &anim = deps.grid_item_anims[i];
      anim.tcx = base_cx;
      anim.tcy = base_cy + pp.shift_y;
      anim.tw = focused ? deps.focus_cover_w : static_cast<float>(deps.layout.cover_w);
      anim.th = focused ? deps.focus_cover_h : static_cast<float>(deps.layout.cover_h);
      anim.t_alpha = focused ? 255.0f : deps.unfocused_alpha;
      if (!deps.animate_enabled || deps.page_animating) {
        anim.SnapToTarget();
      } else {
        anim.Update(deps.dt, deps.card_move_linear_speed_x, deps.card_move_linear_speed_y,
                    deps.card_move_tail_ratio, deps.card_move_tail_min_mul,
                    deps.card_scale_linear_speed_w, deps.card_scale_linear_speed_h,
                    deps.card_scale_tail_ratio, deps.card_scale_tail_min_mul);
        deps.any_grid_animating = deps.any_grid_animating || anim.IsAnimating();
      }
      render_items.push_back(RenderEntry{i, focused, pp.page == deps.shelf_page});
    }
  }

  auto draw_cover = [&](const BookItem &item, SDL_Rect dst, Uint8 alpha) {
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
        return;
      }
    }
    SDL_Color c = item.is_dir ? SDL_Color{86, 121, 157, alpha} : SDL_Color{66, 81, 102, alpha};
    deps.draw_rect(dst.x, dst.y, dst.w, dst.h, c, true);
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

    SDL_RenderSetClipRect(deps.renderer, &clip);
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
    SDL_RenderSetClipRect(deps.renderer, nullptr);
  };

  auto render_shelf_static_layer = [&]() {
    if (deps.ui_assets.background_main) {
      SDL_Rect bg_dst{0, 0, deps.layout.screen_w, deps.layout.screen_h};
      SDL_RenderCopy(deps.renderer, deps.ui_assets.background_main, nullptr, &bg_dst);
    }

    for (const RenderEntry &e : render_items) {
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

    if (deps.ui_assets.top_status_bar) draw_native(deps.ui_assets.top_status_bar, 0, 0);
    if (deps.ui_assets.bottom_hint_bar) {
      int bw = 0;
      int bh = 0;
      deps.get_texture_size(deps.ui_assets.bottom_hint_bar, bw, bh);
      draw_native(deps.ui_assets.bottom_hint_bar, 0, deps.layout.screen_h - bh);
    }
    if (deps.ui_assets.nav_l1_icon) draw_native(deps.ui_assets.nav_l1_icon, deps.layout.nav_l1_x, deps.layout.nav_l1_y);
    if (deps.ui_assets.nav_r1_icon) draw_native(deps.ui_assets.nav_r1_icon, deps.layout.nav_r1_x, deps.layout.nav_r1_y);
    if (deps.ui_assets.nav_selected_pill) {
      int pw = 0;
      int ph = 0;
      deps.get_texture_size(deps.ui_assets.nav_selected_pill, pw, ph);
      const int slot_center_x =
          deps.layout.nav_start_x + deps.nav_selected_index * deps.layout.nav_slot_w + deps.layout.nav_slot_w / 2;
      draw_native(deps.ui_assets.nav_selected_pill, slot_center_x - pw / 2, deps.layout.nav_y);
    }
    const std::array<std::string, 4> nav_labels = {"ALL COMICS", "ALL BOOKS", "COLLECTIONS", "HISTORY"};
    SDL_Color nav_text{238, 242, 250, 255};
    for (int i = 0; i < static_cast<int>(nav_labels.size()); ++i) {
      int tw = 0;
      int th = 0;
      SDL_Texture *tex = nullptr;
      deps.get_text_texture(nav_labels[i], nav_text, tw, th, tex);
      if (!tex) continue;
      const int slot_x = deps.layout.nav_start_x + i * deps.layout.nav_slot_w;
      const int tx = slot_x + std::max(0, (deps.layout.nav_slot_w - tw) / 2);
      const int ty = deps.layout.nav_y + std::max(0, (32 - th) / 2);
      SDL_Rect td{tx, ty, tw, th};
      SDL_RenderCopy(deps.renderer, tex, nullptr, &td);
    }
  };

  const bool can_use_shelf_render_cache = false && deps.renderer_supports_target_textures &&
                                          !deps.page_animating && !deps.any_grid_animating;
  const bool cache_matches = deps.render_cache.texture &&
                             deps.render_cache.focus_index == deps.focus_index &&
                             deps.render_cache.shelf_page == deps.shelf_page &&
                             deps.render_cache.nav_selected_index == deps.nav_selected_index &&
                             deps.render_cache.content_version == deps.shelf_content_version;
  if (can_use_shelf_render_cache && cache_matches) {
    SDL_RenderCopy(deps.renderer, deps.render_cache.texture, nullptr, nullptr);
  } else {
    InvalidateShelfRenderCache(deps.render_cache, deps.forget_texture_size);
    render_shelf_static_layer();
  }

  for (const RenderEntry &e : render_items) {
    if (!e.focused) continue;
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
}
