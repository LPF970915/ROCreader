#include "ui_text_cache.h"

#include <SDL.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
void DestroyCachedTexture(SDL_Texture *&texture, const BeforeDestroyTextTextureFn &before_destroy) {
  if (!texture) return;
  if (before_destroy) before_destroy(texture);
  SDL_DestroyTexture(texture);
  texture = nullptr;
}

std::string MakeTextKey(const std::string &text, SDL_Color color) {
  return text + "|" + std::to_string(static_cast<int>(color.r)) + "," +
         std::to_string(static_cast<int>(color.g)) + "," + std::to_string(static_cast<int>(color.b));
}

void PruneTextCache(UiTextCacheState &state, const BeforeDestroyTextTextureFn &before_destroy = {}) {
  while (state.text_cache.size() > state.max_text_cache_entries) {
    auto oldest = state.text_cache.end();
    for (auto it = state.text_cache.begin(); it != state.text_cache.end(); ++it) {
      if (oldest == state.text_cache.end() || it->second.last_use < oldest->second.last_use) oldest = it;
    }
    if (oldest == state.text_cache.end()) break;
    DestroyCachedTexture(oldest->second.texture, before_destroy);
    state.text_cache.erase(oldest);
  }
}

#ifdef HAVE_SDL2_TTF
TTF_Font *SelectFont(UiTextCacheState &state, UiTextRole role) {
  if (role == UiTextRole::Title) return state.title_font;
  if (role == UiTextRole::Reader) return state.reader_font;
  return state.font;
}

const char *RolePrefix(UiTextRole role) {
  if (role == UiTextRole::Title) return "t24|";
  if (role == UiTextRole::Reader) return "r|";
  return "";
}
#endif
}

void OpenUiFonts(UiTextCacheState &state, const std::filesystem::path &exe_path,
                 const std::filesystem::path &ui_path, int reader_font_pt) {
#ifndef HAVE_SDL2_TTF
  (void)state;
  (void)exe_path;
  (void)ui_path;
  (void)reader_font_pt;
#else
  if (state.font_attempted) return;
  state.font_attempted = true;
  if (state.font) return;
  const std::vector<std::string> candidates = {
      (exe_path / "fonts" / "ui_font_02.ttf").lexically_normal().string(),
      (exe_path / "fonts" / "ui_font.ttf").lexically_normal().string(),
      (exe_path.parent_path() / "fonts" / "ui_font_02.ttf").lexically_normal().string(),
      (exe_path.parent_path() / "fonts" / "ui_font.ttf").lexically_normal().string(),
      (std::filesystem::current_path() / "fonts" / "ui_font_02.ttf").lexically_normal().string(),
      (std::filesystem::current_path() / "fonts" / "ui_font.ttf").lexically_normal().string(),
      (ui_path.parent_path() / "fonts" / "ui_font_02.ttf").lexically_normal().string(),
      (ui_path.parent_path() / "fonts" / "ui_font.ttf").lexically_normal().string(),
      (exe_path / ".." / "ui_font_02.ttf").lexically_normal().string(),
      (exe_path / ".." / "ui_font.ttf").lexically_normal().string(),
      (std::filesystem::current_path() / "ui_font_02.ttf").lexically_normal().string(),
      (std::filesystem::current_path() / "ui_font.ttf").lexically_normal().string(),
      "ui_font_02.ttf",
      "ui_font.ttf",
      (ui_path / "fonts" / "ui_font_02.ttf").string(),
      (ui_path / "fonts" / "ui_font.ttf").string(),
      (ui_path / "fonts" / "ui_font.otf").string(),
      "C:/Windows/Fonts/msyh.ttc",
      "C:/Windows/Fonts/msyh.ttf",
      "C:/Windows/Fonts/simhei.ttf",
      "C:/Windows/Fonts/simsun.ttc",
      "C:/Windows/Fonts/arial.ttf",
      "/Roms/APPS/ROCreader/fonts/ui_font_02.ttf",
      "/Roms/APPS/ROCreader/fonts/ui_font.ttf",
      "/mnt/mmc/ROCreader/fonts/ui_font_02.ttf",
      "/mnt/mmc/ROCreader/fonts/ui_font.ttf",
      "/mnt/mmc/Roms/ROCreader/fonts/ui_font_02.ttf",
      "/mnt/mmc/Roms/ROCreader/fonts/ui_font.ttf",
      "/mnt/mmc2/ROCreader/fonts/ui_font_02.ttf",
      "/mnt/mmc2/ROCreader/fonts/ui_font.ttf",
      "/mnt/mmc2/Roms/ROCreader/fonts/ui_font_02.ttf",
      "/mnt/mmc2/Roms/ROCreader/fonts/ui_font.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
  };
  for (const auto &path : candidates) {
    if (!std::filesystem::exists(path)) continue;
    std::cout << "[native_h700] ui font try: " << path << "\n";
    TTF_Font *font = TTF_OpenFont(path.c_str(), 16);
    if (!font) {
      std::cerr << "[native_h700] ui font open failed: body path=" << path
                << " err=" << TTF_GetError() << "\n";
      continue;
    }
    TTF_Font *title_font = TTF_OpenFont(path.c_str(), 24);
    if (!title_font) {
      std::cerr << "[native_h700] ui font open failed: title path=" << path
                << " err=" << TTF_GetError() << "\n";
      TTF_CloseFont(font);
      continue;
    }
    TTF_Font *reader_font = TTF_OpenFont(path.c_str(), reader_font_pt);
    if (!reader_font) {
      std::cerr << "[native_h700] ui font open failed: reader path=" << path
                << " pt=" << reader_font_pt
                << " err=" << TTF_GetError() << "\n";
      TTF_CloseFont(title_font);
      TTF_CloseFont(font);
      continue;
    }
    state.font = font;
    state.title_font = title_font;
    state.reader_font = reader_font;
    std::cout << "[native_h700] ui font selected: " << path;
    if (path.find("ui_font.ttf") != std::string::npos ||
        path.find("ui_font_02.ttf") != std::string::npos) {
      std::cout << " (project font)";
    }
    std::cout << " reader_pt=" << reader_font_pt << "\n";
    break;
  }
  if (!state.font || !state.title_font || !state.reader_font) {
    if (state.reader_font) {
      TTF_CloseFont(state.reader_font);
      state.reader_font = nullptr;
    }
    if (state.title_font) {
      TTF_CloseFont(state.title_font);
      state.title_font = nullptr;
    }
    if (state.font) {
      TTF_CloseFont(state.font);
      state.font = nullptr;
    }
    std::cerr << "[native_h700] warning: ui fonts not fully available; body/title/reader text disabled\n";
  }
#endif
}

