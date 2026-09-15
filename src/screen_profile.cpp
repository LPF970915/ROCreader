#include "screen_profile.h"

#include "../GKD350HUltra/gkd350h_ultra_profile.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "filesystem_compat.h"
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kDefaultScreenW = 720;
constexpr int kDefaultScreenH = 480;

std::string ToLowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

std::string TrimAscii(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.erase(text.begin());
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  return text;
}

std::string CompactAsciiAlnum(const std::string &text) {
  std::string compact;
  compact.reserve(text.size());
  for (unsigned char ch : text) {
    if (std::isalnum(ch) == 0) continue;
    compact.push_back(static_cast<char>(std::tolower(ch)));
  }
  return compact;
}

bool ParsePositiveInt(const char *text, int &out_value) {
  if (!text || !*text) return false;
  try {
    const int value = std::stoi(text);
    if (value <= 0) return false;
    out_value = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ReadEnvScreenSize(int &out_w, int &out_h) {
  const char *env_w = std::getenv("ROCREADER_SCREEN_W");
  const char *env_h = std::getenv("ROCREADER_SCREEN_H");
  int parsed_w = 0;
  int parsed_h = 0;
  if (!ParsePositiveInt(env_w, parsed_w) || !ParsePositiveInt(env_h, parsed_h)) return false;
  out_w = parsed_w;
  out_h = parsed_h;
  return true;
}

bool ReadEnvScreenProfile(int &out_w, int &out_h) {
  const char *env_profile = std::getenv("ROCREADER_SCREEN_PROFILE");
  if (!env_profile || !*env_profile) return false;
  const std::string profile = ToLowerAscii(env_profile);
  if (gkd350h_ultra::IsProfileName(profile)) {
    out_w = gkd350h_ultra::kScreenW;
    out_h = gkd350h_ultra::kScreenH;
    return true;
  }
  if (profile == "1024x768" || profile == "brick" || profile == "trimui-brick" || profile == "rgds-plus" ||
      profile == "rgdsplus") {
    out_w = 1024;
    out_h = 768;
    return true;
  }
  if (profile == "720x720") {
    out_w = 720;
    out_h = 720;
    return true;
  }
  if (profile == "640x480" || profile == "640") {
    out_w = 640;
    out_h = 480;
    return true;
  }
  if (profile == "720x480" || profile == "720") {
    out_w = 720;
    out_h = 480;
    return true;
  }
  return false;
}

bool ReadConfigScreenProfile(int &out_w, int &out_h) {
  std::vector<std::filesystem::path> candidates;
  if (const char *root = std::getenv("ROCREADER_ROOT"); root && *root) {
    candidates.emplace_back(std::filesystem::path(root) / "native_config.ini");
  }
  candidates.emplace_back(std::filesystem::current_path() / "native_config.ini");
  candidates.emplace_back(std::filesystem::current_path().parent_path() / "native_config.ini");

  for (const auto &path : candidates) {
    std::ifstream in(path);
    if (!in) continue;
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = ToLowerAscii(line.substr(0, eq));
      if (key != "screen_profile") continue;
      const std::string value = ToLowerAscii(line.substr(eq + 1));
      if (gkd350h_ultra::IsProfileName(value)) {
        out_w = gkd350h_ultra::kScreenW;
        out_h = gkd350h_ultra::kScreenH;
        return true;
      }
      if (value == "1024x768" || value == "brick" || value == "trimui-brick" || value == "rgds-plus" ||
          value == "rgdsplus") {
        out_w = 1024;
        out_h = 768;
        return true;
      }
      if (value == "720x720") {
        out_w = 720;
        out_h = 720;
        return true;
      }
      if (value == "640x480" || value == "640") {
        out_w = 640;
        out_h = 480;
        return true;
      }
      if (value == "720x480" || value == "720") {
        out_w = 720;
        out_h = 480;
        return true;
      }
    }
  }
  return false;
}

std::string CanonicalModelTokenFromText(const std::string &text) {
  if (text.empty()) return {};
  const std::string lower = ToLowerAscii(text);
  if (lower.find("rgds+") != std::string::npos) return "rgds-plus";
  const std::string compact = CompactAsciiAlnum(text);
  if (compact.empty()) return {};

  const std::vector<std::pair<std::string, std::string>> aliases = {
      {"gkd350hultra", gkd350h_ultra::kCanonicalModel},
      {"gkd350h", gkd350h_ultra::kCanonicalModel},
      {"gamekiddygkdatom", gkd350h_ultra::kCanonicalModel},
      {"gkdatom", gkd350h_ultra::kCanonicalModel},
      {"trimuibrick", "trimui-brick"},
      {"brick", "trimui-brick"},
      {"tg3040", "trimui-brick"},
      {"rgdsplus", "rgds-plus"},
      {"rgds", "rgds"},
      {"rg34xxsp", "rg34xx-sp"},
      {"34xxsp", "rg34xx-sp"},
      {"rg35xxplus", "rg35xx-plus"},
      {"35xxplus", "rg35xx-plus"},
      {"rg35xxpro", "rg35xx-pro"},
      {"35xxpro", "rg35xx-pro"},
      {"rg35xxsp", "rg35xx-sp"},
      {"35xxsp", "rg35xx-sp"},
      {"rg35xxh", "rg35xx-h"},
      {"35xxh", "rg35xx-h"},
      {"rg40xxh", "rg40xx-h"},
      {"40xxh", "rg40xx-h"},
      {"rg40xxv", "rg40xx-v"},
      {"40xxv", "rg40xx-v"},
      {"rgcubexx", "rgcubexx"},
      {"cubexx", "rgcubexx"},
      {"rg28xx", "rg28xx"},
      {"28xx", "rg28xx"},
      {"rg34xx", "rg34xx"},
      {"34xx", "rg34xx"},
      {"rg35xx2024", "rg35xx"},
      {"35xx2024", "rg35xx"},
      {"rg35xx", "rg35xx"},
      {"35xx", "rg35xx"},
      {"rg40xx", "rg40xx"},
      {"40xx", "rg40xx"},
  };
  for (const auto &[needle, canonical] : aliases) {
    if (compact.find(needle) != std::string::npos) return canonical;
  }
  return {};
}

bool ApplyProfileFromModelToken(const std::string &model_token, int &out_w, int &out_h) {
  if (model_token.empty()) return false;
  // Keep machine-to-profile mapping explicit so model-specific panels don't
  // silently fold into the wrong layout.
  if (gkd350h_ultra::IsCanonicalModel(model_token)) {
    out_w = gkd350h_ultra::kScreenW;
    out_h = gkd350h_ultra::kScreenH;
    return true;
  }
  if (model_token == "trimui-brick") {
    out_w = 1024;
    out_h = 768;
    return true;
  }
  if (model_token == "rgds-plus") {
    out_w = 1024;
    out_h = 768;
    return true;
  }
  if (model_token == "rgds") {
    out_w = 640;
    out_h = 480;
    return true;
  }
  if (model_token == "rgcubexx") {
    out_w = 720;
    out_h = 720;
    return true;
  }
  if (model_token == "rg34xx" || model_token == "rg34xx-sp") {
    out_w = 720;
    out_h = 480;
    return true;
  }
  if (model_token == "rg28xx" ||
      model_token == "rg35xx" ||
      model_token == "rg35xx-plus" ||
      model_token == "rg35xx-h" ||
      model_token == "rg35xx-sp" ||
      model_token == "rg35xx-pro" ||
      model_token == "rg40xx" ||
      model_token == "rg40xx-v" ||
      model_token == "rg40xx-h") {
    out_w = 640;
    out_h = 480;
    return true;
  }
  return false;
}

std::string ExtractModelToken(const std::string &text) {
  if (text.empty()) return {};

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = TrimAscii(ToLowerAscii(line.substr(0, eq)));
    if (key != "model" && key != "board" && key != "device_model" && key != "machine") continue;
    const std::string token = CanonicalModelTokenFromText(line.substr(eq + 1));
    if (!token.empty()) return token;
  }
  return CanonicalModelTokenFromText(text);
}

