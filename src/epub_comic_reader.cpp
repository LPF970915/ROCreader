#include "epub_comic_reader.h"
#include "image_flow_layout.h"
#include "image_decode.h"
#include "runtime_log.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include "filesystem_compat.h"
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HAVE_LIBZIP
#include <zip.h>

namespace {

std::string NormalizeZipPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  std::vector<std::string> parts;
  size_t cursor = 0;
  while (cursor <= path.size()) {
    const size_t slash = path.find('/', cursor);
    std::string part = path.substr(cursor, slash == std::string::npos ? std::string::npos : slash - cursor);
    if (part.empty() || part == ".") {
      // skip
    } else if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else {
      parts.push_back(std::move(part));
    }
    if (slash == std::string::npos) break;
    cursor = slash + 1;
  }
  std::string out;
  for (const auto &part : parts) {
    if (!out.empty()) out.push_back('/');
    out += part;
  }
  return out;
}

std::string ResolveRelative(const std::string &base_dir, const std::string &href) {
  if (href.empty()) return NormalizeZipPath(base_dir);
  if (base_dir.empty()) return NormalizeZipPath(href);
  return NormalizeZipPath(base_dir + "/" + href);
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

std::string StripHrefSuffix(std::string href) {
  const size_t hash = href.find('#');
  if (hash != std::string::npos) href.erase(hash);
  const size_t query = href.find('?');
  if (query != std::string::npos) href.erase(query);
  return href;
}

bool ReadZipEntry(zip_t *za, const std::string &name, std::vector<unsigned char> &out) {
  zip_stat_t st{};
  zip_int64_t index = zip_name_locate(za, name.c_str(), 0);
  if (index < 0) {
    index = zip_name_locate(za, name.c_str(), ZIP_FL_NOCASE);
  }
  if (index < 0) return false;
  if (zip_stat_index(za, static_cast<zip_uint64_t>(index), 0, &st) != 0) return false;
  if (st.size > static_cast<zip_uint64_t>(128 * 1024 * 1024)) return false;
  zip_file_t *zf = zip_fopen_index(za, static_cast<zip_uint64_t>(index), 0);
  if (!zf) return false;
  out.resize(static_cast<size_t>(st.size));
  size_t total = 0;
  while (total < out.size()) {
    const zip_int64_t rd = zip_fread(zf, out.data() + total, out.size() - total);
    if (rd < 0) {
      zip_fclose(zf);
      out.clear();
      return false;
    }
    if (rd == 0) break;
    total += static_cast<size_t>(rd);
  }
  zip_fclose(zf);
  out.resize(total);
  return !out.empty();
}

bool ReadZipEntry(zip_t *za, const std::string &name, std::string &out) {
  std::vector<unsigned char> bytes;
  if (!ReadZipEntry(za, name, bytes)) return false;
  out.assign(bytes.begin(), bytes.end());
  return true;
}

SDL_Surface *LoadSurfaceFromMemory(const unsigned char *data, size_t size) {
  return DecodeSurfaceFromMemory(data, size);
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

bool ResampleRgbaSurface(SDL_Surface *src, int dst_w, int dst_h, std::vector<unsigned char> &out,
                         const std::atomic<bool> *cancel) {
  if (!src || src->w <= 0 || src->h <= 0 || dst_w <= 0 || dst_h <= 0) return false;
  out.assign(static_cast<size_t>(dst_w * dst_h * 4), 0);
  const bool area = dst_w < src->w || dst_h < src->h;
  for (int y = 0; y < dst_h; ++y) {
    if (cancel && cancel->load()) {
      out.clear();
      return false;
    }
    for (int x = 0; x < dst_w; ++x) {
      const size_t di = static_cast<size_t>((y * dst_w + x) * 4);
      if (area) {
        const double x0 = static_cast<double>(x) * src->w / dst_w;
        const double x1 = static_cast<double>(x + 1) * src->w / dst_w;
        const double y0 = static_cast<double>(y) * src->h / dst_h;
        const double y1 = static_cast<double>(y + 1) * src->h / dst_h;
        const int ix0 = std::max(0, static_cast<int>(std::floor(x0)));
        const int ix1 = std::min(src->w, static_cast<int>(std::ceil(x1)));
        const int iy0 = std::max(0, static_cast<int>(std::floor(y0)));
        const int iy1 = std::min(src->h, static_cast<int>(std::ceil(y1)));
        double sum[4] = {0.0, 0.0, 0.0, 0.0};
        double total = 0.0;
        for (int sy = iy0; sy < iy1; ++sy) {
          const double wy = std::max(0.0, std::min(y1, static_cast<double>(sy + 1)) -
                                              std::max(y0, static_cast<double>(sy)));
          if (wy <= 0.0) continue;
          const unsigned char *row = static_cast<const unsigned char *>(src->pixels) + sy * src->pitch;
          for (int sx = ix0; sx < ix1; ++sx) {
            const double wx = std::max(0.0, std::min(x1, static_cast<double>(sx + 1)) -
                                                std::max(x0, static_cast<double>(sx)));
            const double weight = wx * wy;
            if (weight <= 0.0) continue;
            const unsigned char *p = row + sx * 4;
            for (int c = 0; c < 4; ++c) sum[c] += static_cast<double>(p[c]) * weight;
            total += weight;
          }
        }
        if (total <= 0.0) total = 1.0;
        for (int c = 0; c < 4; ++c) {
          out[di + c] = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(sum[c] / total)), 0, 255));
        }
      } else {
        const double sx = (static_cast<double>(x) + 0.5) * src->w / dst_w - 0.5;
        const double sy = (static_cast<double>(y) + 0.5) * src->h / dst_h - 0.5;
        const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, src->w - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, src->h - 1);
        const int x1 = std::min(x0 + 1, src->w - 1);
        const int y1 = std::min(y0 + 1, src->h - 1);
        const double fx = std::clamp(sx - std::floor(sx), 0.0, 1.0);
        const double fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
        const unsigned char *row0 = static_cast<const unsigned char *>(src->pixels) + y0 * src->pitch;
        const unsigned char *row1 = static_cast<const unsigned char *>(src->pixels) + y1 * src->pitch;
        const unsigned char *p00 = row0 + x0 * 4;
        const unsigned char *p10 = row0 + x1 * 4;
        const unsigned char *p01 = row1 + x0 * 4;
        const unsigned char *p11 = row1 + x1 * 4;
        for (int c = 0; c < 4; ++c) {
          const double top = p00[c] * (1.0 - fx) + p10[c] * fx;
          const double bottom = p01[c] * (1.0 - fx) + p11[c] * fx;
          out[di + c] =
              static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(top * (1.0 - fy) + bottom * fy)), 0, 255));
        }
      }
    }
  }
  return true;
}

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
  if (!za) {
    runtime_log::Line("[epub_comic] zip_open failed path=" + path + " zerr=" + std::to_string(zerr));
    return false;
  }

  std::string container_xml;
  if (!ReadZipEntry(za, "META-INF/container.xml", container_xml)) {
    runtime_log::Line("[epub_comic] missing META-INF/container.xml path=" + path);
    zip_close(za);
    return false;
  }

  std::string rootfile_tag;
  if (!PickFirstMatch(container_xml, std::regex("<rootfile\\b([^>]*)>", std::regex::icase), rootfile_tag)) {
    runtime_log::Line("[epub_comic] cannot locate rootfile path=" + path);
    zip_close(za);
    return false;
  }
  AttrMap rootfile_attrs = ParseTagAttrs(rootfile_tag);
  const auto full_it = rootfile_attrs.find("full-path");
  if (full_it == rootfile_attrs.end() || full_it->second.empty()) {
    runtime_log::Line("[epub_comic] rootfile full-path missing path=" + path);
    zip_close(za);
    return false;
  }

  const std::string opf_path = full_it->second;
  std::string opf;
  if (!ReadZipEntry(za, opf_path, opf)) {
    runtime_log::Line("[epub_comic] cannot read OPF path=" + path + " opf=" + opf_path);
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
  std::vector<std::string> manifest_images;

  const std::regex item_re("<item\\b([^>]*)>", std::regex::icase);
  for (std::sregex_iterator it(opf.begin(), opf.end(), item_re), end; it != end; ++it) {
    AttrMap attrs = ParseTagAttrs((*it)[1].str());
    const auto id_it = attrs.find("id");
    const auto href_it = attrs.find("href");
    if (id_it == attrs.end() || href_it == attrs.end() || id_it->second.empty() || href_it->second.empty()) {
      continue;
    }
    ManifestItem item;
    item.href = StripHrefSuffix(href_it->second);
    const auto mt_it = attrs.find("media-type");
    if (mt_it != attrs.end()) item.media_type = mt_it->second;
    id_to_item[id_it->second] = item;
    const std::string resolved = ResolveRelative(opf_dir, item.href);
    href_to_media_type[resolved] = item.media_type;
    if (IsHtmlMediaType(item.media_type)) manifest_html_docs.push_back(resolved);
    if (item.media_type.rfind("image/", 0) == 0 || IsLikelyImagePath(resolved)) {
      manifest_images.push_back(resolved);
    }
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
    runtime_log::Line("[epub_comic] no html/xhtml spine content path=" + path);
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
      const std::string img_entry = ResolveRelative(doc_base, StripHrefSuffix(src_it->second));
      const auto mt_it = href_to_media_type.find(img_entry);
      const bool looks_like_image =
          (mt_it != href_to_media_type.end() && mt_it->second.rfind("image/", 0) == 0) ||
          IsLikelyImagePath(img_entry);
      if (!looks_like_image) continue;
      pages.push_back(PageEntry{img_entry});
    }
  }
  if (pages.empty()) {
    for (const auto &img : manifest_images) {
      pages.push_back(PageEntry{img});
    }
    if (!pages.empty()) {
      runtime_log::Line("[epub_comic] using manifest image sequence path=" + path +
                        " pages=" + std::to_string(pages.size()));
    }
  }

  if (pages.empty()) {
    runtime_log::Line("[epub_comic] no image pages path=" + path);
    zip_close(za);
    return false;
  }
  runtime_log::Line("[epub_comic] image pages path=" + path +
                    " pages=" + std::to_string(pages.size()) +
                    " first=" + pages.front().image_entry);

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
  if (!ReadZipEntry(impl_->zip, entry.image_entry, bytes)) {
    runtime_log::Line("[epub_comic] page size read failed page=" + std::to_string(page_index) +
                      " entry=" + entry.image_entry);
    return false;
  }
  SDL_Surface *surface = LoadSurfaceFromMemory(bytes.data(), bytes.size());
  if (!surface) {
    runtime_log::Line("[epub_comic] page size decode failed page=" + std::to_string(page_index) +
                      " entry=" + entry.image_entry +
                      " bytes=" + std::to_string(bytes.size()));
    return false;
  }
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
  scale = std::max(image_flow::kMinRenderScale, scale);
  if (cancel && cancel->load()) return false;
