#include "epub_comic_reader.h"

#include <SDL.h>
#ifdef HAVE_SDL2_IMAGE
#include <SDL_image.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HAVE_LIBZIP
#include <zip.h>

namespace {

std::string ResolveRelative(const std::string &base_dir, const std::string &href) {
  try {
    std::filesystem::path p = std::filesystem::path(base_dir) / std::filesystem::path(href);
    return p.lexically_normal().generic_string();
  } catch (...) {
    return href;
  }
}

using AttrMap = std::unordered_map<std::string, std::string>;

AttrMap ParseTagAttrs(const std::string &attrs_raw) {
  AttrMap attrs;
  const std::regex attr_re("([A-Za-z_:][-A-Za-z0-9_:.]*)\\s*=\\s*(['\"])(.*?)\\2", std::regex::icase);
  for (std::sregex_iterator it(attrs_raw.begin(), attrs_raw.end(), attr_re), end; it != end; ++it) {
    std::string key = (*it)[1].str();
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    attrs[key] = (*it)[3].str();
  }
  return attrs;
}

bool PickFirstMatch(const std::string &src, const std::regex &re, std::string &out) {
  std::smatch m;
  if (!std::regex_search(src, m, re) || m.size() < 2) return false;
  out = m[1].str();
  return true;
}

bool IsHtmlMediaType(const std::string &media_type) {
  return media_type == "application/xhtml+xml" || media_type == "text/html";
}

bool IsLikelyImagePath(const std::string &path) {
  std::string ext;
  try {
    ext = std::filesystem::path(path).extension().string();
  } catch (...) {
    return false;
  }
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" ||
         ext == ".bmp" || ext == ".gif";
}

bool ReadZipEntry(zip_t *za, const std::string &name, std::vector<unsigned char> &out) {
  zip_stat_t st{};
  if (zip_stat(za, name.c_str(), 0, &st) != 0) return false;
  if (st.size > static_cast<zip_uint64_t>(128 * 1024 * 1024)) return false;
  zip_file_t *zf = zip_fopen(za, name.c_str(), 0);
  if (!zf) return false;
  out.resize(static_cast<size_t>(st.size));
  const zip_int64_t rd = zip_fread(zf, out.data(), st.size);
  zip_fclose(zf);
  if (rd < 0) {
    out.clear();
    return false;
  }
  out.resize(static_cast<size_t>(rd));
  return !out.empty();
}

bool ReadZipEntry(zip_t *za, const std::string &name, std::string &out) {
  std::vector<unsigned char> bytes;
  if (!ReadZipEntry(za, name, bytes)) return false;
  out.assign(bytes.begin(), bytes.end());
  return true;
}

SDL_Surface *LoadSurfaceFromMemory(const unsigned char *data, size_t size) {
  if (!data || size == 0) return nullptr;
  SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
  if (!rw) return nullptr;
#ifdef HAVE_SDL2_IMAGE
  return IMG_Load_RW(rw, 1);
#else
  return SDL_LoadBMP_RW(rw, 1);
#endif
}

struct ManifestItem {
  std::string href;
  std::string media_type;
};

struct PageEntry {
  std::string image_entry;
  int width = 0;
  int height = 0;
  bool size_known = false;
};

} // namespace

struct EpubComicReader::Impl {
  zip_t *zip = nullptr;
  std::string path;
  std::vector<PageEntry> pages;
  int current_page = 0;
};
#endif

