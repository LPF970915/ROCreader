#include "status_bar_runtime.h"

#include <algorithm>
#include <string>

void DrawStatusBarRuntime(const StatusBarRenderDeps &deps) {
#ifdef HAVE_SDL2_TTF
  if (!deps.renderer || !deps.status) return;
  const SystemStatusSnapshot &status = *deps.status;
  SDL_Color text_color{238, 242, 250, 255};
  SDL_Color outline_color{238, 242, 250, 255};
  SDL_Color fill_color = status.charging ? SDL_Color{104, 214, 141, 255} : SDL_Color{238, 242, 250, 255};
  if (deps.theme != 0) {
    text_color = SDL_Color{58, 64, 76, 255};
    outline_color = SDL_Color{58, 64, 76, 255};
    fill_color = status.charging ? SDL_Color{76, 170, 98, 255} : SDL_Color{58, 64, 76, 255};
  }

  auto scale_px = [&](int value) { return deps.scale_px ? deps.scale_px(value) : value; };

  const int center_y = deps.top_bar_y + deps.top_bar_h / 2;
  const bool trimui_brick_status_layout = deps.input_profile == InputProfile::TrimuiBrick;
  const bool gkd350h_ultra_status_layout = deps.input_profile == InputProfile::GKD350HUltra;
  const int base_screen_w = 720;
  const int extra_status_x = trimui_brick_status_layout ? 0 : std::max(0, deps.screen_w - scale_px(base_screen_w));
  auto status_x = [&](int base_x) { return scale_px(base_x) + extra_status_x; };
  const int battery_shift_y = trimui_brick_status_layout ? 5 : (gkd350h_ultra_status_layout ? 0 : scale_px(3));
  const int h700_battery_shift_x = (deps.screen_w <= 640) ? -80 : 0;
  const int battery_icon_x = trimui_brick_status_layout ? 750
                             : gkd350h_ultra_status_layout ? 1249
                                                          : status_x(552 + h700_battery_shift_x);
  const int battery_text_x = trimui_brick_status_layout ? 806
                             : gkd350h_ultra_status_layout ? 1308
                                                          : status_x(587 + h700_battery_shift_x);
  const int clock_shift_x = trimui_brick_status_layout ? 64 : 40;
  const int clock_shift_y = trimui_brick_status_layout ? 5 : (gkd350h_ultra_status_layout ? 0 : scale_px(3));
  int clock_right = trimui_brick_status_layout ? deps.screen_w - 26 - clock_shift_x
                    : gkd350h_ultra_status_layout ? 1468
                                                 : deps.screen_w - 16 - clock_shift_x;

  if (!status.clock_text.empty()) {
    TextCacheEntry *clock_tex = deps.get_text_texture ? deps.get_text_texture(status.clock_text, text_color) : nullptr;
    if (clock_tex && clock_tex->texture) {
      const int clock_x = clock_right - clock_tex->w;
      const int clock_y = center_y - clock_tex->h / 2 + clock_shift_y;
      SDL_Rect td{clock_x, clock_y, clock_tex->w, clock_tex->h};
      SDL_RenderCopy(deps.renderer, clock_tex->texture, nullptr, &td);
    }
  }

  const int avatar_badge_size = gkd350h_ultra_status_layout ? 64 : scale_px(28);
  const int avatar_badge_x = gkd350h_ultra_status_layout
                                 ? 1512
                                 : deps.screen_w - scale_px(12) - avatar_badge_size;
  const int avatar_badge_y = gkd350h_ultra_status_layout ? 8 : scale_px(4);
  if (deps.draw_rect) {
    deps.draw_rect(avatar_badge_x, avatar_badge_y, avatar_badge_size, avatar_badge_size,
                   SDL_Color{26, 32, 42, 220}, true);
    deps.draw_rect(avatar_badge_x, avatar_badge_y, avatar_badge_size, avatar_badge_size,
                   SDL_Color{152, 185, 210, 235}, false);
  }
  if (deps.selected_avatar_badge_texture) {
    SDL_Rect avatar_dst{avatar_badge_x, avatar_badge_y, avatar_badge_size, avatar_badge_size};
    SDL_RenderCopy(deps.renderer, deps.selected_avatar_badge_texture, nullptr, &avatar_dst);
  }

  if (status.battery_available) {
    const std::string battery_text = std::to_string(status.battery_percent) + "%";
    TextCacheEntry *battery_tex = deps.get_text_texture ? deps.get_text_texture(battery_text, text_color) : nullptr;
    int battery_text_w = battery_tex ? battery_tex->w : 0;
    int battery_text_h = battery_tex ? battery_tex->h : 0;

    const int cap_w = gkd350h_ultra_status_layout ? 4 : scale_px(4);
    const int cap_h = gkd350h_ultra_status_layout ? 10 : scale_px(8);
    const int body_w = gkd350h_ultra_status_layout ? 47 : scale_px(24);
    const int body_h = gkd350h_ultra_status_layout ? 29 : scale_px(12);
    const int icon_x = battery_icon_x;
    const int icon_y = gkd350h_ultra_status_layout ? 26 : center_y - body_h / 2 + battery_shift_y;

    if (deps.draw_rect) {
      const int outline_width = gkd350h_ultra_status_layout ? 3 : 1;
      for (int inset = 0; inset < outline_width; ++inset) {
        deps.draw_rect(icon_x + inset, icon_y + inset,
                       body_w - inset * 2, body_h - inset * 2, outline_color, false);
      }
      deps.draw_rect(icon_x + body_w, center_y - cap_h / 2 + battery_shift_y, cap_w, cap_h, outline_color, true);

      const int inner_pad = gkd350h_ultra_status_layout ? 5 : scale_px(2);
      const int inner_w = body_w - inner_pad * 2;
      const int inner_h = body_h - inner_pad * 2;
      const int fill_w = std::clamp((inner_w * status.battery_percent) / 100, 0, inner_w);
      if (fill_w > 0) {
        deps.draw_rect(icon_x + inner_pad, icon_y + inner_pad, fill_w, inner_h, fill_color, true);
      }
    }

    if (battery_tex && battery_tex->texture) {
      SDL_Rect td{battery_text_x, center_y - battery_text_h / 2 + battery_shift_y, battery_text_w, battery_text_h};
      SDL_RenderCopy(deps.renderer, battery_tex->texture, nullptr, &td);
    }
  }
#else
  (void)deps;
#endif
}