#ifdef HAVE_LIBZIP
  PageEntry &entry = impl_->pages[page_index];
  std::vector<unsigned char> bytes;
  if (!ReadZipEntry(impl_->zip, entry.image_entry, bytes)) {
    runtime_log::Line("[epub_comic] render read failed page=" + std::to_string(page_index) +
                      " entry=" + entry.image_entry);
    return false;
  }
  if (cancel && cancel->load()) return false;
  SDL_Surface *surface = LoadSurfaceFromMemory(bytes.data(), bytes.size());
  if (!surface) {
    runtime_log::Line("[epub_comic] render decode failed page=" + std::to_string(page_index) +
                      " entry=" + entry.image_entry +
                      " bytes=" + std::to_string(bytes.size()));
    return false;
  }

  SDL_Surface *rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(surface);
  if (!rgba_surface) {
    runtime_log::Line("[epub_comic] render convert failed page=" + std::to_string(page_index) +
                      " entry=" + entry.image_entry +
                      " err=" + SDL_GetError());
    return false;
  }

  entry.width = rgba_surface->w;
  entry.height = rgba_surface->h;
  entry.size_known = (entry.width > 0 && entry.height > 0);

  w = std::max(1, static_cast<int>(std::round(static_cast<float>(rgba_surface->w) * scale)));
  h = std::max(1, static_cast<int>(std::round(static_cast<float>(rgba_surface->h) * scale)));

  if (!ResampleRgbaSurface(rgba_surface, w, h, rgba, cancel)) {
    runtime_log::Line("[epub_comic] render resample failed page=" + std::to_string(page_index) +
                      " src=" + std::to_string(rgba_surface->w) + "x" +
                      std::to_string(rgba_surface->h) +
                      " dst=" + std::to_string(w) + "x" + std::to_string(h));
    SDL_FreeSurface(rgba_surface);
    return false;
  }
  SDL_FreeSurface(rgba_surface);
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
