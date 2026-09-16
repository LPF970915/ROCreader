#include "version_update_runtime.h"
#include "app_language.h"
#include "gkd_menu_button_metrics.h"
#include "online_source_transport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {
constexpr int kButtonWidth = 168;
constexpr int kButtonHeight = 34;
constexpr int kProgressBarWidth = 228;
constexpr int kProgressBarHeight = 16;
constexpr const char *kGithubContentsApi =
    "https://api.github.com/repos/LPF970915/ROCreader/contents/Downloads?ref=main";
constexpr const char *kGkdGithubContentsApi =
    "https://api.github.com/repos/LPF970915/ROCreader/contents/GKD350HUltra/Downloads?ref=main";
constexpr const char *kRgdsPlusGithubContentsApi =
    "https://api.github.com/repos/LPF970915/ROCreader/contents/RGDSPlus/Downloads?ref=main";
constexpr const char *kUpdateContentsUrlEnv = "ROCREADER_UPDATE_CONTENTS_URL";
constexpr const char *kPendingMarkerFilename = "ROCreader_update_pending.txt";
constexpr const char *kUserAgent = "ROCreader-Updater";
constexpr const char *kDownloadTempFilename = "ROCreader_update_download.tmp";
constexpr const char *kInstalledVersionFilename = "version.txt";
constexpr int kDownloadConnectTimeoutSec = 15;
constexpr int kDownloadMaxTimeSec = 300;
constexpr int kDownloadLowSpeedBytesPerSec = 1024;
constexpr int kDownloadLowSpeedWindowSec = 20;

int ScalePx(float scale, int value) {
  return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * std::max(0.1f, scale))));
}

std::filesystem::path UpdateLogPath() {
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec) return {};
  const std::filesystem::path cache_dir = cwd / "cache";
  std::filesystem::create_directories(cache_dir, ec);
  if (ec) return cwd / "version_update.log";
  return cache_dir / "version_update.log";
}

void AppendUpdateLog(const std::string &message) {
  const std::filesystem::path log_path = UpdateLogPath();
  if (log_path.empty()) return;
  std::ofstream out(log_path, std::ios::app);
  if (!out) return;
  using clock = std::chrono::system_clock;
  const auto now = clock::to_time_t(clock::now());
  out << now << " " << message << "\n";
}

void DrawCenteredText(SDL_Renderer *renderer, const SDL_Rect &bounds,
                      const std::function<TextCacheEntry *(const std::string &, SDL_Color)> &get_text_texture,
                      const std::string &text, SDL_Color color, int center_y) {
  if (!renderer || !get_text_texture || text.empty()) return;
  TextCacheEntry *entry = get_text_texture(text, color);
  if (!entry || !entry->texture) return;
  SDL_Rect dst{
      bounds.x + std::max(0, (bounds.w - entry->w) / 2),
      center_y - entry->h / 2,
      entry->w,
      entry->h,
  };
  SDL_RenderCopy(renderer, entry->texture, nullptr, &dst);
}

std::string ToLowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool IsRgdsPlusUpdateProfile(InputProfile input_profile) {
  const char *model = std::getenv("ROCREADER_DEVICE_MODEL");
  return input_profile == InputProfile::RGDS && model && ToLowerAscii(model) == "rgds-plus";
}

std::string ResolveGithubContentsUrl(InputProfile input_profile) {
  const char *env_url = std::getenv(kUpdateContentsUrlEnv);
  if (!env_url || !*env_url) {
    if (IsRgdsPlusUpdateProfile(input_profile)) return kRgdsPlusGithubContentsApi;
    return input_profile == InputProfile::GKD350HUltra ? kGkdGithubContentsApi : kGithubContentsApi;
  }

  const std::string url = env_url;
  const std::string tree_prefix = "https://github.com/";
  const std::string tree_marker = "/tree/";
  if (url.rfind(tree_prefix, 0) == 0) {
    const std::string rest = url.substr(tree_prefix.size());
    const size_t owner_end = rest.find('/');
    if (owner_end != std::string::npos) {
      const size_t repo_end = rest.find('/', owner_end + 1);
      if (repo_end != std::string::npos) {
        const size_t tree_pos = rest.find(tree_marker, repo_end);
        if (tree_pos != std::string::npos) {
          const size_t branch_start = tree_pos + tree_marker.size();
          const size_t branch_end = rest.find('/', branch_start);
          if (branch_end != std::string::npos) {
            const std::string owner = rest.substr(0, owner_end);
            const std::string repo = rest.substr(owner_end + 1, repo_end - owner_end - 1);
            const std::string branch = rest.substr(branch_start, branch_end - branch_start);
            const std::string path = rest.substr(branch_end + 1);
            return "https://api.github.com/repos/" + owner + "/" + repo + "/contents/" + path + "?ref=" + branch;
          }
        }
      }
    }
  }

  return url;
}

bool TryExtractVersionToken(const std::string &filename, std::string &out_version) {
  const std::string lower_name = ToLowerAscii(filename);
  const size_t ver_pos = lower_name.rfind("ver");
  const size_t zip_pos = lower_name.rfind(".zip");
  if (ver_pos == std::string::npos || zip_pos == std::string::npos || ver_pos >= zip_pos) return false;
  size_t version_end = ver_pos + 3;
  while (version_end < zip_pos) {
    const unsigned char ch = static_cast<unsigned char>(filename[version_end]);
    if (!std::isdigit(ch) && filename[version_end] != '.') break;
    ++version_end;
  }
  out_version = filename.substr(ver_pos, version_end - ver_pos);
  return !out_version.empty();
}

std::vector<int> ParseVersionParts(const std::string &version_text) {
  std::vector<int> parts;
  int value = -1;
  for (char ch : version_text) {
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      if (value < 0) value = 0;
      value = value * 10 + (ch - '0');
    } else if (value >= 0) {
      parts.push_back(value);
      value = -1;
    }
  }
  if (value >= 0) parts.push_back(value);
  return parts;
}

