#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EpubReader {
public:
  struct ExtractedText {
    std::string text;
    uintmax_t logical_size = 0;
    long long logical_mtime = 0;
  };

  struct CoverImage {
    std::vector<unsigned char> bytes;
    uintmax_t logical_size = 0;
    long long logical_mtime = 0;
  };

  bool ExtractText(const std::string &path,
                   const std::string &asset_cache_dir,
                   ExtractedText &out,
                   std::string &error);
  bool ExtractCoverImage(const std::string &path, CoverImage &out, std::string &error);
  bool Available() const;
  const char *BackendName() const;
};
