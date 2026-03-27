#include "epub_cover_cache.h"

#include "epub_reader.h"

#include <SDL.h>

#include <filesystem>
#include <sstream>
#include <string>

namespace {
std::string MakeEpubCoverCacheKey(const std::string &doc_path, uintmax_t logical_size,
                                  long long logical_mtime, int cover_w, int cover_h,
                                  const EpubCoverTextureDeps &deps) {
  return deps.normalize_path_key(doc_path) + "|" + std::to_string(logical_size) + "|" +
         std::to_string(logical_mtime) + "|" + std::to_string(cover_w) + "x" +
         std::to_string(cover_h) + "|epub-cover-v1";
}

std::filesystem::path GetEpubCoverCacheFile(const std::string &doc_path, uintmax_t logical_size,
                                            long long logical_mtime,
                                            const EpubCoverTextureDeps &deps) {
  const std::string cache_key =
      MakeEpubCoverCacheKey(doc_path, logical_size, logical_mtime, deps.cover_w, deps.cover_h, deps);
  const size_t hash_value = std::hash<std::string>{}(cache_key);
  std::ostringstream oss;
  oss << std::hex << hash_value << ".bmp";
  return deps.cover_thumb_cache_dir / oss.str();
}

SDL_Texture *LoadCachedEpubCoverTexture(const std::string &doc_path, uintmax_t logical_size,
                                        long long logical_mtime, EpubCoverTextureDeps &deps) {
  const std::filesystem::path cache_file = GetEpubCoverCacheFile(doc_path, logical_size, logical_mtime, deps);
  std::error_code ec;
  if (!std::filesystem::exists(cache_file, ec) || ec) return nullptr;
  SDL_Surface *cover_surface = deps.load_surface_from_file(cache_file.string());
  if (!cover_surface) return nullptr;
  SDL_Texture *normalized =
      deps.create_normalized_cover_texture(deps.renderer, cover_surface, deps.cover_w, deps.cover_h,
                                           static_cast<float>(deps.cover_w) / static_cast<float>(deps.cover_h));
  if (!normalized) normalized = deps.create_texture_from_surface(deps.renderer, cover_surface);
  SDL_FreeSurface(cover_surface);
  if (normalized) {
    deps.remember_texture_size(normalized, deps.cover_w, deps.cover_h);
  }
  return normalized;
}

void SaveEpubCoverCacheToDisk(const std::string &doc_path, uintmax_t logical_size,
                              long long logical_mtime, SDL_Surface *cover_surface,
                              const EpubCoverTextureDeps &deps) {
  if (!cover_surface) return;
  std::error_code ec;
  std::filesystem::create_directories(deps.cover_thumb_cache_dir, ec);
  const std::filesystem::path cache_file = GetEpubCoverCacheFile(doc_path, logical_size, logical_mtime, deps);
  SDL_SaveBMP(cover_surface, cache_file.string().c_str());
}
}

SDL_Texture *CreateEpubFirstImageCoverTexture(const std::string &doc_path,
                                              EpubCoverTextureDeps &deps) {
  EpubReader epub;
  EpubReader::CoverImage cover_image;
  std::string error;
  if (!epub.ExtractCoverImage(doc_path, cover_image, error)) return nullptr;

  if (SDL_Texture *cached =
          LoadCachedEpubCoverTexture(doc_path, cover_image.logical_size, cover_image.logical_mtime, deps)) {
    return cached;
  }

  SDL_Surface *cover_surface =
      deps.load_surface_from_memory(cover_image.bytes.data(), cover_image.bytes.size());
  if (!cover_surface) return nullptr;
  SDL_Texture *normalized =
      deps.create_normalized_cover_texture(deps.renderer, cover_surface, deps.cover_w, deps.cover_h,
                                           static_cast<float>(deps.cover_w) / static_cast<float>(deps.cover_h));
  if (!normalized) normalized = deps.create_texture_from_surface(deps.renderer, cover_surface);
  if (normalized) {
    deps.remember_texture_size(normalized, deps.cover_w, deps.cover_h);
    SaveEpubCoverCacheToDisk(doc_path, cover_image.logical_size, cover_image.logical_mtime,
                             cover_surface, deps);
  }
  SDL_FreeSurface(cover_surface);
  return normalized;
}
