#include "audio_runtime.h"

#include <SDL.h>

#include <cstdlib>
#include <iostream>
#include <vector>

bool SfxBank::Init(const std::filesystem::path &exe_path) {
  const std::filesystem::path root = ResolveSoundsRoot(exe_path);
  std::cout << "[native_h700] sound root: " << root.lexically_normal().string() << "\n";

#ifdef HAVE_SDL2_MIXER
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
    Mix_AllocateChannels(8);
    move_ = LoadMixChunk(root / "move.wav");
    select_ = LoadMixChunk(root / "select.wav");
    back_ = LoadMixChunk(root / "back.wav");
    change_ = LoadMixChunk(root / "change.wav");
    backend_ = Backend::Mixer;
    ready_ = true;
    return true;
  }
  std::cerr << "[native_h700] mixer init failed, fallback to SDL audio: " << Mix_GetError() << "\n";
#endif

  SDL_AudioSpec desired{};
  desired.freq = 44100;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 1024;
  audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &audio_spec_, 0);
  if (audio_dev_ == 0) {
    backend_ = Backend::None;
    ready_ = false;
    std::cerr << "[native_h700] SDL audio init failed: " << SDL_GetError() << "\n";
    return false;
  }
  SDL_PauseAudioDevice(audio_dev_, 0);

  move_pcm_ = LoadPcm(root / "move.wav");
  select_pcm_ = LoadPcm(root / "select.wav");
  back_pcm_ = LoadPcm(root / "back.wav");
  change_pcm_ = LoadPcm(root / "change.wav");
  backend_ = Backend::SdlQueue;
  ready_ = true;
  return true;
}

void SfxBank::Shutdown() {
#ifdef HAVE_SDL2_MIXER
  FreeMixChunk(move_);
  FreeMixChunk(select_);
  FreeMixChunk(back_);
  FreeMixChunk(change_);
  if (backend_ == Backend::Mixer) Mix_CloseAudio();
#endif
  FreePcm(move_pcm_);
  FreePcm(select_pcm_);
  FreePcm(back_pcm_);
  FreePcm(change_pcm_);
  if (audio_dev_ != 0) {
    SDL_CloseAudioDevice(audio_dev_);
    audio_dev_ = 0;
  }
  backend_ = Backend::None;
  ready_ = false;
}

void SfxBank::Play(SfxId id) {
  if (!ready_) return;
  if (backend_ == Backend::Mixer) {
#ifdef HAVE_SDL2_MIXER
    Mix_Chunk *chunk = nullptr;
    if (id == SfxId::Move) chunk = move_;
    else if (id == SfxId::Select) chunk = select_;
    else if (id == SfxId::Back) chunk = back_;
    else if (id == SfxId::Change) chunk = change_;
    if (chunk) Mix_PlayChannel(-1, chunk, 0);
#endif
    return;
  }
  if (backend_ == Backend::SdlQueue && audio_dev_ != 0) {
    const PcmData *pcm = nullptr;
    if (id == SfxId::Move) pcm = &move_pcm_;
    else if (id == SfxId::Select) pcm = &select_pcm_;
    else if (id == SfxId::Back) pcm = &back_pcm_;
    else if (id == SfxId::Change) pcm = &change_pcm_;
    if (pcm && pcm->data && pcm->len > 0) {
      SDL_ClearQueuedAudio(audio_dev_);
      if (volume_ >= SDL_MIX_MAXVOLUME) {
        SDL_QueueAudio(audio_dev_, pcm->data, pcm->len);
      } else if (volume_ > 0) {
        std::vector<Uint8> mixed(static_cast<size_t>(pcm->len), 0);
        SDL_MixAudioFormat(mixed.data(), pcm->data, audio_spec_.format, pcm->len, volume_);
        SDL_QueueAudio(audio_dev_, mixed.data(), pcm->len);
      }
    }
  }
}

