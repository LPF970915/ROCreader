#pragma once

#include <string>
#include <vector>

struct BookItem {
  std::string name;
  std::string path;
  bool is_dir = false;
};

class BookScanner {
public:
  static std::vector<BookItem> Scan(const std::string &root);
  static std::vector<BookItem> ScanRoots(const std::vector<std::string> &roots);
  static std::vector<BookItem> ScanPath(const std::string &path, bool allow_subdirs);
};
