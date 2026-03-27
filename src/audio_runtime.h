#pragma once

#include <SDL.h>
#ifdef HAVE_SDL2_MIXER
#include <SDL_mixer.h>
#endif

#include <filesystem>

enum class SfxId { Move, Select, Back, Change };

class SfxBank {
public:
  bool Init(const std::filesystem::path &exe_path);
  void Shutdown();
  void Play(SfxId id);
  void SetVolume(int volume);
  int Volume() const;
  const char *BackendName() const;

private:
  struct PcmData {
    Uint8 *data = nullptr;
    Uint32 len = 0;
  };

  enum class Backend {
    None,
    Mixer,
    SdlQueue,
  };

  static std::filesystem::path ResolveSoundsRoot(const std::filesystem::path &exe_path);
#ifdef HAVE_SDL2_MIXER
  static Mix_Chunk *LoadMixChunk(const std::filesystem::path &path);
  static void FreeMixChunk(Mix_Chunk *&chunk);
  Mix_Chunk *move_ = nullptr;
  Mix_Chunk *select_ = nullptr;
  Mix_Chunk *back_ = nullptr;
  Mix_Chunk *change_ = nullptr;
#endif
  PcmData LoadPcm(const std::filesystem::path &path);
  static void FreePcm(PcmData &pcm);

  PcmData move_pcm_{};
  PcmData select_pcm_{};
  PcmData back_pcm_{};
  PcmData change_pcm_{};
  SDL_AudioDeviceID audio_dev_ = 0;
  SDL_AudioSpec audio_spec_{};
  Backend backend_ = Backend::None;
  bool ready_ = false;
  int volume_ = SDL_MIX_MAXVOLUME;
};
