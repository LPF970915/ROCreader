#include "rgds_render.h"

#include "image_runtime_tuning.h"
#include "runtime_log.h"
#include "sdl_utils.h"

#include <algorithm>
#include <vector>

namespace rgds {
namespace {

void OpenBottomFonts(RenderResources &resources) {
  OpenUiFonts(resources.bottom_text_cache, resources.exe_path, resources.ui_path, resources.body_font_pt,
              resources.title_font_pt, resources.reader_font_pt);
}

TextCacheEntry *GetBottomTextTexture(RenderResources &resources, SDL_Renderer *renderer, const std::string &text,
                                     SDL_Color color, UiTextRole role) {
  OpenBottomFonts(resources);
  return GetUiTextTexture(resources.bottom_text_cache, renderer, text, color, role);
}

SettingsRuntimeRenderServices MakeBottomMenuRenderServices(const BottomRenderDeps &deps) {
  SDL_Renderer *renderer = deps.runtime.bottom_renderer;
  return MakeMenuSceneRenderServices(MenuSceneRenderServiceCallbacks{
      [renderer](int x, int y, int w, int h, SDL_Color c, bool filled) {
        DrawRect(renderer, x, y, w, h, c, filled);
      },
      deps.get_texture_size,
      [&deps](const std::string &text, SDL_Color color) -> TextCacheEntry * {
        return GetBottomTextTexture(deps.resources, deps.runtime.bottom_renderer, text, color, UiTextRole::Body);
      },
      [&deps](const std::string &text, SDL_Color color) -> TextCacheEntry * {
        return GetBottomTextTexture(deps.resources, deps.runtime.bottom_renderer, text, color, UiTextRole::Title);
      },
      [&deps](const std::string &text, SDL_Color color) -> TextCacheEntry * {
        return GetBottomTextTexture(deps.resources, deps.runtime.bottom_renderer, text, color, UiTextRole::Reader);
      },
      deps.utf8_ellipsize,
      []() {},
  });
}

void DrawBottomMenu(const BottomRenderDeps &deps) {
  MenuSceneRenderContext menu_render_context{
      deps.runtime.bottom_renderer,
      deps.resources.bottom_ui_assets,
      deps.cfg,
      deps.input_profile,
      deps.menu_state,
      deps.sidebar_mask_max_alpha,
      deps.txt_transcode_job,
      deps.system_settings_state,
      deps.txt_settings_state,
      deps.contributor_avatar_entries ? *deps.contributor_avatar_entries : deps.resources.bottom_contributor_avatar_entries,
      deps.contributor_avatar_state,
      deps.key_calibration_state,
      deps.has_calibrated_keymap,
      deps.version_update_state,
      deps.online_source_state,
      deps.menu_layout,
      MakeBottomMenuRenderServices(deps),
      false,
  };
  deps.menu_scene.Draw(menu_render_context);
}

float FocusFlashAlpha(uint32_t now, uint32_t flash_until) {
  if (flash_until == 0 || SDL_TICKS_PASSED(now, flash_until)) return 0.0f;
  constexpr float kFlashDurationMs = 450.0f;
  const float t = static_cast<float>(flash_until - now) / kFlashDurationMs;
  const float breathe = 0.72f + 0.28f * std::sin(t * 3.1415926f * 2.0f);
  return std::clamp(t * breathe, 0.0f, 1.0f);
}

void StrokeFocusFrame(SDL_Renderer *renderer, float alpha) {
  if (!renderer || alpha <= 0.001f) return;
  const Uint8 line_alpha = static_cast<Uint8>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 120, 210, 255, line_alpha);
  for (int i = 0; i < 3; ++i) {
    SDL_Rect border{i, i, std::max(1, ScreenW() - i * 2), std::max(1, ScreenH() - i * 2)};
    SDL_RenderDrawRect(renderer, &border);
  }
}

void SyncReaderCanvasToBottom(Runtime &runtime, SDL_Renderer *top_renderer, const ReaderLayout &reader_layout) {
  if (!runtime.reader_canvas || !runtime.bottom_reader_canvas || runtime.stacked_preview) return;
  if (runtime.spanning) return;
  if (!top_renderer || runtime.bottom_reader_canvas == runtime.reader_canvas) return;

  const SDL_Rect rect{0, 0, reader_layout.canvas_w, reader_layout.canvas_h};
  if (rect.w <= 0 || rect.h <= 0) return;
  std::vector<unsigned char> pixels(static_cast<size_t>(rect.w) * static_cast<size_t>(rect.h) * 4u);
  const int pitch = rect.w * 4;
  const int read_result = SDL_RenderReadPixels(top_renderer, &rect, SDL_PIXELFORMAT_RGBA8888,
                                               pixels.data(), pitch);
  if (read_result != 0) {
    runtime_log::Line(std::string("main: RGDS bottom reader sync read failed: ") + SDL_GetError());
    return;
  }
  if (SDL_UpdateTexture(runtime.bottom_reader_canvas, &rect, pixels.data(), pitch) != 0) {
    runtime_log::Line(std::string("main: RGDS bottom reader sync update failed: ") + SDL_GetError());
  }
}

void CopyTopReaderCanvas(SDL_Renderer *renderer, SDL_Texture *reader_canvas, const ReaderLayout &reader_layout) {
  if (!renderer || !reader_canvas) return;
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_Rect top_dst{0, 0, ScreenW(), ScreenH()};
  SDL_RenderCopy(renderer, reader_canvas, &reader_layout.top_src, &top_dst);
}

bool HasCompatibleReaderCanvasFrame(const Runtime &runtime, const ReaderLayout &reader_layout) {
  return runtime.reader_canvas_content_valid &&
         runtime.reader_canvas_content_w == reader_layout.canvas_w &&
         runtime.reader_canvas_content_h == reader_layout.canvas_h;
}

void MarkReaderCanvasFrame(Runtime &runtime, const ReaderLayout &reader_layout) {
  runtime.reader_canvas_content_w = reader_layout.canvas_w;
  runtime.reader_canvas_content_h = reader_layout.canvas_h;
  runtime.reader_canvas_content_valid = true;
}

void ClearReaderCanvasForLayout(Runtime &runtime, SDL_Renderer *renderer, const ReaderLayout &reader_layout) {
  if (!runtime.reader_canvas || !renderer) return;
  SDL_SetRenderTarget(renderer, runtime.reader_canvas);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_Rect rect{0, 0, reader_layout.canvas_w, reader_layout.canvas_h};
  SDL_RenderFillRect(renderer, &rect);
  MarkReaderCanvasFrame(runtime, reader_layout);
}

} // namespace

