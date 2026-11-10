// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of audio_devices.cpp: the playback devices SDL3 sees, for [audio] device and the play menu. SDL ids change between runs,
// so `id` holds the device name as well (what [audio] device matches on, audio_xaudio2_linux.cpp). SDL does not say which device is
// the default; is_default stays false and "System default" in the menu stands for it.
#include <windows.h>   // the compat layer: MultiByteToWideChar
#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "nxrt/audio_sink.h"

namespace nxrt::audio {

std::vector<OutputDevice> list_output_devices() {
    std::vector<OutputDevice> out;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return out;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; ids && i < count; i++) {
        const char* n = SDL_GetAudioDeviceName(ids[i]);
        if (!n || !*n) continue;
        OutputDevice d;
        d.name = n;
        const int wn = MultiByteToWideChar(CP_UTF8, 0, n, -1, nullptr, 0);
        if (wn > 1) {
            d.id.resize(static_cast<size_t>(wn - 1));
            MultiByteToWideChar(CP_UTF8, 0, n, -1, d.id.data(), wn);
        }
        out.push_back(std::move(d));
    }
    SDL_free(ids);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return out;
}

}  // namespace nxrt::audio
