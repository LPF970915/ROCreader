#include "app_layout.h"
#include "shelf_scene.h"
#include "app_stores.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

struct Fixture {
  SDL_Surface *surface = nullptr;
  SDL_Renderer *renderer = nullptr;
  UiAssets assets{};
  ShelfRuntimeState runtime;
  ShelfRenderCache cache;
  std::unordered_map<int, GridItemAnim> animations;
  std::unordered_map<std::string, int> draws;
  std::vector<SDL_Texture *> test_textures;
  bool active = false;
  ShelfRuntimeRenderDeps deps;

  explicit Fixture(const LayoutMetrics &layout)
      : deps{nullptr, assets, nullptr, runtime, cache, animations,
             MakeShelfSceneLayoutMetrics(layout), layout.grid_cols,
             layout.grid_cols * layout.visible_rows, 1.0f / 60.0f, true, active,
             false, 0, 0, 0, 0, 0, 0, 0, 0, false, 0, 255, 0, 0, 0,
             0, 0, 0, 0, 0, 0, 0, 0, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}} {
    surface = SDL_CreateRGBSurfaceWithFormat(0, layout.screen_w, layout.screen_h, 32, SDL_PIXELFORMAT_RGBA32);
    Require(surface != nullptr, SDL_GetError());
    renderer = SDL_CreateSoftwareRenderer(surface);
    Require(renderer != nullptr, SDL_GetError());
    deps.renderer = renderer;
    deps.unfocused_alpha = 180;
    deps.focus_cover_w = std::round(layout.cover_w * 1.045f);
    deps.focus_cover_h = std::round(layout.cover_h * 1.045f);
    deps.cover_aspect = 2.0f / 3.0f;
    deps.card_move_linear_speed_x = deps.card_move_linear_speed_y = 860;
    deps.card_move_tail_ratio = deps.card_scale_tail_ratio = 0.52f;
    deps.card_move_tail_min_mul = 0.12f;
    deps.card_scale_tail_min_mul = 0.10f;
    deps.card_scale_linear_speed_w = 140;
    deps.card_scale_linear_speed_h = 210;
    deps.draw_rect = [&](int x, int y, int w, int h, SDL_Color color, bool) {
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_Rect rect{x, y, w, h};
      SDL_RenderFillRect(renderer, &rect);
    };
    deps.get_cover_texture = [&](const BookItem &book) -> SDL_Texture * {
      ++draws[book.path];
      return nullptr;
    };
    deps.get_texture_size = [](SDL_Texture *, int &w, int &h) { w = h = 0; };
    deps.get_text_texture = [](const std::string &, SDL_Color, int &w, int &h, SDL_Texture *&tex) {
      w = h = 0;
      tex = nullptr;
    };
    deps.get_title_ellipsized = [](const std::string &s, int, const auto &) { return s; };
    deps.shelf_title_text = [](const BookItem &book) { return book.path; };
    for (int i = 0; i < 19; ++i) {
      BookItem book{};
      book.path = "book-" + std::to_string(i);
      runtime.items.push_back(book);
    }
  }

  ~Fixture() {
    DestroyShelfRenderCache(cache, {});
    for (SDL_Texture *texture : test_textures) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
  }

  void Draw() {
    draws.clear();
    active = false;
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderClear(renderer);
    DrawShelfRuntime(deps);
  }

  void Settle() {
    for (int frame = 0; frame < 180; ++frame) {
      Draw();
      if (!active) return;
    }
    throw std::runtime_error("animation did not settle");
  }

  void Save(const std::string &suffix) {
    const std::string filename = "build/shelf_animation_" + std::to_string(deps.layout.screen_w) +
                                 "x" + std::to_string(deps.layout.screen_h) + suffix + ".bmp";
    Require(SDL_SaveBMP(surface, filename.c_str()) == 0, SDL_GetError());
  }

  SDL_Texture *SolidTexture(int w, int h, SDL_Color color) {
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    Require(image != nullptr, SDL_GetError());
    SDL_FillRect(image, nullptr, SDL_MapRGBA(image->format, color.r, color.g, color.b, color.a));
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, image);
    SDL_FreeSurface(image);
    Require(texture != nullptr, SDL_GetError());
    test_textures.push_back(texture);
    return texture;
  }

  std::vector<Uint32> Pixels() {
    std::vector<Uint32> pixels(surface->w * surface->h);
    Require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32,
                                 pixels.data(), surface->w * 4) == 0, SDL_GetError());
    return pixels;
  }
};

