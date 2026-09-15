#include "audio_runtime.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: rgds_audio_probe <ROCreader runtime directory>\n");
    return 2;
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const char *driver = SDL_GetCurrentAudioDriver();
  std::printf("audio driver: %s\n", driver ? driver : "none");
  const std::filesystem::path root(argv[1]);
  for (const char *name : {"move.wav", "select.wav", "back.wav", "change.wav"}) {
    SDL_AudioSpec spec{};
    Uint8 *data = nullptr;
    Uint32 size = 0;
    if (!SDL_LoadWAV((root / "sounds" / name).string().c_str(), &spec, &data, &size)) {
      std::fprintf(stderr, "load %s: %s\n", name, SDL_GetError());
      SDL_Quit();
      return 1;
    }
    const bool nonempty = data && size > 0;
    SDL_FreeWAV(data);
    if (!nonempty) {
      std::fprintf(stderr, "empty sound: %s\n", name);
      SDL_Quit();
      return 1;
    }
    std::printf("asset: %s bytes=%u rate=%d channels=%u\n", name, size, spec.freq, spec.channels);
  }
  SfxBank bank;
  if (!bank.Init(root)) {
    std::fprintf(stderr, "SfxBank initialization failed\n");
    bank.Shutdown();
    SDL_Quit();
    return 1;
  }
  bank.SetVolume(48);
  std::printf("SfxBank backend: %s, volume: %d\n", bank.BackendName(), bank.Volume());
  const bool valid_backend = std::strcmp(bank.BackendName(), "none") != 0;
  for (SfxId id : {SfxId::Move, SfxId::Select, SfxId::Back, SfxId::Change}) {
    SDL_ClearError();
    bank.Play(id);
    if (*SDL_GetError()) {
      std::fprintf(stderr, "play: %s\n", SDL_GetError());
      bank.Shutdown();
      SDL_Quit();
      return 1;
    }
    std::printf("played sound %d\n", static_cast<int>(id));
    SDL_Delay(1500);
  }
  bank.Shutdown();
  SDL_Quit();
  std::puts("PASS: SfxBank initialized and all four sounds submitted");
  return valid_backend ? 0 : 1;
}
