#include "txt_settings_runtime.h"
#include "app_language.h"
#include "gkd_menu_button_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {
constexpr int kColorOptionCount = 5;
constexpr int kSelectableRowCount = 4;
constexpr int kTranscodeRow = 3;

constexpr std::array<SDL_Color, kColorOptionCount> kBackgroundColors = {{
    {255, 255, 255, 255},
    {239, 226, 198, 255},
    {205, 220, 204, 255},
    {88, 102, 118, 255},
    {0, 0, 0, 255},
}};

constexpr std::array<SDL_Color, kColorOptionCount> kFontColors = {{
    {255, 255, 255, 255},
    {214, 219, 224, 255},
    {124, 134, 145, 255},
    {70, 78, 88, 255},
    {0, 0, 0, 255},
}};

constexpr std::array<int, 10> kFontPointSizes = {{18, 20, 22, 24, 26, 28, 30, 32, 34, 36}};

int ColorLuma(SDL_Color color) { return color.r * 299 + color.g * 587 + color.b * 114; }

int ScalePx(float scale, int value) {
  return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * std::max(0.1f, scale))));
}

void SyncSelectedOptionForRow(TxtSettingsState &state) {
  if (state.selected_row == 0) {
    state.selected_option = ClampTxtColorIndex(state.background_color);
  } else if (state.selected_row == 1) {
    state.selected_option = ClampTxtColorIndex(state.font_color);
  } else if (state.selected_row == 2) {
    state.selected_option = std::clamp(state.selected_option, 0, 1);
  } else {
    state.selected_option = 0;
  }
}
} // namespace

int ClampTxtColorIndex(int value) { return std::clamp(value, 0, kColorOptionCount - 1); }

int ClampTxtFontSizeLevel(int value) {
  return std::clamp(value, 0, static_cast<int>(kFontPointSizes.size()) - 1);
}

int TxtFontPointSizeForLevel(int level) {
  return kFontPointSizes[static_cast<size_t>(ClampTxtFontSizeLevel(level))];
}

SDL_Color GetTxtBackgroundColor(int index) {
  return kBackgroundColors[static_cast<size_t>(ClampTxtColorIndex(index))];
}

SDL_Color GetTxtFontColor(int index) {
  return kFontColors[static_cast<size_t>(ClampTxtColorIndex(index))];
}

bool HandleTxtSettingsInput(const InputManager &input, TxtSettingsState &state,
                            const TxtSettingsCallbacks &callbacks) {
  if (!state.panel_active) {
    if (input.IsJustPressed(Button::A) || input.IsJustPressed(Button::Right)) {
      state.panel_active = true;
      state.selected_row = std::clamp(state.selected_row, 0, kSelectableRowCount - 1);
      state.selected_option = std::clamp(state.selected_option, 0, kColorOptionCount - 1);
      if (callbacks.refresh_state) callbacks.refresh_state(state);
      return true;
    }
    return false;
  }

  if (input.IsJustPressed(Button::B)) {
    state.panel_active = false;
    return true;
  }

  if (input.IsJustPressed(Button::Up) || input.IsRepeated(Button::Up)) {
    state.selected_row = (state.selected_row - 1 + kSelectableRowCount) % kSelectableRowCount;
    SyncSelectedOptionForRow(state);
    return true;
  }

  if (input.IsJustPressed(Button::Down) || input.IsRepeated(Button::Down)) {
    state.selected_row = (state.selected_row + 1) % kSelectableRowCount;
    SyncSelectedOptionForRow(state);
    return true;
  }

  if (state.selected_row == 0 || state.selected_row == 1) {
    if (input.IsJustPressed(Button::Left) || input.IsRepeated(Button::Left)) {
      state.selected_option = ClampTxtColorIndex(state.selected_option - 1);
      return true;
    }
    if (input.IsJustPressed(Button::Right) || input.IsRepeated(Button::Right)) {
      state.selected_option = ClampTxtColorIndex(state.selected_option + 1);
      return true;
    }
    if (input.IsJustPressed(Button::A)) {
      if (state.selected_row == 0 && callbacks.set_background_color) {
        return callbacks.set_background_color(state.selected_option, state);
      }
      if (state.selected_row == 1 && callbacks.set_font_color) {
        return callbacks.set_font_color(state.selected_option, state);
      }
    }
    return false;
  }

  if (state.selected_row == 2) {
    if (input.IsJustPressed(Button::Left)) {
      state.selected_option = 0;
      return true;
    }
    if (input.IsJustPressed(Button::Right)) {
      state.selected_option = 1;
      return true;
    }
    int delta = 0;
    if (input.IsJustPressed(Button::A) || input.IsRepeated(Button::A)) {
      delta = state.selected_option == 0 ? -1 : 1;
    } else if (input.IsRepeated(Button::Left) && state.selected_option == 0) {
      delta = -1;
    } else if (input.IsRepeated(Button::Right) && state.selected_option == 1) {
      delta = 1;
    }
    if (delta != 0 && callbacks.adjust_font_size) {
      return callbacks.adjust_font_size(delta, state);
    }
    return delta != 0;
  }

  if ((input.IsJustPressed(Button::A) || input.IsRepeated(Button::A)) && callbacks.start_transcode) {
    return callbacks.start_transcode();
  }
  return false;
}

