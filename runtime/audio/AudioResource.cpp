#include "AudioInternal.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace zplus::audio::detail {

Resource& checkedResource(AudioState& audio, std::int64_t handle) {
    const auto found = audio.resources.find(handle);
    if (handle <= 0 || found == audio.resources.end()) {
        throw std::out_of_range("invalid or unloaded audio handle " +
            std::to_string(handle));
    }
    return *found->second;
}

const Resource& checkedResource(const AudioState& audio, std::int64_t handle) {
    const auto found = audio.resources.find(handle);
    if (handle <= 0 || found == audio.resources.end()) {
        throw std::out_of_range("invalid or unloaded audio handle " +
            std::to_string(handle));
    }
    return *found->second;
}

} // namespace zplus::audio::detail

namespace zplus::audio {
namespace {

std::int64_t load(const std::string& path, detail::ResourceKind kind) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureEngine(audio, audio.testNoDevice);
    detail::validateAudioFile(path);
    if (audio.nextHandle <= 0 ||
        audio.nextHandle == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("audio handle space is exhausted");
    }

    auto resource = std::make_unique<detail::Resource>();
    resource->kind = kind;
    resource->path = path;
    resource->durationSeconds = detail::audioFileDuration(path);
    const ma_uint32 flags = kind == detail::ResourceKind::Music
        ? MA_SOUND_FLAG_STREAM
        : MA_SOUND_FLAG_NO_SPATIALIZATION;
    detail::checkResult(ma_sound_init_from_file(
        &audio.engine, path.c_str(), flags, nullptr, nullptr, &resource->sound),
        "load audio file '" + path + "'");
    resource->initialized = true;
    ma_sound_set_volume(&resource->sound, 1.0f);
    ma_sound_set_looping(&resource->sound, MA_FALSE);

    const std::int64_t handle = audio.nextHandle++;
    audio.resources.emplace(handle, std::move(resource));
    return handle;
}

void destroyResource(detail::Resource& resource) {
    for (auto& instance : resource.instances) {
        if (instance->initialized) {
            ma_sound_stop(&instance->sound);
            ma_sound_uninit(&instance->sound);
            instance->initialized = false;
        }
    }
    resource.instances.clear();
    if (resource.initialized) {
        ma_sound_stop(&resource.sound);
        ma_sound_uninit(&resource.sound);
        resource.initialized = false;
    }
}

} // namespace

std::int64_t loadMusic(const std::string& path) {
    return load(path, detail::ResourceKind::Music);
}

std::int64_t loadSound(const std::string& path) {
    return load(path, detail::ResourceKind::Sound);
}

void unload(std::int64_t handle) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    auto found = audio.resources.find(handle);
    if (handle <= 0 || found == audio.resources.end()) {
        throw std::out_of_range("invalid or unloaded audio handle " +
            std::to_string(handle));
    }
    destroyResource(*found->second);
    audio.resources.erase(found);
}

void unloadAll() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    for (auto& [handle, resource] : audio.resources) {
        (void)handle;
        destroyResource(*resource);
    }
    audio.resources.clear();
}

} // namespace zplus::audio