bool IsVersionNewer(const std::string &candidate, const std::string &baseline) {
  const std::vector<int> lhs = ParseVersionParts(candidate);
  const std::vector<int> rhs = ParseVersionParts(baseline);
  const size_t count = std::max(lhs.size(), rhs.size());
  for (size_t i = 0; i < count; ++i) {
    const int lv = i < lhs.size() ? lhs[i] : 0;
    const int rv = i < rhs.size() ? rhs[i] : 0;
    if (lv != rv) return lv > rv;
  }
  return false;
}

std::string TrimAsciiWhitespace(std::string text) {
  auto is_space = [](unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
  };
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

std::string ReadInstalledVersionFromFile(const std::filesystem::path &version_path) {
  if (version_path.empty()) return {};
  std::ifstream in(version_path);
  if (!in) return {};
  std::string line;
  std::getline(in, line);
  return TrimAsciiWhitespace(line);
}

std::string DetectInstalledVersionLabel() {
  std::vector<std::filesystem::path> candidates;
  for (const std::string &root : storage_paths::DetectRocreaderRoots()) {
    if (!root.empty()) candidates.push_back(std::filesystem::path(root) / kInstalledVersionFilename);
  }
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    candidates.push_back(cwd / kInstalledVersionFilename);
    candidates.push_back(cwd.parent_path() / kInstalledVersionFilename);
  }
  for (const auto &candidate : candidates) {
    const std::string version = ReadInstalledVersionFromFile(candidate);
    if (!version.empty()) return version;
  }
  return {};
}

std::string DetectCurrentVersionLabel(const std::filesystem::path &runtime_root) {
  std::vector<std::filesystem::path> candidates;
  for (const std::string &root : storage_paths::DetectRocreaderRoots()) {
    if (!root.empty()) candidates.push_back(std::filesystem::path(root) / kInstalledVersionFilename);
  }
  if (!runtime_root.empty()) {
    candidates.push_back(runtime_root / kInstalledVersionFilename);
    candidates.push_back(runtime_root.parent_path() / kInstalledVersionFilename);
  }
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    candidates.push_back(cwd / kInstalledVersionFilename);
    candidates.push_back(cwd.parent_path() / kInstalledVersionFilename);
  }

  for (const auto &candidate : candidates) {
    const std::string version = ReadInstalledVersionFromFile(candidate);
    if (!version.empty()) return version;
  }
  return "v0.0.0-ui";
}

std::string FormatDownloadSpeed(double bytes_per_sec) {
  if (bytes_per_sec <= 0.0) return std::string(u8"下载速度 0 KB/s");
  constexpr double kKiB = 1024.0;
  constexpr double kMiB = 1024.0 * 1024.0;
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  if (bytes_per_sec >= kMiB) {
    oss.precision(2);
    oss << u8"下载速度 " << (bytes_per_sec / kMiB) << " MB/s";
  } else {
    oss.precision(1);
    oss << u8"下载速度 " << (bytes_per_sec / kKiB) << " KB/s";
  }
  return oss.str();
}

bool IsUrlUnreserved(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
         ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string UrlEncodePathPreservingSlashes(const std::string &path) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(path.size());
  for (unsigned char ch : path) {
    if (ch == '/') {
      encoded.push_back('/');
    } else if (IsUrlUnreserved(ch)) {
      encoded.push_back(static_cast<char>(ch));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[(ch >> 4) & 0x0F]);
      encoded.push_back(kHex[ch & 0x0F]);
    }
  }
  return encoded;
}

void PushUniqueUrl(std::vector<std::string> &urls, const std::string &url) {
  if (url.empty()) return;
  if (std::find(urls.begin(), urls.end(), url) == urls.end()) urls.push_back(url);
}

std::vector<std::string> BuildDownloadUrlCandidates(const std::string &url) {
  std::vector<std::string> urls;

  const std::string raw_prefix = "https://raw.githubusercontent.com/";
  if (url.rfind(raw_prefix, 0) == 0) {
    const std::string rest = url.substr(raw_prefix.size());
    const size_t first_slash = rest.find('/');
    const size_t second_slash = first_slash == std::string::npos ? std::string::npos : rest.find('/', first_slash + 1);
    const size_t third_slash = second_slash == std::string::npos ? std::string::npos : rest.find('/', second_slash + 1);
    if (third_slash != std::string::npos) {
      const std::string owner = rest.substr(0, first_slash);
      const std::string repo = rest.substr(first_slash + 1, second_slash - first_slash - 1);
      const std::string branch = rest.substr(second_slash + 1, third_slash - second_slash - 1);
      const std::string path = rest.substr(third_slash + 1);
      const std::string encoded_path = UrlEncodePathPreservingSlashes(path);
      PushUniqueUrl(urls, "https://github.com/" + owner + "/" + repo + "/raw/refs/heads/" + branch + "/" + encoded_path);
      PushUniqueUrl(urls, raw_prefix + owner + "/" + repo + "/" + branch + "/" + encoded_path);
      PushUniqueUrl(urls, "https://github.com/" + owner + "/" + repo + "/raw/refs/heads/" + branch + "/" + path);
    }
  }
  PushUniqueUrl(urls, url);
  return urls;
}

std::string EscapeForPosix(const std::string &value) {
  std::string escaped = "'";
  for (char ch : value) {
    if (ch == '\'') escaped += "'\\''";
    else escaped.push_back(ch);
  }
  escaped += "'";
  return escaped;
}

std::string EscapeForPowerShell(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '\'') escaped += "''";
    else escaped.push_back(ch);
  }
  return escaped;
}

std::string RunCommandCapture(const std::string &command) {
#if defined(_WIN32)
  FILE *pipe = _popen(command.c_str(), "r");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (!pipe) return {};

  std::string output;
  std::array<char, 512> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

#if defined(_WIN32)
  _pclose(pipe);
#else
  pclose(pipe);
#endif
  return output;
}

