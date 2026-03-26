#include "storage_paths.h"

#include <cstdlib>
#include <filesystem>
#include <unordered_set>
#include <system_error>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::unordered_set<std::string> g_seen_dir_keys;

std::string CanonicalKey(const fs::path &p) {
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(p, ec);
  if (!ec) {
    return canonical.lexically_normal().string();
  }
  return p.lexically_normal().string();
}

void AddIfDirExists(std::vector<std::string> &out, const fs::path &p) {
  if (fs::exists(p) && fs::is_directory(p)) {
    const std::string s = p.string();
    const std::string key = CanonicalKey(p);
    if (!g_seen_dir_keys.insert(key).second) {
      return;
    }
    out.push_back(s);
  }
}

std::vector<fs::path> Card1Candidates() {
  std::vector<fs::path> roots;
  if (const char *env = std::getenv("ROCREADER_CARD1_ROOT"); env && *env) {
    roots.emplace_back(env);
  }
  roots.emplace_back("/mnt/mmc");
  roots.emplace_back("/media/mmc");
  roots.emplace_back("/media/sdcard");
  roots.emplace_back("/mnt/SDCARD");
  roots.emplace_back("/sdcard");
  return roots;
}

std::vector<fs::path> Card2Candidates() {
  std::vector<fs::path> roots;
  if (const char *env = std::getenv("ROCREADER_CARD2_ROOT"); env && *env) {
    roots.emplace_back(env);
  }
  roots.emplace_back("/mnt/mmc2");
  roots.emplace_back("/media/mmc2");
  roots.emplace_back("/media/sdcard2");
  roots.emplace_back("/mnt/SDCARD2");
  roots.emplace_back("/sdcard2");
  return roots;
}

void AddRocreaderCandidates(std::vector<std::string> &out, const std::vector<fs::path> &roots) {
  for (const auto &root : roots) {
    AddIfDirExists(out, root / "ROCreader");
    AddIfDirExists(out, root / "Roms" / "ROCreader");
    AddIfDirExists(out, root / "APPS" / "ROCreader");
    AddIfDirExists(out, root / "Roms" / "APPS" / "ROCreader");
  }
}

void AddRuntimeCandidates(std::vector<std::string> &out) {
  // Explicit override for unusual firmware mount layouts.
  if (const char *env = std::getenv("ROCREADER_ROOT"); env && *env) {
    AddIfDirExists(out, fs::path(env));
  }

  // Common hardcoded locations observed on handheld firmwares.
  AddIfDirExists(out, fs::path("/Roms/APPS/ROCreader"));
  AddIfDirExists(out, fs::path("/mnt/mmc/Roms/APPS/ROCreader"));
  AddIfDirExists(out, fs::path("/mnt/mmc2/Roms/APPS/ROCreader"));
  AddIfDirExists(out, fs::path("/media/mmc/Roms/APPS/ROCreader"));
  AddIfDirExists(out, fs::path("/media/mmc2/Roms/APPS/ROCreader"));

  // Current working directory can be ROCreader or APPS.
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  if (!ec) {
    AddIfDirExists(out, cwd);
    AddIfDirExists(out, cwd / "ROCreader");
    AddIfDirExists(out, cwd.parent_path() / "ROCreader");
  }
}

std::string DetectPrimaryRocreaderRoot() {
  std::vector<std::string> candidates;

  // Explicit override always wins.
  if (const char *env = std::getenv("ROCREADER_ROOT"); env && *env) {
    AddIfDirExists(candidates, fs::path(env));
  }

  // Card1 only (temporary strategy): do not include card2 aliases.
  AddIfDirExists(candidates, fs::path("/mnt/mmc/Roms/APPS/ROCreader"));
  AddIfDirExists(candidates, fs::path("/media/mmc/Roms/APPS/ROCreader"));
  AddIfDirExists(candidates, fs::path("/mnt/mmc/ROCreader"));
  AddIfDirExists(candidates, fs::path("/media/mmc/ROCreader"));
  AddIfDirExists(candidates, fs::path("/Roms/APPS/ROCreader"));

  // Last resort: runtime-derived paths (still single-root selection).
  if (candidates.empty()) {
    AddRuntimeCandidates(candidates);
  }

  if (candidates.empty()) {
    return {};
  }
  return candidates.front();
}
} // namespace

namespace storage_paths {

std::vector<std::string> DetectRocreaderRoots() {
#ifdef _WIN32
  g_seen_dir_keys.clear();
  std::vector<std::string> out;

  // Allow explicit override first for unusual preview/test layouts.
  if (const char *env = std::getenv("ROCREADER_ROOT"); env && *env) {
    AddIfDirExists(out, fs::path(env));
  }

  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  if (!ec) {
    // Flattened project layout: run from ROCreader root directly.
    AddIfDirExists(out, cwd);
    // Keep a couple of compatibility fallbacks for older preview launch styles.
    AddIfDirExists(out, cwd / "ROCreader");
    AddIfDirExists(out, cwd.parent_path() / "ROCreader");
    AddIfDirExists(out, cwd.parent_path());
  }
  return out;
#else
  g_seen_dir_keys.clear();
  const std::string primary = DetectPrimaryRocreaderRoot();
  if (primary.empty()) {
    return {};
  }
  return {primary};
#endif
}

std::vector<std::string> DetectBooksRoots() {
  std::vector<std::string> out;
  for (const auto &root : DetectRocreaderRoots()) {
    AddIfDirExists(out, fs::path(root) / "books");
  }
#ifdef _WIN32
  // Local dev fallback (Windows only).
  AddIfDirExists(out, fs::path("../books"));
#else
  // Local dev fallback on Linux/macOS when launching from project/build dir.
  AddIfDirExists(out, fs::path("./books"));
  AddIfDirExists(out, fs::path("../books"));
  AddIfDirExists(out, fs::path("../../books"));
#endif
  return out;
}

std::vector<std::string> DetectCoverRoots() {
  std::vector<std::string> out;
  for (const auto &root : DetectRocreaderRoots()) {
    AddIfDirExists(out, fs::path(root) / "book_covers");
  }
#ifdef _WIN32
  // Local dev fallback (Windows only).
  AddIfDirExists(out, fs::path("../book_covers"));
#else
  // Local dev fallback on Linux/macOS when launching from project/build dir.
  AddIfDirExists(out, fs::path("./book_covers"));
  AddIfDirExists(out, fs::path("../book_covers"));
  AddIfDirExists(out, fs::path("../../book_covers"));
#endif
  return out;
}

} // namespace storage_paths