bool EpubComicReader::Open(const std::string &path) {
  Close();
#ifdef HAVE_LIBZIP
  int zerr = 0;
  zip_t *za = zip_open(path.c_str(), ZIP_RDONLY, &zerr);
  if (!za) return false;

  std::string container_xml;
  if (!ReadZipEntry(za, "META-INF/container.xml", container_xml)) {
    zip_close(za);
    return false;
  }

  std::string rootfile_tag;
  if (!PickFirstMatch(container_xml, std::regex("<rootfile\\b([^>]*)>", std::regex::icase), rootfile_tag)) {
    zip_close(za);
    return false;
  }
  AttrMap rootfile_attrs = ParseTagAttrs(rootfile_tag);
  const auto full_it = rootfile_attrs.find("full-path");
  if (full_it == rootfile_attrs.end() || full_it->second.empty()) {
    zip_close(za);
    return false;
  }

  const std::string opf_path = full_it->second;
  std::string opf;
  if (!ReadZipEntry(za, opf_path, opf)) {
    zip_close(za);
    return false;
  }
  std::string opf_dir;
  try {
    opf_dir = std::filesystem::path(opf_path).parent_path().generic_string();
  } catch (...) {
    opf_dir.clear();
  }

  std::unordered_map<std::string, ManifestItem> id_to_item;
  std::unordered_map<std::string, std::string> href_to_media_type;
  std::vector<std::string> ordered_docs;
  std::vector<std::string> manifest_html_docs;

  const std::regex item_re("<item\\b([^>]*)>", std::regex::icase);
  for (std::sregex_iterator it(opf.begin(), opf.end(), item_re), end; it != end; ++it) {
    AttrMap attrs = ParseTagAttrs((*it)[1].str());
    const auto id_it = attrs.find("id");
    const auto href_it = attrs.find("href");
    if (id_it == attrs.end() || href_it == attrs.end() || id_it->second.empty() || href_it->second.empty()) {
      continue;
    }
    ManifestItem item;
    item.href = href_it->second;
    const auto mt_it = attrs.find("media-type");
    if (mt_it != attrs.end()) item.media_type = mt_it->second;
    id_to_item[id_it->second] = item;
    const std::string resolved = ResolveRelative(opf_dir, item.href);
    href_to_media_type[resolved] = item.media_type;
    if (IsHtmlMediaType(item.media_type)) manifest_html_docs.push_back(resolved);
  }

  const std::regex spine_re("<itemref\\b[^>]*idref\\s*=\\s*['\"]([^'\"]+)['\"][^>]*>", std::regex::icase);
  for (std::sregex_iterator it(opf.begin(), opf.end(), spine_re), end; it != end; ++it) {
    const std::string idref = (*it)[1].str();
    const auto mit = id_to_item.find(idref);
    if (mit == id_to_item.end()) continue;
    if (!IsHtmlMediaType(mit->second.media_type)) continue;
    ordered_docs.push_back(ResolveRelative(opf_dir, mit->second.href));
  }
  if (ordered_docs.empty()) ordered_docs = manifest_html_docs;
  if (ordered_docs.empty()) {
    zip_close(za);
    return false;
  }

  std::vector<PageEntry> pages;
  const std::regex img_tag_re("<img\\b([^>]*)>", std::regex::icase);
  for (const auto &doc : ordered_docs) {
    std::string html;
    if (!ReadZipEntry(za, doc, html)) continue;
    const std::string doc_base = std::filesystem::path(doc).parent_path().generic_string();
    for (std::sregex_iterator it(html.begin(), html.end(), img_tag_re), end; it != end; ++it) {
      AttrMap attrs = ParseTagAttrs((*it)[1].str());
      const auto src_it = attrs.find("src");
      if (src_it == attrs.end() || src_it->second.empty()) continue;
      const std::string img_entry = ResolveRelative(doc_base, src_it->second);
      const auto mt_it = href_to_media_type.find(img_entry);
      const bool looks_like_image =
          (mt_it != href_to_media_type.end() && mt_it->second.rfind("image/", 0) == 0) ||
          IsLikelyImagePath(img_entry);
      if (!looks_like_image) continue;
      pages.push_back(PageEntry{img_entry});
    }
  }

  if (pages.empty()) {
    zip_close(za);
    return false;
  }

  impl_ = new Impl();
  impl_->zip = za;
  impl_->path = path;
  impl_->pages = std::move(pages);
  impl_->current_page = 0;
  return true;
#else
  (void)path;
  return false;
#endif
}

void EpubComicReader::Close() {
#ifdef HAVE_LIBZIP
  if (!impl_) return;
  if (impl_->zip) {
    zip_close(impl_->zip);
    impl_->zip = nullptr;
  }
  delete impl_;
  impl_ = nullptr;
#else
  path_.clear();
  page_count_ = 0;
  current_page_ = 0;
#endif
}

bool EpubComicReader::IsOpen() const {
#ifdef HAVE_LIBZIP
  return impl_ && impl_->zip && !impl_->pages.empty();
#else
  return false;
#endif
}

bool EpubComicReader::HasRealRenderer() const {
#ifdef HAVE_LIBZIP
  return true;
#else
  return false;
#endif
}

const char *EpubComicReader::BackendName() const {
#ifdef HAVE_LIBZIP
  return "libzip";
#else
  return "none";
#endif
}

int EpubComicReader::PageCount() const {
#ifdef HAVE_LIBZIP
  return impl_ ? static_cast<int>(impl_->pages.size()) : 0;
#else
  return 0;
#endif
}

int EpubComicReader::CurrentPage() const {
#ifdef HAVE_LIBZIP
  return impl_ ? impl_->current_page : 0;
#else
  return current_page_;
#endif
}

