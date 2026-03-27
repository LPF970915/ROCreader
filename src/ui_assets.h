#pragma once

#include <SDL.h>

#include <functional>

struct UiAssets {
  SDL_Texture *background_main = nullptr;
  SDL_Texture *top_status_bar = nullptr;
  SDL_Texture *bottom_hint_bar = nullptr;
  SDL_Texture *nav_l1_icon = nullptr;
  SDL_Texture *nav_r1_icon = nullptr;
  SDL_Texture *nav_selected_pill = nullptr;
  SDL_Texture *book_under_shadow = nullptr;
  SDL_Texture *book_select = nullptr;
  SDL_Texture *book_title_shadow = nullptr;
  SDL_Texture *book_cover_txt = nullptr;
  SDL_Texture *book_cover_pdf = nullptr;
  SDL_Texture *settings_preview_theme = nullptr;
  SDL_Texture *settings_preview_animations = nullptr;
  SDL_Texture *settings_preview_audio = nullptr;
  SDL_Texture *settings_preview_keyguide = nullptr;
  SDL_Texture *settings_preview_contact = nullptr;
  SDL_Texture *settings_preview_clean_history = nullptr;
  SDL_Texture *settings_preview_clean_cache = nullptr;
  SDL_Texture *settings_preview_txt_to_utf8 = nullptr;
  SDL_Texture *settings_preview_exit = nullptr;
};

using BeforeDestroyTextureFn = std::function<void(SDL_Texture *)>;

void DestroyUiAssets(UiAssets &assets, const BeforeDestroyTextureFn &before_destroy = {});
