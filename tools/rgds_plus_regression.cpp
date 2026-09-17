#include "app_layout.h"
#include "lid_power_control.h"
#include "key_guide_panel.h"
#include "status_bar_runtime.h"
#include "txt_reader_runtime.h"
#include "txt_session_facade.h"
#include "txt_settings_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifndef HAVE_SDL2_TTF
#error RGDS plus regression tests require SDL2_ttf
#endif

namespace {
void Require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

using Font = std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)>;

void TestFontSizeLevels(const std::filesystem::path &out) {
  const std::array<int, 10> sizes{{18, 20, 22, 24, 26, 28, 30, 32, 34, 36}};
  const auto config_path = (out / "font_size_config.ini").string();
  ConfigStore config(config_path);
  Require(config.Get().txt_font_size_level == 3, "default font size changed");
  for (int level = 0; level < static_cast<int>(sizes.size()); ++level) {
    Require(ClampTxtFontSizeLevel(level) == level, "valid font level was clamped");
    Require(TxtFontPointSizeForLevel(level) == sizes[level], "font level maps to wrong size");
    config.Mutable().txt_font_size_level = level;
    config.Save();
    ConfigStore restored(config_path);
    Require(restored.Get().txt_font_size_level == level, "saved font level lost on restart");
  }
  for (const auto boundary : {std::pair<int, int>{-1, 0}, {10, 9}, {999, 9}}) {
    config.Mutable().txt_font_size_level = boundary.first;
    config.Save();
    ConfigStore restored(config_path);
    Require(restored.Get().txt_font_size_level == boundary.second, "invalid saved font level not clamped");
    Require(TxtFontPointSizeForLevel(boundary.first) == sizes[boundary.second],
            "invalid font level maps outside the size table");
  }
  std::cout << "[pass] font sizes: 18..36, legacy indices, defaults, persistence and boundaries\n";
}

void TestKeyGuideTitle() {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_RGBA32);
  Require(surface != nullptr, SDL_GetError());
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  Require(renderer != nullptr, SDL_GetError());
  UiAssets assets{};
  NativeConfig cfg{};
  std::vector<SettingId> menu;
  animation::TweenFloat anim;
  TxtTranscodeJob transcode{};
  SystemSettingsState system{};
  TxtSettingsState txt{};
  std::vector<ContributorAvatarEntry> avatars;
  ContributorAvatarState avatar{};
  KeyCalibrationState calibration{};
  VersionUpdateState update{};
  OnlineSourceState online{};
  SettingsRuntimeRenderDeps deps{renderer, assets, cfg, InputProfile::RGDS, menu, 0,
                                anim, 0, transcode, system, txt, avatars, avatar,
                                calibration, false, update, online, {}, {}};
  std::string title;
  deps.services.draw_rect = [](int, int, int, int, SDL_Color, bool) {};
  deps.services.get_title_text_texture = [&](const std::string &text, SDL_Color) -> TextCacheEntry * {
    title = text;
    return nullptr;
  };
  const char *old_model = std::getenv("ROCREADER_DEVICE_MODEL");
  const std::string saved_model = old_model ? old_model : "";
  auto set_model = [](const std::string &model) {
#ifdef _WIN32
    _putenv_s("ROCREADER_DEVICE_MODEL", model.c_str());
#else
    if (model.empty()) unsetenv("ROCREADER_DEVICE_MODEL");
    else setenv("ROCREADER_DEVICE_MODEL", model.c_str(), 1);
#endif
  };
  for (const char *model : {"rgds", "rgds-plus"}) {
    set_model(model);
    for (int language = 0; language < 12; ++language) {
      DrawKeyGuidePanel(deps, SDL_Rect{0, 0, 1024, 768}, language, 100);
      const bool plus = std::string(model) == "rgds-plus";
      Require((title.find("RGDSplus") != std::string::npos) == plus, "key guide model title mismatch");
      if (language == 0) {
        Require(title == (plus ? u8"RGDSplus\u53cc\u5c4f\u4e13\u7528\u6620\u5c04"
                              : u8"RGDS \u53cc\u5c4f\u4e13\u7528\u6620\u5c04"),
                "Chinese key guide title mismatch");
      }
    }
  }
  set_model(saved_model);
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  std::cout << "[pass] key guide titles: RGDS/Plus, 12 languages\n";
}

