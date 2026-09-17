#include "zip_image_reader.h"
#include "zip_image_runtime.h"
#include "epub_comic_reader.h"
#include "epub_comic_runtime.h"

#include <zip.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <iterator>

namespace {
void Require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

std::vector<char> Image(int w, int h, int page) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  Require(surface != nullptr, SDL_GetError());
  for (int y = 0; y < h; ++y) {
    SDL_Rect row{0, y, w, 1};
    SDL_FillRect(surface, &row, SDL_MapRGBA(surface->format, y % 251, y / 251, page, 255));
  }
  const char *path = "build/image_flow_fixture.bmp";
  Require(SDL_SaveBMP(surface, path) == 0, SDL_GetError());
  SDL_FreeSurface(surface);
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string Archive(bool epub, int w, int h) {
  const std::string path = epub ? "build/image_flow_fixture.epub" : "build/image_flow_fixture.zip";
  zip_t *archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
  Require(archive != nullptr, "create archive");
  auto first = Image(w, h, 80);
  auto second = Image(w, h + 51, 160);
  auto add = [&](const char *name, const void *data, size_t size) {
    zip_source_t *source = zip_source_buffer(archive, data, size, 0);
    Require(source && zip_file_add(archive, name, source, ZIP_FL_OVERWRITE) >= 0, "add entry");
  };
  const std::string container =
      "<container><rootfiles><rootfile full-path=\"book.opf\"/></rootfiles></container>";
  const std::string opf =
      "<package><manifest>"
      "<item id=\"p1\" href=\"001.bmp\" media-type=\"image/bmp\"/>"
      "<item id=\"p2\" href=\"002.bmp\" media-type=\"image/bmp\"/>"
      "<item id=\"doc\" href=\"pages.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine><itemref idref=\"doc\"/></spine></package>";
  const std::string html = "<html><body><img src=\"001.bmp\"/><img src=\"002.bmp\"/></body></html>";
  if (epub) {
    add("META-INF/container.xml", container.data(), container.size());
    add("book.opf", opf.data(), opf.size());
    add("pages.xhtml", html.data(), html.size());
  }
  add("001.bmp", first.data(), first.size());
  add("002.bmp", second.data(), second.size());
  Require(zip_close(archive) == 0, "close archive");
  return path;
}

template <class Reader>
void CheckScale(bool epub) {
  Reader reader;
  Require(reader.Open(Archive(epub, 80, 200)), "reader open");
  for (float scale : {0.05f, 0.075f, 0.1f, 0.5f}) {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    Require(reader.RenderPageRGBA(0, scale, rgba, w, h), "decode");
    Require(w == std::lround(80 * scale) && h == std::lround(200 * scale),
            std::string(epub ? "EPUB" : "ZIP") + " decode/layout scale mismatch at " + std::to_string(scale));
  }
}

template <class Runtime, class Progress>
void CheckFlow(bool epub, int viewport_w, int viewport_h, int page_h, int rotation) {
  const bool horizontal = rotation == 90 || rotation == 270;
  const bool positive = rotation == 0 || rotation == 270;
  const int cross = horizontal ? viewport_h : viewport_w;
  const int extent = horizontal ? viewport_w : viewport_h;
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, viewport_w, viewport_h, 32, SDL_PIXELFORMAT_RGBA32);
  Require(surface != nullptr, SDL_GetError());
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  Require(renderer != nullptr, SDL_GetError());
  {
    Runtime runtime;
    Progress progress{};
    progress.rotation = rotation;
    Require(runtime.Open(renderer, Archive(epub, cross, page_h), viewport_w, viewport_h, progress), "runtime open");
    const Uint32 begin = SDL_GetTicks();
    while (!runtime.CanDrawPageAt(0) || !runtime.CanDrawPageAt(1)) {
      runtime.PrefetchPageAt(1);
      runtime.Tick();
      Require(SDL_GetTicks() - begin < 15000, "prefetch timeout");
      SDL_Delay(2);
    }
    int previous = 0;
    const int start = std::max(0, page_h - extent);
    for (int offset : {page_h - 1, 0, start, start + 1, page_h / 2}) {
      runtime.ScrollByPixels(offset - previous);
      previous = offset;
      Require(runtime.Progress().scroll_y == offset && runtime.CurrentPage() == 0, "scroll position");
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
      SDL_RenderClear(renderer);
      runtime.Draw(renderer);
      SDL_RenderPresent(renderer);
      for (int flow = 0; flow < std::min(extent, page_h * 2 + 51 - offset); ++flow) {
        int row = offset + flow;
        const int page = row < page_h ? 80 : 160;
        if (row >= page_h) row -= page_h;
        const int coordinate = positive ? flow : extent - 1 - flow;
        const int x = horizontal ? coordinate : viewport_w / 2;
        const int y = horizontal ? viewport_h / 2 : coordinate;
        const Uint32 pixel = *reinterpret_cast<Uint32 *>(
            static_cast<unsigned char *>(surface->pixels) + y * surface->pitch + x * 4);
        Uint8 r, g, b, a;
        SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
        Require(r == row % 251 && g == row / 251 && b == page,
                std::string(epub ? "EPUB" : "ZIP") + " flow mismatch viewport=" +
                    std::to_string(viewport_w) + "x" + std::to_string(viewport_h) +
                    " page_h=" + std::to_string(page_h) + " rotation=" + std::to_string(rotation) +
                    " offset=" + std::to_string(offset) + " flow=" + std::to_string(flow));
      }
    }
  }
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
}
}  // namespace

int main(int argc, char **argv) {
  try {
    Require(SDL_Init(SDL_INIT_TIMER) == 0, SDL_GetError());
    const bool scale_only = argc > 1 && std::string(argv[1]) == "scale";
    if (scale_only) {
      CheckScale<ZipImageReader>(false);
      CheckScale<EpubComicReader>(true);
      std::cout << "[pass] ZIP/EPUB decode scales match layout\n";
    } else {
      for (const auto size : {std::pair<int, int>{640, 480}, {720, 480}, {720, 720},
                              {1024, 768}, {1600, 1440}, {1024, 1536}}) {
        for (int rotation : {0, 90, 180, 270}) {
          const int extent = rotation == 90 || rotation == 270 ? size.first : size.second;
          for (int height : {extent - 165, extent, extent + 211}) {
            CheckFlow<ZipImageRuntime, ZipImageRuntimeProgress>(false, size.first, size.second, height, rotation);
            CheckFlow<EpubComicRuntime, EpubRuntimeProgress>(true, size.first, size.second, height, rotation);
          }
        }
        std::cout << "[pass] ZIP/EPUB flow " << size.first << "x" << size.second << ", all rotations\n";
      }
    }
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[FAIL] " << error.what() << "\n";
    SDL_Quit();
    return 1;
  }
}
