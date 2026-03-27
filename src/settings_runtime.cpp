#include "settings_runtime.h"

#include <algorithm>
#include <cmath>

namespace {
std::string SettingLabel(SettingId id) {
  switch (id) {
  case SettingId::KeyGuide: return std::string(u8"按键说明");
  case SettingId::ClearHistory: return std::string(u8"清除历史");
  case SettingId::CleanCache: return std::string(u8"清除缓存");
  case SettingId::TxtToUtf8: return std::string(u8"TXT转码");
  case SettingId::ContactMe: return std::string(u8"联系我");
  case SettingId::ExitApp: return std::string(u8"退出");
  }
  return {};
}

SDL_Texture *SelectedPreviewTexture(const UiAssets &ui_assets, SettingId id) {
  switch (id) {
  case SettingId::KeyGuide: return ui_assets.settings_preview_keyguide;
  case SettingId::ClearHistory: return ui_assets.settings_preview_clean_history;
  case SettingId::CleanCache: return ui_assets.settings_preview_clean_cache;
  case SettingId::TxtToUtf8: return ui_assets.settings_preview_txt_to_utf8;
  case SettingId::ContactMe: return ui_assets.settings_preview_contact;
  case SettingId::ExitApp: return ui_assets.settings_preview_exit;
  }
  return nullptr;
}
}

void HandleSettingsInput(SettingsRuntimeInputDeps &deps) {
  if (!deps.ui_cfg.animations) {
    deps.menu_anim.Snap(deps.menu_closing ? 0.0f : 1.0f);
  }
  deps.menu_anim.Update(deps.dt);

  if (deps.menu_closing && deps.menu_anim.Value() <= 0.0001f && !deps.menu_anim.IsAnimating()) {
    if (deps.on_close) deps.on_close();
    return;
  }

  if (!deps.settings_close_armed) {
    const bool any_toggle_held = deps.input.IsPressed(Button::Start) || deps.input.IsPressed(Button::Select);
    if (!any_toggle_held) deps.settings_close_armed = true;
  }

  if (deps.settings_close_armed && deps.settings_toggle_guard <= 0.0f && !deps.menu_closing &&
      (deps.input.IsJustPressed(Button::B) || deps.menu_toggle_request)) {
    if (deps.ui_cfg.animations) deps.menu_anim.AnimateTo(0.0f, 0.16f, animation::Ease::InOutCubic);
    else deps.menu_anim.Snap(0.0f);
    deps.menu_closing = true;
    return;
  }

  if (deps.menu_closing) return;

  if (deps.input.IsJustPressed(Button::Up) || deps.input.IsRepeated(Button::Up)) {
    deps.menu_selected = std::clamp(deps.menu_selected - 1, 0, static_cast<int>(deps.menu_items.size()) - 1);
  } else if (deps.input.IsJustPressed(Button::Down) || deps.input.IsRepeated(Button::Down)) {
    deps.menu_selected = std::clamp(deps.menu_selected + 1, 0, static_cast<int>(deps.menu_items.size()) - 1);
  } else if (deps.input.IsJustPressed(Button::A) || deps.input.IsJustPressed(Button::Right)) {
    const SettingId id = deps.menu_items[deps.menu_selected];
    if (id == SettingId::ExitApp) {
      if (deps.on_exit_app) deps.on_exit_app();
    } else if (id == SettingId::ClearHistory) {
      if (deps.on_clear_history) deps.on_clear_history();
    } else if (id == SettingId::CleanCache) {
      if (deps.on_clean_cache) deps.on_clean_cache();
    } else if (id == SettingId::TxtToUtf8) {
      if (deps.on_txt_to_utf8) deps.on_txt_to_utf8();
    }
  }
}