void CheckNavigationClip(int w, int h, bool online, bool cached, bool parent_clip) {
  const LayoutMetrics &layout = SelectLayoutProfile(w, h);
  const int boundary = layout.nav_bar_y + layout.nav_bar_h;
  Fixture f(layout);
  Require(f.deps.layout.nav_bar_bottom == boundary, "navigation boundary was not propagated");
  SDL_Texture *card = f.SolidTexture(16, 24, SDL_Color{240, 20, 20, 255});
  SDL_Texture *title = f.SolidTexture(w, 20, SDL_Color{240, 20, 20, 255});
  SDL_Texture *chrome = f.SolidTexture(20, 20, SDL_Color{20, 240, 20, 255});
  f.assets.book_under_shadow = card;
  f.assets.book_select = card;
  f.assets.book_title_shadow = card;
  f.assets.top_status_bar = chrome;
  f.assets.nav_selected_pill = chrome;
  f.deps.get_texture_size = [](SDL_Texture *tex, int &tw, int &th) {
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
  };
  f.deps.get_cover_texture = [card](const BookItem &) { return card; };
  f.deps.get_cached_cover_texture = f.deps.get_cover_texture;
  f.deps.get_text_texture = [title, chrome](const std::string &text, SDL_Color,
                                          int &tw, int &th, SDL_Texture *&tex) {
    tex = text.find("book-") == 0 ? title : chrome;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
  };
  f.deps.online_shelf_active = [online]() { return online; };
  f.deps.renderer_supports_target_textures = cached;
  f.deps.title_marquee_offset = 37;
  SDL_Rect parent{13, 7, w - 26, h - 14};
  SDL_Texture *parent_target = nullptr;
  if (cached && parent_clip) {
    parent_target = SDL_CreateTexture(f.renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, w, h);
    Require(parent_target != nullptr, SDL_GetError());
    f.test_textures.push_back(parent_target);
    Require(SDL_SetRenderTarget(f.renderer, parent_target) == 0, SDL_GetError());
  }
  if (parent_clip) SDL_RenderSetClipRect(f.renderer, &parent);
  auto books = f.runtime.items;
  f.runtime.items.clear();
  f.Draw();
  const auto expected = f.Pixels();
  f.runtime.items = books;
  ++f.deps.shelf_content_version;

  auto check = [&]() {
    Require(SDL_GetRenderTarget(f.renderer) == parent_target, "shelf changed caller render target");
    const auto pixels = f.Pixels();
    for (int y = 0; y < boundary; ++y) {
      for (int x = 0; x < w; ++x) {
        if (pixels[y * w + x] != expected[y * w + x]) {
          f.Save("_clip_failure");
          throw std::runtime_error("navigation pixel mismatch at " + std::to_string(x) + "," +
                                   std::to_string(y) + " expected=" + std::to_string(expected[y * w + x]) +
                                   " actual=" + std::to_string(pixels[y * w + x]) +
                                   " progress=" + std::to_string(f.deps.page_slide_value));
        }
      }
    }
    Require(SDL_RenderIsClipEnabled(f.renderer) == (parent_clip ? SDL_TRUE : SDL_FALSE),
            "shelf changed caller clip enable state");
    if (parent_clip) {
      SDL_Rect actual{};
      SDL_RenderGetClipRect(f.renderer, &actual);
      Require(actual.x == parent.x && actual.y == parent.y &&
                  actual.w == parent.w && actual.h == parent.h,
              "shelf changed caller clip rectangle");
    }
    bool visible = false;
    for (int y = boundary; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        visible = visible || pixels[y * w + x] != expected[y * w + x];
      }
    }
    Require(visible, "shelf clip hid all covers");
    if (parent_clip) {
      for (int y = boundary; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          if (x < parent.x || x >= parent.x + parent.w || y >= parent.y + parent.h) {
            Require(pixels[y * w + x] == expected[y * w + x], "shelf escaped caller clip");
          }
        }
      }
    }
  };
  if (cached) {
    // Force static cards and their focused overlay to straddle the boundary.
    f.deps.layout.grid_start_y = boundary - 30;
    f.deps.animate_enabled = false;
    f.Draw();
    check();
    f.deps.focus_index = 1;
    f.Draw();
    check();
    Require(!f.cache.static_page_textures.empty(), "static cache path was not tested");
  } else {
    f.Draw();
    for (int direction : {1, -1}) {
      f.deps.page_anim_from = direction > 0 ? 0 : 1;
      f.deps.page_anim_to = direction > 0 ? 1 : 0;
      f.deps.shelf_page = f.deps.page_anim_to;
      f.deps.focus_index = f.deps.shelf_page * layout.grid_cols;
      f.deps.page_animating = true;
      for (int frame = 0; frame <= 20; ++frame) {
        f.deps.page_slide_value = frame / 20.0f;
        f.Draw();
        check();
      }
    }
  }
  f.Save(cached ? "_clip_cached" : (online ? "_clip_online" : "_clip_local"));
  SDL_SetRenderTarget(f.renderer, nullptr);
  std::cout << "[pass] navigation clip " << w << "x" << h << " online=" << online
            << " cache=" << cached << " parent=" << parent_clip << "\n";
}

