#pragma once

#include <cstdint>
#include <string>

class EpubReader {
public:
  struct ExtractedText {
    std::string text;
    uintmax_t logical_size = 0;
    long long logical_mtime = 0;
  };

  bool ExtractText(const std::string &path,
                   const std::string &asset_cache_dir,
                   ExtractedText &out,
                   std::string &error);
  bool Available() const;
  const char *BackendName() const;
};
