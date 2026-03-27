#pragma once

#include <SDL.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

struct EpubCoverTextureDeps {
  SDL_Renderer *renderer = nullptr;
  int cover_w = 0;
  int cover_h = 0;
  std::filesystem::path cover_thumb_cache_dir;
  std::function<std::string(const std::string &)> normalize_path_key;
  std::function<SDL_Surface *(const std::string &)> load_surface_from_file;
  std::function<SDL_Surface *(const void *, size_t)> load_surface_from_memory;
  std::function<SDL_Texture *(SDL_Renderer *, SDL_Surface *, int, int, float)> create_normalized_cover_texture;
  std::function<SDL_Texture *(SDL_Renderer *, SDL_Surface *)> create_texture_from_surface;
  std::function<void(SDL_Texture *, int, int)> remember_texture_size;
};

SDL_Texture *CreateEpubFirstImageCoverTexture(const std::string &doc_path,
                                              EpubCoverTextureDeps &deps);
