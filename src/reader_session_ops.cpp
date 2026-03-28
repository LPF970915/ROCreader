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
      deps.close_text_reader();
      deps.ui.mode = ReaderMode::Pdf;
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
    EpubRuntimeProgress epub_progress;
    epub_progress.page = deps.ui.progress.page;
    epub_progress.rotation = deps.ui.progress.rotation;
    epub_progress.zoom = deps.ui.progress.zoom;
    epub_progress.scroll_x = deps.ui.progress.scroll_x;
    epub_progress.scroll_y = deps.ui.progress.scroll_y;
    if (deps.epub_runtime.Open(deps.renderer, book_path, deps.screen_w, deps.screen_h, epub_progress)) {
      deps.close_text_reader();
      deps.pdf_runtime.Close();
      deps.ui.mode = ReaderMode::Epub;
      opened = true;
    }
    if (!opened && !deps.epub_runtime.HasRealRenderer()) {
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
    deps.epub_runtime.Close();
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
  } else if (deps.ui.mode == ReaderMode::Epub && deps.epub_runtime.IsOpen()) {
    const EpubRuntimeProgress active_epub = deps.epub_runtime.Progress();
    deps.ui.progress.page = active_epub.page;
    deps.ui.progress.scroll_x = active_epub.scroll_x;
    deps.ui.progress.scroll_y = active_epub.scroll_y;
    deps.ui.progress.zoom = active_epub.zoom;
    deps.ui.progress.rotation = active_epub.rotation;
  } else if (deps.ui.mode == ReaderMode::Txt && deps.ui.txt_reader.open) {
    deps.ui.progress.page = (deps.ui.txt_reader.line_h > 0) ? (deps.ui.txt_reader.scroll_px / deps.ui.txt_reader.line_h) : 0;
    deps.ui.progress.scroll_y = deps.ui.txt_reader.scroll_px;
    deps.ui.txt_reader.resume_cache_dirty = true;
    deps.persist_current_txt_resume_snapshot(deps.ui.current_book, true);
  }

  deps.progress_store.Set(deps.ui.current_book, deps.ui.progress);

  if (deps.ui.mode == ReaderMode::Pdf) {
    deps.pdf_runtime.Close();
  } else if (deps.ui.mode == ReaderMode::Epub) {
    deps.epub_runtime.Close();
  } else if (deps.ui.mode == ReaderMode::Txt) {
    deps.close_text_reader();
  }

  deps.ui.mode = ReaderMode::None;
  deps.ui.progress_overlay_visible = false;
  ResetReaderInputState(deps.ui);
}