int RunCommand(const std::string &command) {
  AppendUpdateLog("RunCommand: " + command);
  return std::system(command.c_str());
}

std::string HttpGetText(const std::string &url) {
#if defined(_WIN32)
  const std::string escaped_url = EscapeForPowerShell(url);
  const std::string command =
      "powershell -NoProfile -Command \"[Net.ServicePointManager]::SecurityProtocol = "
      "[Net.SecurityProtocolType]::Tls12; "
      "$ProgressPreference='SilentlyContinue'; "
      "(Invoke-WebRequest -UseBasicParsing -Headers @{ 'User-Agent'='" + std::string(kUserAgent) + "' } "
      "-Uri '" + escaped_url + "').Content\"";
  return RunCommandCapture(command);
#else
  if (std::string output = ::HttpGetText(url); !output.empty()) {
    AppendUpdateLog("HttpGetText online transport success url=" + url + " bytes=" + std::to_string(output.size()));
    return output;
  }
  AppendUpdateLog("HttpGetText online transport returned empty; falling back to command transports url=" + url);
  const std::string curl_command =
      "curl -LfsS -H " + EscapeForPosix(std::string("User-Agent: ") + kUserAgent) + " "
      + EscapeForPosix(url) + " 2>/dev/null";
  std::string output = RunCommandCapture(curl_command);
  if (!output.empty()) return output;
  const std::string wget_command =
      "wget -qO- --header=" + EscapeForPosix(std::string("User-Agent: ") + kUserAgent) + " "
      + EscapeForPosix(url) + " 2>/dev/null";
  return RunCommandCapture(wget_command);
#endif
}

bool DownloadFile(const std::string &url, const std::filesystem::path &output_path) {
#if defined(_WIN32)
  const std::string escaped_url = EscapeForPowerShell(url);
  const std::string escaped_path = EscapeForPowerShell(output_path.string());
  const std::string command =
      "powershell -NoProfile -Command \"[Net.ServicePointManager]::SecurityProtocol = "
      "[Net.SecurityProtocolType]::Tls12; "
      "$ProgressPreference='SilentlyContinue'; "
      "Invoke-WebRequest -UseBasicParsing -Headers @{ 'User-Agent'='" + std::string(kUserAgent) + "' } "
      "-Uri '" + escaped_url + "' -OutFile '" + escaped_path + "'\"";
  const bool ok = RunCommand(command) == 0;
  AppendUpdateLog(std::string("PowerShell download result: ") + (ok ? "success" : "failed"));
  return ok;
#else
  if (::DownloadFile(url, output_path)) {
    AppendUpdateLog("DownloadFile online transport success url=" + url);
    return true;
  }
  AppendUpdateLog("DownloadFile online transport failed; falling back to command transports url=" + url);
  const std::string quoted_output = EscapeForPosix(output_path.string());
  const std::string header = EscapeForPosix(std::string("User-Agent: ") + kUserAgent);
  const std::string raw_header = EscapeForPosix("Accept: application/vnd.github.raw");
  const bool is_github_contents_api = url.find("://api.github.com/repos/") != std::string::npos &&
                                      url.find("/contents/") != std::string::npos;
  for (const std::string &candidate_url : BuildDownloadUrlCandidates(url)) {
    const std::string quoted_url = EscapeForPosix(candidate_url);
    AppendUpdateLog("Trying download URL: " + candidate_url);
    std::vector<std::string> commands = {
        "curl -L --fail --silent --show-error --http1.1 --connect-timeout "
            + std::to_string(kDownloadConnectTimeoutSec) + " --max-time "
            + std::to_string(kDownloadMaxTimeSec) + " --speed-limit "
            + std::to_string(kDownloadLowSpeedBytesPerSec) + " --speed-time "
            + std::to_string(kDownloadLowSpeedWindowSec) + " -H " + header + " "
            + quoted_url + " -o " + quoted_output,
        "curl -k -L --fail --silent --show-error --http1.1 --connect-timeout "
            + std::to_string(kDownloadConnectTimeoutSec) + " --max-time "
            + std::to_string(kDownloadMaxTimeSec) + " --speed-limit "
            + std::to_string(kDownloadLowSpeedBytesPerSec) + " --speed-time "
            + std::to_string(kDownloadLowSpeedWindowSec) + " -H " + header + " "
            + quoted_url + " -o " + quoted_output,
        "wget -q --timeout=" + std::to_string(kDownloadConnectTimeoutSec)
            + " --tries=1 --user-agent=" + EscapeForPosix(kUserAgent)
            + " -O " + quoted_output + " " + quoted_url,
        "wget -q --no-check-certificate --timeout=" + std::to_string(kDownloadConnectTimeoutSec)
            + " --tries=1 --user-agent=" + EscapeForPosix(kUserAgent)
            + " -O " + quoted_output + " " + quoted_url,
        "busybox wget -q -T " + std::to_string(kDownloadConnectTimeoutSec)
            + " -O " + quoted_output + " " + quoted_url,
    };
    if (is_github_contents_api) {
      commands.insert(commands.begin(), {
          "curl -L --fail --silent --show-error --http1.1 --connect-timeout "
              + std::to_string(kDownloadConnectTimeoutSec) + " --max-time "
              + std::to_string(kDownloadMaxTimeSec) + " -H " + header + " -H " + raw_header + " "
              + quoted_url + " -o " + quoted_output,
          "curl -k -L --fail --silent --show-error --http1.1 --connect-timeout "
              + std::to_string(kDownloadConnectTimeoutSec) + " --max-time "
              + std::to_string(kDownloadMaxTimeSec) + " -H " + header + " -H " + raw_header + " "
              + quoted_url + " -o " + quoted_output,
          "wget -q --timeout=" + std::to_string(kDownloadConnectTimeoutSec)
              + " --tries=1 --user-agent=" + EscapeForPosix(kUserAgent)
              + " --header=" + raw_header + " -O " + quoted_output + " " + quoted_url,
          "wget -q --no-check-certificate --timeout=" + std::to_string(kDownloadConnectTimeoutSec)
              + " --tries=1 --user-agent=" + EscapeForPosix(kUserAgent)
              + " --header=" + raw_header + " -O " + quoted_output + " " + quoted_url,
      });
    }

    for (const std::string &command : commands) {
      std::error_code remove_ec;
      std::filesystem::remove(output_path, remove_ec);
      const int rc = RunCommand(command);
      AppendUpdateLog("Download command exit code: " + std::to_string(rc));
      if (rc == 0) return true;
    }
  }
  return false;
#endif
}

