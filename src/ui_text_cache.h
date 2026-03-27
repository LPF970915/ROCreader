#pragma once

#include <SDL.h>
#ifdef HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

struct TextCacheEntry {
  SDL_Texture *texture = nullptr;
  int w = 0;
  int h = 0;
  uint32_t last_use = 0;
};

struct TitleEllipsisCacheEntry {
  std::string display;
  uint32_t last_use = 0;
};

enum class UiTextRole {
  Body,
  Title,
  Reader,
};

struct UiTextCacheState {
#ifdef HAVE_SDL2_TTF
  TTF_Font *font = nullptr;
  TTF_Font *title_font = nullptr;
  TTF_Font *reader_font = nullptr;
#endif
  std::unordered_map<std::string, TextCacheEntry> text_cache;
  std::unordered_map<std::string, TitleEllipsisCacheEntry> title_ellipsize_cache;
  bool font_attempted = false;
  size_t max_text_cache_entries = 0;
};

using BeforeDestroyTextTextureFn = std::function<void(SDL_Texture *)>;
using MeasureTextWidthFn = std::function<int(const std::string &)>;

void OpenUiFonts(UiTextCacheState &state, const std::filesystem::path &exe_path,
                 const std::filesystem::path &ui_path, int reader_font_pt);
void ClearUiTextCache(UiTextCacheState &state, const BeforeDestroyTextTextureFn &before_destroy = {});
void ShutdownUiTextCache(UiTextCacheState &state, const BeforeDestroyTextTextureFn &before_destroy = {});
TextCacheEntry *GetUiTextTexture(UiTextCacheState &state, SDL_Renderer *renderer, const std::string &text,
                                 SDL_Color color, UiTextRole role);
std::string GetTitleEllipsized(UiTextCacheState &state, const std::string &raw_name, int text_area_w,
                               const MeasureTextWidthFn &measure);