void LoadRenderResources(RenderResources &resources, RenderResourceLoadDeps deps) {
  resources.exe_path = deps.exe_path;
  resources.ui_path = deps.ui_path;
  resources.body_font_pt = deps.body_font_pt;
  resources.title_font_pt = deps.title_font_pt;
  resources.reader_font_pt = deps.reader_font_pt;
  resources.bottom_text_cache.max_text_cache_entries = deps.max_text_cache_entries;

  if (!deps.runtime.bottom_renderer) return;
  UiAssetsLoaderDeps loader_deps{
      deps.runtime.bottom_renderer,
      deps.exe_path,
      deps.ui_profile_name,
      deps.load_texture_from_file,
      deps.load_surface_from_memory,
      deps.create_texture_from_surface,
      deps.remember_texture_size,
  };
  LoadUiAssets(resources.bottom_ui_assets, loader_deps);
  LoadContributorAvatarEntries(resources.bottom_contributor_avatar_entries,
                               deps.ui_path / "common",
                               deps.exe_path,
                               deps.runtime.bottom_renderer,
                               deps.language_index,
                               deps.avatar_texture_size,
                               deps.load_surface_from_memory,
                               deps.remember_texture_size,
                               deps.before_destroy_texture);
}

void DestroyRenderResources(RenderResources &resources, const BeforeDestroyTextureFn &before_destroy) {
  DestroyContributorAvatarEntries(resources.bottom_contributor_avatar_entries, before_destroy);
  DestroyUiAssets(resources.bottom_ui_assets, before_destroy);
  ShutdownUiTextCache(resources.bottom_text_cache, before_destroy);
}

