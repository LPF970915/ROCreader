#include "pdf_reader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

#if defined(HAVE_MUPDF)
#include <mupdf/fitz.h>

struct PdfReader::Impl {
  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;
  int page_count = 0;
  int current_page = 0;
  std::string path;
};

#elif defined(HAVE_POPPLER)
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-page-renderer.h>

struct PdfReader::Impl {
  std::unique_ptr<poppler::document> doc;
  int page_count = 0;
  int current_page = 0;
  std::string path;
};
#endif

bool PdfReader::Open(const std::string &path) {
  Close();
#if defined(HAVE_MUPDF)
  impl_ = new Impl();
  impl_->ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
  if (!impl_->ctx) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  fz_try(impl_->ctx) {
    fz_register_document_handlers(impl_->ctx);
    impl_->doc = fz_open_document(impl_->ctx, path.c_str());
    impl_->page_count = fz_count_pages(impl_->ctx, impl_->doc);
    impl_->current_page = 0;
    impl_->path = path;
  }
  fz_catch(impl_->ctx) {
    Close();
    return false;
  }
  return impl_->doc != nullptr && impl_->page_count > 0;
#elif defined(HAVE_POPPLER)
  if (!std::filesystem::exists(path)) return false;
  impl_ = new Impl();
  impl_->doc.reset(poppler::document::load_from_file(path));
  if (!impl_->doc) {
    Close();
    return false;
  }
  impl_->page_count = impl_->doc->pages();
  impl_->current_page = 0;
  impl_->path = path;
  return impl_->page_count > 0;
#else
  if (!std::filesystem::exists(path)) return false;
  path_ = path;
  page_count_ = 200; // Mock count in fallback mode
  current_page_ = 0;
  return true;
#endif
}

void PdfReader::Close() {
#if defined(HAVE_MUPDF)
  if (!impl_) return;
  if (impl_->doc) {
    fz_drop_document(impl_->ctx, impl_->doc);
    impl_->doc = nullptr;
  }
  if (impl_->ctx) {
    fz_drop_context(impl_->ctx);
    impl_->ctx = nullptr;
  }
  delete impl_;
  impl_ = nullptr;
#elif defined(HAVE_POPPLER)
  if (!impl_) return;
  impl_->doc.reset();
  delete impl_;
  impl_ = nullptr;
#else
  path_.clear();
  page_count_ = 0;
  current_page_ = 0;
#endif
}

bool PdfReader::IsOpen() const {
#if defined(HAVE_MUPDF)
  return impl_ && impl_->doc && impl_->page_count > 0;
#elif defined(HAVE_POPPLER)
  return impl_ && impl_->doc && impl_->page_count > 0;
#else
  return !path_.empty() && page_count_ > 0;
#endif
}

bool PdfReader::HasRealRenderer() const {
#if defined(HAVE_MUPDF) || defined(HAVE_POPPLER)
  return true;
#else
  return false;
#endif
}

const char *PdfReader::BackendName() const {
#if defined(HAVE_MUPDF)
  return "MuPDF";
#elif defined(HAVE_POPPLER)
  return "Poppler";
#else
  return "Mock";
#endif
}

int PdfReader::PageCount() const {
#if defined(HAVE_MUPDF) || defined(HAVE_POPPLER)
  return impl_ ? impl_->page_count : 0;
#else
  return page_count_;
#endif
}

int PdfReader::CurrentPage() const {
#if defined(HAVE_MUPDF) || defined(HAVE_POPPLER)
  return impl_ ? impl_->current_page : 0;
#else
  return current_page_;
#endif
}

void PdfReader::SetPage(int page_index) {
  const int count = PageCount();
  if (count <= 0) return;
  page_index = std::clamp(page_index, 0, count - 1);
#if defined(HAVE_MUPDF) || defined(HAVE_POPPLER)
  impl_->current_page = page_index;
#else
  current_page_ = page_index;
#endif
}

void PdfReader::NextPage() { SetPage(CurrentPage() + 1); }
void PdfReader::PrevPage() { SetPage(CurrentPage() - 1); }

