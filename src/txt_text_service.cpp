#include "txt_text_service.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
constexpr char kTxtParagraphIndentAscii[] = "  ";

std::filesystem::path GetTxtLayoutCacheFile(const TxtTextServiceState &state,
                                            const std::string &cache_key) {
  const size_t hash_value = std::hash<std::string>{}(cache_key + "|layout");
  std::ostringstream oss;
  oss << std::hex << hash_value << ".bin";
  return state.cache_dir / oss.str();
}

std::filesystem::path GetTxtResumeCacheFile(const TxtTextServiceState &state,
                                            const std::string &cache_key) {
  const size_t hash_value = std::hash<std::string>{}(cache_key + "|resume");
  std::ostringstream oss;
  oss << std::hex << hash_value << ".resume";
  return state.cache_dir / oss.str();
}

size_t Utf8CharLen(unsigned char c) {
  if ((c & 0x80u) == 0) return 1;
  if ((c & 0xE0u) == 0xC0u) return 2;
  if ((c & 0xF0u) == 0xE0u) return 3;
  if ((c & 0xF8u) == 0xF0u) return 4;
  return 1;
}

#ifdef HAVE_SDL2_TTF
std::string NormalizeTextParagraph(const std::string &line) {
  auto is_ignorable_at = [&](size_t pos, size_t &len) -> bool {
    if (pos >= line.size()) {
      len = 0;
      return false;
    }
    const unsigned char c = static_cast<unsigned char>(line[pos]);
    if (c == ' ' || c == '\t') {
      len = 1;
      return true;
    }
    if (pos + 3 <= line.size() &&
        static_cast<unsigned char>(line[pos]) == 0xE3 &&
        static_cast<unsigned char>(line[pos + 1]) == 0x80 &&
        static_cast<unsigned char>(line[pos + 2]) == 0x80) {
      len = 3;
      return true;
    }
    len = 0;
    return false;
  };

  size_t start = 0;
  while (start < line.size()) {
    size_t len = 0;
    if (!is_ignorable_at(start, len)) break;
    start += len;
  }

  size_t end = line.size();
  while (end > start) {
    size_t probe = end - 1;
    if (probe >= 2 &&
        static_cast<unsigned char>(line[probe - 2]) == 0xE3 &&
        static_cast<unsigned char>(line[probe - 1]) == 0x80 &&
        static_cast<unsigned char>(line[probe]) == 0x80) {
      end -= 3;
      continue;
    }
    if (line[probe] == ' ' || line[probe] == '\t') {
      --end;
      continue;
    }
    break;
  }

  if (end <= start) return "";
  return std::string(kTxtParagraphIndentAscii) + line.substr(start, end - start);
}

std::vector<std::string> WrapTextLine(const std::string &line, int max_width_px, TTF_Font *font) {
  std::vector<std::string> out;
  if (line.empty()) {
    out.push_back("");
    return out;
  }
  if (!font) {
    out.push_back(line);
    return out;
  }

  max_width_px = std::max(40, max_width_px);
  std::string expanded;
  expanded.reserve(line.size() + 8);
  std::vector<size_t> char_offsets;
  char_offsets.reserve(line.size() + 1);
  char_offsets.push_back(0);
  for (size_t pos = 0; pos < line.size();) {
    const unsigned char c = static_cast<unsigned char>(line[pos]);
    size_t len = Utf8CharLen(c);
    if (pos + len > line.size()) len = 1;
    if (len == 1 && line[pos] == '\t') expanded += "    ";
    else expanded.append(line, pos, len);
    pos += len;
    char_offsets.push_back(expanded.size());
  }

  const size_t total_chars = char_offsets.size() - 1;
  size_t start_char = 0;
  while (start_char < total_chars) {
    size_t lo = start_char + 1;
    size_t hi = total_chars;
    size_t best = start_char + 1;
    while (lo <= hi) {
      const size_t mid = lo + (hi - lo) / 2;
      const size_t byte_start = char_offsets[start_char];
      const size_t byte_end = char_offsets[mid];
      const std::string candidate = expanded.substr(byte_start, byte_end - byte_start);
      int text_w = 0;
      if (TTF_SizeUTF8(font, candidate.c_str(), &text_w, nullptr) == 0 && text_w <= max_width_px) {
        best = mid;
        lo = mid + 1;
      } else {
        if (mid == 0) break;
        hi = mid - 1;
      }
    }

    size_t break_char = best;
    if (best < total_chars) {
      for (size_t scan = best; scan > start_char; --scan) {
        const size_t b0 = char_offsets[scan - 1];
        const size_t b1 = char_offsets[scan];
        if (b1 == b0 + 1) {
          const char ch = expanded[b0];
          if (ch == ' ' || ch == '-' || ch == ',' || ch == '.' || ch == ';' || ch == ':' ||
              ch == '!' || ch == '?') {
            break_char = scan;
            break;
          }
        }
      }
    }

    const size_t byte_start = char_offsets[start_char];
    const size_t byte_end = char_offsets[break_char];
    out.emplace_back(expanded.substr(byte_start, byte_end - byte_start));
    start_char = break_char;
    while (start_char < total_chars) {
      const size_t b0 = char_offsets[start_char];
      const size_t b1 = char_offsets[start_char + 1];
      if (b1 != b0 + 1 || expanded[b0] != ' ') break;
      ++start_char;
    }
  }

  if (out.empty()) out.push_back("");
  return out;
}
#endif
}