void ClearUiTextCache(UiTextCacheState &state, const BeforeDestroyTextTextureFn &before_destroy) {
  for (auto &kv : state.text_cache) {
    DestroyCachedTexture(kv.second.texture, before_destroy);
  }
  state.text_cache.clear();
  state.title_ellipsize_cache.clear();
}

void ShutdownUiTextCache(UiTextCacheState &state, const BeforeDestroyTextTextureFn &before_destroy) {
  ClearUiTextCache(state, before_destroy);
#ifdef HAVE_SDL2_TTF
  if (state.reader_font) {
    TTF_CloseFont(state.reader_font);
    state.reader_font = nullptr;
  }
  if (state.title_font) {
    TTF_CloseFont(state.title_font);
    state.title_font = nullptr;
  }
  if (state.font) {
    TTF_CloseFont(state.font);
    state.font = nullptr;
  }
#endif
  state.font_attempted = false;
}

TextCacheEntry *GetUiTextTexture(UiTextCacheState &state, SDL_Renderer *renderer, const std::string &text,
                                 SDL_Color color, UiTextRole role) {
#ifndef HAVE_SDL2_TTF
  (void)state;
  (void)renderer;
  (void)text;
  (void)color;
  (void)role;
  return nullptr;
#else
  TTF_Font *font = SelectFont(state, role);
  if (!font || !renderer || text.empty()) return nullptr;
  const std::string key = std::string(RolePrefix(role)) + MakeTextKey(text, color);
  auto it = state.text_cache.find(key);
  if (it != state.text_cache.end()) {
    it->second.last_use = SDL_GetTicks();
    return &it->second;
  }
  SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) return nullptr;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  const int width = surface->w;
  const int height = surface->h;
  SDL_FreeSurface(surface);
  if (!texture) return nullptr;
  TextCacheEntry entry;
  entry.texture = texture;
  entry.w = width;
  entry.h = height;
  entry.last_use = SDL_GetTicks();
  auto [inserted, _] = state.text_cache.emplace(key, entry);
  PruneTextCache(state);
  return &inserted->second;
#endif
}

std::string GetTitleEllipsized(UiTextCacheState &state, const std::string &raw_name, int text_area_w,
                               const MeasureTextWidthFn &measure) {
  if (raw_name.empty()) return raw_name;
  const std::string key = raw_name + "|" + std::to_string(text_area_w);
  auto it = state.title_ellipsize_cache.find(key);
  if (it != state.title_ellipsize_cache.end()) {
    it->second.last_use = SDL_GetTicks();
    return it->second.display;
  }
  std::string display = raw_name;
  if (measure && measure(display) > text_area_w) {
    for (size_t max_chars = 24; max_chars >= 2; --max_chars) {
      std::string candidate = raw_name;
      if (candidate.size() > max_chars) {
        candidate = raw_name.substr(0, max_chars - 1) + "...";
      }
      display = std::move(candidate);
      if (measure(display) <= text_area_w || max_chars == 2) break;
    }
  }
  state.title_ellipsize_cache[key] = TitleEllipsisCacheEntry{display, SDL_GetTicks()};
  if (state.title_ellipsize_cache.size() > 128) {
    auto oldest = state.title_ellipsize_cache.end();
    for (auto eit = state.title_ellipsize_cache.begin(); eit != state.title_ellipsize_cache.end(); ++eit) {
      if (oldest == state.title_ellipsize_cache.end() || eit->second.last_use < oldest->second.last_use) {
        oldest = eit;
      }
    }
    if (oldest != state.title_ellipsize_cache.end()) state.title_ellipsize_cache.erase(oldest);
  }
  return display;
}
