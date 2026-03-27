#include "boot_runtime.h"

#include <algorithm>
#include <cmath>

namespace {
bool StartNextBootCountRoot(BootRuntimeState &state, const std::vector<std::string> &books_roots) {
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  while (state.count_root_index < books_roots.size()) {
    const std::filesystem::path root_path(books_roots[state.count_root_index++]);
    std::error_code ec;
    if (!std::filesystem::exists(root_path, ec) || !std::filesystem::is_directory(root_path, ec)) continue;
    state.count_it = std::filesystem::recursive_directory_iterator(root_path, opts, ec);
    if (ec) continue;
    state.count_end = std::filesystem::recursive_directory_iterator();
    state.count_iterator_active = true;
    return true;
  }
  state.count_iterator_active = false;
  return false;
}

float BootProgressRatio(const BootRuntimeState &state) {
  switch (state.phase) {
  case BootPhase::CountBooks: {
    const float pulse = std::fmod(state.timer * 0.85f, 1.0f);
    return 0.05f + pulse * 0.15f;
  }
  case BootPhase::ScanBooks:
    return state.total_books == 0
               ? 0.55f
               : (0.20f + 0.35f * (static_cast<float>(state.scan_index) /
                                   static_cast<float>(std::max<size_t>(1, state.total_books))));
  case BootPhase::GenerateCovers:
    return state.cover_generate_queue.empty()
               ? 1.0f
               : (0.55f + 0.45f * (static_cast<float>(state.cover_generate_index) /
                                   static_cast<float>(std::max<size_t>(1, state.cover_generate_queue.size()))));
  case BootPhase::Finalize:
  case BootPhase::Done:
    return 1.0f;
  }
  return 0.0f;
}

std::string MakeBootScanText(size_t current, size_t total) {
  return std::string(u8"资源加载中...（") + std::to_string(current) + "/" + std::to_string(total) + u8"）";
}

std::string MakeBootCoverText(size_t current, size_t total) {
  return std::string(u8"封面缓存生成中...（") + std::to_string(current) + "/" + std::to_string(total) + u8"）";
}
}

void TickBootRuntime(BootRuntimeState &state, float dt, const BootRuntimeTickDeps &deps) {
  state.timer += dt;
  if (state.phase == BootPhase::CountBooks) {
    if (!state.count_iterator_active) {
      StartNextBootCountRoot(state, deps.books_roots);
    }
    size_t processed = 0;
    while (processed < deps.count_batch_entries) {
      if (!state.count_iterator_active) {
        if (!StartNextBootCountRoot(state, deps.books_roots)) break;
      }
      if (state.count_it == state.count_end) {
        state.count_iterator_active = false;
        continue;
      }
      std::error_code ec;
      const auto entry = *state.count_it;
      state.count_it.increment(ec);
      ++processed;
      if (ec || !entry.is_regular_file(ec)) continue;
      const std::string ext = deps.get_lower_ext ? deps.get_lower_ext(entry.path().string()) : std::string{};
      if (ext == ".pdf" || ext == ".txt") {
        state.supported_paths.push_back(entry.path().string());
      }
    }
    state.status_text = MakeBootScanText(0, 0);
    if (!state.count_iterator_active && state.count_root_index >= deps.books_roots.size()) {
      std::sort(state.supported_paths.begin(), state.supported_paths.end());
      state.total_books = state.supported_paths.size();
      state.scan_index = 0;
      state.cover_generate_queue.clear();
      state.phase = BootPhase::ScanBooks;
      state.status_text = MakeBootScanText(0, state.total_books);
    }
  } else if (state.phase == BootPhase::ScanBooks) {
    size_t processed = 0;
    while (processed < deps.scan_batch_entries && state.scan_index < state.supported_paths.size()) {
      const std::string &book_path = state.supported_paths[state.scan_index];
      const std::string ext = deps.get_lower_ext ? deps.get_lower_ext(book_path) : std::string{};
      if (ext == ".pdf") {
        BookItem item;
        item.name = std::filesystem::path(book_path).filename().string();
        item.path = book_path;
        item.is_dir = false;
        const bool manual = deps.has_manual_cover_exact_or_fuzzy && deps.has_manual_cover_exact_or_fuzzy(item);
        const bool cached = deps.has_cached_pdf_cover_on_disk && deps.has_cached_pdf_cover_on_disk(book_path);
        const bool can_render = deps.pdf_backend_available && deps.pdf_backend_available();
        if (!manual && !cached && can_render) {
          state.cover_generate_queue.push_back(book_path);
        }
      }
      ++state.scan_index;
      ++processed;
    }
    state.status_text = MakeBootScanText(state.scan_index, state.total_books);
    if (state.scan_index >= state.supported_paths.size()) {
      state.cover_generate_index = 0;
      state.phase = BootPhase::GenerateCovers;
      state.status_text = MakeBootCoverText(0, state.cover_generate_queue.size());
      if (state.cover_generate_queue.empty()) {
        state.phase = BootPhase::Finalize;
      }
    }
  } else if (state.phase == BootPhase::GenerateCovers) {
    size_t processed = 0;
    while (processed < deps.cover_generate_batch_entries &&
           state.cover_generate_index < state.cover_generate_queue.size()) {
      const std::string &doc_path = state.cover_generate_queue[state.cover_generate_index];
      if (deps.create_doc_first_page_cover_texture) {
        if (SDL_Texture *generated = deps.create_doc_first_page_cover_texture(doc_path)) {
          if (deps.destroy_generated_cover_texture) deps.destroy_generated_cover_texture(generated);
        }
      }
      ++state.cover_generate_index;
      ++processed;
    }
    state.status_text = MakeBootCoverText(state.cover_generate_index, state.cover_generate_queue.size());
    if (state.cover_generate_index >= state.cover_generate_queue.size()) {
      state.phase = BootPhase::Finalize;
    }
  }

  if (state.phase == BootPhase::Finalize) {
    if (deps.on_finish) deps.on_finish(state.total_books, state.cover_generate_queue.size());
    state.phase = BootPhase::Done;
  }
}

void DrawBootRuntime(const BootRuntimeRenderDeps &deps) {
  deps.draw_rect(0, 0, deps.screen_w, deps.screen_h, SDL_Color{20, 20, 24, 255}, true);
  const int bar_x = 40;
  const int bar_y = deps.screen_h / 2;
  const int bar_w = deps.screen_w - 80;
  const float progress = std::clamp(BootProgressRatio(deps.state), 0.0f, 1.0f);
  const int fill_w = static_cast<int>(std::round(progress * bar_w));
  deps.draw_rect(bar_x, bar_y, bar_w, 16, SDL_Color{48, 52, 60, 255}, true);
  deps.draw_rect(bar_x, bar_y, fill_w, 16, SDL_Color{210, 210, 210, 255}, true);
  deps.draw_rect(bar_x, bar_y, bar_w, 16, SDL_Color{255, 255, 255, 220}, false);
#ifdef HAVE_SDL2_TTF
  const SDL_Color boot_text_color{232, 236, 244, 255};
  if (!deps.state.status_text.empty() && deps.get_text_texture) {
    if (TextCacheEntry *te = deps.get_text_texture(deps.state.status_text, boot_text_color); te && te->texture) {
      SDL_Rect td{std::max(0, (deps.screen_w - te->w) / 2), bar_y + 28, te->w, te->h};
      SDL_RenderCopy(deps.renderer, te->texture, nullptr, &td);
    }
  }
#endif
}