bool PdfReader::PageSize(int page_index, int &w, int &h) const {
  if (!IsOpen()) return false;
  page_index = std::clamp(page_index, 0, PageCount() - 1);
#if defined(HAVE_MUPDF)
  fz_rect bounds{};
  fz_try(impl_->ctx) {
    fz_page *page = fz_load_page(impl_->ctx, impl_->doc, page_index);
    bounds = fz_bound_page(impl_->ctx, page);
    fz_drop_page(impl_->ctx, page);
  }
  fz_catch(impl_->ctx) {
    return false;
  }
  w = std::max(1, static_cast<int>(std::round(bounds.x1 - bounds.x0)));
  h = std::max(1, static_cast<int>(std::round(bounds.y1 - bounds.y0)));
  return true;
#elif defined(HAVE_POPPLER)
  std::unique_ptr<poppler::page> page(impl_->doc->create_page(page_index));
  if (!page) return false;
  poppler::rectf rect = page->page_rect();
  w = std::max(1, static_cast<int>(std::round(rect.width())));
  h = std::max(1, static_cast<int>(std::round(rect.height())));
  return true;
#else
  w = 1200;
  h = 1700;
  return true;
#endif
}

bool PdfReader::CurrentPageSize(int &w, int &h) const { return PageSize(CurrentPage(), w, h); }

bool PdfReader::RenderPageRGBA(int page_index, float scale, std::vector<unsigned char> &rgba, int &w, int &h,
                               const std::atomic<bool> *cancel) {
  if (!IsOpen()) return false;
  page_index = std::clamp(page_index, 0, PageCount() - 1);
  scale = std::max(0.1f, scale);
  if (cancel && cancel->load()) return false;

#if defined(HAVE_MUPDF)
  fz_pixmap *pix = nullptr;
  fz_device *dev = nullptr;
  fz_page *page = nullptr;
  fz_rect bounds{};
  fz_irect ibounds{};
  fz_matrix m = fz_scale(scale, scale);

  fz_try(impl_->ctx) {
    page = fz_load_page(impl_->ctx, impl_->doc, page_index);
    bounds = fz_bound_page(impl_->ctx, page);
    bounds = fz_transform_rect(bounds, m);
    ibounds = fz_round_rect(bounds);
    pix = fz_new_pixmap_with_bbox(impl_->ctx, fz_device_rgb(impl_->ctx), ibounds, nullptr, 0);
    fz_clear_pixmap_with_value(impl_->ctx, pix, 255);
    dev = fz_new_draw_device(impl_->ctx, m, pix);
    fz_run_page(impl_->ctx, page, dev, fz_identity, nullptr);
    fz_close_device(impl_->ctx, dev);

    w = fz_pixmap_width(impl_->ctx, pix);
    h = fz_pixmap_height(impl_->ctx, pix);
    const int stride = fz_pixmap_stride(impl_->ctx, pix);
    const unsigned char *samples = fz_pixmap_samples(impl_->ctx, pix);
    rgba.assign(static_cast<size_t>(w * h * 4), 255);
    for (int y = 0; y < h; ++y) {
      if (cancel && cancel->load()) return false;
      const unsigned char *row = samples + y * stride;
      for (int x = 0; x < w; ++x) {
        const int si = x * 3;
        const int di = (y * w + x) * 4;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      }
    }
  }
  fz_always(impl_->ctx) {
    if (dev) fz_drop_device(impl_->ctx, dev);
    if (pix) fz_drop_pixmap(impl_->ctx, pix);
    if (page) fz_drop_page(impl_->ctx, page);
  }
  fz_catch(impl_->ctx) {
    return false;
  }
  return true;
#elif defined(HAVE_POPPLER)
  std::unique_ptr<poppler::page> page(impl_->doc->create_page(page_index));
  if (!page) return false;
  poppler::page_renderer renderer;
  renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
  renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
  const double dpi = 72.0 * static_cast<double>(scale);
  if (cancel && cancel->load()) return false;
  poppler::image img = renderer.render_page(page.get(), dpi, dpi);
  if (!img.is_valid()) return false;

  w = img.width();
  h = img.height();
  if (w <= 0 || h <= 0) return false;
  rgba.assign(static_cast<size_t>(w * h * 4), 255);

  const unsigned char *src = reinterpret_cast<const unsigned char *>(img.data());
  const int stride = img.bytes_per_row();
  const poppler::image::format_enum fmt = img.format();
  for (int y = 0; y < h; ++y) {
    if (cancel && cancel->load()) return false;
    const unsigned char *row = src + y * stride;
    for (int x = 0; x < w; ++x) {
      const int di = (y * w + x) * 4;
      if (fmt == poppler::image::format_argb32) {
        // Little-endian memory: B G R A.
        const int si = x * 4;
        rgba[di + 0] = row[si + 2];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 0];
        rgba[di + 3] = row[si + 3];
      } else if (fmt == poppler::image::format_rgb24) {
        const int si = x * 3;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      } else {
        const int si = x * 4;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      }
    }
  }
  return true;
