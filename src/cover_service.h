#pragma once

#include "book_scanner.h"
#include "shelf_runtime.h"

#include <SDL.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct CoverServiceDeps {
  SDL_Renderer *renderer = nullptr;
  int cover_w = 0;
  int cover_h = 0;
  float cover_aspect = 0.0f;
  std::filesystem::path cover_thumb_cache_dir;
  std::vector<std::string> cover_roots;
  SDL_Texture *shared_txt_cover = nullptr;
  SDL_Texture *shared_pdf_cover = nullptr;
  std::function<std::string(const std::string &)> normalize_path_key;
  std::function<std::string(const std::string &)> get_lower_ext;
  std::function<SDL_Surface *(const std::string &)> load_surface_from_file;
  std::function<SDL_Surface *(const void *, size_t)> load_surface_from_memory;
  std::function<SDL_Texture *(SDL_Renderer *, SDL_Surface *, int, int, float)> create_normalized_cover_texture;
  std::function<SDL_Texture *(SDL_Renderer *, SDL_Surface *)> create_texture_from_surface;
  std::function<void(SDL_Texture *, int, int)> remember_texture_size;
};

bool HasManualCoverExactOrFuzzy(const BookItem &item, const CoverServiceDeps &deps);
bool HasCachedPdfCoverOnDisk(const std::string &doc_path, const CoverServiceDeps &deps);
SDL_Texture *CreatePdfFirstPageCoverTexture(const std::string &doc_path, CoverServiceDeps &deps);
SDL_Texture *ResolveBookCoverTexture(const BookItem &item, ShelfCategory category, CoverServiceDeps &deps);
