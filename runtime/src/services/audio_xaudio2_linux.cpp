// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of audio_xaudio2.cpp: the host sink plays the guest's interleaved int16 PCM through an SDL3 audio stream (PipeWire,
// PulseAudio or ALSA, whichever SDL finds) on the output device chosen by [audio] device. [audio] sink = xaudio2 and auto select it, so
// the ini is the same on both systems; it logs and reports itself as "sdl".
// open() opens a device stream in the guest's format (SDL converts if the device wants another), write() appends to the stream and
// drops what would push the queue past kMaxQueuedFrames, and SDL's own audio thread drains the stream at the device rate, which is the
// device clock the renderer paces against. SDL follows the system default device when it changes and reopens a lost device itself,
// so this sink needs no device-loss handling of its own. The stream's get callback (on SDL's audio thread) counts underruns: SDL asking
// for more data than the stream holds once playback has started. The stream gain carries the process master volume ([audio] volume).
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "nxrt/audio_sink.h"
#include "nxrt/config.h"
#include "nxrt/log.h"
#include "nxrt/text.h"

namespace nxrt::audio {
namespace {

// The same bound as the XAudio2 sink (60 buffers of 1024 frames, about 1.3 s at 48 kHz): write() never queues more.
constexpr uint32_t kMaxQueuedFrames = 60 * 1024;

using text::lower;

// The device [audio] device asks for: 0 (the default device, which SDL follows when it changes) for "default" or empty, else the first
// device whose name equals the text, else the first whose name contains it (ASCII case-insensitive). No match: a warning and the default.
SDL_AudioDeviceID pick_device(const std::string& want) {
    if (want.empty() || lower(want) == "default") return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID exact = 0, partial = 0;
    const std::string needle = lower(want);
    for (int i = 0; ids && i < count; i++) {
        const char* n = SDL_GetAudioDeviceName(ids[i]);
        NXLOG_I("audio", "sdl: output device: %s", n ? n : "?");
        if (!n) continue;
        if (!exact && want == n) exact = ids[i];
        if (!partial && lower(n).find(needle) != std::string::npos) partial = ids[i];
    }
    SDL_free(ids);
    if (exact) return exact;
    if (partial) return partial;
    NXLOG_W("audio", "sdl: [audio] device = %s matches no output device; using the default device", want.c_str());
    return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
}

class SdlSink final : public AudioSink {
public:
    ~SdlSink() override { close(); }

    bool open(uint32_t sample_rate, uint32_t channels) override;
    void close() override;
    uint32_t write(const int16_t* pcm, uint32_t frames) override;
    uint32_t queued_frames() override;
    bool has_device_clock() const override { return true; }   // SDL's audio thread drains the stream at the device rate
    const char* name() const override { return "sdl"; }
    SinkStats stats() override;

private:
    static void SDLCALL on_get(void* self, SDL_AudioStream*, int additional, int total) {
        static_cast<SdlSink*>(self)->on_device_pull(additional, total);
    }
    void on_device_pull(int additional, int total);
    uint32_t queued_locked() const;
    SinkStats stats_locked_copy() const;   // with mu_ held

    std::mutex mu_;                     // open/close/write/queued (the audio services call from more than one thread)
    SDL_AudioStream* stream_ = nullptr;
    bool sdl_audio_ = false;            // this sink holds an SDL_INIT_AUDIO reference
    uint32_t rate_ = 0, channels_ = 0;
    float applied_volume_ = 1.0f;