bool ReadBoardIniModelToken(std::string &out_token) {
  const std::vector<std::filesystem::path> candidates = {
      "/oem/board.ini",
      "/mnt/vendor/oem/board.ini",
  };
  for (const auto &path : candidates) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string token = ExtractModelToken(text);
    if (token.empty()) continue;
    out_token = token;
    return true;
  }
  return false;
}

bool ReadDeviceModelScreenProfile(int &out_w, int &out_h) {
  const std::string token = DetectDeviceModelToken();
  if (!token.empty() && ApplyProfileFromModelToken(token, out_w, out_h)) return true;
  return false;
}

bool ParseFramebufferSize(const std::string &text, int &out_w, int &out_h) {
  const size_t comma = text.find(',');
  if (comma == std::string::npos) return false;
  const std::string w_text = text.substr(0, comma);
  const std::string h_text = text.substr(comma + 1);
  try {
    const int parsed_w = std::stoi(w_text);
    const int parsed_h = std::stoi(h_text);
    if (parsed_w <= 0 || parsed_h <= 0) return false;
    out_w = parsed_w;
    out_h = parsed_h;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseScreenSizeToken(const std::string &text, int &out_w, int &out_h) {
  for (size_t i = 0; i < text.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
    size_t width_end = i;
    while (width_end < text.size() && std::isdigit(static_cast<unsigned char>(text[width_end]))) {
      ++width_end;
    }
    if (width_end >= text.size() || (text[width_end] != 'x' && text[width_end] != 'X')) {
      i = width_end;
      continue;
    }
    const size_t height_start = width_end + 1;
    size_t height_end = height_start;
    while (height_end < text.size() && std::isdigit(static_cast<unsigned char>(text[height_end]))) {
      ++height_end;
    }
    if (height_end == height_start) {
      i = width_end;
      continue;
    }
    try {
      const int parsed_w = std::stoi(text.substr(i, width_end - i));
      const int parsed_h = std::stoi(text.substr(height_start, height_end - height_start));
      if (parsed_w > 0 && parsed_h > 0) {
        out_w = parsed_w;
        out_h = parsed_h;
        return true;
      }
    } catch (...) {
    }
    i = height_end;
  }
  return false;
}

bool ReadFramebufferModeSize(int &out_w, int &out_h) {
  const std::vector<std::string> candidates = {
      "/sys/class/graphics/fb0/modes",
      "/sys/class/graphics/fb0/mode",
      "/sys/class/graphics/fb1/modes",
      "/sys/class/graphics/fb1/mode",
  };
  for (const std::string &path : candidates) {
    std::ifstream in(path);
    if (!in) continue;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      if (ParseScreenSizeToken(line, out_w, out_h)) return true;
    }
  }
  return false;
}

bool ReadFramebufferSize(int &out_w, int &out_h) {
  const std::vector<std::string> candidates = {
      "/sys/class/graphics/fb0/virtual_size",
      "/sys/class/graphics/fb1/virtual_size",
  };
  for (const std::string &path : candidates) {
    std::ifstream in(path);
    if (!in) continue;
    std::string line;
    if (!std::getline(in, line) || line.empty()) continue;
    if (ParseFramebufferSize(line, out_w, out_h)) return true;
  }
  return false;
}

bool ReadSdlDisplaySize(int &out_w, int &out_h) {
  SDL_DisplayMode mode{};
  if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
    out_w = mode.w;
    out_h = mode.h;
    return true;
  }
  if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
    out_w = mode.w;
    out_h = mode.h;
    return true;
  }
  return false;
}

