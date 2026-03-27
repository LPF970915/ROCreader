#pragma once

#include "ui_assets.h"

#include <SDL.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

struct UiAssetsLoadResult {
  std::filesystem::path ui_root_hit;
  std::filesystem::path ui_pack_hit;
  size_t packed_asset_count = 0;
};

struct UiAssetsLoaderDeps {
  SDL_Renderer *renderer = nullptr;
  std::filesystem::path exe_path;
  std::function<SDL_Texture *(SDL_Renderer *, const std::string &)> load_texture_from_file;
  std::function<SDL_Surface *(const void *, size_t)> load_surface_from_memory;
  std::function<SDL_Texture *(SDL_Renderer *, SDL_Surface *)> create_texture_from_surface;
  std::function<void(SDL_Texture *, int, int)> remember_texture_size;
};

UiAssetsLoadResult LoadUiAssets(UiAssets &assets, UiAssetsLoaderDeps &deps);