    SinkStats st_;                                  // written/accepted/dropped/peaks: under mu_
    std::atomic<uint64_t> underruns_{0};            // SDL's audio thread
    std::atomic<bool> started_{false};              // set by the first write that queued audio
    std::atomic<uint32_t> low_frames_{UINT32_MAX};  // fewest frames the stream held when SDL pulled, after the start
};

bool SdlSink::open(uint32_t sample_rate, uint32_t channels) {
    close();
    std::lock_guard<std::mutex> lk(mu_);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        NXLOG_W("audio", "sdl: audio init failed: %s", SDL_GetError());
        return false;
    }
    sdl_audio_ = true;
    const SDL_AudioDeviceID dev = pick_device(config_get_str("audio", "device", "default"));
    // 5.1 in SDL's order is FL FR FC LFE BL BR, the same as the guest's (and WAVE's) order, so no remap is needed.
    const SDL_AudioSpec spec{SDL_AUDIO_S16, static_cast<int>(channels), static_cast<int>(sample_rate)};
    stream_ = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);
    if (!stream_) {
        NXLOG_W("audio", "sdl: cannot open an output device (%u Hz, %u channels): %s", sample_rate, channels, SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdl_audio_ = false;
        return false;
    }
    rate_ = sample_rate;
    channels_ = channels;
    st_.sample_rate = sample_rate;
    started_ = false;
    low_frames_ = UINT32_MAX;
    applied_volume_ = effective_volume();
    SDL_SetAudioStreamGain(stream_, applied_volume_);
    SDL_SetAudioStreamGetCallback(stream_, &SdlSink::on_get, this);
    SDL_ResumeAudioStreamDevice(stream_);   // device streams start paused
    const char* dn = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    NXLOG_I("audio", "sdl: opened %u Hz, %u channels, volume %.2f on %s (driver %s)", sample_rate, channels, applied_volume_,
            dn ? dn : "?", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
    return true;
}

void SdlSink::close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (stream_) {
        SDL_DestroyAudioStream(stream_);   // closes the device; the callback does not run after this returns
        stream_ = nullptr;
        NXLOG_I("audio", "%s", format_stats(name(), stats_locked_copy()).c_str());
    }
    if (sdl_audio_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdl_audio_ = false;
    }
}

uint32_t SdlSink::queued_locked() const {
    if (!stream_ || !channels_) return 0;
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    return bytes > 0 ? static_cast<uint32_t>(bytes) / (channels_ * sizeof(int16_t)) : 0;
}

uint32_t SdlSink::write(const int16_t* pcm, uint32_t frames) {
    std::lock_guard<std::mutex> lk(mu_);
    st_.frames_written += frames;
    if (!stream_) {
        st_.frames_dropped += frames;
        return 0;
    }
    const float volume = effective_volume();
    if (volume != applied_volume_) {
        SDL_SetAudioStreamGain(stream_, volume);
        applied_volume_ = volume;
    }
    const uint32_t queued = queued_locked();
    const uint32_t room = queued < kMaxQueuedFrames ? kMaxQueuedFrames - queued : 0;
    const uint32_t take = std::min(frames, room);
    if (take && !SDL_PutAudioStreamData(stream_, pcm, static_cast<int>(take * channels_ * sizeof(int16_t)))) {
        NXLOG_W("audio", "sdl: queueing audio failed: %s", SDL_GetError());
        st_.frames_dropped += frames;
        return 0;
    }
    st_.frames_accepted += take;
    st_.frames_dropped += frames - take;
    if (take) started_ = true;
    st_.queued_peak_frames = std::max(st_.queued_peak_frames, queued + take);
    return take;
}

uint32_t SdlSink::queued_frames() {
    std::lock_guard<std::mutex> lk(mu_);
    return queued_locked();
}

// SDL's audio thread, with the stream locked: `additional` bytes are needed now, `total` is the whole request. A request the stream
// cannot fill once playback has started is an underrun (SDL plays silence for the gap).
void SdlSink::on_device_pull(int additional, int total) {
    if (!started_.load(std::memory_order_relaxed) || !channels_) return;
    const uint32_t frame_bytes = channels_ * sizeof(int16_t);
    const uint32_t held = static_cast<uint32_t>(std::max(0, total - additional)) / frame_bytes;
    uint32_t low = low_frames_.load(std::memory_order_relaxed);
    while (held < low && !low_frames_.compare_exchange_weak(low, held, std::memory_order_relaxed)) {}
    if (additional > 0) underruns_.fetch_add(1, std::memory_order_relaxed);
}

SinkStats SdlSink::stats_locked_copy() const {
    SinkStats s = st_;
    s.underruns = underruns_.load(std::memory_order_relaxed);
    const uint32_t low = low_frames_.load(std::memory_order_relaxed);
    s.queued_low_frames = low == UINT32_MAX ? 0 : low;
    return s;
}

SinkStats SdlSink::stats() {
    std::lock_guard<std::mutex> lk(mu_);
    return stats_locked_copy();
}

}  // namespace

std::shared_ptr<AudioSink> make_xaudio2_sink() { return std::make_shared<SdlSink>(); }

}  // namespace nxrt::audio
