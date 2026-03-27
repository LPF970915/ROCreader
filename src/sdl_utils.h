#pragma once

#include <SDL.h>

#include <cstddef>
#include <string>

SDL_Texture *LoadTextureFromFile(SDL_Renderer *renderer, const std::string &path);
SDL_Surface *LoadSurfaceFromFile(const std::string &path);
SDL_Surface *LoadSurfaceFromMemory(const void *data, size_t size);
SDL_Texture *CreateNormalizedCoverTexture(SDL_Renderer *renderer, SDL_Surface *src_surface, int cover_w,
                                          int cover_h, float cover_aspect);
SDL_Texture *CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface);
void DrawRect(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color, bool fill = true);
