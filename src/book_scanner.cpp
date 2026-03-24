#include "book_scanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
bool HasSupportedExt(const fs::path &p) {
  if (!p.has_extension()) {
    return false;
  }
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".pdf" || ext == ".txt";
}
} // namespace

std::vector<BookItem> BookScanner::Scan(const std::string &root) {
  return ScanRoots({root});
}

std::vector<BookItem> BookScanner::ScanRoots(const std::vector<std::string> &roots) {
  std::vector<BookItem> out;
  for (const auto &root : roots) {
    std::vector<BookItem> scanned = ScanPath(root, true);
    out.insert(out.end(), scanned.begin(), scanned.end());
  }

  std::sort(out.begin(), out.end(), [](const BookItem &a, const BookItem &b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir > b.is_dir;
    }
    return a.name < b.name;
  });

  return out;
}

std::vector<BookItem> BookScanner::ScanPath(const std::string &path, bool allow_subdirs) {
  std::vector<BookItem> out;
  fs::path root_path(path);
  if (!fs::exists(root_path) || !fs::is_directory(root_path)) {
    return out;
  }

  for (const auto &entry : fs::directory_iterator(root_path)) {
    const bool is_dir = entry.is_directory();
    if (is_dir && !allow_subdirs) {
      continue;
    }

    BookItem item;
    item.name = entry.path().filename().string();
    item.path = entry.path().string();
    item.is_dir = is_dir;

    if (item.is_dir || HasSupportedExt(entry.path())) {
      out.push_back(std::move(item));
    }
  }

  std::sort(out.begin(), out.end(), [](const BookItem &a, const BookItem &b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir > b.is_dir;
    }
    return a.name < b.name;
  });

  return out;
}
