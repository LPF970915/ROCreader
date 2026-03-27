#pragma once

#include "book_scanner.h"
#include "ui_text_cache.h"

#include <SDL.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

enum class BootPhase { CountBooks, ScanBooks, GenerateCovers, Finalize, Done };

struct BootRuntimeState {
  float timer = 0.0f;
  BootPhase phase = BootPhase::CountBooks;
  std::vector<std::string> supported_paths;
  size_t scan_index = 0;
  std::vector<std::string> cover_generate_queue;
  size_t cover_generate_index = 0;
  size_t total_books = 0;
  std::filesystem::recursive_directory_iterator count_it;
  std::filesystem::recursive_directory_iterator count_end;
  size_t count_root_index = 0;
  bool count_iterator_active = false;
  std::string status_text = "Loading resources...(0/0)";
};

struct BootRuntimeTickDeps {
  const std::vector<std::string> &books_roots;
  size_t count_batch_entries = 0;
  size_t scan_batch_entries = 0;
  size_t cover_generate_batch_entries = 0;
  std::function<std::string(const std::string &)> get_lower_ext;
  std::function<bool()> pdf_backend_available;
  std::function<bool(const BookItem &)> has_manual_cover_exact_or_fuzzy;
  std::function<bool(const std::string &)> has_cached_pdf_cover_on_disk;
  std::function<SDL_Texture *(const std::string &)> create_doc_first_page_cover_texture;
  std::function<void(SDL_Texture *)> destroy_generated_cover_texture;
  std::function<void(size_t, size_t)> on_finish;
};

struct BootRuntimeRenderDeps {
  SDL_Renderer *renderer = nullptr;
  const BootRuntimeState &state;
  int screen_w = 0;
  int screen_h = 0;
  std::function<void(int, int, int, int, SDL_Color, bool)> draw_rect;
  std::function<TextCacheEntry *(const std::string &, SDL_Color)> get_text_texture;
};

void TickBootRuntime(BootRuntimeState &state, float dt, const BootRuntimeTickDeps &deps);
void DrawBootRuntime(const BootRuntimeRenderDeps &deps);