#else
  // Placeholder rendering in fallback mode.
  int bw = 1200;
  int bh = 1700;
  w = std::max(1, static_cast<int>(std::round(bw * scale)));
  h = std::max(1, static_cast<int>(std::round(bh * scale)));
  rgba.assign(static_cast<size_t>(w * h * 4), 255);
  for (int y = 0; y < h; ++y) {
    const bool stripe = ((y / 24) % 2) == 0;
    for (int x = 0; x < w; ++x) {
      const int i = (y * w + x) * 4;
      unsigned char v = stripe ? 245 : 232;
      if (x < 10 || y < 10 || x >= w - 10 || y >= h - 10) v = 180;
      rgba[i + 0] = v;
      rgba[i + 1] = v;
      rgba[i + 2] = v;
      rgba[i + 3] = 255;
    }
  }
  return true;
#endif
}

bool PdfReader::RenderPageRegionRGBA(int page_index, float scale, int src_x, int src_y, int src_w, int src_h,
                                     std::vector<unsigned char> &rgba, int &w, int &h,
                                     const std::atomic<bool> *cancel) {
  if (!IsOpen()) return false;
  page_index = std::clamp(page_index, 0, PageCount() - 1);
  scale = std::max(0.1f, scale);
  src_x = std::max(0, src_x);
  src_y = std::max(0, src_y);
  src_w = std::max(1, src_w);
  src_h = std::max(1, src_h);
  if (cancel && cancel->load()) return false;

#if defined(HAVE_POPPLER)
  std::unique_ptr<poppler::page> page(impl_->doc->create_page(page_index));
  if (!page) return false;
  poppler::page_renderer renderer;
  renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
  renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
  const double dpi = 72.0 * static_cast<double>(scale);
  if (cancel && cancel->load()) return false;
  // The strip scheduler computes crop rectangles in scaled output pixels. poppler-cpp expects
  // the crop rectangle in the page's original coordinate space, so convert it back first.
  const double inv_scale = 1.0 / static_cast<double>(scale);
  const int crop_x = std::max(0, static_cast<int>(std::floor(static_cast<double>(src_x) * inv_scale)));
  const int crop_y = std::max(0, static_cast<int>(std::floor(static_cast<double>(src_y) * inv_scale)));
  const int crop_w = std::max(1, static_cast<int>(std::ceil(static_cast<double>(src_w) * inv_scale)));
  const int crop_h = std::max(1, static_cast<int>(std::ceil(static_cast<double>(src_h) * inv_scale)));
  poppler::image img = renderer.render_page(page.get(), dpi, dpi, crop_x, crop_y, crop_w, crop_h);
  if (!img.is_valid()) return false;

  w = img.width();
  h = img.height();
  if (w <= 0 || h <= 0) return false;
  rgba.assign(static_cast<size_t>(w * h * 4), 255);

  const unsigned char *src = reinterpret_cast<const unsigned char *>(img.data());
  const int stride = img.bytes_per_row();
  const poppler::image::format_enum fmt = img.format();
  for (int y = 0; y < h; ++y) {
    if (cancel && cancel->load()) return false;
    const unsigned char *row = src + y * stride;
    for (int x = 0; x < w; ++x) {
      const int di = (y * w + x) * 4;
      if (fmt == poppler::image::format_argb32) {
        const int si = x * 4;
        rgba[di + 0] = row[si + 2];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 0];
        rgba[di + 3] = row[si + 3];
      } else if (fmt == poppler::image::format_rgb24) {
        const int si = x * 3;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      } else {
        const int si = x * 4;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      }
    }
  }
  return true;