bool LooksLikeZipFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[4] = {};
  in.read(magic, sizeof(magic));
  return in.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
         magic[0] == 'P' && magic[1] == 'K' &&
         ((magic[2] == '\003' && magic[3] == '\004') ||
          (magic[2] == '\005' && magic[3] == '\006') ||
          (magic[2] == '\007' && magic[3] == '\010'));
}

bool ExtractJsonStringField(const std::string &object_text, const std::string &field_name, std::string &out_value) {
  const std::string key = "\"" + field_name + "\"";
  const size_t key_pos = object_text.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon_pos = object_text.find(':', key_pos + key.size());
  if (colon_pos == std::string::npos) return false;
  const size_t first_quote = object_text.find('"', colon_pos + 1);
  if (first_quote == std::string::npos) return false;
  size_t cursor = first_quote + 1;
  std::string value;
  while (cursor < object_text.size()) {
    const char ch = object_text[cursor++];
    if (ch == '\\' && cursor < object_text.size()) {
      value.push_back(object_text[cursor++]);
      continue;
    }
    if (ch == '"') {
      out_value = value;
      return true;
    }
    value.push_back(ch);
  }
  return false;
}

bool ExtractJsonNumberField(const std::string &object_text, const std::string &field_name, uint64_t &out_value) {
  const std::string key = "\"" + field_name + "\"";
  const size_t key_pos = object_text.find(key);
  if (key_pos == std::string::npos) return false;
  const size_t colon_pos = object_text.find(':', key_pos + key.size());
  if (colon_pos == std::string::npos) return false;
  size_t cursor = colon_pos + 1;
  while (cursor < object_text.size() && std::isspace(static_cast<unsigned char>(object_text[cursor])) != 0) ++cursor;
  size_t end = cursor;
  while (end < object_text.size() && std::isdigit(static_cast<unsigned char>(object_text[end])) != 0) ++end;
  if (end == cursor) return false;
  try {
    out_value = static_cast<uint64_t>(std::stoull(object_text.substr(cursor, end - cursor)));
    return true;
  } catch (...) {
    return false;
  }
}

struct RemoteArchiveInfo {
  std::string filename;
  std::string version;
  std::string download_url;
  std::string api_url;
  uint64_t size_bytes = 0;
};

bool IsPackageForProfile(const std::string &filename, InputProfile input_profile) {
  const std::string lower = ToLowerAscii(filename);
  const std::string plus_suffix = " for rgds plus.zip";
  const bool is_plus = lower.size() >= plus_suffix.size() &&
                      lower.compare(lower.size() - plus_suffix.size(), plus_suffix.size(), plus_suffix) == 0;
  if (IsRgdsPlusUpdateProfile(input_profile)) return is_plus;
  if (is_plus) return false;
  if (input_profile == InputProfile::GKD350HUltra) {
    return lower.find("gkd350h ultra") != std::string::npos ||
           lower.find("gkd350hultra") != std::string::npos;
  }
  return lower.find("gkd350h ultra") == std::string::npos &&
         lower.find("gkd350hultra") == std::string::npos;
}

bool SelectLatestRemoteArchive(const std::string &json, RemoteArchiveInfo &out_info, InputProfile input_profile) {
  size_t search_pos = 0;
  bool found = false;
  RemoteArchiveInfo best{};
  while (true) {
    const size_t name_key_pos = json.find("\"name\"", search_pos);
    if (name_key_pos == std::string::npos) break;
    const size_t object_start = json.rfind('{', name_key_pos);
    const size_t object_end = json.find('}', name_key_pos);
    if (object_start == std::string::npos || object_end == std::string::npos || object_end <= object_start) {
      search_pos = name_key_pos + 6;
      continue;
    }
    const std::string object_text = json.substr(object_start, object_end - object_start + 1);
    search_pos = object_end + 1;

    RemoteArchiveInfo candidate{};
    if (!ExtractJsonStringField(object_text, "name", candidate.filename)) continue;
    std::string lower_name = ToLowerAscii(candidate.filename);
    if (lower_name.size() < 4 || lower_name.substr(lower_name.size() - 4) != ".zip") continue;
    if (!IsPackageForProfile(candidate.filename, input_profile)) continue;
    if (!TryExtractVersionToken(candidate.filename, candidate.version)) continue;
    if (!ExtractJsonStringField(object_text, "download_url", candidate.download_url)) continue;
    ExtractJsonStringField(object_text, "url", candidate.api_url);
    if (!ExtractJsonNumberField(object_text, "size", candidate.size_bytes)) continue;

    if (!found || IsVersionNewer(candidate.version, best.version)) {
      best = std::move(candidate);
      found = true;
    }
  }

  if (!found) return false;
  AppendUpdateLog("Latest remote archive: " + best.filename + " version=" + best.version
                  + " bytes=" + std::to_string(best.size_bytes));
  out_info = std::move(best);
  return true;
}

