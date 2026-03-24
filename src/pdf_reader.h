#pragma once

#include <SDL.h>

#include <string>
#include <vector>

class PdfReader {
public:
  bool Open(const std::string &path);
  void Close();
  bool IsOpen() const;
  bool HasRealRenderer() const;
  const char *BackendName() const;

  int PageCount() const;
  int CurrentPage() const;
  void SetPage(int page_index);
  void NextPage();
  void PrevPage();

  // Base page size before rotation.
  bool PageSize(int page_index, int &w, int &h) const;
  bool CurrentPageSize(int &w, int &h) const;

  // Render page RGBA buffer at scale (before rotation). Caller creates SDL texture.
  bool RenderPageRGBA(int page_index, float scale, std::vector<unsigned char> &rgba, int &w, int &h);
  bool RenderCurrentPageRGBA(float scale, std::vector<unsigned char> &rgba, int &w, int &h);

private:
#if defined(HAVE_MUPDF) || defined(HAVE_POPPLER)
  struct Impl;
  Impl *impl_ = nullptr;
#else
  // Fallback mock mode when MuPDF is unavailable at build time.
  std::string path_;
  int page_count_ = 0;
  int current_page_ = 0;
#endif
};