void CheckProfile(int w, int h, float dt) {
  Fixture f(SelectLayoutProfile(w, h));
  f.deps.dt = dt;
  f.deps.focus_index = 3;
  f.Draw();
  const float old_focus_w = f.animations.at(3).w;
  const float normal_w = f.deps.layout.cover_w;
  const float previous_y = f.animations.at(4).cy;
  f.Save("_before");
  f.deps.focus_index = 4;
  f.deps.shelf_page = 1;
  f.deps.page_animating = true;
  f.deps.page_anim_from = 0;
  f.deps.page_anim_to = 1;
  f.deps.page_anim_dir = 1;
  f.deps.page_slide_value = 0;
  f.Draw();
  Require(f.animations.at(3).w < old_focus_w && f.animations.at(3).w > normal_w,
          "row change snapped the outgoing focus scale");
  Require(f.animations.at(4).w > normal_w && f.animations.at(4).w < f.deps.focus_cover_w,
          "row change snapped the incoming focus scale");
  Require(std::abs(f.animations.at(4).cy - previous_y) < 0.01f,
          "overlapping row jumped at the start of scrolling");
  Require(f.active, "focus animation was not kept active");
  for (const auto &draw : f.draws) Require(draw.second == 1, "overlapping row drawn more than once");
  f.Save("_start");
  f.deps.page_slide_value = 0.5f;
  f.Draw();
  const float pitch = f.deps.layout.cover_h + f.deps.layout.grid_gap_y;
  Require(std::abs(f.animations.at(4).cy - (previous_y - pitch * 0.5f)) < 0.01f,
          "row scrolling used a whole-screen offset");
  f.Save("_middle");
  f.deps.page_slide_value = 1;
  f.Draw();
  f.deps.page_animating = false;
  f.Settle();
  f.Save("_after");
  Require(std::abs(f.animations.at(4).w - f.deps.focus_cover_w) <= 0.25f, "focus failed to settle");

  // Reverse direction, then interrupt the scroll with another row move.
  for (int page : {0, 1, 2, 1}) {
    f.deps.page_anim_from = f.deps.shelf_page;
    f.deps.page_anim_to = page;
    f.deps.page_anim_dir = page > f.deps.shelf_page ? 1 : -1;
    f.deps.shelf_page = page;
    f.deps.focus_index = page * 4;
    f.deps.page_animating = true;
    f.deps.page_slide_value = 0.2f;
    f.Draw();
    for (const auto &draw : f.draws) Require(draw.second == 1, "repeated navigation duplicated a cover");
  }
  // Last, incomplete row and disabled animations must still snap immediately.
  f.deps.animate_enabled = false;
  f.deps.page_animating = false;
  f.deps.shelf_page = 4;
  f.deps.focus_index = 18;
  f.Draw();
  Require(f.draws.size() == 3, "partial last row drew invalid items");
  Require(!f.active && f.animations.at(18).w == f.deps.focus_cover_w,
          "disabled animations did not snap");
  std::cout << "[pass] shelf " << w << "x" << h << " dt=" << dt
            << ": scaling, row overlap, reverse, repeat, last row, disabled\n";
}

float HorizontalDuration(int w, int h) {
  Fixture f(SelectLayoutProfile(w, h));
  f.Draw();
  f.deps.focus_index = 1;
  float elapsed = 0;
  do {
    f.Draw();
    elapsed += f.deps.dt;
    Require(elapsed < 3, "horizontal animation never settled");
  } while (f.active);
  return elapsed;
}

