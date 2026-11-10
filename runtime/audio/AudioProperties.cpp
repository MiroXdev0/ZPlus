#include "AudioInternal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zplus::audio {
namespace {

double cursor(const ma_sound& sound) {
    float seconds = 0.0f;
    detail::checkResult(ma_sound_get_cursor_in_seconds(&sound, &seconds),
        "query audio playback position");
    return seconds;
}

} // namespace

void setVolume(std::int64_t handle, double volume) {
    detail::validateVolume(volume);
    const double clamped = std::clamp(volume, 0.0, 1.0);
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    resource.volume = clamped;
    ma_sound_set_volume(&resource.sound, static_cast<float>(clamped));
    for (auto& instance : resource.instances) {
        ma_sound_set_volume(&instance->sound, static_cast<float>(clamped));
    }
}

double getVolume(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return detail::checkedResource(audio, handle).volume;
}

void setLooping(std::int64_t handle, bool looping) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    resource.looping = looping;
    ma_sound_set_looping(&resource.sound, looping ? MA_TRUE : MA_FALSE);
    for (auto& instance : resource.instances) {
        ma_sound_set_looping(&instance->sound, looping ? MA_TRUE : MA_FALSE);
    }
}

bool isLooping(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return detail::checkedResource(audio, handle).looping;
}

void setPlaybackPosition(std::int64_t handle, double seconds) {
    detail::validatePosition(seconds);
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    if (seconds > resource.durationSeconds) {
        throw std::out_of_range("playback position exceeds audio duration");
    }
    detail::checkResult(ma_sound_seek_to_second(
        &resource.sound, static_cast<float>(seconds)), "seek audio playback");
    if (resource.kind == detail::ResourceKind::Music && resource.paused) {
        resource.pausedPosition = seconds;
    }
    for (auto& instance : resource.instances) {
        if (instance->initialized) {
            detail::checkResult(ma_sound_seek_to_second(
                &instance->sound, static_cast<float>(seconds)),
                "seek sound playback");
            if (instance->paused) instance->pausedPosition = seconds;
        }
    }
}

double getPlaybackPosition(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    const detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        return resource.paused ? resource.pausedPosition : cursor(resource.sound);
    }
    double position = 0.0;
    for (const auto& instance : resource.instances) {
        if (instance->paused) {
            position = std::max(position, instance->pausedPosition);
        } else if (ma_sound_is_playing(&instance->sound)) {
            position = std::max(position, cursor(instance->sound));
        }
    }
    return position;
}

double getDuration(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return detail::checkedResource(audio, handle).durationSeconds;
}

} // namespace zplus::audio