bool FetchLatestRemoteArchive(RemoteArchiveInfo &out_info, InputProfile input_profile) {
  const std::string contents_url = ResolveGithubContentsUrl(input_profile);
  const std::string json = HttpGetText(contents_url);
  AppendUpdateLog("Fetched GitHub contents metadata url=" + contents_url + " bytes=" + std::to_string(json.size()));
  return SelectLatestRemoteArchive(json, out_info, input_profile);
}

std::filesystem::path DetectDownloadRoot() {
  const std::vector<std::string> card_roots = storage_paths::DetectStorageCardRoots();
  for (const std::string &root : card_roots) {
    if (root.empty()) continue;
    return std::filesystem::path(root);
  }
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) return cwd;
  return {};
}

std::filesystem::path PendingDownloadsDir(const std::filesystem::path &download_root) {
  return download_root / "Downloads";
}

std::filesystem::path PendingMarkerPath(const std::filesystem::path &download_root) {
  return PendingDownloadsDir(download_root) / kPendingMarkerFilename;
}

void RemovePendingMarkerFile(const std::filesystem::path &marker_path) {
  if (marker_path.empty()) return;
  std::error_code ec;
  std::filesystem::remove(marker_path, ec);
}

void RemovePendingMarkerAndInstalledPackage(const std::filesystem::path &marker_path,
                                            const std::filesystem::path &package_path) {
  RemovePendingMarkerFile(marker_path);
  if (package_path.empty()) return;
  std::error_code ec;
  std::filesystem::remove(package_path, ec);
}

bool IsTrimuiBrickPackageName(const std::string &filename) {
  const std::string lower = ToLowerAscii(filename);
  return lower.find("trimui brick") != std::string::npos || lower.find("trimuibrick") != std::string::npos;
}

void ClearInstalledPendingArtifacts(const std::filesystem::path &downloads_dir,
                                    const std::string &installed_version, InputProfile input_profile) {
  if (downloads_dir.empty() || installed_version.empty()) return;
  std::error_code ec;
  if (!std::filesystem::exists(downloads_dir, ec) || ec || !std::filesystem::is_directory(downloads_dir, ec)) {
    return;
  }

  const std::filesystem::path marker_path = downloads_dir / kPendingMarkerFilename;
  std::filesystem::path marker_package_path;
  std::string marker_version;
  {
    std::ifstream in(marker_path);
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = line.substr(0, eq);
      const std::string value = line.substr(eq + 1);
      if (key == "filename") marker_package_path = downloads_dir / value;
      else if (key == "version") marker_version = TrimAsciiWhitespace(value);
    }
  }
  if (!marker_package_path.empty() && marker_version.empty()) {
    TryExtractVersionToken(marker_package_path.filename().string(), marker_version);
  }
  if (!marker_package_path.empty() &&
      (!IsRgdsPlusUpdateProfile(input_profile) ||
       IsPackageForProfile(marker_package_path.filename().string(), input_profile)) &&
      (marker_version.empty() || !IsVersionNewer(marker_version, installed_version))) {
    AppendUpdateLog("Clearing stale pending marker for installed version " + installed_version);
    RemovePendingMarkerAndInstalledPackage(marker_path, marker_package_path);
  }

  for (std::filesystem::directory_iterator it(downloads_dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (!filesystem_compat::IsRegularFile(*it, ec)) {
      ec.clear();
      continue;
    }
    const std::string filename = it->path().filename().string();
    if (IsRgdsPlusUpdateProfile(input_profile)) {
      if (!IsPackageForProfile(filename, input_profile)) continue;
    } else if (!IsTrimuiBrickPackageName(filename) &&
               !IsPackageForProfile(filename, InputProfile::GKD350HUltra)) {
      continue;
    }
    std::string version;
    if (!TryExtractVersionToken(filename, version)) continue;
    if (!IsVersionNewer(version, installed_version)) {
      std::error_code remove_ec;
      std::filesystem::remove(it->path(), remove_ec);
    }
  }
}

void ClearInstalledPendingArtifactsForAllRoots(const std::string &installed_version, InputProfile input_profile) {
  if (installed_version.empty()) return;
  std::vector<std::filesystem::path> roots;
  for (const std::string &root : storage_paths::DetectStorageCardRoots()) {
    if (!root.empty()) roots.emplace_back(root);
  }
  for (const std::string &root : storage_paths::DetectRocreaderRoots()) {
    if (!root.empty()) roots.emplace_back(std::filesystem::path(root));
  }
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    roots.push_back(cwd);
    roots.push_back(cwd.parent_path());
  }
  for (const auto &root : roots) {
    ClearInstalledPendingArtifacts(root / "Downloads", installed_version, input_profile);
  }
}

bool ReadPendingMarker(const std::filesystem::path &marker_path, VersionUpdateState &state) {
  std::ifstream in(marker_path);
  if (!in) return false;
  std::string line;
  std::filesystem::path package_path;
  std::string version;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "filename") package_path = marker_path.parent_path() / value;
    else if (key == "version") version = value;
  }
  version = TrimAsciiWhitespace(version);
  std::error_code ec;
  if (package_path.empty() || !std::filesystem::exists(package_path, ec) || ec) {
    RemovePendingMarkerFile(marker_path);
    return false;
  }
  if (IsRgdsPlusUpdateProfile(state.input_profile) &&
      !IsPackageForProfile(package_path.filename().string(), state.input_profile)) {
    AppendUpdateLog("Ignoring pending package for another device: " + package_path.string());
    return false;
  }
  if (version.empty()) {
    TryExtractVersionToken(package_path.filename().string(), version);
  }
  if (!version.empty() && !state.current_version.empty() && !IsVersionNewer(version, state.current_version)) {
    AppendUpdateLog("Pending marker is stale; installed version is already " + state.current_version);
    RemovePendingMarkerAndInstalledPackage(marker_path, package_path);
    state.status = VersionUpdateStatus::UpToDate;
    state.has_pending_package = false;
    state.pending_package_path.clear();
    state.latest_version = state.current_version;
    return false;
  }
  if (version.empty()) {
    AppendUpdateLog("Pending marker has no version; clearing stale package " + package_path.string());
    RemovePendingMarkerAndInstalledPackage(marker_path, package_path);
    return false;
  }
  state.pending_package_path = package_path;
  state.latest_version = version;
  state.has_pending_package = true;
  state.status = VersionUpdateStatus::Downloaded;
  state.download_in_progress = false;
  state.download_progress_pct = 100;
  return true;
}

