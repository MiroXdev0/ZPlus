#include "AudioInternal.hpp"

#include <algorithm>

namespace zplus::audio {

void setMasterVolume(double volume) {
    detail::validateVolume(volume);
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    audio.masterVolume = std::clamp(volume, 0.0, 1.0);
    if (audio.engineInitialized) {
        ma_engine_set_volume(&audio.engine,
            audio.muted ? 0.0f : static_cast<float>(audio.masterVolume));
    }
}

double getMasterVolume() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return audio.masterVolume;
}

void mute() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    audio.muted = true;
    if (audio.engineInitialized) ma_engine_set_volume(&audio.engine, 0.0f);
}

void unmute() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    audio.muted = false;
    if (audio.engineInitialized) {
        ma_engine_set_volume(&audio.engine,
            static_cast<float>(audio.masterVolume));
    }
}

bool isMuted() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return audio.muted;
}

} // namespace zplus::audio
