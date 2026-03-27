#include "cover_service.h"

#include "cover_resolver.h"
#include "epub_cover_cache.h"
#include "pdf_reader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
int ClampIntLocal(int value, int lo, int hi) {
  return std::max(lo, std::min(hi, value));
}

std::string MakePdfCoverCacheKey(const std::string &doc_path, const CoverServiceDeps &deps) {
  std::error_code ec;
  const uintmax_t file_size = std::filesystem::file_size(std::filesystem::path(doc_path), ec);
  const auto mtime_raw = std::filesystem::last_write_time(std::filesystem::path(doc_path), ec);
  const long long file_mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
  return deps.normalize_path_key(doc_path) + "|" + std::to_string(ec ? 0 : file_size) + "|" +
         std::to_string(file_mtime) + "|" + std::to_string(deps.cover_w) + "x" +
         std::to_string(deps.cover_h);
}

std::filesystem::path GetPdfCoverCacheFile(const std::string &doc_path, const CoverServiceDeps &deps) {
  const size_t hash_value = std::hash<std::string>{}(MakePdfCoverCacheKey(doc_path, deps));
  std::ostringstream oss;
  oss << std::hex << hash_value << ".bmp";
  return deps.cover_thumb_cache_dir / oss.str();
}

SDL_Texture *LoadCachedPdfCoverTexture(const std::string &doc_path, CoverServiceDeps &deps) {
  const std::filesystem::path cache_file = GetPdfCoverCacheFile(doc_path, deps);
  std::error_code ec;
  if (!std::filesystem::exists(cache_file, ec) || ec) return nullptr;
  SDL_Surface *cover_surface = deps.load_surface_from_file(cache_file.string());
  if (!cover_surface) return nullptr;
  SDL_Texture *normalized =
      deps.create_normalized_cover_texture(deps.renderer, cover_surface, deps.cover_w, deps.cover_h, deps.cover_aspect);
  if (!normalized) normalized = deps.create_texture_from_surface(deps.renderer, cover_surface);
  SDL_FreeSurface(cover_surface);
  if (normalized) {
    deps.remember_texture_size(normalized, deps.cover_w, deps.cover_h);
  }
  return normalized;
}

void SavePdfCoverCacheToDisk(const std::string &doc_path, const std::vector<unsigned char> &cover_rgba,
                             const CoverServiceDeps &deps) {
  if (cover_rgba.size() != static_cast<size_t>(deps.cover_w * deps.cover_h * 4)) return;
  std::error_code ec;
  std::filesystem::create_directories(deps.cover_thumb_cache_dir, ec);
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
      const_cast<unsigned char *>(cover_rgba.data()), deps.cover_w, deps.cover_h, 32, deps.cover_w * 4,
      SDL_PIXELFORMAT_RGBA32);
  if (!surface) return;
  SDL_SaveBMP(surface, GetPdfCoverCacheFile(doc_path, deps).string().c_str());
  SDL_FreeSurface(surface);
}

SDL_Texture *LoadCoverFromPath(const std::string &cover_path, CoverServiceDeps &deps) {
  if (cover_path.empty()) return nullptr;
  SDL_Surface *cover_surface = deps.load_surface_from_file(cover_path);
  if (!cover_surface) return nullptr;
  SDL_Texture *normalized =
      deps.create_normalized_cover_texture(deps.renderer, cover_surface, deps.cover_w, deps.cover_h, deps.cover_aspect);
  if (!normalized) normalized = deps.create_texture_from_surface(deps.renderer, cover_surface);
  SDL_FreeSurface(cover_surface);
  return normalized;
}

SDL_Texture *LoadManualCoverExactThenFuzzy(const BookItem &item, CoverServiceDeps &deps) {
  const std::string exact_cover_path =
      cover_resolver::ResolveCoverPathExact(item.path, item.is_dir, deps.cover_roots);
  if (!exact_cover_path.empty()) {
    SDL_Texture *texture = LoadCoverFromPath(exact_cover_path, deps);
    if (texture) return texture;
  }
  const std::string fuzzy_cover_path =
      cover_resolver::ResolveCoverPathFuzzy(item.path, item.is_dir, deps.cover_roots);
  if (!fuzzy_cover_path.empty()) {
    SDL_Texture *texture = LoadCoverFromPath(fuzzy_cover_path, deps);
    if (texture) return texture;
  }
  return nullptr;
}

