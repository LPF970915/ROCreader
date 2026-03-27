#include "ui_assets_loader.h"

#include <SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
void XorUiPayload(std::vector<unsigned char> &data, const std::string &name) {
  static const std::string key = "ROCreader::native_h700::ui_pack";
  if (name.empty()) return;
  for (size_t i = 0; i < data.size(); ++i) {
    const unsigned char k = static_cast<unsigned char>(key[i % key.size()]);
    const unsigned char n = static_cast<unsigned char>(name[i % name.size()]);
    data[i] = static_cast<unsigned char>(data[i] ^ k ^ n ^ static_cast<unsigned char>((i * 131u) & 0xFFu));
  }
}

void LoadUiAsset(SDL_Texture *&slot, const std::string &name,
                 std::unordered_map<std::string, std::vector<unsigned char>> &packed_ui_assets,
                 const std::vector<std::filesystem::path> &ui_roots, UiAssetsLoaderDeps &deps,
                 UiAssetsLoadResult &result) {
  if (slot) {
    SDL_DestroyTexture(slot);
    slot = nullptr;
  }
  auto packed_it = packed_ui_assets.find(name);
  if (packed_it != packed_ui_assets.end()) {
    SDL_Surface *surface = deps.load_surface_from_memory(packed_it->second.data(), packed_it->second.size());
    if (surface) {
      slot = deps.create_texture_from_surface(deps.renderer, surface);
      SDL_FreeSurface(surface);
    }
    if (slot) {
      int width = 0;
      int height = 0;
      SDL_QueryTexture(slot, nullptr, nullptr, &width, &height);
      deps.remember_texture_size(slot, width, height);
    } else {
      std::cerr << "[native_h700] ui asset load failed from pack: " << name << " err=" << SDL_GetError() << "\n";
    }
    return;
  }

  std::filesystem::path found_path;
  for (const auto &root : ui_roots) {
    const auto candidate = root / name;
    if (std::filesystem::exists(candidate)) {
      found_path = candidate;
      if (result.ui_root_hit.empty()) result.ui_root_hit = root;
      break;
    }
  }
  if (found_path.empty()) {
    std::cerr << "[native_h700] ui asset not found: " << name << "\n";
    return;
  }
  slot = deps.load_texture_from_file(deps.renderer, found_path.string());
  if (!slot) {
    std::cerr << "[native_h700] ui asset load failed: " << found_path.string() << " err=" << SDL_GetError() << "\n";
    return;
  }
  int width = 0;
  int height = 0;
  SDL_QueryTexture(slot, nullptr, nullptr, &width, &height);
  deps.remember_texture_size(slot, width, height);
}
}

UiAssetsLoadResult LoadUiAssets(UiAssets &assets, UiAssetsLoaderDeps &deps) {
  UiAssetsLoadResult result;
  std::vector<std::filesystem::path> ui_roots = {
      deps.exe_path / "ui",
      deps.exe_path / ".." / "ui",
      std::filesystem::current_path() / "ui",
  };
  std::vector<std::filesystem::path> ui_pack_paths = {
      deps.exe_path / "ui.pack",
      deps.exe_path / ".." / "ui.pack",
      std::filesystem::current_path() / "ui.pack",
  };
  std::unordered_map<std::string, std::vector<unsigned char>> packed_ui_assets;

  for (const auto &pack_path : ui_pack_paths) {
    if (!std::filesystem::exists(pack_path)) continue;
    std::ifstream in(pack_path, std::ios::binary);
    if (!in) continue;
    char magic[8] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::string(magic, sizeof(magic)) != "RCUIPK01") continue;
    uint32_t count = 0;
    in.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (!in) continue;
    std::unordered_map<std::string, std::vector<unsigned char>> loaded;
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t name_len = 0;
      uint32_t original_size = 0;
      uint32_t enc_size = 0;
      in.read(reinterpret_cast<char *>(&name_len), sizeof(name_len));
      if (!in) {
        ok = false;
        break;
      }
      std::string name(name_len, '\0');
      if (name_len > 0) in.read(name.data(), static_cast<std::streamsize>(name_len));
      in.read(reinterpret_cast<char *>(&original_size), sizeof(original_size));
      in.read(reinterpret_cast<char *>(&enc_size), sizeof(enc_size));
      if (!in) {
        ok = false;
        break;
      }
      std::vector<unsigned char> payload(enc_size, 0);
      if (enc_size > 0) in.read(reinterpret_cast<char *>(payload.data()), static_cast<std::streamsize>(enc_size));
      if (!in) {
        ok = false;
        break;
      }
      XorUiPayload(payload, name);
      if (payload.size() != original_size) {
        ok = false;
        break;
      }
      loaded[name] = std::move(payload);
    }
    if (!ok) continue;
    packed_ui_assets = std::move(loaded);
    result.ui_pack_hit = pack_path;
    result.packed_asset_count = packed_ui_assets.size();
    break;
  }

  LoadUiAsset(assets.background_main, "background_main.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.top_status_bar, "top_status_bar.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.bottom_hint_bar, "bottom_hint_bar.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.nav_l1_icon, "nav_l1_icon.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.nav_r1_icon, "nav_r1_icon.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.nav_selected_pill, "nav_selected_pill.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.book_under_shadow, "book_under_shadow.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.book_select, "book_select.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.book_title_shadow, "book_title_shadow.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.book_cover_txt, "book_cover_txt.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.book_cover_pdf, "book_cover_pdf.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_theme, "settings_preview_theme.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_animations, "settings_preview_animations.png", packed_ui_assets, ui_roots,
              deps, result);
  LoadUiAsset(assets.settings_preview_audio, "settings_preview_audio.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_keyguide, "Menu_Button Instructions.png", packed_ui_assets, ui_roots, deps,
              result);
  LoadUiAsset(assets.settings_preview_contact, "Menu_Contact Me.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_clean_history, "Menu_CleanHistory.png", packed_ui_assets, ui_roots, deps,
              result);
  LoadUiAsset(assets.settings_preview_clean_cache, "Menu_CleanCache.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_txt_to_utf8, "Menu_TXTtoUTF8.png", packed_ui_assets, ui_roots, deps, result);
  LoadUiAsset(assets.settings_preview_exit, "Menu_Exit.png", packed_ui_assets, ui_roots, deps, result);
  return result;
}