void DrawSettingsRuntime(SettingsRuntimeRenderDeps &deps) {
  const float anim_progress = deps.menu_anim.Value();
  const float eased =
      deps.cfg.animations ? animation::ApplyEase(animation::Ease::OutCubic, anim_progress) : anim_progress;
  const int menu_y = deps.layout.settings_y_offset;
  const int menu_h = std::max(0, deps.layout.screen_h - menu_y);
  const int menu_width = deps.layout.settings_sidebar_w;
  const int x = static_cast<int>(-menu_width + menu_width * eased);

  deps.draw_rect(x, menu_y, menu_width, menu_h,
                 SDL_Color{0, 0, 0, static_cast<Uint8>(eased * deps.sidebar_mask_max_alpha)}, true);

  const int preview_x = x + menu_width;
  const int preview_w = std::max(0, deps.layout.screen_w - preview_x);
  int preview_center_x = preview_x + preview_w / 2;
  if (preview_w > 0) {
    const SettingId selected =
        deps.menu_items[std::clamp(deps.menu_selected, 0, static_cast<int>(deps.menu_items.size()) - 1)];
    SDL_Texture *preview_tex = SelectedPreviewTexture(deps.ui_assets, selected);
    if (preview_tex) {
      int pw = 0;
      int ph = 0;
      deps.get_texture_size(preview_tex, pw, ph);
      SDL_Rect pd{preview_x, menu_y, pw, ph};
      SDL_RenderCopy(deps.renderer, preview_tex, nullptr, &pd);
      preview_center_x = pd.x + pd.w / 2;
    }
  }

  deps.draw_rect(x, menu_y, menu_width, menu_h, SDL_Color{24, 34, 46, 236}, true);
  deps.draw_rect(x + menu_width - 1, menu_y, 1, menu_h, SDL_Color{82, 125, 158, 255}, true);

  int text_left = x + 32;
  int y = menu_y + 84 + deps.layout.settings_content_offset_y;
#ifdef HAVE_SDL2_TTF
  const std::string menu_title = std::string(u8"ROC全能漫画阅读器");
  const SDL_Color title_color{240, 246, 255, 255};
  const SDL_Color item_color{230, 236, 248, 255};
  TextCacheEntry *title_tex = deps.get_title_text_texture ? deps.get_title_text_texture(menu_title, title_color) : nullptr;
  int divider_y = menu_y + 68 + deps.layout.settings_content_offset_y;
  if (title_tex && title_tex->texture) {
    const int side_margin = std::max(0, (menu_width - title_tex->w) / 2);
    const int title_x = x + side_margin;
    const int title_y = menu_y + side_margin + deps.layout.settings_content_offset_y;
    const int title_gap = side_margin;
    divider_y = title_y + title_tex->h + title_gap;
    SDL_Rect td{title_x, title_y, title_tex->w, title_tex->h};
    SDL_RenderCopy(deps.renderer, title_tex->texture, nullptr, &td);
  }
  deps.draw_rect(x + 8, divider_y, menu_width - 16, 1, SDL_Color{66, 95, 124, 255}, true);
  y = divider_y + 12;
  text_left = x + 32;
#else
  deps.draw_rect(x + 8, 72 + deps.layout.settings_content_offset_y, menu_width - 16, 1,
                 SDL_Color{66, 95, 124, 255}, true);
#endif

  for (size_t i = 0; i < deps.menu_items.size(); ++i) {
    const bool sel = static_cast<int>(i) == deps.menu_selected;
    const SDL_Color c = sel ? SDL_Color{63, 119, 158, 255} : SDL_Color{57, 73, 96, 214};
    deps.draw_rect(x + 12, y, menu_width - 24, 30, c, true);
    if (sel) {
      deps.draw_rect(x + 12, y, 3, 30, SDL_Color{139, 214, 255, 255}, true);
      deps.draw_rect(x + 11, y - 1, menu_width - 22, 32, SDL_Color{85, 152, 198, 208}, false);
    }
#ifdef HAVE_SDL2_TTF
    const std::string label_text = SettingLabel(deps.menu_items[i]);
    if (!label_text.empty() && deps.get_text_texture) {
      TextCacheEntry *label_tex = deps.get_text_texture(label_text, item_color);
      if (label_tex && label_tex->texture) {
        const int ty = y + std::max(0, (30 - label_tex->h) / 2);
        SDL_Rect td{text_left, ty, label_tex->w, label_tex->h};
        SDL_RenderCopy(deps.renderer, label_tex->texture, nullptr, &td);
      }
    }
#endif
    y += 42;
  }

  const SettingId selected =
      deps.menu_items[std::clamp(deps.menu_selected, 0, static_cast<int>(deps.menu_items.size()) - 1)];
  if (selected == SettingId::TxtToUtf8 && deps.txt_transcode_job.active) {
    const int bar_w = std::min(260, std::max(160, preview_w - 36));
    const int bar_h = 14;
    const int bar_x = std::clamp(preview_center_x - bar_w / 2, preview_x + 8,
                                 preview_x + std::max(8, preview_w - bar_w - 8));
    const int bar_y = 308;
    const float progress =
        (deps.txt_transcode_job.total > 0)
            ? static_cast<float>(deps.txt_transcode_job.processed) / static_cast<float>(deps.txt_transcode_job.total)
            : 0.0f;
    const int fill_w = std::clamp(static_cast<int>(std::lround(bar_w * progress)), 0, bar_w);
    deps.draw_rect(bar_x, bar_y, bar_w, bar_h, SDL_Color{46, 52, 62, 224}, true);
    deps.draw_rect(bar_x, bar_y, fill_w, bar_h, SDL_Color{63, 119, 158, 255}, true);
    deps.draw_rect(bar_x, bar_y, bar_w, bar_h, SDL_Color{255, 255, 255, 210}, false);
#ifdef HAVE_SDL2_TTF
    if (deps.get_text_texture) {
      const int pct = std::clamp(static_cast<int>(std::lround(progress * 100.0f)), 0, 100);
      const std::string progress_text = std::string(u8"转码中 ") + std::to_string(deps.txt_transcode_job.processed) +
                                        "/" + std::to_string(deps.txt_transcode_job.total) + "  (" +
                                        std::to_string(pct) + "%)";
      TextCacheEntry *progress_tex = deps.get_text_texture(progress_text, SDL_Color{245, 248, 252, 255});
      if (progress_tex && progress_tex->texture) {
        SDL_Rect pd{preview_center_x - progress_tex->w / 2, bar_y + bar_h + 10, progress_tex->w, progress_tex->h};
        SDL_RenderCopy(deps.renderer, progress_tex->texture, nullptr, &pd);
      }
      if (!deps.txt_transcode_job.current_file.empty() && deps.utf8_ellipsize) {
        const std::string file_text = deps.utf8_ellipsize(deps.txt_transcode_job.current_file, 24);
        TextCacheEntry *file_tex = deps.get_text_texture(file_text, SDL_Color{184, 197, 212, 255});
        if (file_tex && file_tex->texture) {
          SDL_Rect fd{preview_center_x - file_tex->w / 2, bar_y + bar_h + 32, file_tex->w, file_tex->h};
          SDL_RenderCopy(deps.renderer, file_tex->texture, nullptr, &fd);
        }
      }
    }
#endif
  }

  auto draw_native_topmost = [&](SDL_Texture *tex, int px, int py) {
    if (!tex) return;
    int tw = 0;
    int th = 0;
    deps.get_texture_size(tex, tw, th);
    if (tw <= 0 || th <= 0) return;
    SDL_Rect dst{px, py, tw, th};
    SDL_RenderCopy(deps.renderer, tex, nullptr, &dst);
  };

  if (deps.ui_assets.top_status_bar) {
    draw_native_topmost(deps.ui_assets.top_status_bar, 0, 0);
  } else {
    deps.draw_rect(0, deps.layout.top_bar_y, deps.layout.screen_w, deps.layout.top_bar_h, SDL_Color{8, 10, 14, 255},
                   true);
  }

  if (deps.draw_volume_overlay) deps.draw_volume_overlay();

  if (deps.ui_assets.bottom_hint_bar) {
    int bw = 0;
    int bh = 0;
    deps.get_texture_size(deps.ui_assets.bottom_hint_bar, bw, bh);
    draw_native_topmost(deps.ui_assets.bottom_hint_bar, 0, deps.layout.screen_h - bh);
  } else {
    deps.draw_rect(0, deps.layout.bottom_bar_y, deps.layout.screen_w, deps.layout.bottom_bar_h,
                   SDL_Color{8, 10, 14, 255}, true);
  }
}
