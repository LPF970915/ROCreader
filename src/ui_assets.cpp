#include "ui_assets.h"

namespace {
void DestroyTexture(SDL_Texture *&texture, const BeforeDestroyTextureFn &before_destroy) {
  if (!texture) return;
  if (before_destroy) before_destroy(texture);
  SDL_DestroyTexture(texture);
  texture = nullptr;
}
}

void DestroyUiAssets(UiAssets &assets, const BeforeDestroyTextureFn &before_destroy) {
  DestroyTexture(assets.background_main, before_destroy);
  DestroyTexture(assets.top_status_bar, before_destroy);
  DestroyTexture(assets.bottom_hint_bar, before_destroy);
  DestroyTexture(assets.nav_l1_icon, before_destroy);
  DestroyTexture(assets.nav_r1_icon, before_destroy);
  DestroyTexture(assets.nav_selected_pill, before_destroy);
  DestroyTexture(assets.book_under_shadow, before_destroy);
  DestroyTexture(assets.book_select, before_destroy);
  DestroyTexture(assets.book_title_shadow, before_destroy);
  DestroyTexture(assets.book_cover_txt, before_destroy);
  DestroyTexture(assets.book_cover_pdf, before_destroy);
  DestroyTexture(assets.settings_preview_theme, before_destroy);
  DestroyTexture(assets.settings_preview_animations, before_destroy);
  DestroyTexture(assets.settings_preview_audio, before_destroy);
  DestroyTexture(assets.settings_preview_keyguide, before_destroy);
  DestroyTexture(assets.settings_preview_contact, before_destroy);
  DestroyTexture(assets.settings_preview_clean_history, before_destroy);
  DestroyTexture(assets.settings_preview_clean_cache, before_destroy);
  DestroyTexture(assets.settings_preview_txt_to_utf8, before_destroy);
  DestroyTexture(assets.settings_preview_exit, before_destroy);
}