void SfxBank::SetVolume(int volume) {
  if (volume < 0) volume_ = 0;
  else if (volume > SDL_MIX_MAXVOLUME) volume_ = SDL_MIX_MAXVOLUME;
  else volume_ = volume;
#ifdef HAVE_SDL2_MIXER
  if (backend_ == Backend::Mixer) {
    Mix_Volume(-1, volume_);
    if (move_) Mix_VolumeChunk(move_, volume_);
    if (select_) Mix_VolumeChunk(select_, volume_);
    if (back_) Mix_VolumeChunk(back_, volume_);
    if (change_) Mix_VolumeChunk(change_, volume_);
  }
#endif
}

int SfxBank::Volume() const { return volume_; }

const char *SfxBank::BackendName() const {
  if (backend_ == Backend::Mixer) return "mixer";
  if (backend_ == Backend::SdlQueue) return "sdl_queue";
  return "none";
}

std::filesystem::path SfxBank::ResolveSoundsRoot(const std::filesystem::path &exe_path) {
  const std::vector<std::filesystem::path> roots = {
      exe_path / "sounds",
      exe_path / ".." / "sounds",
      std::filesystem::current_path() / "sounds",
  };
  for (const auto &candidate : roots) {
    if (std::filesystem::exists(candidate / "move.wav")) return candidate.lexically_normal();
  }
  return (exe_path / "sounds").lexically_normal();
}

#ifdef HAVE_SDL2_MIXER
Mix_Chunk *SfxBank::LoadMixChunk(const std::filesystem::path &path) {
  Mix_Chunk *chunk = Mix_LoadWAV(path.string().c_str());
  if (!chunk) {
    std::cerr << "[native_h700] sound load failed: " << path.string() << " err=" << Mix_GetError() << "\n";
  }
  return chunk;
}

void SfxBank::FreeMixChunk(Mix_Chunk *&chunk) {
  if (chunk) {
    Mix_FreeChunk(chunk);
    chunk = nullptr;
  }
}
#endif

SfxBank::PcmData SfxBank::LoadPcm(const std::filesystem::path &path) {
  PcmData out{};
  SDL_AudioSpec wav_spec{};
  Uint8 *wav_buf = nullptr;
  Uint32 wav_len = 0;
  if (!SDL_LoadWAV(path.string().c_str(), &wav_spec, &wav_buf, &wav_len)) {
    std::cerr << "[native_h700] sound load failed: " << path.string() << " err=" << SDL_GetError() << "\n";
    return out;
  }

  SDL_AudioCVT cvt{};
  if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq, audio_spec_.format,
                        audio_spec_.channels, audio_spec_.freq) < 0) {
    std::cerr << "[native_h700] sound convert setup failed: " << path.string() << " err=" << SDL_GetError() << "\n";
    SDL_FreeWAV(wav_buf);
    return out;
  }

  if (!cvt.needed) {
    out.data = static_cast<Uint8 *>(SDL_malloc(wav_len));
    if (out.data) {
      SDL_memcpy(out.data, wav_buf, wav_len);
      out.len = wav_len;
    }
    SDL_FreeWAV(wav_buf);
    return out;
  }

  cvt.len = static_cast<int>(wav_len);
  cvt.buf = static_cast<Uint8 *>(SDL_malloc(cvt.len * cvt.len_mult));
  if (!cvt.buf) {
    SDL_FreeWAV(wav_buf);
    return out;
  }
  SDL_memcpy(cvt.buf, wav_buf, wav_len);
  SDL_FreeWAV(wav_buf);
  if (SDL_ConvertAudio(&cvt) != 0) {
    std::cerr << "[native_h700] sound convert failed: " << path.string() << " err=" << SDL_GetError() << "\n";
    SDL_free(cvt.buf);
    return out;
  }

  out.data = static_cast<Uint8 *>(SDL_malloc(cvt.len_cvt));
  if (out.data) {
    SDL_memcpy(out.data, cvt.buf, cvt.len_cvt);
    out.len = static_cast<Uint32>(cvt.len_cvt);
  }
  SDL_free(cvt.buf);
  return out;
}

void SfxBank::FreePcm(PcmData &pcm) {
  if (pcm.data) {
    SDL_free(pcm.data);
    pcm.data = nullptr;
    pcm.len = 0;
  }
}
