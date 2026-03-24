#pragma once

#include <string>
#include <vector>

namespace cover_resolver {

// Resolve exact/manual cover image for a shelf item.
// For files: exact basename lookup under cover roots.
// For folders: internal cover files first, then exact folder-name lookup in cover roots.
std::string ResolveCoverPathExact(const std::string &item_path, bool is_dir, const std::vector<std::string> &cover_roots);

// Resolve fuzzy/manual cover image for a shelf item.
// Uses trimmed-prefix candidates (e.g., without volume/chapter suffixes).
std::string ResolveCoverPathFuzzy(const std::string &item_path, bool is_dir, const std::vector<std::string> &cover_roots);

// Backward-compatible helper: exact first, then fuzzy.
std::string ResolveCoverPath(const std::string &item_path, bool is_dir, const std::vector<std::string> &cover_roots);

} // namespace cover_resolver
