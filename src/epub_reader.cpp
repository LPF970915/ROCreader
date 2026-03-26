#include "epub_reader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef HAVE_LIBZIP
#include <zip.h>
#endif

namespace {

#ifdef HAVE_LIBZIP
std::string ResolveRelative(const std::string &base_dir, const std::string &href) {
  try {
    std::filesystem::path p = std::filesystem::path(base_dir) / std::filesystem::path(href);
    return p.lexically_normal().generic_string();
  } catch (...) {
    return href;
  }
}

std::string DecodeHtmlEntities(std::string s) {
  const std::pair<const char *, const char *> table[] = {
      {"&nbsp;", " "},
      {"&lt;", "<"},
      {"&gt;", ">"},
      {"&amp;", "&"},
      {"&quot;", "\""},
      {"&apos;", "'"},
  };
  for (const auto &kv : table) {
    std::string key(kv.first);
    std::string val(kv.second);
    size_t pos = 0;
    while ((pos = s.find(key, pos)) != std::string::npos) {
      s.replace(pos, key.size(), val);
      pos += val.size();
    }
  }
  return s;
}

std::string HtmlToText(std::string html) {
  html = std::regex_replace(
      html,
      std::regex("<script[^>]*>[\\s\\S]*?</script>", std::regex::icase),
      " ");
  html = std::regex_replace(
      html,
      std::regex("<style[^>]*>[\\s\\S]*?</style>", std::regex::icase),
      " ");
  html = std::regex_replace(html, std::regex("<br\\s*/?>", std::regex::icase), "\n");
  html = std::regex_replace(html, std::regex("</p\\s*>", std::regex::icase), "\n\n");
  html = std::regex_replace(html, std::regex("</div\\s*>", std::regex::icase), "\n");
  html = std::regex_replace(html, std::regex("<[^>]+>", std::regex::icase), " ");
  html = DecodeHtmlEntities(std::move(html));
  html = std::regex_replace(html, std::regex("[ \\t\\r\\f\\v]+"), " ");
  html = std::regex_replace(html, std::regex("\\n{3,}"), "\n\n");
  return html;
}

bool PickFirstMatch(const std::string &src, const std::regex &re, std::string &out) {
  std::smatch m;
  if (!std::regex_search(src, m, re) || m.size() < 2) return false;
  out = m[1].str();
  return true;
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

bool IsHtmlMediaType(const std::string &media_type) {
  return media_type == "application/xhtml+xml" || media_type == "text/html";
}

bool WriteBinaryFile(const std::filesystem::path &path, const std::string &data) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(out);
}

struct ManifestItem {
  std::string href;
  std::string media_type;
  std::string properties;
};

struct EpubPackage {
  std::unordered_map<std::string, ManifestItem> id_to_item;
  std::unordered_map<std::string, std::string> href_to_media_type;
  std::vector<std::string> ordered_docs;
  std::string opf_dir;
};

bool ReadZipEntry(zip_t *za, const std::string &name, std::string &content) {
  zip_stat_t st{};
  if (zip_stat(za, name.c_str(), 0, &st) != 0) return false;
  if (st.size > static_cast<zip_uint64_t>(64 * 1024 * 1024)) return false;
  zip_file_t *zf = zip_fopen(za, name.c_str(), 0);
  if (!zf) return false;
  std::string data;
  data.resize(static_cast<size_t>(st.size));
  const zip_int64_t rd = zip_fread(zf, data.data(), st.size);
  zip_fclose(zf);
  if (rd < 0) return false;
  data.resize(static_cast<size_t>(rd));
  content = std::move(data);
  return true;
}

bool ParsePackage(zip_t *za, EpubPackage &pkg, std::string &error) {
  std::string container_xml;
  if (!ReadZipEntry(za, "META-INF/container.xml", container_xml)) {
    error = "missing META-INF/container.xml";
    return false;
  }

  std::string rootfile_tag;
  if (!PickFirstMatch(container_xml, std::regex("<rootfile\\b([^>]*)>", std::regex::icase), rootfile_tag)) {
    error = "cannot locate rootfile in container.xml";
    return false;
  }

  AttrMap rootfile_attrs = ParseTagAttrs(rootfile_tag);
  const auto full_it = rootfile_attrs.find("full-path");
  if (full_it == rootfile_attrs.end() || full_it->second.empty()) {
    error = "rootfile full-path missing";
    return false;
  }
  const std::string opf_path = full_it->second;

  std::string opf;
  if (!ReadZipEntry(za, opf_path, opf)) {
    error = "cannot read OPF: " + opf_path;
    return false;
  }

  try {
    pkg.opf_dir = std::filesystem::path(opf_path).parent_path().generic_string();
  } catch (...) {
    pkg.opf_dir.clear();
  }

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
    const auto prop_it = attrs.find("properties");
    if (prop_it != attrs.end()) item.properties = prop_it->second;
    pkg.id_to_item[id_it->second] = item;

    const std::string resolved_href = ResolveRelative(pkg.opf_dir, item.href);
    pkg.href_to_media_type[resolved_href] = item.media_type;
    if (IsHtmlMediaType(item.media_type)) {
      manifest_html_docs.push_back(resolved_href);
    }
  }