void DrawFocusFlash(SDL_Renderer *renderer, uint32_t now, const InteractionState &interaction, bool top_screen) {
  if (interaction.focus_top != top_screen) return;
  StrokeFocusFrame(renderer, FocusFlashAlpha(now, interaction.focus_flash_until));
}

void DrawTopReaderSlice(Runtime &runtime, SDL_Renderer *renderer, const ReaderScene &reader_scene,
                        ReaderSceneRenderDeps reader_render_deps, const ReaderLayout &reader_layout) {
  if (!runtime.reader_canvas || !renderer) {
    reader_scene.Draw(reader_render_deps);
    return;
  }
  if (reader_layout.mode == ReaderLayoutMode::HorizontalSpread && reader_render_deps.reader_manager) {
    IReaderModule *module = reader_render_deps.reader_manager->Module(reader_render_deps.ui.mode);
    if (module && module->IsOpen() && IsImageReaderMode(reader_render_deps.ui.mode, module) && module->PageCount() > 0) {
      module->UpdateViewport(ScreenW(), ScreenH());
      if (reader_render_deps.tick_modules) module->Tick(reader_render_deps.dt);
      const SDL_Rect left_dst{0, 0, ScreenW(), ScreenH()};
      const SDL_Rect right_dst{ScreenW(), 0, ScreenW(), ScreenH()};
      const int page = std::clamp(module->CurrentPage(), 0, std::max(0, module->PageCount() - 1));
      const bool right_exists = page + 1 < module->PageCount();
      const int prefetch_pages = image_runtime_tuning::HorizontalSpreadPrefetchPages(2);
      for (int ahead = 1; ahead <= prefetch_pages; ++ahead) {
        const int next_page = page + ahead;
        if (next_page >= module->PageCount()) break;
        module->PrefetchPageAt(next_page);
      }

      const bool left_ready = module->CanDrawPageAt(page);
      const bool right_ready = !right_exists || module->CanDrawPageAt(page + 1);
      if (!left_ready) {
        if (!HasCompatibleReaderCanvasFrame(runtime, reader_layout)) {
          ClearReaderCanvasForLayout(runtime, renderer, reader_layout);
        }
        SyncReaderCanvasToBottom(runtime, renderer, reader_layout);
        CopyTopReaderCanvas(renderer, runtime.reader_canvas, reader_layout);
        return;
      }

      SDL_SetRenderTarget(renderer, runtime.reader_canvas);
      SDL_Rect clear_left{0, 0, ScreenW(), ScreenH()};
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderFillRect(renderer, &clear_left);
      module->DrawPageAt(renderer, page, left_dst);
      if (right_exists) {
        if (right_ready) {
          SDL_Rect clear_right{ScreenW(), 0, ScreenW(), ScreenH()};
          SDL_RenderFillRect(renderer, &clear_right);
          module->DrawPageAt(renderer, page + 1, right_dst);
        }
      }
      MarkReaderCanvasFrame(runtime, reader_layout);
      SyncReaderCanvasToBottom(runtime, renderer, reader_layout);
      CopyTopReaderCanvas(renderer, runtime.reader_canvas, reader_layout);
      return;
    }
  }
  SDL_SetRenderTarget(renderer, runtime.reader_canvas);
  reader_render_deps.renderer = renderer;
  reader_render_deps.screen_w = reader_layout.canvas_w;
  reader_render_deps.screen_h = reader_layout.canvas_h;
  reader_scene.Draw(reader_render_deps);
  MarkReaderCanvasFrame(runtime, reader_layout);
  SyncReaderCanvasToBottom(runtime, renderer, reader_layout);
  CopyTopReaderCanvas(renderer, runtime.reader_canvas, reader_layout);
}