void CheckInput() {
  InputManager input("", InputProfile::DesktopDefault);
  ShelfScene scene;
  ShelfSceneState state;
  ShelfRuntimeState runtime;
  runtime.items.resize(19);
  state.focus_index = 3;
  ShelfSceneInputContext context{input, runtime, state, 4, 1.0f / 30.0f, true, 0.18f, 1, 10, {}};
  auto press = [&](SDL_Keycode key, int expected_focus, int expected_page, bool repeat = false) {
    input.ResetAll();
    input.BeginFrame(context.dt);
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    input.HandleEvent(event);
    input.EndFrame();
    scene.HandleInput(context);
    if (repeat) {
      input.BeginFrame(1.0f);
      input.EndFrame();
      scene.HandleInput(context);
    }
    if (state.focus_index != expected_focus || state.shelf_page != expected_page) {
      throw std::runtime_error("input focus=" + std::to_string(state.focus_index) +
                               " page=" + std::to_string(state.shelf_page) +
                               " expected=" + std::to_string(expected_focus) + "/" + std::to_string(expected_page));
    }
  };
  press(SDLK_RIGHT, 4, 1);
  Require(state.page_animating && state.page_slide.IsAnimating(), "right wrap did not animate");
  state.page_slide.Update(0.09f);
  Require(std::abs(state.page_slide.Value() - 0.875f) < 0.001f,
          "page transition did not use cubic ease-out");
  state.page_slide.Update(0.10f);
  Require(!state.page_slide.IsAnimating() && state.page_slide.Value() == 1.0f,
          "page transition did not finish within the reference duration");
  press(SDLK_LEFT, 3, 0);
  scene.TickAnimations(state, 2.0f, true);
  Require(state.page_animating && state.page_slide.Value() > 0 && state.page_slide.Value() < 1,
          "idle/cover stall consumed the entire row animation");
  for (int frame = 0; frame < 12; ++frame) scene.TickAnimations(state, 1.0f / 60.0f, true);
  Require(!state.page_animating, "bounded row animation never completed");
  press(SDLK_DOWN, 7, 1);
  press(SDLK_UP, 3, 0);
  press(SDLK_DOWN, 11, 2, true);
  context.animations_enabled = false;
  press(SDLK_DOWN, 15, 3);
  Require(!state.page_animating, "disabled navigation still animated");
  press(SDLK_DOWN, 18, 4);
  press(SDLK_RIGHT, 18, 4);
  std::cout << "[pass] input: horizontal wrap, vertical navigation, held repeat, boundaries, disabled\n";
}

void CheckConfig() {
  const char *path = "build/shelf_animation_config.ini";
  for (int enabled : {1, 0}) {
    {
      std::ofstream file(path, std::ios::binary);
      file << "animations=" << enabled << " \r\n"
           << "audio=1\r\nlid_close_screen_off=1\r\n";
    }
    ConfigStore config(path);
    Require(config.Get().animations == (enabled == 1), "CRLF animation setting read incorrectly");
    Require(config.Get().audio && config.Get().lid_close_screen_off, "CRLF boolean setting read incorrectly");
    config.Save();
    ConfigStore reopened(path);
    Require(reopened.Get().animations == (enabled == 1), "animation setting changed on save/reopen");
  }
  std::cout << "[pass] animation config: CRLF, trailing whitespace, save/reopen, explicit disabled\n";
}
}

int main(int, char **) {
  try {
    Require(SDL_Init(SDL_INIT_TIMER) == 0, SDL_GetError());
    CheckConfig();
    CheckInput();
    for (const auto size : {std::pair<int, int>{640, 480}, {720, 480}, {720, 720}, {1024, 768}, {1600, 1440}}) {
      for (float dt : {1.0f / 60.0f, 1.0f / 30.0f}) CheckProfile(size.first, size.second, dt);
      for (bool parent : {false, true}) {
        CheckNavigationClip(size.first, size.second, false, false, parent);
        CheckNavigationClip(size.first, size.second, true, false, parent);
        CheckNavigationClip(size.first, size.second, false, true, parent);
      }
    }
    const float reference = HorizontalDuration(1600, 1440);
    for (const auto size : {std::pair<int, int>{640, 480}, {720, 480}, {1024, 768}}) {
      const float duration = HorizontalDuration(size.first, size.second);
      Require(std::abs(duration - reference) < 0.10f, "small displays animate faster than GKD");
      std::cout << "[pass] duration " << size.first << "x" << size.second << ": "
                << duration << "s (GKD " << reference << "s)\n";
    }
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[FAIL] " << error.what() << "\n";
    SDL_Quit();
    return 1;
  }
}
