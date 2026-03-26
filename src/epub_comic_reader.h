#pragma once

#include <string>
#include <vector>

class EpubComicReader {
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

  bool PageSize(int page_index, int &w, int &h);
  bool CurrentPageSize(int &w, int &h);
  bool RenderPageRGBA(int page_index, float scale, std::vector<unsigned char> &rgba, int &w, int &h);
  bool RenderCurrentPageRGBA(float scale, std::vector<unsigned char> &rgba, int &w, int &h);

private:
#ifdef HAVE_LIBZIP
  struct Impl;
  Impl *impl_ = nullptr;
#else
  std::string path_;
  int page_count_ = 0;
  int current_page_ = 0;
#endif
};