bool WritePendingMarker(const VersionUpdateState &state) {
  if (state.pending_marker_path.empty() || state.pending_package_path.empty()) return false;
  std::ofstream out(state.pending_marker_path, std::ios::trunc);
  if (!out) return false;
  out << "filename=" << state.pending_package_path.filename().string() << "\n";
  out << "version=" << state.latest_version << "\n";
  out << "size=" << state.expected_download_bytes << "\n";
  return static_cast<bool>(out);
}

void JoinDownloadThread(VersionUpdateState &state) {
  if (state.download_thread.joinable()) state.download_thread.join();
  state.download_thread_state.reset();
}

void ReleaseDownloadThreadWithoutBlocking(VersionUpdateState &state) {
  if (state.download_thread.joinable()) {
    state.download_thread.detach();
    AppendUpdateLog("Detached active download thread during shutdown to avoid blocking exit.");
  }
  state.download_thread_state.reset();
}

bool BeginVersionUpdateDownloadInternal(VersionUpdateState &state) {
  JoinDownloadThread(state);

  const std::string installed_version = DetectInstalledVersionLabel();
  if (!installed_version.empty()) {
    state.current_version = installed_version;
    ClearInstalledPendingArtifactsForAllRoots(installed_version, state.input_profile);
  }

  state.download_root = DetectDownloadRoot();
  if (state.download_root.empty()) {
    state.status = VersionUpdateStatus::NoNetwork;
    return false;
  }
  state.pending_marker_path = PendingMarkerPath(state.download_root);
  if (ReadPendingMarker(state.pending_marker_path, state)) {
    return true;
  }

  RemoteArchiveInfo remote_info{};
  if (!FetchLatestRemoteArchive(remote_info, state.input_profile)) {
    state.status = VersionUpdateStatus::NoNetwork;
    state.download_in_progress = false;
    return false;
  }

  state.latest_version = remote_info.version;
  const bool has_newer_remote = IsVersionNewer(remote_info.version, state.current_version);
  if (!has_newer_remote) {
    state.status = VersionUpdateStatus::UpToDate;
    state.download_in_progress = false;
    state.download_progress_pct = 0;
    return true;
  }

  const std::filesystem::path downloads_dir = PendingDownloadsDir(state.download_root);
  std::error_code ec;
  std::filesystem::create_directories(downloads_dir, ec);
  if (ec) {
    state.status = VersionUpdateStatus::DownloadFailed;
    return false;
  }

  state.active_download_filename = remote_info.filename;
  state.active_download_url = remote_info.download_url;
  state.active_download_api_url = remote_info.api_url;
  state.expected_download_bytes = remote_info.size_bytes;
  state.pending_package_path = downloads_dir / remote_info.filename;
  state.temp_package_path = downloads_dir / kDownloadTempFilename;
  AppendUpdateLog("Preparing download root=" + state.download_root.string());
  AppendUpdateLog("Pending package path=" + state.pending_package_path.string());
  AppendUpdateLog("Temp package path=" + state.temp_package_path.string());
  AppendUpdateLog("Download URL=" + state.active_download_url);
  AppendUpdateLog("Download API URL=" + state.active_download_api_url);

  if (std::filesystem::exists(state.pending_package_path, ec) && !ec) {
    const uint64_t existing_size = std::filesystem::file_size(state.pending_package_path, ec);
    if (!ec && existing_size == state.expected_download_bytes) {
      state.has_pending_package = true;
      state.status = VersionUpdateStatus::Downloaded;
      state.download_in_progress = false;
      state.download_progress_pct = 100;
      WritePendingMarker(state);
      return true;
    }
  }
  ec.clear();
  std::filesystem::remove(state.temp_package_path, ec);

  state.download_thread_done = false;
  state.download_thread_success = false;
  state.download_thread_state = std::make_shared<VersionUpdateDownloadThreadState>();
  state.download_progress_pct = 0;
  state.last_observed_download_bytes = 0;
  state.download_speed_bytes_per_sec = 0.0;
  state.has_pending_package = false;
  state.download_in_progress = true;
  state.status = VersionUpdateStatus::Downloading;

  const std::string download_url = state.active_download_url;
  const std::string download_api_url = state.active_download_api_url;
  const std::filesystem::path temp_path = state.temp_package_path;
  const std::filesystem::path final_path = state.pending_package_path;
  const std::shared_ptr<VersionUpdateDownloadThreadState> thread_state = state.download_thread_state;

  state.download_thread = std::thread([download_url, download_api_url, temp_path, final_path, thread_state]() {
    bool success = DownloadFile(download_url, temp_path);
    if (success && !LooksLikeZipFile(temp_path)) {
      AppendUpdateLog("Downloaded file is not a zip; trying fallback URL.");
      std::error_code remove_ec;
      std::filesystem::remove(temp_path, remove_ec);
      success = false;
    }
    if (!success && !download_api_url.empty()) {
      AppendUpdateLog("Falling back to GitHub contents raw API download.");
      success = DownloadFile(download_api_url, temp_path);
      if (success && !LooksLikeZipFile(temp_path)) {
        AppendUpdateLog("GitHub contents raw API download did not produce a zip.");
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        success = false;
      }
    }
    if (success) {
      std::error_code move_ec;
      std::filesystem::remove(final_path, move_ec);
      move_ec.clear();
      std::filesystem::rename(temp_path, final_path, move_ec);
      success = !move_ec;
      if (!success) {
        AppendUpdateLog("Failed to rename downloaded package into final filename.");
        move_ec.clear();
        std::filesystem::remove(temp_path, move_ec);
      }
    } else {
      AppendUpdateLog("DownloadFile returned failure.");
      std::error_code remove_ec;
      std::filesystem::remove(temp_path, remove_ec);
    }
    if (thread_state) {
      thread_state->success = success;
      thread_state->done = true;
    }
  });

  return true;
}
} // namespace