void DrawTxtSettingsPreview(const TxtSettingsRenderDeps &deps) {
  if (!deps.renderer) return;

  const SDL_Color text_color = deps.light_theme ? SDL_Color{44, 50, 60, 255} : SDL_Color{236, 241, 247, 255};
  const SDL_Color muted_color = deps.light_theme ? SDL_Color{128, 134, 142, 255} : SDL_Color{149, 164, 181, 255};
  const SDL_Color button_fill = deps.light_theme ? SDL_Color{229, 224, 214, 240} : SDL_Color{29, 42, 57, 230};
  const SDL_Color button_selected = deps.light_theme ? SDL_Color{213, 228, 239, 250} : SDL_Color{41, 82, 113, 240};
  const SDL_Color button_border = deps.light_theme ? SDL_Color{104, 122, 144, 255} : SDL_Color{122, 201, 255, 255};
  const SDL_Color divider_color = deps.light_theme ? SDL_Color{138, 154, 170, 255} : SDL_Color{66, 95, 124, 255};
  const SDL_Color transcode_fill = deps.light_theme ? SDL_Color{72, 122, 164, 255} : SDL_Color{63, 119, 158, 255};

  auto get_text_entry = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    return deps.get_text_texture ? deps.get_text_texture(text, color) : nullptr;
  };
  auto get_emphasis_entry = [&](const std::string &text, SDL_Color color) -> TextCacheEntry * {
    if (deps.get_emphasis_text_texture) return deps.get_emphasis_text_texture(text, color);
    return get_text_entry(text, color);
  };

  const float scale = deps.ui_scale;
  const bool gkd_profile = deps.gkd_profile;
  const int divider_inset = ScalePx(scale, gkd_profile ? 18 : 10);
  const int preview_padding_x = ScalePx(scale, gkd_profile ? 32 : 16);
  const int row_center0 = deps.first_row_y + deps.row_height / 2;
  const int row_center1 = deps.first_row_y + deps.row_pitch + deps.row_height / 2;
  const int row_center2 = deps.first_row_y + deps.row_pitch * 2 + deps.row_height / 2;
  const int row_center3 = deps.first_row_y + deps.row_pitch * 3 + deps.row_height / 2;
  const int arrow_text_h = std::max(
      get_emphasis_entry("<", text_color) ? get_emphasis_entry("<", text_color)->h : 0,
      get_emphasis_entry(">", text_color) ? get_emphasis_entry(">", text_color)->h : 0);
  const int transcode_text_h =
      get_text_entry(LocalizedAppText(deps.language_index, AppTextId::TxtStartTranscode), text_color)
          ? get_text_entry(LocalizedAppText(deps.language_index, AppTextId::TxtStartTranscode), text_color)->h
          : 0;
  const int button_h = gkd_profile ? gkd_menu::ControlH(scale)
                                   : std::max(ScalePx(scale, 28), arrow_text_h + ScalePx(scale, 8));
  const int button_w = gkd_profile ? gkd_menu::StepButtonW(scale) : std::max(ScalePx(scale, 28), button_h);
  const int color_block_h = gkd_profile ? std::max(ScalePx(scale, 36), button_h - ScalePx(scale, 4)) : button_h;
  const int color_block_w = gkd_profile ? std::max(ScalePx(scale, 56), color_block_h * 3 / 2) : button_w * 2;
  const int color_gap = ScalePx(scale, gkd_profile ? 10 : 8);
  const int transcode_button_h = gkd_profile
                                     ? gkd_menu::ControlH(scale)
                                     : std::max(ScalePx(scale, 28), transcode_text_h + ScalePx(scale, 8));
  const int number_w = ScalePx(scale, 30);

  deps.draw_rect(deps.preview_rect.x + divider_inset,
                 deps.first_row_y - ScalePx(scale, gkd_profile ? 18 : 12),
                 std::max(0, deps.preview_rect.w - divider_inset * 2),
                 ScalePx(scale, 1),
                 divider_color,
                 true);

  const std::array<const char *, 4> labels = {{
      LocalizedAppText(deps.language_index, AppTextId::TxtBackgroundColor),
      LocalizedAppText(deps.language_index, AppTextId::TxtFontColor),
      LocalizedAppText(deps.language_index, AppTextId::TxtFontSize),
      LocalizedAppText(deps.language_index, AppTextId::TxtTranscode),
  }};
  const std::array<int, 4> row_centers = {{row_center0, row_center1, row_center2, row_center3}};

  const int color_controls_w = kColorOptionCount * color_block_w + (kColorOptionCount - 1) * color_gap;
  const int mid_gap = ScalePx(scale, 10);
  const int min_transcode_button_w = gkd_profile ? gkd_menu::LongButtonW(scale) : ScalePx(scale, 82);
  const int transcode_button_padding = ScalePx(scale, 18);
  const int transcode_button_w =
      gkd_profile
          ? min_transcode_button_w
          : std::max(min_transcode_button_w,
                     (get_text_entry(LocalizedAppText(deps.language_index, AppTextId::TxtStartTranscode), text_color)
                              ? get_text_entry(LocalizedAppText(deps.language_index, AppTextId::TxtStartTranscode),
                                               text_color)
                                    ->w
                              : 0) +
                         transcode_button_padding * 2);
  const int right_padding_x = gkd_profile ? gkd_menu::RightPadding(scale) : preview_padding_x;
  const int content_left = deps.preview_rect.x + (gkd_profile ? ScalePx(scale, 16) : preview_padding_x);
  const int control_right = deps.preview_rect.x + deps.preview_rect.w - right_padding_x;

  for (size_t i = 0; i < labels.size(); ++i) {
    if (TextCacheEntry *entry = get_text_entry(labels[i], text_color); entry && entry->texture) {
      SDL_Rect dst{content_left, row_centers[i] - entry->h / 2, entry->w, entry->h};
      SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
    }
  }

  const int color_left_x = control_right - color_controls_w;
  const int right_btn_x = control_right - button_w;
  const int number_x = right_btn_x - mid_gap - number_w;
  const int left_btn_x = number_x - mid_gap - button_w;
  const int button_y = row_center2 - button_h / 2;
  const bool left_selected = deps.state.panel_active && deps.state.selected_row == 2 && deps.state.selected_option == 0;
  const bool right_selected = deps.state.panel_active && deps.state.selected_row == 2 && deps.state.selected_option == 1;

  for (int i = 0; i < kColorOptionCount; ++i) {
    const int x = color_left_x + i * (color_block_w + color_gap);
    const int y0 = row_center0 - color_block_h / 2;
    const int y1 = row_center1 - color_block_h / 2;
    const bool selected_bg = deps.state.panel_active && deps.state.selected_row == 0 && deps.state.selected_option == i;
    const bool selected_fg = deps.state.panel_active && deps.state.selected_row == 1 && deps.state.selected_option == i;
    const SDL_Color bg_color = GetTxtBackgroundColor(i);
    const SDL_Color fg_color = GetTxtFontColor(i);
    const SDL_Color bg_contrast = ColorLuma(bg_color) > 140000 ? SDL_Color{20, 24, 32, 255} : SDL_Color{245, 248, 252, 255};
    const SDL_Color fg_contrast = ColorLuma(fg_color) > 140000 ? SDL_Color{20, 24, 32, 255} : SDL_Color{245, 248, 252, 255};

    deps.draw_rect(x, y0, color_block_w, color_block_h, bg_color, true);
    deps.draw_rect(x, y0, color_block_w, color_block_h,
                   selected_bg ? button_border : (deps.state.background_color == i ? text_color : muted_color), false);
    if (selected_bg && color_block_w > 8 && color_block_h > 8) {
      deps.draw_rect(x + ScalePx(scale, 2), y0 + ScalePx(scale, 2),
                     color_block_w - ScalePx(scale, 4), color_block_h - ScalePx(scale, 4), bg_contrast, false);
    }

    deps.draw_rect(x, y1, color_block_w, color_block_h, fg_color, true);
    deps.draw_rect(x, y1, color_block_w, color_block_h,
                   selected_fg ? button_border : (deps.state.font_color == i ? text_color : muted_color), false);
    if (selected_fg && color_block_w > 8 && color_block_h > 8) {
      deps.draw_rect(x + ScalePx(scale, 2), y1 + ScalePx(scale, 2),
                     color_block_w - ScalePx(scale, 4), color_block_h - ScalePx(scale, 4), fg_contrast, false);
    }
  }

  deps.draw_rect(left_btn_x, button_y, button_w, button_h, left_selected ? button_selected : button_fill, true);
  deps.draw_rect(left_btn_x, button_y, button_w, button_h, left_selected ? button_border : muted_color, false);
  if (TextCacheEntry *entry = get_emphasis_entry("<", text_color); entry && entry->texture) {
    SDL_Rect dst{left_btn_x + (button_w - entry->w) / 2, button_y + (button_h - entry->h) / 2, entry->w, entry->h};
    SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
  }

  if (TextCacheEntry *entry = get_text_entry(std::to_string(TxtFontPointSizeForLevel(deps.state.font_size_level)), text_color);
      entry && entry->texture) {
    SDL_Rect dst{number_x + (number_w - entry->w) / 2, row_center2 - entry->h / 2, entry->w, entry->h};
    SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
  }

  deps.draw_rect(right_btn_x, button_y, button_w, button_h, right_selected ? button_selected : button_fill, true);
  deps.draw_rect(right_btn_x, button_y, button_w, button_h, right_selected ? button_border : muted_color, false);
  if (TextCacheEntry *entry = get_emphasis_entry(">", text_color); entry && entry->texture) {
    SDL_Rect dst{right_btn_x + (button_w - entry->w) / 2, button_y + (button_h - entry->h) / 2, entry->w, entry->h};
    SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
  }

  const int transcode_button_x = control_right - transcode_button_w;
  const int transcode_button_y = row_center3 - transcode_button_h / 2;
  const bool transcode_selected = deps.state.panel_active && deps.state.selected_row == kTranscodeRow;
  deps.draw_rect(transcode_button_x, transcode_button_y, transcode_button_w, transcode_button_h,
                 transcode_selected ? button_selected : button_fill, true);
  deps.draw_rect(transcode_button_x, transcode_button_y, transcode_button_w, transcode_button_h,
                 transcode_selected ? button_border : muted_color, false);
  if (TextCacheEntry *entry =
          get_text_entry(LocalizedAppText(deps.language_index, AppTextId::TxtStartTranscode), text_color);
      entry && entry->texture) {
    SDL_Rect dst{transcode_button_x + (transcode_button_w - entry->w) / 2,
                 row_center3 - entry->h / 2,
                 entry->w,
                 entry->h};
    SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
  }

  if (!deps.txt_transcode_job.active) return;

  const int bar_w = std::max(ScalePx(scale, 150), deps.preview_rect.w - ScalePx(scale, 64));
  const int bar_h = ScalePx(scale, 14);
  const int bar_x = deps.preview_rect.x + (deps.preview_rect.w - bar_w) / 2;
  const int bar_y = row_center3 + ScalePx(scale, 26);
  const float progress = deps.txt_transcode_job.total > 0
                             ? static_cast<float>(deps.txt_transcode_job.processed) /
                                   static_cast<float>(deps.txt_transcode_job.total)
                             : 0.0f;
  const int fill_w = std::clamp(static_cast<int>(bar_w * progress), 0, bar_w);

  deps.draw_rect(bar_x, bar_y, bar_w, bar_h, SDL_Color{46, 52, 62, 224}, true);
  deps.draw_rect(bar_x, bar_y, fill_w, bar_h, transcode_fill, true);
  deps.draw_rect(bar_x, bar_y, bar_w, bar_h, SDL_Color{255, 255, 255, 210}, false);

  if (TextCacheEntry *entry = get_text_entry(
          std::to_string(deps.txt_transcode_job.processed) + "/" + std::to_string(deps.txt_transcode_job.total),
          text_color);
      entry && entry->texture) {
    SDL_Rect dst{deps.preview_rect.x + (deps.preview_rect.w - entry->w) / 2, bar_y + bar_h + ScalePx(scale, 8), entry->w, entry->h};
    SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
  }

  if (!deps.txt_transcode_job.current_file.empty() && deps.utf8_ellipsize) {
    const std::string file_text = deps.utf8_ellipsize(deps.txt_transcode_job.current_file, 24);
    if (TextCacheEntry *entry = get_text_entry(file_text, muted_color); entry && entry->texture) {
      SDL_Rect dst{deps.preview_rect.x + (deps.preview_rect.w - entry->w) / 2, bar_y + bar_h + ScalePx(scale, 28), entry->w, entry->h};
      SDL_RenderCopy(deps.renderer, entry->texture, nullptr, &dst);
    }
  }
}
