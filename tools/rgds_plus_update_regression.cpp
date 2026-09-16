// Include the implementation to exercise private selection and marker helpers.
#include "../src/version_update_runtime.cpp"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

void SetEnv(const char *key, const char *value) {
#ifdef _WIN32
  _putenv_s(key, value);
#else
  setenv(key, value, 1);
#endif
}
}

int main(int argc, char **argv) {
  try {
    SetEnv("ROCREADER_DEVICE_MODEL", "rgds-plus");
    SetEnv("ROCREADER_UPDATE_CONTENTS_URL", "");
    Require(ResolveGithubContentsUrl(InputProfile::RGDS) == kRgdsPlusGithubContentsApi,
            "Plus default must use its own directory");
    SetEnv("ROCREADER_UPDATE_CONTENTS_URL",
           "https://github.com/LPF970915/ROCreader/tree/main/RGDSPlus/Downloads");
    Require(ResolveGithubContentsUrl(InputProfile::RGDS) == kRgdsPlusGithubContentsApi,
            "GitHub tree URL conversion failed");
    Require(IsPackageForProfile("ROCreaderver2.65 for RGDS plus.zip", InputProfile::RGDS),
            "Plus package rejected");
    for (const char *name : {"ROCreaderver9.99.zip", "ROCreaderver9.99 for RGDS.zip",
                             "ROCreaderver9.99 for Trimui Brick.zip", "ROCreaderver9.99 for GKD350H Ultra.zip"}) {
      Require(!IsPackageForProfile(name, InputProfile::RGDS), "foreign package accepted");
    }
    Require(IsVersionNewer("ver2.65", "ver2.64"), "next release not detected");
    Require(!IsVersionNewer("ver2.64", "ver2.64"), "same release must remain up to date");
    RemoteArchiveInfo archive;
    Require(SelectLatestRemoteArchive(R"([
      {"name":"ROCreaderver2.03 for RGDS plus.zip","download_url":"https://example.com/old.zip","size":1},
      {"name":"ROCreaderver2.65 for RGDS plus.zip","download_url":"https://example.com/plus.zip","size":2},
      {"name":"ROCreaderver9.99 for GKD350H Ultra.zip","download_url":"https://example.com/gkd.zip","size":3}
    ])", archive, InputProfile::RGDS), "metadata selection failed");
    Require(archive.version == "ver2.65" && archive.size_bytes == 2,
            "latest Plus release selection failed");
    if (argc == 2) {
      std::ifstream file(argv[1]);
      std::ostringstream json;
      json << file.rdbuf();
      Require(SelectLatestRemoteArchive(json.str(), archive, InputProfile::RGDS),
              "live GitHub metadata selection failed");
      std::cout << "[pass] GitHub metadata: " << archive.version << " bytes=" << archive.size_bytes << "\n";
    }

    const auto root = std::filesystem::path("build") / "rgds_plus_update_checks";
    std::filesystem::create_directories(root);
    const auto own = root / "ROCreaderver2.64 for RGDS plus.zip";
    const auto other = root / "ROCreaderver2.63 for GKD350H Ultra.zip";
    const auto marker = root / kPendingMarkerFilename;
    { std::ofstream out(own); out << "fixture"; }
    { std::ofstream out(other); out << "fixture"; }
    { std::ofstream out(marker); out << "filename=" << other.filename().string() << "\nversion=ver2.63\n"; }
    ClearInstalledPendingArtifacts(root, "ver2.64", InputProfile::RGDS);
    Require(!std::filesystem::exists(own), "stale Plus package not removed");
    Require(std::filesystem::exists(other) && std::filesystem::exists(marker),
            "foreign update data was removed");
    VersionUpdateState state;
    state.input_profile = InputProfile::RGDS;
    state.current_version = "ver2.64";
    Require(!ReadPendingMarker(marker, state), "foreign marker accepted");
    Require(std::filesystem::exists(other), "foreign package deleted");
    std::filesystem::remove(marker);
    std::filesystem::remove(other);

    SetEnv("ROCREADER_UPDATE_CONTENTS_URL", "");
    SetEnv("ROCREADER_DEVICE_MODEL", "rgds");
    Require(!IsPackageForProfile("ROCreaderver2.65 for RGDS plus.zip", InputProfile::RGDS),
            "original RGDS accepted Plus package");
    Require(ResolveGithubContentsUrl(InputProfile::GKD350HUltra) == kGkdGithubContentsApi,
            "GKD default changed");
    Require(ResolveGithubContentsUrl(InputProfile::DesktopDefault) == kGithubContentsApi,
            "generic default changed");
    std::cout << "[pass] Plus update URL, package isolation, versions and pending cleanup\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
