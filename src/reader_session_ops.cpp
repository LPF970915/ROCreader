#include "reader_session_ops.h"

#include <iostream>

bool OpenReaderSession(const std::string &book_path, const std::string &ext, ReaderOpenDeps &deps) {
  deps.ui.current_book = book_path;
  bool opened = false;

  if (ext == ".txt") {
    opened = deps.open_text_book(book_path);
  } else if (ext == ".pdf") {
    PdfRuntimeProgress pdf_progress;
    pdf_progress.page = deps.ui.progress.page;
    pdf_progress.rotation = deps.ui.progress.rotation;
    pdf_progress.zoom = deps.ui.progress.zoom;
    pdf_progress.scroll_y = deps.ui.progress.scroll_y;
    if (deps.pdf_runtime.Open(deps.renderer, book_path, deps.screen_w, deps.screen_h, pdf_progress)) {
      deps.clear_reader_page_size_cache();
      deps.adaptive_render = ReaderAdaptiveRenderState{};
      deps.reset_reader_async_state();
      deps.close_text_reader();
      deps.ui.mode = ReaderMode::Pdf;
      deps.invalidate_all_render_cache();
      deps.display_state = ReaderViewState{};
      deps.ready_state = ReaderViewState{};
      deps.display_state_valid = false;
      deps.ready_state_valid = false;
      const PdfRuntimeProgress active_pdf = deps.pdf_runtime.Progress();
      deps.target_state = ReaderViewState{active_pdf.page, active_pdf.zoom, active_pdf.rotation};
      deps.clamp_scroll();
      opened = true;
    }
    if (!opened && !deps.pdf_runtime.HasRealRenderer()) {
      if (!deps.ui.warned_mock_pdf_backend) {
        std::cerr << "[reader] blocked: current build has no real document backend. "
                     "Please rebuild with REQUIRE_MUPDF=1 and install MuPDF (preferred) or poppler-cpp.\n";
        deps.ui.warned_mock_pdf_backend = true;
      }
    }
  } else if (ext == ".epub") {
    if (deps.epub_comic.Open(book_path)) {
      deps.clear_reader_page_size_cache();
      deps.adaptive_render = ReaderAdaptiveRenderState{};
      deps.reset_reader_async_state();
      deps.close_text_reader();
      deps.pdf_runtime.Close();
      deps.ui.mode = ReaderMode::Epub;
      deps.epub_comic.SetPage(deps.ui.progress.page);
      deps.invalidate_all_render_cache();
      deps.display_state = ReaderViewState{};
      deps.ready_state = ReaderViewState{};
      deps.display_state_valid = false;
      deps.ready_state_valid = false;
      deps.target_state = ReaderViewState{deps.reader_current_page(), deps.ui.progress.zoom, deps.ui.progress.rotation};
      deps.clamp_scroll();
      opened = true;
    }
    if (!opened && !deps.epub_comic.HasRealRenderer()) {
      if (!deps.ui.warned_epub_backend) {
        std::cerr << "[reader] blocked: current build has no epub comic backend. "
                     "Please rebuild with libzip (pkg-config libzip) available.\n";
        deps.ui.warned_epub_backend = true;
      }
    }
  } else {
    std::cerr << "[reader] unsupported format for runtime reader: " << book_path << "\n";
  }

  if (!opened) {
    if (ext == ".pdf" || ext == ".epub") {
      std::cerr << "[reader] failed to open: " << book_path << "\n";
    }
    deps.ui.current_book.clear();
    deps.close_text_reader();
    deps.pdf_runtime.Close();
    deps.epub_comic.Close();
    deps.clear_reader_page_size_cache();
    deps.invalidate_all_render_cache();
  }

  deps.ui.progress_overlay_visible = false;
  ResetReaderInputState(deps.ui);
  return opened;
}

void CloseReaderSession(ReaderCloseDeps &deps) {
  if (deps.ui.mode == ReaderMode::Pdf && deps.pdf_runtime.IsOpen()) {
    const PdfRuntimeProgress active_pdf = deps.pdf_runtime.Progress();
    deps.ui.progress.page = active_pdf.page;
    deps.ui.progress.scroll_y = active_pdf.scroll_y;
    deps.ui.progress.zoom = active_pdf.zoom;
    deps.ui.progress.rotation = active_pdf.rotation;
  } else if (deps.ui.mode == ReaderMode::Epub && deps.epub_comic.IsOpen()) {
    deps.ui.progress.page = deps.epub_comic.CurrentPage();
  } else if (deps.ui.mode == ReaderMode::Txt && deps.ui.txt_reader.open) {
    deps.ui.progress.page = (deps.ui.txt_reader.line_h > 0) ? (deps.ui.txt_reader.scroll_px / deps.ui.txt_reader.line_h) : 0;
    deps.ui.progress.scroll_y = deps.ui.txt_reader.scroll_px;
    deps.ui.txt_reader.resume_cache_dirty = true;
    deps.persist_current_txt_resume_snapshot(deps.ui.current_book, true);
  }

  deps.progress_store.Set(deps.ui.current_book, deps.ui.progress);

  if (deps.ui.mode == ReaderMode::Pdf) {
    deps.pdf_runtime.Close();
    deps.clear_reader_page_size_cache();
    deps.reset_reader_async_state();
  } else if (deps.ui.mode == ReaderMode::Epub) {
    deps.epub_comic.Close();
    deps.clear_reader_page_size_cache();
    deps.reset_reader_async_state();
  } else if (deps.ui.mode == ReaderMode::Txt) {
    deps.close_text_reader();
  }

  deps.invalidate_all_render_cache();
  deps.display_state = ReaderViewState{};
  deps.ready_state = ReaderViewState{};
  deps.target_state = ReaderViewState{};
  deps.display_state_valid = false;
  deps.ready_state_valid = false;
  deps.ui.mode = ReaderMode::None;
  deps.ui.progress_overlay_visible = false;
  ResetReaderInputState(deps.ui);
}
