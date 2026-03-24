#include "cover_resolver.h"

#include <array>
#include <filesystem>
#include <regex>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

const std::array<const char *, 5> kCoverExts = {".png", ".jpg", ".jpeg", ".webp", ".bmp"};
const std::array<const char *, 6> kFolderCoverNames = {
    "folder_cover.png", "cover.png", "folder_cover.jpg", "cover.jpg", "folder_cover.webp", "cover.webp"};

std::string FirstExistingWithBase(const std::vector<std::string> &roots, const std::string &base) {
  for (const auto &root : roots) {
    for (const char *ext : kCoverExts) {
      fs::path p = fs::path(root) / (base + ext);
      if (fs::exists(p) && fs::is_regular_file(p)) return p.string();
    }
  }
  return {};
}

std::string TrimVersionSuffix(std::string name) {
  // Strip suffixes like "-v2", "_03", "(12)", "[7]".
  const std::regex suffix_re(R"(([-_][vV]?\d+|[\(\[]\d+[\)\]])$)");
  while (true) {
    std::smatch m;
    if (!std::regex_search(name, m, suffix_re) || m.empty()) break;
    std::string next = name.substr(0, m.position());
    while (!next.empty() && (next.back() == ' ' || next.back() == '\t')) next.pop_back();
    if (next == name) break;
    name = next;
  }
  return name;
}

std::string TrimEpisodeSuffix(std::string name) {
  // Strip trailing episode/chapter tails like:
  // " 1-7話", " 23話", " 第3话", " 12-13集", " 95-96", and optional ending tags.
  static const std::regex ending_tag_re(R"(\s*[\[\(][^)\]]*[\]\)]\s*$)");
  static const std::regex episode_re(
      R"(\s*(第\s*)?\d+(\s*[-~—－]\s*\d+)?\s*(話|话|集|章|卷)?\s*$)",
      std::regex::icase);

  // Remove one trailing [tag]/(tag), then episode pattern, then trim spaces.
  name = std::regex_replace(name, ending_tag_re, "");
  name = std::regex_replace(name, episode_re, "");
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '-' || name.back() == '_')) {
    name.pop_back();
  }
  return name;
}

void AddCandidate(std::vector<std::string> &out, const std::string &s, std::unordered_set<std::string> &seen) {
  if (s.empty()) return;
  if (seen.insert(s).second) out.push_back(s);
}

std::vector<std::string> BuildFuzzyCandidates(const std::string &base) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  AddCandidate(out, TrimVersionSuffix(base), seen);
  AddCandidate(out, TrimEpisodeSuffix(base), seen);
  AddCandidate(out, TrimEpisodeSuffix(TrimVersionSuffix(base)), seen);
  std::vector<std::string> filtered;
  for (const auto &cand : out) {
    if (!cand.empty() && cand != base) filtered.push_back(cand);
  }
  return filtered;
}

} // namespace

namespace cover_resolver {

std::string ResolveCoverPathExact(const std::string &item_path,
                                  bool is_dir,
                                  const std::vector<std::string> &cover_roots) {
  fs::path p(item_path);
  if (item_path.empty()) return {};

  if (is_dir) {
    for (const char *name : kFolderCoverNames) {
      fs::path candidate = p / name;
      if (fs::exists(candidate) && fs::is_regular_file(candidate)) return candidate.string();
    }
  }

  const std::string base = p.stem().string();
  if (!base.empty()) {
    std::string found = FirstExistingWithBase(cover_roots, base);
    if (!found.empty()) return found;
  }

  return {};
}

std::string ResolveCoverPathFuzzy(const std::string &item_path,
                                  bool is_dir,
                                  const std::vector<std::string> &cover_roots) {
  fs::path p(item_path);
  if (item_path.empty()) return {};

  const std::string base = p.stem().string();
  if (!base.empty()) {
    const std::vector<std::string> candidates = BuildFuzzyCandidates(base);
    for (const auto &cand : candidates) {
      std::string found = FirstExistingWithBase(cover_roots, cand);
      if (!found.empty()) return found;
    }
  }

  // For files inside folder-mode shelves, fallback to parent folder cover.
  if (!is_dir) {
    const fs::path parent = p.parent_path();
    if (!parent.empty()) {
      for (const char *name : kFolderCoverNames) {
        fs::path candidate = parent / name;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) return candidate.string();
      }
      const std::string parent_base = parent.filename().string();
      if (!parent_base.empty()) {
        const std::vector<std::string> parent_candidates = BuildFuzzyCandidates(parent_base);
        for (const auto &cand : parent_candidates) {
          std::string found = FirstExistingWithBase(cover_roots, cand);
          if (!found.empty()) return found;
        }
      }
    }
  }

  return {};
}

std::string ResolveCoverPath(const std::string &item_path, bool is_dir, const std::vector<std::string> &cover_roots) {
  std::string found = ResolveCoverPathExact(item_path, is_dir, cover_roots);
  if (!found.empty()) return found;
  return ResolveCoverPathFuzzy(item_path, is_dir, cover_roots);
}

} // namespace cover_resolver