std::string DetectComicFolderType(const std::string &folder_path, const CoverServiceDeps &deps) {
  std::error_code ec;
  const std::filesystem::path root(folder_path);
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return {};
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  bool has_pdf = false;
  bool has_epub = false;
  for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
    if (ec) continue;
    const auto &entry = *it;
    if (!entry.is_regular_file(ec)) continue;
    const std::string ext = deps.get_lower_ext(entry.path().string());
    if (ext == ".pdf") has_pdf = true;
    else if (ext == ".epub") has_epub = true;
  }
  if (has_pdf) return ".pdf";
  if (has_epub) return ".epub";
  return {};
}

std::string FindFirstDocInFolder(const std::string &folder_path, const std::string &wanted_ext,
                                 const CoverServiceDeps &deps) {
  std::error_code ec;
  const std::filesystem::path root(folder_path);
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return {};
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  std::vector<std::string> matches;
  for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
    if (ec) continue;
    const auto &entry = *it;
    if (!entry.is_regular_file(ec)) continue;
    if (deps.get_lower_ext(entry.path().string()) != wanted_ext) continue;
    matches.push_back(entry.path().string());
  }
  std::sort(matches.begin(), matches.end());
  return matches.empty() ? std::string{} : matches.front();
}

SDL_Texture *CreateEpubFirstImageCoverTextureLocal(const std::string &doc_path, CoverServiceDeps &deps) {
  EpubCoverTextureDeps cover_deps{
      deps.renderer,
      deps.cover_w,
      deps.cover_h,
      deps.cover_thumb_cache_dir,
      deps.normalize_path_key,
      deps.load_surface_from_file,
      deps.load_surface_from_memory,
      deps.create_normalized_cover_texture,
      deps.create_texture_from_surface,
      deps.remember_texture_size,
  };
  return CreateEpubFirstImageCoverTexture(doc_path, cover_deps);
}
}

bool HasManualCoverExactOrFuzzy(const BookItem &item, const CoverServiceDeps &deps) {
  const std::string exact_cover_path =
      cover_resolver::ResolveCoverPathExact(item.path, item.is_dir, deps.cover_roots);
  if (!exact_cover_path.empty()) return true;
  const std::string fuzzy_cover_path =
      cover_resolver::ResolveCoverPathFuzzy(item.path, item.is_dir, deps.cover_roots);
  return !fuzzy_cover_path.empty();
}

bool HasCachedPdfCoverOnDisk(const std::string &doc_path, const CoverServiceDeps &deps) {
  std::error_code ec;
  return std::filesystem::exists(GetPdfCoverCacheFile(doc_path, deps), ec) && !ec;
}

