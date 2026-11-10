#include "AudioInternal.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace zplus::audio::detail {
namespace {

void processShutdown() {
    shutdown();
}

} // namespace

AudioState& state() {
    static AudioState audio;
    return audio;
}

void ensureEngine(AudioState& audio, bool noDevice) {
    if (audio.engineInitialized) return;
    if (!noDevice) {
        ensureContext(audio);
        refreshDevices(audio);
        if (audio.devices.empty()) {
            throw std::runtime_error("audio device unavailable: no playback devices were found");
        }
    }

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = noDevice ? MA_TRUE : MA_FALSE;
    if (noDevice) {
        config.channels = 2;
        config.sampleRate = 48000;
    } else {
        config.pContext = &audio.context;
        if (audio.hasSelectedDevice) {
            config.pPlaybackDeviceID = &audio.selectedDevice;
        }
    }
    checkResult(ma_engine_init(&config, &audio.engine), "initialize audio engine");
    audio.engineInitialized = true;
    audio.testNoDevice = noDevice;
    ma_engine_set_volume(&audio.engine,
        audio.muted ? 0.0f : static_cast<float>(audio.masterVolume));
    static std::once_flag shutdownRegistration;
    std::call_once(shutdownRegistration, [] { std::atexit(processShutdown); });
}

void shutdownLocked(AudioState& audio) {
    for (auto& [handle, resource] : audio.resources) {
        (void)handle;
        for (auto& instance : resource->instances) {
            if (instance->initialized) {
                ma_sound_stop(&instance->sound);
                ma_sound_uninit(&instance->sound);
                instance->initialized = false;
            }
        }
        resource->instances.clear();
        if (resource->initialized) {
            ma_sound_stop(&resource->sound);
            ma_sound_uninit(&resource->sound);
            resource->initialized = false;
        }
    }
    audio.resources.clear();
    if (audio.engineInitialized) {
        ma_engine_uninit(&audio.engine);
        audio.engineInitialized = false;
    }
    if (audio.contextInitialized) {
        ma_context_uninit(&audio.context);
        audio.contextInitialized = false;
    }
    audio.devices.clear();
    audio.hasSelectedDevice = false;
    audio.testNoDevice = false;
    audio.muted = false;
    audio.masterVolume = 1.0;
}

void initializeForTestsWithoutDevice() {
    AudioState& audio = state();
    std::lock_guard lock(audio.mutex);
    ensureEngine(audio, true);
}

} // namespace zplus::audio::detail

namespace zplus::audio {

void initialize() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureEngine(audio);
}

bool initialized() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    return audio.engineInitialized;
}

void shutdown() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::shutdownLocked(audio);
}

} // namespace zplus::audio