  const std::regex spine_re("<itemref\\b[^>]*idref\\s*=\\s*['\"]([^'\"]+)['\"][^>]*>", std::regex::icase);
  for (std::sregex_iterator it(opf.begin(), opf.end(), spine_re), end; it != end; ++it) {
    const std::string idref = (*it)[1].str();
    const auto mit = pkg.id_to_item.find(idref);
    if (mit == pkg.id_to_item.end()) continue;
    if (!IsHtmlMediaType(mit->second.media_type)) continue;
    pkg.ordered_docs.push_back(ResolveRelative(pkg.opf_dir, mit->second.href));
  }

  if (pkg.ordered_docs.empty()) {
    pkg.ordered_docs = manifest_html_docs;
  }
  if (pkg.ordered_docs.empty()) {
    error = "no html/xhtml spine content";
    return false;
  }
  return true;
}

bool ExtractImageRefsFromHtml(const std::string &html,
                              const std::string &doc_path,
                              std::vector<std::string> &out_refs) {
  const std::regex img_tag_re("<img\\b([^>]*)>", std::regex::icase);
  const std::string doc_base = std::filesystem::path(doc_path).parent_path().generic_string();
  for (std::sregex_iterator it(html.begin(), html.end(), img_tag_re), end; it != end; ++it) {
    AttrMap attrs = ParseTagAttrs((*it)[1].str());
    const auto src_it = attrs.find("src");
    if (src_it == attrs.end() || src_it->second.empty()) continue;
    out_refs.push_back(ResolveRelative(doc_base, src_it->second));
  }
  return !out_refs.empty();
}
#endif

} // namespace

bool EpubReader::Available() const {
#ifdef HAVE_LIBZIP
  return true;
#else
  return false;
#endif
}

const char *EpubReader::BackendName() const {
#ifdef HAVE_LIBZIP
  return "libzip";
#else
  return "none";
#endif
}

bool EpubReader::ExtractText(const std::string &path,
                             const std::string &asset_cache_dir,
                             ExtractedText &out,
                             std::string &error) {
#ifndef HAVE_LIBZIP
  (void)path;
  (void)asset_cache_dir;
  (void)out;
  error = "epub backend unavailable (libzip not enabled)";
  return false;
#else
  int zerr = 0;
  zip_t *za = zip_open(path.c_str(), ZIP_RDONLY, &zerr);
  if (!za) {
    error = "zip open failed";
    return false;
  }

  auto close_zip = [&]() {
    if (za) zip_close(za);
    za = nullptr;
  };

  EpubPackage pkg;
  if (!ParsePackage(za, pkg, error)) {
    close_zip();
    return false;
  }

  std::ostringstream all;
  uintmax_t total_size = 0;
  std::unordered_map<std::string, std::string> extracted_assets;
  const std::filesystem::path asset_root(asset_cache_dir);
  const std::regex img_tag_re("<img\\b([^>]*)>", std::regex::icase);
  for (const auto &doc : pkg.ordered_docs) {
    std::string html;
    if (!ReadZipEntry(za, doc, html)) continue;
    total_size += static_cast<uintmax_t>(html.size());
    std::string expanded;
    size_t cursor = 0;
    const std::string doc_base = std::filesystem::path(doc).parent_path().generic_string();
    for (std::sregex_iterator it(html.begin(), html.end(), img_tag_re), end; it != end; ++it) {
      const std::smatch &m = *it;
      const size_t pos = static_cast<size_t>(m.position());
      expanded.append(html, cursor, pos - cursor);
      cursor = pos + static_cast<size_t>(m.length());
      AttrMap attrs = ParseTagAttrs(m[1].str());
      auto src_it = attrs.find("src");
      if (src_it == attrs.end() || src_it->second.empty()) continue;
      const std::string img_entry = ResolveRelative(doc_base, src_it->second);
      std::string cached_path;
      std::string img_key;
      std::string img_ext;
      auto cache_it = extracted_assets.find(img_entry);
      if (cache_it != extracted_assets.end()) {
        cached_path = cache_it->second;
        const std::filesystem::path p(cached_path);
        img_key = p.stem().string();
        img_ext = p.extension().string();
        if (!img_ext.empty() && img_ext[0] == '.') img_ext.erase(0, 1);
        if (img_ext.empty()) img_ext = "bin";
      } else {
        std::string image_data;
        if (!ReadZipEntry(za, img_entry, image_data) || image_data.empty()) continue;
        const size_t img_hash = std::hash<std::string>{}(path + "|" + img_entry);
        std::ostringstream oss;
        oss << std::hex << img_hash;
        img_key = oss.str();
        img_ext = std::filesystem::path(img_entry).extension().string();
        if (!img_ext.empty() && img_ext[0] == '.') img_ext.erase(0, 1);
        if (img_ext.empty()) img_ext = "bin";
        const std::filesystem::path out_file = asset_root / (img_key + "." + img_ext);
        if (!WriteBinaryFile(out_file, image_data)) continue;
        cached_path = out_file.string();
        extracted_assets.emplace(img_entry, cached_path);
      }
      if (!img_key.empty()) {
        expanded += "\n[[EPUB_IMG:" + img_key + "|" + img_ext + "]]\n";
        expanded += "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n";
      }
    }
    expanded.append(html, cursor, std::string::npos);
    const std::string body = HtmlToText(std::move(expanded));
    if (!body.empty()) all << body << "\n\n";
    if (all.tellp() > static_cast<std::streamoff>(12 * 1024 * 1024)) break;
  }
  close_zip();

  out.text = all.str();
  if (out.text.empty()) {
    error = "no readable text content in epub";
    return false;
  }

  std::error_code ec;
  const auto mtime_raw = std::filesystem::last_write_time(std::filesystem::path(path), ec);
  out.logical_size = total_size;
  out.logical_mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
  return true;
#endif
}