void ConfigureVersionUpdateProfile(VersionUpdateState &state, InputProfile input_profile) {
  state.input_profile = input_profile;
}

bool BeginVersionUpdateDownload(VersionUpdateState &state) {
  return BeginVersionUpdateDownloadInternal(state);
}

bool HandleVersionUpdateInput(const InputManager &input, VersionUpdateState &state,
                              const VersionUpdateCallbacks &callbacks) {
  if (!state.panel_active) {
    if (input.IsJustPressed(Button::A) || input.IsJustPressed(Button::Right)) {
      state.panel_active = true;
      return true;
    }
    return false;
  }

  if (input.IsJustPressed(Button::B)) {
    state.panel_active = false;
    return true;
  }

  if (state.download_in_progress) {
    if (input.IsJustPressed(Button::A) || input.IsJustPressed(Button::Left) ||
        input.IsJustPressed(Button::Right) || input.IsJustPressed(Button::Up) ||
        input.IsJustPressed(Button::Down) || input.IsRepeated(Button::Left) ||
        input.IsRepeated(Button::Right) || input.IsRepeated(Button::Up) ||
        input.IsRepeated(Button::Down)) {
      return true;
    }
    return false;
  }

  if (input.IsJustPressed(Button::Left) || input.IsJustPressed(Button::Right) ||
      input.IsJustPressed(Button::Up) || input.IsJustPressed(Button::Down) ||
      input.IsRepeated(Button::Left) || input.IsRepeated(Button::Right) ||
      input.IsRepeated(Button::Up) || input.IsRepeated(Button::Down)) {
    return true;
  }

  if ((input.IsJustPressed(Button::A) || input.IsJustPressed(Button::Right)) && callbacks.start_check_and_update) {
    callbacks.start_check_and_update(state);
    return true;
  }

  return false;
}

void InitializeVersionUpdateState(VersionUpdateState &state, const std::filesystem::path &runtime_root) {
  state.current_version = DetectCurrentVersionLabel(runtime_root);
  const std::string installed_version = DetectInstalledVersionLabel();
  if (!installed_version.empty()) {
    state.current_version = installed_version;
    ClearInstalledPendingArtifactsForAllRoots(installed_version, state.input_profile);
  }
  state.download_root = DetectDownloadRoot();
  if (state.download_root.empty()) return;
  state.pending_marker_path = PendingMarkerPath(state.download_root);
  ReadPendingMarker(state.pending_marker_path, state);
}

VersionUpdateTickResult TickVersionUpdateState(VersionUpdateState &state, float dt) {
  VersionUpdateTickResult result{};
  if (state.download_in_progress && !state.temp_package_path.empty()) {
    std::error_code ec;
    const uint64_t size = std::filesystem::exists(state.temp_package_path, ec)
                              ? std::filesystem::file_size(state.temp_package_path, ec)
                              : 0;
    if (!ec && state.expected_download_bytes > 0) {
      const int next_progress = std::clamp(
          static_cast<int>((size * 100ull) / state.expected_download_bytes), 0, 99);
      if (next_progress != state.download_progress_pct) {
        state.download_progress_pct = next_progress;
        result.state_changed = true;
      }
    }
    if (!ec) {
      if (dt > 0.0001f) {
        const uint64_t delta_bytes =
            size >= state.last_observed_download_bytes ? (size - state.last_observed_download_bytes) : 0;
        const double instant_speed = static_cast<double>(delta_bytes) / static_cast<double>(dt);
        if (state.download_speed_bytes_per_sec <= 0.0) {
          state.download_speed_bytes_per_sec = instant_speed;
        } else {
          state.download_speed_bytes_per_sec =
              state.download_speed_bytes_per_sec * 0.72 + instant_speed * 0.28;
        }
      }
      state.last_observed_download_bytes = size;
    }
  }

  if (state.download_thread_state) {
    state.download_thread_done = state.download_thread_state->done.load();
    state.download_thread_success = state.download_thread_state->success.load();
  }
  if (!state.download_thread_done) return result;

  JoinDownloadThread(state);
  state.download_in_progress = false;
  result.state_changed = true;
  if (state.download_thread_success) {
    state.has_pending_package = true;
    state.status = VersionUpdateStatus::Downloaded;
    state.download_progress_pct = 100;
    state.download_speed_bytes_per_sec = 0.0;
    if (WritePendingMarker(state)) {
      AppendUpdateLog("Update package downloaded successfully.");
    } else {
      state.has_pending_package = false;
      state.status = VersionUpdateStatus::DownloadFailed;
      AppendUpdateLog("Update package downloaded but pending marker write failed.");
    }
  } else if (state.status == VersionUpdateStatus::Downloading) {
    state.status = VersionUpdateStatus::DownloadFailed;
    state.download_progress_pct = 0;
    state.download_speed_bytes_per_sec = 0.0;
    AppendUpdateLog("Update package download failed.");
  }
  state.download_thread_done = false;
  return result;
}

void ShutdownVersionUpdateState(VersionUpdateState &state) {
  if (state.download_thread.joinable() && state.download_thread_done.load()) {
    JoinDownloadThread(state);
    return;
  }
  ReleaseDownloadThreadWithoutBlocking(state);
}