void TestLid(const std::filesystem::path &out) {
  RgdsPlusLidMonitor lid;
  Require(!lid.Observe(3, 0, true), "open lid triggered suspend");
  Require(!lid.Observe(2, 100, true), "close was not debounced");
  Require(!lid.Observe(0, 3099, true), "close debounce too short");
  Require(lid.Observe(2, 3100, true), "bit-zero close not detected");
  Require(!lid.Observe(2, 10000, true), "close triggered repeatedly");
  lid.SuspendCompleted(true, 10000);
  Require(!lid.Observe(0, 15000, true), "wake while closed immediately slept");
  Require(!lid.Observe(1, 15100, true), "bit-zero open not recognized");
  Require(!lid.Observe(0, 15200, true), "second close not debounced");
  Require(lid.Observe(0, 18200, true), "second close lost after resume");
  lid.SuspendCompleted(false, 18200);
  Require(!lid.Observe(0, 21199, true), "failed sleep retried too quickly");
  Require(lid.Observe(0, 21200, true), "failed sleep never retried");

  RgdsPlusLidMonitor bounce;
  Require(!bounce.Observe(2, 0, true), "initial closed startup not debounced");
  Require(!bounce.Observe(3, 2000, true), "reopen triggered sleep");
  Require(!bounce.Observe(2, 2500, true), "reclose not debounced");
  Require(!bounce.Observe(-1, 5400, true), "missing sensor triggered sleep");
  Require(!bounce.Observe(2, 5500, true), "invalid read kept old debounce");
  Require(!bounce.Observe(4, 8400, true), "invalid hall bits accepted");
  Require(!bounce.Observe(2, 8500, false), "disabled lid triggered sleep");
  Require(!bounce.Observe(2, 12000, true), "enabling while closed skipped delay");
  Require(bounce.Observe(2, 15000, true), "enabling lid did not take effect");
  RgdsPlusLidMonitor wrap;
  Require(!wrap.Observe(2, UINT32_MAX - 999, true), "wrapped initial close");
  Require(!wrap.Observe(2, 1999, true), "tick wrap shortened debounce");
  Require(wrap.Observe(2, 2000, true), "tick wrap lost close");

  const auto sensor = out / "hallkey";
  const char *old_env = std::getenv("ROCREADER_RGDS_HALL_PATH");
  const std::string saved_env = old_env ? old_env : "";
  auto set_hall_path = [](const std::string &path) {
#ifdef _WIN32
    _putenv_s("ROCREADER_RGDS_HALL_PATH", path.c_str());
#else
    setenv("ROCREADER_RGDS_HALL_PATH", path.c_str(), 1);
#endif
  };
  set_hall_path(sensor.string());
  Require(std::getenv("ROCREADER_RGDS_HALL_PATH") == sensor.string(), "hall fixture environment not set");
  RgdsPlusLidMonitor polling;
  Require(!polling.Poll(0, true), "missing hall file triggered suspend");
  { std::ofstream file(sensor); file << "2\n"; }
  Require(!polling.Poll(100, true), "poll skipped close debounce");
  { std::ofstream file(sensor); file << "2junk\n"; }
  Require(!polling.Poll(3000, true), "malformed hall file accepted");
  { std::ofstream file(sensor); file << "2\n"; }
  Require(!polling.Poll(3100, true), "bad read did not reset close timer");
  Require(polling.Poll(6100, true), "idle hall polling lost close");
  set_hall_path(saved_env);
  std::cout << "[pass] lid: debounce, bit flags, resume, retries, invalid/missing sensor, disabled, tick wrap\n";
}