bool TryApplyProfileFromDetectedSize(ScreenProfile &profile) {
  if ((profile.detected_w == gkd350h_ultra::kScreenW && profile.detected_h == gkd350h_ultra::kScreenH) ||
      (profile.detected_w == gkd350h_ultra::kScreenH && profile.detected_h == gkd350h_ultra::kScreenW)) {
    profile.screen_w = gkd350h_ultra::kScreenW;
    profile.screen_h = gkd350h_ultra::kScreenH;
    profile.profile_name = gkd350h_ultra::kProfileName;
    return true;
  }
  if (profile.detected_w == 1024 && profile.detected_h == 768) {
    profile.screen_w = 1024;
    profile.screen_h = 768;
    profile.profile_name = "1024x768";
    return true;
  }
  if (profile.detected_w == 720 && profile.detected_h == 720) {
    profile.screen_w = 720;
    profile.screen_h = 720;
    profile.profile_name = "720x720";
    return true;
  }
  if (profile.detected_w == 640 || profile.detected_h == 640) {
    profile.screen_w = 640;
    profile.screen_h = 480;
    profile.profile_name = "640x480";
    return true;
  }
  if (profile.detected_w == 720 || profile.detected_h == 720) {
    profile.screen_w = 720;
    profile.screen_h = 480;
    profile.profile_name = "720x480";
    return true;
  }
  return false;
}

