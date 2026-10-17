// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The system music player's output (Burnout Revenge's EA Trax, which the
// game plays through XMP: kernel/xmsg.cpp). One song at a time: a thread
// decodes it (ASF/WMA with FFmpeg, resampled to 48 kHz stereo float) ahead
// into a ring of ~1.5 s, and the XAudio render frame (apu/audio.cpp) takes
// 256 samples of it each frame, mixed into the front left and right of the
// game's own mix at the XMP volume. Built for Burnout only (BR_GAME_BURNOUT).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>

namespace apu::music
{
    // Any thread. Starts `file` from its beginning, dropping what was queued
    // of the song before. Not paused afterwards. Returns the song's number
    // (from 1), which the end handler gets.
    uint64_t Play(const std::filesystem::path& file);
    // Silence; nothing is decoded until the next Play.
    void Stop();
    // What is queued stays for Resume; nothing is taken from it meanwhile.
    void Pause();
    void Resume();
    // XMPSetVolume's 0..1 (applied from the next frame, ramped over it).
    void SetVolume(float volume);

    // Called on the audio thread once a song played to its end, or right
    // after Play when it can't be opened or decoded (`failed`): the XMP
    // player moves to its next song from it, unless another Play came in
    // meanwhile (`song` isn't the last one's number). Set once, before any
    // Play.
    void SetSongEndHandler(std::function<void(uint64_t song, bool failed)> handler);

    // The audio thread (XAudioSubmitRenderDriverFrame): adds `frames`
    // samples of music to `out` (interleaved, `channels` channels, the
    // first two front left and right).
    void MixInto(float* out, uint32_t frames, uint32_t channels);
}
