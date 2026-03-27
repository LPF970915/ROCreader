#pragma once

#include "reader_session_state.h"

#include <SDL.h>
#ifdef HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct TxtTextServiceState {
  std::unordered_map<std::string, TxtLayoutCacheEntry> layout_cache;
  std::filesystem::path cache_dir;
  size_t max_cache_entries = 0;
  size_t max_wrapped_lines = 0;
};

using NormalizePathKeyFn = std::function<std::string(const std::string &)>;

std::string MakeTxtLayoutCacheKey(const std::string &path, const SDL_Rect &bounds, int line_h,
                                  uintmax_t file_size, long long file_mtime,
                                  const NormalizePathKeyFn &normalize_path_key);
void PruneTxtLayoutCache(TxtTextServiceState &state);
void ClearTxtLayoutCache(TxtTextServiceState &state);
bool LoadTxtLayoutCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                TxtLayoutCacheEntry &entry);
void SaveTxtLayoutCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const TxtLayoutCacheEntry &entry);
bool LoadTxtResumeCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                TxtResumeCacheEntry &entry);
void SaveTxtResumeCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const TxtReaderState &state_to_save);
SDL_Rect GetTxtViewportBounds(SDL_Renderer *renderer, int screen_w, int screen_h,
                              int margin_x, int margin_y);
#ifdef HAVE_SDL2_TTF
bool AppendWrappedTextLine(TxtReaderState &state, const std::string &line, TTF_Font *font,
                           size_t max_wrapped_lines);
#endif