std::string MakeTxtLayoutCacheKey(const std::string &path, const SDL_Rect &bounds, int line_h,
                                  uintmax_t file_size, long long file_mtime,
                                  const NormalizePathKeyFn &normalize_path_key) {
  return normalize_path_key(path) + "|" + std::to_string(file_size) + "|" +
         std::to_string(file_mtime) + "|" + std::to_string(bounds.w) + "x" +
         std::to_string(bounds.h) + "|" + std::to_string(line_h);
}

void PruneTxtLayoutCache(TxtTextServiceState &state) {
  while (state.layout_cache.size() > state.max_cache_entries) {
    auto oldest = state.layout_cache.end();
    for (auto it = state.layout_cache.begin(); it != state.layout_cache.end(); ++it) {
      if (oldest == state.layout_cache.end() || it->second.last_use < oldest->second.last_use) {
        oldest = it;
      }
    }
    if (oldest == state.layout_cache.end()) break;
    state.layout_cache.erase(oldest);
  }
}

void ClearTxtLayoutCache(TxtTextServiceState &state) {
  state.layout_cache.clear();
}

bool LoadTxtLayoutCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                TxtLayoutCacheEntry &entry) {
  std::ifstream in(GetTxtLayoutCacheFile(state, cache_key), std::ios::binary);
  if (!in) return false;
  auto read_u32 = [&](uint32_t &v) -> bool {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
    return static_cast<bool>(in);
  };
  auto read_i32 = [&](int &v) -> bool {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
    return static_cast<bool>(in);
  };
  uint32_t line_count = 0;
  if (!read_u32(line_count) || !read_i32(entry.viewport_w) || !read_i32(entry.viewport_h) ||
      !read_i32(entry.line_h) || !read_i32(entry.content_h)) {
    return false;
  }
  uint32_t flags = 0;
  if (!read_u32(flags)) return false;
  entry.truncated = (flags & 1u) != 0;
  entry.limit_hit = (flags & 2u) != 0;
  entry.lines.clear();
  entry.lines.reserve(line_count);
  for (uint32_t i = 0; i < line_count; ++i) {
    uint32_t len = 0;
    if (!read_u32(len)) return false;
    std::string line(len, '\0');
    if (len > 0) in.read(line.data(), static_cast<std::streamsize>(len));
    if (!in) return false;
    entry.lines.push_back(std::move(line));
  }
  entry.last_use = SDL_GetTicks();
  return true;
}

void SaveTxtLayoutCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const TxtLayoutCacheEntry &entry) {
  std::error_code ec;
  std::filesystem::create_directories(state.cache_dir, ec);
  std::ofstream out(GetTxtLayoutCacheFile(state, cache_key), std::ios::binary | std::ios::trunc);
  if (!out) return;
  auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
  auto write_i32 = [&](int v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
  write_u32(static_cast<uint32_t>(entry.lines.size()));
  write_i32(entry.viewport_w);
  write_i32(entry.viewport_h);
  write_i32(entry.line_h);
  write_i32(entry.content_h);
  uint32_t flags = (entry.truncated ? 1u : 0u) | (entry.limit_hit ? 2u : 0u);
  write_u32(flags);
  for (const auto &line : entry.lines) {
    write_u32(static_cast<uint32_t>(line.size()));
    if (!line.empty()) out.write(line.data(), static_cast<std::streamsize>(line.size()));
  }
}

bool LoadTxtResumeCacheFromDisk(TxtTextServiceState &state, const std::string &cache_key,
                                TxtResumeCacheEntry &entry) {
  std::ifstream in(GetTxtResumeCacheFile(state, cache_key), std::ios::binary);
  if (!in) return false;
  auto read_u32 = [&](uint32_t &v) -> bool {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
    return static_cast<bool>(in);
  };
  auto read_u64 = [&](uint64_t &v) -> bool {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
    return static_cast<bool>(in);
  };
  auto read_i32 = [&](int &v) -> bool {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
    return static_cast<bool>(in);
  };
  auto read_string = [&](std::string &s) -> bool {
    uint32_t len = 0;
    if (!read_u32(len)) return false;
    s.assign(len, '\0');
    if (len > 0) in.read(s.data(), static_cast<std::streamsize>(len));
    return static_cast<bool>(in);
  };
  uint32_t line_count = 0;
  uint64_t parse_pos = 0;
  uint32_t flags = 0;
  if (!read_u32(line_count) || !read_i32(entry.viewport_w) || !read_i32(entry.viewport_h) ||
      !read_i32(entry.line_h) || !read_i32(entry.content_h) ||
      !read_i32(entry.scroll_px) || !read_i32(entry.target_scroll_px) ||
      !read_u64(parse_pos) || !read_u32(flags)) {
    return false;
  }
  entry.parse_pos = static_cast<size_t>(parse_pos);
  entry.loading = (flags & 1u) != 0;
  entry.truncated = (flags & 2u) != 0;
  entry.limit_hit = (flags & 4u) != 0;
  entry.truncation_notice_added = (flags & 8u) != 0;
  entry.lines.clear();
  entry.lines.reserve(line_count);
  for (uint32_t i = 0; i < line_count; ++i) {
    std::string line;
    if (!read_string(line)) return false;
    entry.lines.push_back(std::move(line));
  }
  if (!read_string(entry.pending_raw) || !read_string(entry.pending_line)) return false;
  return true;
}

void SaveTxtResumeCacheToDisk(TxtTextServiceState &state, const std::string &cache_key,
                              const TxtReaderState &state_to_save) {
  std::error_code ec;
  std::filesystem::create_directories(state.cache_dir, ec);
  std::ofstream out(GetTxtResumeCacheFile(state, cache_key), std::ios::binary | std::ios::trunc);
  if (!out) return;
  auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
  auto write_u64 = [&](uint64_t v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
  auto write_i32 = [&](int v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
  auto write_string = [&](const std::string &s) {
    write_u32(static_cast<uint32_t>(s.size()));
    if (!s.empty()) out.write(s.data(), static_cast<std::streamsize>(s.size()));
  };

  write_u32(static_cast<uint32_t>(state_to_save.lines.size()));
  write_i32(state_to_save.viewport_w);
  write_i32(state_to_save.viewport_h);
  write_i32(state_to_save.line_h);
  write_i32(state_to_save.content_h);
  write_i32(state_to_save.scroll_px);
  write_i32(state_to_save.target_scroll_px);
  write_u64(static_cast<uint64_t>(state_to_save.parse_pos));
  uint32_t flags = (state_to_save.loading ? 1u : 0u) |
                   (state_to_save.truncated ? 2u : 0u) |
                   (state_to_save.limit_hit ? 4u : 0u) |
                   (state_to_save.truncation_notice_added ? 8u : 0u);
  write_u32(flags);
  for (const auto &line : state_to_save.lines) write_string(line);
  write_string(state_to_save.pending_raw);
  write_string(state_to_save.pending_line);
}

SDL_Rect GetTxtViewportBounds(SDL_Renderer *renderer, int screen_w, int screen_h,
                              int margin_x, int margin_y) {
  int output_w = screen_w;
  int output_h = screen_h;
  if (renderer) {
    int rw = 0;
    int rh = 0;
    if (SDL_GetRendererOutputSize(renderer, &rw, &rh) == 0) {
      if (rw > 0) output_w = rw;
      if (rh > 0) output_h = rh;
    }
  }
  const int mx = std::max(12, std::min(margin_x, std::max(0, output_w / 12)));
  const int my = std::max(12, std::min(margin_y, std::max(0, output_h / 12)));
  SDL_Rect rect{};
  rect.x = mx;
  rect.y = my;
  rect.w = std::max(100, output_w - mx * 2);
  rect.h = std::max(100, output_h - my * 2);
  return rect;
}

#ifdef HAVE_SDL2_TTF
bool AppendWrappedTextLine(TxtReaderState &state, const std::string &line, TTF_Font *font,
                           size_t max_wrapped_lines) {
  const std::string paragraph = NormalizeTextParagraph(line);
  if (paragraph.empty()) return true;
  const int wrap_width_px = std::max(40, state.viewport_w - 6);
  std::vector<std::string> wrapped = WrapTextLine(paragraph, wrap_width_px, font);
  for (const std::string &wline : wrapped) {
    state.lines.push_back(wline);
    if (state.lines.size() >= max_wrapped_lines) {
      state.content_h = static_cast<int>(state.lines.size()) * state.line_h;
      return false;
    }
  }
  state.content_h = static_cast<int>(state.lines.size()) * state.line_h;
  return true;
}
#endif