bool EpubReader::ExtractCoverImage(const std::string &path, CoverImage &out, std::string &error) {
#ifndef HAVE_LIBZIP
  (void)path;
  (void)out;
  error = "epub backend unavailable (libzip not enabled)";
  return false;
#else
  int zerr = 0;
  zip_t *za = zip_open(path.c_str(), ZIP_RDONLY, &zerr);
  if (!za) {
    error = "zip open failed";
    return false;
  }

  auto close_zip = [&]() {
    if (za) zip_close(za);
    za = nullptr;
  };

  EpubPackage pkg;
  if (!ParsePackage(za, pkg, error)) {
    close_zip();
    return false;
  }

  std::string cover_entry;
  for (const auto &kv : pkg.id_to_item) {
    const ManifestItem &item = kv.second;
    if (item.properties.find("cover-image") != std::string::npos &&
        item.media_type.rfind("image/", 0) == 0) {
      cover_entry = ResolveRelative(pkg.opf_dir, item.href);
      break;
    }
  }

  if (cover_entry.empty()) {
    for (const auto &doc : pkg.ordered_docs) {
      std::string html;
      if (!ReadZipEntry(za, doc, html)) continue;
      std::vector<std::string> refs;
      ExtractImageRefsFromHtml(html, doc, refs);
      for (const auto &ref : refs) {
        const auto mt_it = pkg.href_to_media_type.find(ref);
        if (mt_it != pkg.href_to_media_type.end() && mt_it->second.rfind("image/", 0) == 0) {
          cover_entry = ref;
          break;
        }
        if (mt_it == pkg.href_to_media_type.end()) {
          const std::string ext = std::filesystem::path(ref).extension().string();
          if (!ext.empty()) {
            cover_entry = ref;
            break;
          }
        }
      }
      if (!cover_entry.empty()) break;
    }
  }

  if (cover_entry.empty()) {
    close_zip();
    error = "no cover image found in epub";
    return false;
  }

  std::string image_data;
  if (!ReadZipEntry(za, cover_entry, image_data) || image_data.empty()) {
    close_zip();
    error = "cannot read epub cover image";
    return false;
  }
  close_zip();

  out.bytes.assign(image_data.begin(), image_data.end());
  std::error_code ec;
  const uintmax_t file_size = std::filesystem::file_size(std::filesystem::path(path), ec);
  const auto mtime_raw = std::filesystem::last_write_time(std::filesystem::path(path), ec);
  out.logical_size = ec ? 0 : file_size;
  out.logical_mtime = ec ? 0LL : static_cast<long long>(mtime_raw.time_since_epoch().count());
  return true;
#endif
}