SDL_Texture *CreatePdfFirstPageCoverTexture(const std::string &doc_path, CoverServiceDeps &deps) {
  if (SDL_Texture *cached = LoadCachedPdfCoverTexture(doc_path, deps)) return cached;

  PdfReader preview_pdf;
  if (!preview_pdf.Open(doc_path) || !preview_pdf.HasRealRenderer()) {
    preview_pdf.Close();
    return nullptr;
  }

  int page_w = 0;
  int page_h = 0;
  if (!preview_pdf.PageSize(0, page_w, page_h) || page_w <= 0 || page_h <= 0) {
    preview_pdf.Close();
    return nullptr;
  }

  const float desired_w = static_cast<float>(deps.cover_w) * 1.6f;
  const float desired_h = static_cast<float>(deps.cover_h) * 1.6f;
  const float scale_w = desired_w / static_cast<float>(std::max(1, page_w));
  const float scale_h = desired_h / static_cast<float>(std::max(1, page_h));
  const float render_scale = std::max(0.1f, std::max(scale_w, scale_h));

  std::vector<unsigned char> rgba;
  int src_w = 0;
  int src_h = 0;
  if (!preview_pdf.RenderPageRGBA(0, render_scale, rgba, src_w, src_h) || src_w <= 0 || src_h <= 0) {
    preview_pdf.Close();
    return nullptr;
  }
  preview_pdf.Close();

  SDL_Rect src_crop{0, 0, src_w, src_h};
  const float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
  if (src_aspect > deps.cover_aspect) {
    src_crop.w = std::max(1, static_cast<int>(std::round(static_cast<float>(src_h) * deps.cover_aspect)));
    src_crop.x = (src_w - src_crop.w) / 2;
  } else if (src_aspect < deps.cover_aspect) {
    src_crop.h = std::max(1, static_cast<int>(std::round(static_cast<float>(src_w) / deps.cover_aspect)));
    src_crop.y = (src_h - src_crop.h) / 2;
  }

  std::vector<unsigned char> cover_rgba(static_cast<size_t>(deps.cover_w * deps.cover_h * 4), 0);
  for (int dy = 0; dy < deps.cover_h; ++dy) {
    const int sy = src_crop.y + ((dy * src_crop.h + (deps.cover_h / 2)) / deps.cover_h);
    const int sy_clamped = ClampIntLocal(sy, 0, src_h - 1);
    for (int dx = 0; dx < deps.cover_w; ++dx) {
      const int sx = src_crop.x + ((dx * src_crop.w + (deps.cover_w / 2)) / deps.cover_w);
      const int sx_clamped = ClampIntLocal(sx, 0, src_w - 1);
      const size_t si = static_cast<size_t>((sy_clamped * src_w + sx_clamped) * 4);
      const size_t di = static_cast<size_t>((dy * deps.cover_w + dx) * 4);
      cover_rgba[di + 0] = rgba[si + 0];
      cover_rgba[di + 1] = rgba[si + 1];
      cover_rgba[di + 2] = rgba[si + 2];
      cover_rgba[di + 3] = rgba[si + 3];
    }
  }

  SDL_Texture *texture =
      SDL_CreateTexture(deps.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, deps.cover_w, deps.cover_h);
  if (!texture) return nullptr;
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  if (SDL_UpdateTexture(texture, nullptr, cover_rgba.data(), deps.cover_w * 4) != 0) {
    SDL_DestroyTexture(texture);
    return nullptr;
  }
  SavePdfCoverCacheToDisk(doc_path, cover_rgba, deps);
  return texture;
}

SDL_Texture *ResolveBookCoverTexture(const BookItem &item, ShelfCategory category, CoverServiceDeps &deps) {
  const bool txt_folder = item.is_dir && category == ShelfCategory::AllBooks;
  const bool txt_file = !item.is_dir && deps.get_lower_ext(item.path) == ".txt";
  if (txt_folder || txt_file) return deps.shared_txt_cover;

  if (item.is_dir) {
    if (SDL_Texture *texture = LoadManualCoverExactThenFuzzy(item, deps)) return texture;
    const std::string kind = DetectComicFolderType(item.path, deps);
    if (kind == ".pdf") {
      const std::string pdf_path = FindFirstDocInFolder(item.path, ".pdf", deps);
      if (!pdf_path.empty()) {
        if (SDL_Texture *texture = CreatePdfFirstPageCoverTexture(pdf_path, deps)) return texture;
      }
    } else if (kind == ".epub") {
      const std::string epub_path = FindFirstDocInFolder(item.path, ".epub", deps);
      if (!epub_path.empty()) {
        if (SDL_Texture *texture = CreateEpubFirstImageCoverTextureLocal(epub_path, deps)) return texture;
      }
    }
    return deps.shared_pdf_cover ? deps.shared_pdf_cover : deps.shared_txt_cover;
  }

  const std::string ext = deps.get_lower_ext(item.path);
  if (ext != ".pdf" && ext != ".epub") {
    return LoadManualCoverExactThenFuzzy(item, deps);
  }

  if (SDL_Texture *texture = LoadManualCoverExactThenFuzzy(item, deps)) return texture;
  if (ext == ".pdf") {
    if (SDL_Texture *texture = CreatePdfFirstPageCoverTexture(item.path, deps)) return texture;
  } else {
    if (SDL_Texture *texture = CreateEpubFirstImageCoverTextureLocal(item.path, deps)) return texture;
  }
  return deps.shared_pdf_cover ? deps.shared_pdf_cover : deps.shared_txt_cover;
}