Font OpenFont(const std::string &path, int size) {
  Font font(TTF_OpenFont(path.c_str(), size), TTF_CloseFont);
  Require(font != nullptr, "open font: " + path);
  return font;
}

struct Canvas {
  SDL_Surface *surface;
  SDL_Renderer *renderer;
  Canvas(int w, int h) {
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    Require(surface != nullptr, SDL_GetError());
    renderer = SDL_CreateSoftwareRenderer(surface);
    Require(renderer != nullptr, SDL_GetError());
  }
  ~Canvas() {
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
  }
  void Clear(Uint8 value) {
    SDL_SetRenderDrawColor(renderer, value, value, value, 255);
    SDL_RenderClear(renderer);
  }
  std::vector<Uint32> Pixels() {
    std::vector<Uint32> pixels(surface->w * surface->h);
    Require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 pixels.data(), surface->w * 4) == 0, SDL_GetError());
    return pixels;
  }
  void Save(const std::filesystem::path &path) {
    SDL_RenderPresent(renderer);
    Require(SDL_SaveBMP(surface, path.string().c_str()) == 0, SDL_GetError());
  }
};

struct Textures {
  std::unordered_map<std::string, TextCacheEntry> entries;
  ~Textures() {
    for (auto &item : entries) SDL_DestroyTexture(item.second.texture);
  }
  TextCacheEntry *Get(SDL_Renderer *renderer, TTF_Font *font, const std::string &text,
                     SDL_Color color, bool solid = false) {
    auto &entry = entries[text];
    if (entry.texture) return &entry;
    Require(TTF_SizeUTF8(font, text.c_str(), &entry.w, &entry.h) == 0, TTF_GetError());
    SDL_Surface *surface = solid
        ? SDL_CreateRGBSurfaceWithFormat(0, entry.w, entry.h, 32, SDL_PIXELFORMAT_ARGB8888)
        : TTF_RenderUTF8_Blended(font, text.c_str(), color);
    Require(surface != nullptr, TTF_GetError());
    if (solid) SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, color.r, color.g, color.b, 255));
    entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    Require(entry.texture != nullptr, SDL_GetError());
    return &entry;
  }
};

std::vector<Uint32> DrawStatus(InputProfile profile, int w, int h, TTF_Font *font,
                               int percent, bool charging, int theme, const std::string &clock,
                               bool solid, const std::filesystem::path &snapshot = {}) {
  SetLayoutProfile(SelectLayoutProfile(w, h));
  Canvas canvas(w, std::max(64, Layout().top_bar_h + 8));
  canvas.Clear(theme == 0 ? 18 : 245);
  Textures textures;
  SystemStatusSnapshot status;
  status.battery_available = true;
  status.battery_percent = percent;
  status.charging = charging;
  status.clock_text = clock;
  std::vector<SDL_Rect> rects;
  StatusBarRenderDeps deps;
  deps.renderer = canvas.renderer;
  deps.status = &status;
  deps.input_profile = profile;
  deps.theme = theme;
  deps.screen_w = w;
  deps.top_bar_h = Layout().top_bar_h;
  deps.scale_px = ScalePx;
  deps.draw_rect = [&](int x, int y, int rw, int rh, SDL_Color c, bool filled) {
    SDL_Rect rect{x, y, rw, rh};
    rects.push_back(rect);
    SDL_SetRenderDrawColor(canvas.renderer, c.r, c.g, c.b, c.a);
    if (filled) SDL_RenderFillRect(canvas.renderer, &rect);
    else SDL_RenderDrawRect(canvas.renderer, &rect);
  };
  deps.get_text_texture = [&](const std::string &text, SDL_Color color) {
    if (solid) color = text == clock ? SDL_Color{255, 0, 0, 255} : SDL_Color{0, 0, 255, 255};
    return textures.Get(canvas.renderer, font, text, color, solid);
  };
  DrawStatusBarRuntime(deps);
  auto pixels = canvas.Pixels();
  Require(rects.size() >= 4, "status bar did not draw");
  Require(w == 1024 ? (rects[2].x > 700 && rects[2].x <= 750)
                   : rects[2].x == (w == 640 ? 472 : w == 1600 ? 1249 : 552),
          "battery icon position changed");
  if (solid && w == 1024) {
    // Solid textures expose clipping/overlap across the full measured glyph bounds.
    for (const auto &item : textures.entries) {
      const Uint32 color = item.first == clock ? 0xffff0000u : 0xff0000ffu;
      const auto visible = std::count(pixels.begin(), pixels.end(), color);
      Require(visible == item.second.w * item.second.h,
              "status text clipped or overlapped: " + item.first + " screen=" + std::to_string(w) +
              " text=" + std::to_string(item.second.w) + "x" + std::to_string(item.second.h) +
              " visible=" + std::to_string(visible) + " battery=" + std::to_string(percent));
    }
  }
  if (!snapshot.empty()) canvas.Save(snapshot);
  return pixels;
}