#elif defined(HAVE_MUPDF)
  fz_pixmap *pix = nullptr;
  fz_device *dev = nullptr;
  fz_page *page = nullptr;
  fz_rect bounds{};
  fz_irect page_bounds{};
  fz_irect crop_bounds{};
  fz_matrix m = fz_scale(scale, scale);

  fz_try(impl_->ctx) {
    page = fz_load_page(impl_->ctx, impl_->doc, page_index);
    bounds = fz_bound_page(impl_->ctx, page);
    bounds = fz_transform_rect(bounds, m);
    page_bounds = fz_round_rect(bounds);

    crop_bounds.x0 = src_x;
    crop_bounds.y0 = src_y;
    crop_bounds.x1 = src_x + src_w;
    crop_bounds.y1 = src_y + src_h;
    crop_bounds = fz_intersect_irect(crop_bounds, page_bounds);
    if (crop_bounds.x1 <= crop_bounds.x0 || crop_bounds.y1 <= crop_bounds.y0) {
      fz_throw(impl_->ctx, FZ_ERROR_ARGUMENT, "invalid crop bounds");
    }

    pix = fz_new_pixmap_with_bbox(impl_->ctx, fz_device_rgb(impl_->ctx), crop_bounds, nullptr, 0);
    fz_clear_pixmap_with_value(impl_->ctx, pix, 255);
    dev = fz_new_draw_device(impl_->ctx, m, pix);
    fz_run_page(impl_->ctx, page, dev, fz_identity, nullptr);
    fz_close_device(impl_->ctx, dev);

    w = fz_pixmap_width(impl_->ctx, pix);
    h = fz_pixmap_height(impl_->ctx, pix);
    const int stride = fz_pixmap_stride(impl_->ctx, pix);
    const unsigned char *samples = fz_pixmap_samples(impl_->ctx, pix);
    rgba.assign(static_cast<size_t>(w * h * 4), 255);
    for (int y = 0; y < h; ++y) {
      if (cancel && cancel->load()) return false;
      const unsigned char *row = samples + y * stride;
      for (int x = 0; x < w; ++x) {
        const int si = x * 3;
        const int di = (y * w + x) * 4;
        rgba[di + 0] = row[si + 0];
        rgba[di + 1] = row[si + 1];
        rgba[di + 2] = row[si + 2];
        rgba[di + 3] = 255;
      }
    }
  }
  fz_always(impl_->ctx) {
    if (dev) fz_drop_device(impl_->ctx, dev);
    if (pix) fz_drop_pixmap(impl_->ctx, pix);
    if (page) fz_drop_page(impl_->ctx, page);
  }
  fz_catch(impl_->ctx) {
    return false;
  }
  return true;
#else
  std::vector<unsigned char> full_rgba;
  int full_w = 0;
  int full_h = 0;
  if (!RenderPageRGBA(page_index, scale, full_rgba, full_w, full_h, cancel)) return false;
  if (full_w <= 0 || full_h <= 0) return false;
  src_x = std::min(src_x, std::max(0, full_w - 1));
  src_y = std::min(src_y, std::max(0, full_h - 1));
  src_w = std::min(src_w, full_w - src_x);
  src_h = std::min(src_h, full_h - src_y);
  if (src_w <= 0 || src_h <= 0) return false;
  w = src_w;
  h = src_h;
  rgba.assign(static_cast<size_t>(w * h * 4), 255);
  for (int y = 0; y < h; ++y) {
    if (cancel && cancel->load()) return false;
    const unsigned char *src_row = full_rgba.data() + static_cast<size_t>(((src_y + y) * full_w + src_x) * 4);
    unsigned char *dst_row = rgba.data() + static_cast<size_t>(y * w * 4);
    std::copy(src_row, src_row + static_cast<size_t>(w * 4), dst_row);
  }
  return true;
#endif
}

bool PdfReader::RenderCurrentPageRGBA(float scale, std::vector<unsigned char> &rgba, int &w, int &h,
                                      const std::atomic<bool> *cancel) {
  return RenderPageRGBA(CurrentPage(), scale, rgba, w, h, cancel);
}
