#pragma once

#include <string>
#include <vector>

namespace storage_paths {

std::vector<std::string> DetectRocreaderRoots();
std::vector<std::string> DetectBooksRoots();
std::vector<std::string> DetectCoverRoots();

} // namespace storage_paths