void TestStatus(const std::filesystem::path &out) {
  for (const std::string font_path : {"fonts/ui_font_02.ttf", "fonts/ui_font.ttf"}) {
    auto font = OpenFont(font_path, 26);
    for (int theme : {0, 1}) {
      for (int percent : {0, 9, 50, 100}) {
        for (bool charging : {false, true}) {
          for (const std::string clock : {"00:00", "10:08", "23:59"}) {
            const auto plus = DrawStatus(InputProfile::RGDS, 1024, 768, font.get(),
                                         percent, charging, theme, clock, true);
            const auto brick = DrawStatus(InputProfile::TrimuiBrick, 1024, 768, font.get(),
                                          percent, charging, theme, clock, true);
            Require(plus == brick, "RGDS plus status layout differs from Brick");
          }
        }
      }
    }
  }
  auto font = OpenFont("fonts/ui_font_02.ttf", 26);
  DrawStatus(InputProfile::RGDS, 1024, 768, font.get(), 100, true, 0, "23:59",
             false, out / "status_1024.bmp");
  font = OpenFont("fonts/ui_font_02.ttf", 16);
  const auto rgds = DrawStatus(InputProfile::RGDS, 640, 480, font.get(), 100, false, 0, "23:59", true);
  const auto h700 = DrawStatus(InputProfile::H700Default, 640, 480, font.get(), 100, false, 0, "23:59", true);
  Require(rgds == h700, "original RGDS status geometry changed");
  DrawStatus(InputProfile::DesktopDefault, 720, 480, font.get(), 100, false, 0, "23:59", true);
  font = OpenFont("fonts/ui_font_02.ttf", 32);
  DrawStatus(InputProfile::GKD350HUltra, 1600, 1440, font.get(), 100, false, 0, "23:59", true);
}

void CheckText(const TxtReaderState &state, const SDL_Rect &bounds, TTF_Font *font) {
  Require(state.viewport_x == bounds.x && state.viewport_y == bounds.y &&
              state.viewport_w == bounds.w && state.viewport_h == bounds.h,
          "canvas update overwrote TXT content bounds");
  Require(state.lines.size() == state.line_source_offsets.size(), "TXT source offsets lost");
  for (const auto &line : state.lines) {
    if (line.empty()) continue;
    int width = 0;
    Require(TTF_SizeUTF8(font, line.c_str(), &width, nullptr) == 0, TTF_GetError());
    Require(width <= bounds.w - 8, "TXT line exceeds inner content width");
  }
}