void DrawBottomScreen(const BottomRenderDeps &deps) {
  if (!IsActive(deps.runtime)) return;
  SDL_Renderer *renderer = deps.runtime.bottom_renderer;
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_Rect previous_viewport{};
  SDL_Rect previous_clip{};
  const SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
  SDL_RenderGetViewport(renderer, &previous_viewport);
  SDL_RenderGetClipRect(renderer, &previous_clip);
  if (deps.runtime.stacked_preview) {
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_Rect bottom_viewport{0, ScreenH(), ScreenW(), ScreenH()};
    SDL_RenderSetViewport(renderer, &bottom_viewport);
  } else if (deps.runtime.spanning) {
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_Rect bottom_viewport{ScreenW(), 0, ScreenW(), ScreenH()};
    SDL_RenderSetViewport(renderer, &bottom_viewport);
  }
  if (deps.scene == AppScene::Boot) {
    SDL_SetRenderDrawColor(renderer, 26, 27, 31, 255);
    if (deps.runtime.stacked_preview || deps.runtime.spanning) {
      SDL_Rect rect{0, 0, ScreenW(), ScreenH()};
      SDL_RenderFillRect(renderer, &rect);
      SDL_RenderSetViewport(renderer, &previous_viewport);
      SDL_RenderSetClipRect(renderer, had_clip ? &previous_clip : nullptr);
    } else {
      SDL_RenderClear(renderer);
    }
    return;
  }

  SDL_SetRenderDrawColor(renderer, 22, 32, 42, 255);
  if (deps.runtime.stacked_preview || deps.runtime.spanning) {
    SDL_Rect rect{0, 0, ScreenW(), ScreenH()};
    SDL_RenderFillRect(renderer, &rect);
  } else {
    SDL_RenderClear(renderer);
  }
  if (deps.scene == AppScene::Reader) {
    if (ReaderMenuVisible(deps.interaction)) {
      DrawBottomMenu(deps);
    } else if (deps.render_reader_scene_on_bottom && deps.reader_scene && deps.reader_render_deps) {
      ReaderSceneRenderDeps bottom_reader_render_deps = *deps.reader_render_deps;
      bottom_reader_render_deps.renderer = renderer;
      bottom_reader_render_deps.screen_w = deps.reader_layout.canvas_w;
      bottom_reader_render_deps.screen_h = deps.reader_layout.canvas_h;
      bottom_reader_render_deps.overlay_viewport = deps.reader_layout.overlay_viewport;
      bottom_reader_render_deps.overlay_viewport_enabled = true;
      bottom_reader_render_deps.tick_modules = false;
      bottom_reader_render_deps.services = MakeReaderSceneRenderServices(
          renderer,
          deps.reader_render_deps->services.scale_px,
          [renderer](int x, int y, int w, int h, SDL_Color c, bool filled) {
            DrawRect(renderer, x, y, w, h, c, filled);
          },
          deps.reader_render_deps->services.clamp_text_scroll,
          [&deps, renderer](const std::string &text, SDL_Color color) -> TextCacheEntry * {
            return GetBottomTextTexture(deps.resources, renderer, text, color, UiTextRole::Body);
          },
          [&deps, renderer](const std::string &text, SDL_Color color) -> TextCacheEntry * {
            return GetBottomTextTexture(deps.resources, renderer, text, color, UiTextRole::Reader);
          });
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      deps.reader_scene->Draw(bottom_reader_render_deps);
    } else if ((deps.runtime.stacked_preview || deps.runtime.spanning) && deps.runtime.reader_canvas) {
      SDL_Rect bottom_dst{0, 0, ScreenW(), ScreenH()};
      SDL_RenderCopy(renderer, deps.runtime.reader_canvas, &deps.reader_layout.bottom_src, &bottom_dst);
    } else if (deps.runtime.bottom_reader_canvas) {
      SDL_Rect bottom_dst{0, 0, ScreenW(), ScreenH()};
      SDL_RenderCopy(renderer, deps.runtime.bottom_reader_canvas, &deps.reader_layout.bottom_src, &bottom_dst);
    }
  } else if (deps.scene == AppScene::Shelf) {
    DrawBottomMenu(deps);
  }
  DrawFocusFlash(renderer, deps.now, deps.interaction, false);
  if (deps.runtime.stacked_preview || deps.runtime.spanning) {
    SDL_RenderSetViewport(renderer, &previous_viewport);
    SDL_RenderSetClipRect(renderer, had_clip ? &previous_clip : nullptr);
  }
}

} // namespace rgds