void DrawVersionUpdatePreview(const VersionUpdateRenderDeps &deps) {
  if (!deps.renderer) return;

  const SDL_Color text_color = deps.light_theme ? SDL_Color{44, 50, 60, 255} : SDL_Color{236, 241, 247, 255};
  const SDL_Color muted_color = deps.light_theme ? SDL_Color{113, 120, 130, 255} : SDL_Color{155, 168, 182, 255};
  const SDL_Color button_fill = deps.light_theme ? SDL_Color{225, 233, 241, 248} : SDL_Color{33, 71, 100, 236};
  const SDL_Color button_border = deps.light_theme ? SDL_Color{86, 117, 146, 255} : SDL_Color{122, 201, 255, 255};
  const SDL_Color button_active = deps.light_theme ? SDL_Color{204, 226, 240, 255} : SDL_Color{46, 96, 132, 255};
  const SDL_Color slot_fill = deps.light_theme ? SDL_Color{206, 212, 220, 255} : SDL_Color{62, 77, 92, 255};
  const SDL_Color slot_active = deps.light_theme ? SDL_Color{72, 122, 164, 255} : SDL_Color{122, 201, 255, 255};
  const SDL_Color success_color = deps.light_theme ? SDL_Color{54, 132, 94, 255} : SDL_Color{116, 224, 165, 255};

  const float scale = std::max(0.1f, deps.ui_scale);
  const int button_base_w = deps.gkd_profile ? gkd_menu::WideButtonW(scale) : ScalePx(scale, kButtonWidth);
  const int button_pad_x = ScalePx(scale, 24);
  const int progress_bar_w = ScalePx(scale, kProgressBarWidth);
  const int progress_bar_h = ScalePx(scale, kProgressBarHeight);
  const int center_x = deps.preview_rect.x + deps.preview_rect.w / 2;
  const int content_height = ScalePx(scale, 162);
  const int content_top = deps.preview_rect.y + std::max(0, (deps.preview_rect.h - content_height) / 2);
  const int line1_y = content_top + ScalePx(scale, 18);
  const int line2_y = content_top + ScalePx(scale, 76);
  const int line3_y = content_top + ScalePx(scale, 136);
  const int line4_y = content_top + ScalePx(scale, 162);

  DrawCenteredText(deps.renderer,
                   deps.preview_rect,
                   deps.get_emphasis_text_texture ? deps.get_emphasis_text_texture : deps.get_text_texture,
                   std::string(LocalizedAppText(deps.language_index, AppTextId::VersionCurrentVersion)) + " " +
                       deps.state.current_version,
                   text_color,
                   line1_y);

  TextCacheEntry *button_text_entry = deps.get_text_texture
                                          ? deps.get_text_texture(
                                                std::string(LocalizedAppText(deps.language_index, AppTextId::VersionCheckAndUpdate)),
                                                text_color)
                                          : nullptr;
  const int button_h = deps.gkd_profile
                           ? gkd_menu::ControlH(scale)
                           : std::max(ScalePx(scale, kButtonHeight + 4),
                                      (button_text_entry ? button_text_entry->h : 0) + ScalePx(scale, 8));
  const int button_width =
      deps.gkd_profile
          ? button_base_w
          : std::max(button_base_w, (button_text_entry ? button_text_entry->w : 0) + button_pad_x * 2);
  const bool button_selected = deps.state.panel_active && !deps.state.download_in_progress;
  const int button_x = center_x - button_width / 2;
  const int button_y = line2_y - button_h / 2;
  deps.draw_rect(button_x, button_y, button_width, button_h,
                 button_selected ? button_active : button_fill, true);
  deps.draw_rect(button_x, button_y, button_width, button_h, button_border, false);
  DrawCenteredText(deps.renderer,
                   SDL_Rect{button_x, button_y, button_width, button_h},
                   deps.get_text_texture,
                   std::string(LocalizedAppText(deps.language_index, AppTextId::VersionCheckAndUpdate)),
                   text_color,
                   line2_y);

  switch (deps.state.status) {
  case VersionUpdateStatus::Idle:
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionPressAToCheck)),
                     muted_color,
                     line3_y);
    break;
  case VersionUpdateStatus::NoNetwork:
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionNoNetwork)),
                     muted_color,
                     line3_y);
    break;
  case VersionUpdateStatus::Downloading: {
    const int bar_x = center_x - progress_bar_w / 2;
    const int bar_y = line3_y - progress_bar_h / 2 + ScalePx(scale, 8);
    deps.draw_rect(bar_x, bar_y, progress_bar_w, progress_bar_h, slot_fill, true);
    const int fill_w = std::clamp((progress_bar_w * deps.state.download_progress_pct) / 100, 0, progress_bar_w);
    if (fill_w > 0) {
      deps.draw_rect(bar_x, bar_y, fill_w, progress_bar_h, slot_active, true);
    }
    deps.draw_rect(bar_x, bar_y, progress_bar_w, progress_bar_h, button_border, false);
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionDownloading)) + " "
                         + std::to_string(deps.state.download_progress_pct) + "%",
                     text_color,
                     line3_y - ScalePx(scale, 20));
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     LocalizedDownloadSpeedText(deps.language_index, deps.state.download_speed_bytes_per_sec),
                     muted_color,
                     line4_y);
    break;
  }
  case VersionUpdateStatus::Downloaded:
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionDownloadedPackage)),
                     success_color,
                     line3_y);
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionRestartToInstall)),
                     success_color,
                     line4_y);
    break;
  case VersionUpdateStatus::UpToDate:
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionAlreadyLatest)),
                     muted_color,
                     line3_y);
    break;
  case VersionUpdateStatus::DownloadFailed:
    DrawCenteredText(deps.renderer,
                     deps.preview_rect,
                     deps.get_text_texture,
                     std::string(LocalizedAppText(deps.language_index, AppTextId::VersionDownloadFailed)),
                     muted_color,
                     line3_y);
    break;
  }
}