void ApplyDefaultProfile(ScreenProfile &profile) {
  profile.screen_w = kDefaultScreenW;
  profile.screen_h = kDefaultScreenH;
  profile.profile_name = "720x480";
}

bool TryCommitDetectedProfile(ScreenProfile &profile, int detected_w, int detected_h, const char *source) {
  profile.detected_w = detected_w;
  profile.detected_h = detected_h;
  profile.detection_source = source ? source : "unknown";
  return TryApplyProfileFromDetectedSize(profile);
}

}  // namespace

std::string DetectDeviceModelToken() {
  if (const char *env_model = std::getenv("ROCREADER_DEVICE_MODEL"); env_model && *env_model) {
    const std::string token = ExtractModelToken(env_model);
    if (!token.empty()) return token;
    const std::string lower = ToLowerAscii(env_model);
    if (!lower.empty()) return lower;
  }

  std::string board_ini_token;
  if (ReadBoardIniModelToken(board_ini_token)) return board_ini_token;
  return {};
}

ScreenProfile DetectScreenProfile() {
  ScreenProfile profile;

  int detected_w = 0;
  int detected_h = 0;
  if (ReadEnvScreenProfile(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "env-profile")) return profile;
  }

  if (ReadEnvScreenSize(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "env")) return profile;
  }

  if (ReadConfigScreenProfile(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "config")) return profile;
  }

  std::string board_ini_token;
  if (ReadBoardIniModelToken(board_ini_token)) {
    if (ApplyProfileFromModelToken(board_ini_token, detected_w, detected_h)) {
      if (TryCommitDetectedProfile(profile, detected_w, detected_h, "board-ini")) return profile;
    }
  }

  if (ReadDeviceModelScreenProfile(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "device-model")) return profile;
  }

  if (ReadSdlDisplaySize(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "sdl")) return profile;
  }

  if (ReadFramebufferModeSize(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "fb-mode")) return profile;
  }

  if (ReadFramebufferSize(detected_w, detected_h)) {
    if (TryCommitDetectedProfile(profile, detected_w, detected_h, "fb-size")) return profile;
  }
  return TryCommitDetectedProfile(profile, 720, 720, "board-ini-fallback")
             ? profile
             : ScreenProfile{};
}

bool Uses34xxSpKeymap(const std::string &model_token) {
  // Only the SP hardware should use the dedicated 34xxSP key layout.
  return model_token == "rg34xx-sp";
}

bool Uses35xxHKeymap(const std::string &model_token) {
  return model_token == "rg35xx-h";
}

bool UsesGKD350HUltraKeymap(const std::string &model_token, const ScreenProfile &screen_profile) {
  return gkd350h_ultra::IsCanonicalModel(model_token) ||
         gkd350h_ultra::IsProfileName(screen_profile.profile_name);
}