void TestText(const std::filesystem::path &out, int w, int panel_h, int panels, int level) {
  SetLayoutProfile(SelectLayoutProfile(w, panel_h));
  const int font_pt = ScalePx(TxtFontPointSizeForLevel(level));
  auto font = OpenFont("fonts/ui_font_02.ttf", font_pt);
  const int line_h = TTF_FontHeight(font.get()) + ScalePx(8);
  const int h = panel_h * panels;
  const auto bounds = GetTxtViewportBounds(nullptr, {w, h, Layout().txt_margin_x,
                                                     Layout().txt_margin_y, font_pt, line_h});
  ReaderUiState ui;
  TxtReaderModule module(ui, {});
  TxtTextServiceState service;
  service.cache_dir = out / ("cache_" + std::to_string(w) + "x" + std::to_string(h) +
                              "_" + std::to_string(level));
  service.max_cache_entries = 4;
  service.max_wrapped_lines = 250000;
  std::filesystem::create_directories(service.cache_dir);
  const auto book = service.cache_dir / "long.txt";
  ui.current_book = book.string();
  {
    std::ofstream text(book, std::ios::binary);
    for (int i = 0; i < 256; ++i) {
      for (int j = 0; j < 16; ++j) text << u8"\u4e0a\u4e0b\u53cc\u5c4f\u9605\u8bfb\u6d4b\u8bd5"
                                           u8"\uff0c\u53f3\u4fa7\u6587\u5b57\u5b8c\u6574\u663e\u793a\u3002";
      text << " TXT 1024x768 English punctuation and numbers 1234567890.\n";
    }
    Require(static_cast<bool>(text), "cannot create TXT fixture");
  }
  auto make_facade = [&](int version) {
    return std::make_unique<TxtSessionFacade>(TxtSessionFacadeDeps{
        ui, service, [] {}, [&] { return font != nullptr; },
        [&] { return TTF_FontHeight(font.get()); }, [&] { return bounds; },
        [](const std::string &path) { return path; },
        [](const std::string &, std::string &) { return false; },
        [&](TxtReaderState &state, const std::string &line, size_t offset) {
          return AppendWrappedTextLine(state, line, font.get(), offset, service.max_wrapped_lines);
        },
        [] {}, [] {}, {}, ScalePx(8), 64 * 1024 * 1024, service.max_wrapped_lines, 0, version});
  };

  // Reproduce v6's malformed full layout and a partially parsed resume snapshot.
  auto old = make_facade(6);
  Require(old->OpenTextBook(ui.current_book), "old TXT open failed");
  ui.Txt().viewport_w = w;
  ui.Txt().viewport_h = h;
  auto old_deps = old->MakeDeps();
  ProcessTextLayoutChunk(ui.Txt(), 0, 32768, &ui.Txt().cache_key, old_deps);
  Require(ui.Txt().loading, "fixture must exercise incremental loading");
  old->PersistResumeSnapshot(ui.current_book, true);
  while (ui.Txt().loading) ProcessTextLayoutChunk(ui.Txt(), 0, 32768, &ui.Txt().cache_key, old_deps);
  const auto old_key = ui.Txt().cache_key;
  ui.Txt().scroll_px = (static_cast<int>(ui.Txt().lines.size()) / 3) * line_h;
  ui.progress = module.Progress();
  const auto source_offset = static_cast<size_t>(ui.progress.scroll_x);
  Require(source_offset > 0, "reading anchor is empty");
  old->Close();
  service.layout_cache.clear();

  auto current = make_facade(kTxtLayoutCacheVersion);
  Require(current->OpenTextBook(ui.current_book), "fixed TXT open failed");
  Require(ui.Txt().cache_key != old_key, "old TXT cache version was not invalidated");
  TxtLayoutCacheEntry layout_entry;
  TxtResumeCacheEntry resume_entry;
  Require(LoadTxtLayoutCacheFromDisk(service, old_key, ui.current_book, layout_entry),
          "old full layout fixture missing");
  Require(LoadTxtResumeCacheFromDisk(service, old_key, ui.current_book, resume_entry) && resume_entry.loading,
          "old partial resume fixture missing");
  Require(!LoadTxtResumeCacheFromDisk(service, ui.Txt().cache_key, ui.current_book, resume_entry),
          "old resume accepted under new key");
  auto deps = current->MakeDeps();
  do {
    module.UpdateViewport(w, h);
    CheckText(ui.Txt(), bounds, font.get());
    ProcessTextLayoutChunk(ui.Txt(), 0, 32768, &ui.Txt().cache_key, deps);
  } while (ui.Txt().loading);
  CheckText(ui.Txt(), bounds, font.get());
  const auto upper = std::upper_bound(ui.Txt().line_source_offsets.begin(),
                                      ui.Txt().line_source_offsets.end(), source_offset);
  const int expected_line = static_cast<int>(upper - ui.Txt().line_source_offsets.begin()) - 1;
  Require(ui.Txt().scroll_px == expected_line * line_h, "source reading position changed on rewrap");

  const auto lines = ui.Txt().lines;
  ui.progress = module.Progress();
  current->Close();
  service.layout_cache.clear();
  Require(current->OpenTextBook(ui.current_book), "cached TXT reopen failed");
  Require(!ui.Txt().loading && ui.Txt().lines == lines, "fixed disk layout was not reused");
  module.UpdateViewport(w, h);
  CheckText(ui.Txt(), bounds, font.get());

  if (w == 1024 && panels == 2 && level == 2) {
    Canvas canvas(w, h);
    canvas.Clear(255);
    Textures textures;
    size_t upper_lines = 0, lower_lines = 0;
    TxtReaderRenderDeps render_deps{
        canvas.renderer, ui, [] {},
        [&](const SDL_Rect &rect) { SDL_RenderSetClipRect(canvas.renderer, &rect); },
        [&] { SDL_RenderSetClipRect(canvas.renderer, nullptr); },
        [&](const std::string &text, int x, int y) {
          if (text.empty()) return;
          auto *entry = textures.Get(canvas.renderer, font.get(), text, {20, 20, 20, 255});
          Require(x >= bounds.x && x + entry->w <= bounds.x + bounds.w,
                  "rendered TXT crosses right margin");
          if (y < panel_h) ++upper_lines;
          else ++lower_lines;
          SDL_Rect dst{x, y, entry->w, entry->h};
          SDL_RenderCopy(canvas.renderer, entry->texture, nullptr, &dst);
        }};
    DrawTxtReaderRuntime(render_deps);
    Require(upper_lines > 0 && lower_lines > 0, "TXT missing from one panel");
    canvas.Save(out / "txt_1024x1536.bmp");
  }
  std::cout << "[pass] TXT " << w << "x" << h << " font=" << font_pt
            << " bounds=" << bounds.x << "," << bounds.y << " " << bounds.w << "x" << bounds.h << "\n";
}
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  try {
    Require(SDL_Init(SDL_INIT_TIMER) == 0, SDL_GetError());
    Require(TTF_Init() == 0, TTF_GetError());
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto out = std::filesystem::path("build") / ("rgds_plus_checks_" + std::to_string(stamp));
    std::filesystem::create_directories(out);
    TestLid(out);
    TestFontSizeLevels(out);
    TestKeyGuideTitle();
    TestStatus(out);
    std::cout << "[pass] status: RGDS plus/Brick, original RGDS, desktop, GKD\n";
    for (int level : {0, 2, 4, 5, 6, 7, 8, 9}) TestText(out, 1024, 768, 2, level);
    TestText(out, 640, 480, 2, 2);
    TestText(out, 1024, 768, 1, 2);
    for (const auto size : {std::pair<int, int>{640, 480}, {720, 480}, {720, 720},
                           {1024, 768}, {1600, 1440}}) {
      TestText(out, size.first, size.second, 1, 9);
    }
    std::cout << "[pass] snapshots: " << out.string() << "\n";
    TTF_Quit();
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[FAIL] " << error.what() << "\n";
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
}
