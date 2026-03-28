#pragma once

#include "epub_comic_reader.h"
#include "input_manager.h"
#include "reader_core.h"

#include <SDL.h>

#include <functional>
#include <string>

struct EpubRuntimeProgress {
  int page = 0;
  int rotation = 0;
  float zoom = 1.0f;
  int scroll_x = 0;
  int scroll_y = 0;
};

class EpubRuntime {
public:
  EpubRuntime();
  ~EpubRuntime();

  bool Open(SDL_Renderer *renderer, const std::string &path, int screen_w, int screen_h,
            const EpubRuntimeProgress &initial_progress);
  void Close();

  bool IsOpen() const;
  bool HasRealRenderer() const;
  const char *BackendName() const;
  bool IsRenderPending() const;

  void UpdateViewport(int screen_w, int screen_h);
  void Tick();
  void CommitPendingNavigation();
  void Draw(SDL_Renderer *renderer,
            const std::function<void(const std::string &)> &draw_loading_label,
            const std::function<void(const std::string &)> &draw_fast_flip_label);
  void HandleInput(const InputManager &input, float dt, int tap_step_px);

  int PageCount() const;
  bool PageSize(int page_index, int &w, int &h) const;
  int CurrentPage() const;
  EpubRuntimeProgress Progress() const;

private:
  struct Impl;
  Impl *impl_ = nullptr;
};
