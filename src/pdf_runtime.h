#pragma once

#include <SDL.h>

#include <string>

struct PdfRuntimeProgress {
  int page = 0;
  int rotation = 0;
  float zoom = 1.0f;
  int scroll_y = 0;
};

class PdfRuntime {
public:
  PdfRuntime();
  ~PdfRuntime();

  bool Open(SDL_Renderer *renderer, const std::string &path, int screen_w, int screen_h,
            const PdfRuntimeProgress &initial_progress);
  void Close();

  bool IsOpen() const;
  bool HasRealRenderer() const;

  void UpdateViewport(int screen_w, int screen_h);
  void Tick();
  void Draw(SDL_Renderer *renderer) const;

  void RotateLeft();
  void RotateRight();
  void ZoomOut();
  void ZoomIn();
  void ResetView();
  void ScrollByPixels(int delta_px);
  void JumpByScreen(int direction);

  int PageCount() const;
  bool PageSize(int page_index, int &w, int &h) const;
  int CurrentPage() const;
  PdfRuntimeProgress Progress() const;

private:
  struct Impl;
  Impl *impl_ = nullptr;
};