void EpubComicReader::SetPage(int page_index) {
  const int count = PageCount();
  if (count <= 0) return;
  page_index = std::clamp(page_index, 0, count - 1);
#ifdef HAVE_LIBZIP
  impl_->current_page = page_index;
#else
  current_page_ = page_index;
#endif
}

void EpubComicReader::NextPage() { SetPage(CurrentPage() + 1); }
void EpubComicReader::PrevPage() { SetPage(CurrentPage() - 1); }

bool EpubComicReader::PageSize(int page_index, int &w, int &h) const {
  if (!IsOpen()) return false;
  page_index = std::clamp(page_index, 0, PageCount() - 1);
#ifdef HAVE_LIBZIP
  const PageEntry &entry = impl_->pages[page_index];
  if (entry.size_known && entry.width > 0 && entry.height > 0) {
    w = entry.width;
    h = entry.height;
    return true;
  }

  std::vector<unsigned char> bytes;
  if (!ReadZipEntry(impl_->zip, entry.image_entry, bytes)) return false;
  SDL_Surface *surface = LoadSurfaceFromMemory(bytes.data(), bytes.size());
  if (!surface) return false;
  PageEntry &mutable_entry = impl_->pages[page_index];
  mutable_entry.width = surface->w;
  mutable_entry.height = surface->h;
  mutable_entry.size_known = (mutable_entry.width > 0 && mutable_entry.height > 0);
  SDL_FreeSurface(surface);
  if (!mutable_entry.size_known) return false;
  w = mutable_entry.width;
  h = mutable_entry.height;
  return true;
#else
  (void)w;
  (void)h;
  return false;
#endif
}

bool EpubComicReader::CurrentPageSize(int &w, int &h) const { return PageSize(CurrentPage(), w, h); }

bool EpubComicReader::RenderPageRGBA(int page_index, float scale, std::vector<unsigned char> &rgba, int &w, int &h,
                                     const std::atomic<bool> *cancel) {
  if (!IsOpen()) return false;
  page_index = std::clamp(page_index, 0, PageCount() - 1);
  scale = std::max(0.1f, scale);
  if (cancel && cancel->load()) return false;
#ifdef HAVE_LIBZIP
  PageEntry &entry = impl_->pages[page_index];
  std::vector<unsigned char> bytes;
  if (!ReadZipEntry(impl_->zip, entry.image_entry, bytes)) return false;
  if (cancel && cancel->load()) return false;
  SDL_Surface *surface = LoadSurfaceFromMemory(bytes.data(), bytes.size());
  if (!surface) return false;

  SDL_Surface *rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(surface);
  if (!rgba_surface) return false;

  entry.width = rgba_surface->w;
  entry.height = rgba_surface->h;
  entry.size_known = (entry.width > 0 && entry.height > 0);

  w = std::max(1, static_cast<int>(std::round(static_cast<float>(rgba_surface->w) * scale)));
  h = std::max(1, static_cast<int>(std::round(static_cast<float>(rgba_surface->h) * scale)));

  SDL_Surface *scaled = rgba_surface;
  if (w != rgba_surface->w || h != rgba_surface->h) {
    if (cancel && cancel->load()) {
      SDL_FreeSurface(rgba_surface);
      return false;
    }
    scaled = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!scaled) {
      SDL_FreeSurface(rgba_surface);
      return false;
    }
    if (SDL_BlitScaled(rgba_surface, nullptr, scaled, nullptr) != 0) {
      SDL_FreeSurface(scaled);
      SDL_FreeSurface(rgba_surface);
      return false;
    }
    SDL_FreeSurface(rgba_surface);
  }

  rgba.assign(static_cast<size_t>(w * h * 4), 0);
  for (int y = 0; y < h; ++y) {
    if (cancel && cancel->load()) {
      SDL_FreeSurface(scaled);
      rgba.clear();
      return false;
    }
    const unsigned char *src_row = static_cast<const unsigned char *>(scaled->pixels) + y * scaled->pitch;
    unsigned char *dst_row = rgba.data() + static_cast<size_t>(y * w * 4);
    std::memcpy(dst_row, src_row, static_cast<size_t>(w * 4));
  }
  SDL_FreeSurface(scaled);
  return true;
#else
  (void)rgba;
  (void)w;
  (void)h;
  return false;
#endif
}

bool EpubComicReader::RenderCurrentPageRGBA(float scale, std::vector<unsigned char> &rgba, int &w, int &h,
                                            const std::atomic<bool> *cancel) {
  return RenderPageRGBA(CurrentPage(), scale, rgba, w, h, cancel);
}
