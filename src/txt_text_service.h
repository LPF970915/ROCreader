#pragma once

#include "reader_session_state.h"

#include <SDL.h>
#ifdef HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include <cstdint>
#include "filesystem_compat.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Discard layouts/resume caches wrapped with canvas dimensions instead of content bounds.
constexpr int kTxtLayoutCacheVersion = 7;

struct TxtTextServiceState {
  std::unordered_map<std::string, TxtLayoutCacheEntry> layout_cache;
  std::filesystem::path cache_dir;
  std::filesystem::path removable_cache_dir;
  size_t max_cache_entries = 0;
  size_t max_wrapped_lines = 0;
};

struct TxtViewportRequest {
  int screen_w = 0;
  int screen_h = 0;
  int base_margin_x = 0;
  int base_margin_y = 0;
  int font_pt = 0;
  int line_h = 0;
};

struct TxtWrappedLine {
  std::string text;
  size_t source_offset = 0;
};

using NormalizePathKeyFn = std::function<std::string(const std::string &)>;

std::string MakeTxtLayoutCacheKey(const std::string &path, const SDL_Rect &bounds, int line_h,
                                  uintmax_t file_size, long long file_mtime,
                                  const NormalizePathKeyFn &normalize_path_key);
void PruneTxtLayoutCache(TxtTextServiceState &state);
void ClearTxtLayoutCache(TxtTextServiceState &state);
bool LoadTxtLayoutCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                const std::string &book_path, TxtLayoutCacheEntry &entry);
void SaveTxtLayoutCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const std::string &book_path,
                              const TxtLayoutCacheEntry &entry);
bool LoadTxtResumeCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                const std::string &book_path, TxtResumeCacheEntry &entry);
void SaveTxtResumeCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const std::string &book_path,
                              const TxtReaderState &state_to_save);
SDL_Rect GetTxtViewportBounds(SDL_Renderer *renderer, const TxtViewportRequest &request);
#ifdef HAVE_SDL2_TTF
bool AppendWrappedTextLine(TxtReaderState &state, const std::string &line, TTF_Font *font,
                           size_t source_line_offset, size_t max_wrapped_lines);
#endif
