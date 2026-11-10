#include "AudioInternal.hpp"

#include <algorithm>
#include <stdexcept>

namespace zplus::audio {
namespace {

constexpr std::size_t maximumInstancesPerSound = 64;

double cursor(const ma_sound& sound) {
    float position = 0.0f;
    detail::checkResult(ma_sound_get_cursor_in_seconds(&sound, &position),
        "query audio playback position");
    return position;
}

void seek(ma_sound& sound, double position) {
    detail::checkResult(ma_sound_seek_to_second(
        &sound, static_cast<float>(position)), "seek audio playback");
}

void startMusic(detail::Resource& resource) {
    if (resource.paused) {
        seek(resource.sound, resource.pausedPosition);
        resource.paused = false;
    } else if (!ma_sound_is_playing(&resource.sound)) {
        seek(resource.sound, 0.0);
    }
    detail::checkResult(ma_sound_start(&resource.sound), "start music playback");
}

detail::SoundInstance& createSoundInstance(
    detail::AudioState& audio,
    detail::Resource& resource
) {
    for (auto& instance : resource.instances) {
        if (!ma_sound_is_playing(&instance->sound) && !instance->paused) {
            seek(instance->sound, 0.0);
            return *instance;
        }
    }
    if (resource.instances.size() >= maximumInstancesPerSound) {
        throw std::runtime_error(
            "sound playback limit reached for this audio resource");
    }

    auto instance = std::make_unique<detail::SoundInstance>();
    detail::checkResult(ma_sound_init_copy(
        &audio.engine, &resource.sound, MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr, &instance->sound), "create sound playback instance");
    instance->initialized = true;
    ma_sound_set_volume(&instance->sound, static_cast<float>(resource.volume));
    ma_sound_set_looping(&instance->sound, resource.looping ? MA_TRUE : MA_FALSE);
    resource.instances.push_back(std::move(instance));
    return *resource.instances.back();
}

} // namespace

void play(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        startMusic(resource);
        return;
    }
    detail::SoundInstance& instance = createSoundInstance(audio, resource);
    instance.paused = false;
    detail::checkResult(ma_sound_start(&instance.sound), "start sound playback");
}

void pause(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        if (!ma_sound_is_playing(&resource.sound)) return;
        resource.pausedPosition = cursor(resource.sound);
        detail::checkResult(ma_sound_stop(&resource.sound), "pause music playback");
        resource.paused = true;
        return;
    }
    for (auto& instance : resource.instances) {
        if (!ma_sound_is_playing(&instance->sound)) continue;
        instance->pausedPosition = cursor(instance->sound);
        detail::checkResult(ma_sound_stop(&instance->sound),
            "pause sound playback");
        instance->paused = true;
    }
}

void resume(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        if (resource.paused) startMusic(resource);
        return;
    }
    for (auto& instance : resource.instances) {
        if (!instance->paused) continue;
        seek(instance->sound, instance->pausedPosition);
        instance->paused = false;
        detail::checkResult(ma_sound_start(&instance->sound),
            "resume sound playback");
    }
}

void stop(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        detail::checkResult(ma_sound_stop(&resource.sound), "stop music playback");
        seek(resource.sound, 0.0);
        resource.paused = false;
        resource.pausedPosition = 0.0;
        return;
    }
    for (auto& instance : resource.instances) {
        detail::checkResult(ma_sound_stop(&instance->sound),
            "stop sound playback");
        seek(instance->sound, 0.0);
        instance->paused = false;
        instance->pausedPosition = 0.0;
    }
}

bool isPlaying(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    const detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        return ma_sound_is_playing(&resource.sound) == MA_TRUE;
    }
    return std::any_of(resource.instances.begin(), resource.instances.end(),
        [](const auto& instance) {
            return ma_sound_is_playing(&instance->sound) == MA_TRUE;
        });
}

bool isPaused(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    const detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) return resource.paused;
    return std::any_of(resource.instances.begin(), resource.instances.end(),
        [](const auto& instance) { return instance->paused; });
}

bool isStopped(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    const detail::Resource& resource = detail::checkedResource(audio, handle);
    if (resource.kind == detail::ResourceKind::Music) {
        return !resource.paused &&
            ma_sound_is_playing(&resource.sound) != MA_TRUE;
    }
    return std::none_of(resource.instances.begin(), resource.instances.end(),
        [](const auto& instance) {
            return instance->paused ||
                ma_sound_is_playing(&instance->sound) == MA_TRUE;
        });
}

} // namespace zplus::audio
