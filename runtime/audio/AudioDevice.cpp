#include "AudioInternal.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <limits>
#include <stdexcept>

namespace zplus::audio::detail {

void ensureContext(AudioState& audio) {
    if (audio.contextInitialized) return;
    checkResult(ma_context_init(nullptr, 0, nullptr, &audio.context),
        "initialize audio device context");
    audio.contextInitialized = true;
    refreshDevices(audio);
}

void refreshDevices(AudioState& audio) {
    if (!audio.contextInitialized) return;
    ma_device_info* playbackDevices = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(
            &audio.context, &playbackDevices, &playbackCount, nullptr, nullptr) != MA_SUCCESS) {
        audio.devices.clear();
        return;
    }

    std::deque<Device> refreshed;
    for (ma_uint32 index = 0; index < playbackCount; ++index) {
        ma_device probe;
        ma_device_config probeConfig = ma_device_config_init(ma_device_type_playback);
        probeConfig.playback.pDeviceID = &playbackDevices[index].id;
        probeConfig.playback.format = ma_format_unknown;
        probeConfig.playback.channels = 0;
        probeConfig.sampleRate = 0;
        if (ma_device_init(&audio.context, &probeConfig, &probe) != MA_SUCCESS) {
            continue;
        }
        ma_device_uninit(&probe);

        Device device;
        device.id = playbackDevices[index].id;
        device.name = playbackDevices[index].name;
        device.isDefault = playbackDevices[index].isDefault == MA_TRUE;
        refreshed.push_back(std::move(device));
    }
    audio.devices = std::move(refreshed);
}

} // namespace zplus::audio::detail

namespace zplus::audio {

std::int64_t deviceCount() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    if (audio.devices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("audio device count exceeds the supported range");
    }
    return static_cast<std::int64_t>(audio.devices.size());
}

std::string deviceName(std::int64_t index) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    if (index < 0 || static_cast<std::uint64_t>(index) >= audio.devices.size()) {
        throw std::out_of_range("audio device index is out of range");
    }
    return audio.devices[static_cast<std::size_t>(index)].name;
}

const char* deviceNameData(std::int64_t index) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    if (index < 0 || static_cast<std::uint64_t>(index) >= audio.devices.size()) {
        throw std::out_of_range("audio device index is out of range");
    }
    return audio.devices[static_cast<std::size_t>(index)].name.c_str();
}

std::string defaultDevice() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    const auto found = std::find_if(audio.devices.begin(), audio.devices.end(),
        [](const detail::Device& device) { return device.isDefault; });
    if (found == audio.devices.end()) {
        throw std::runtime_error("audio device unavailable: no default playback device was found");
    }
    return found->name;
}

const char* defaultDeviceData() {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    const auto found = std::find_if(audio.devices.begin(), audio.devices.end(),
        [](const detail::Device& device) { return device.isDefault; });
    if (found == audio.devices.end()) {
        throw std::runtime_error("audio device unavailable: no default playback device was found");
    }
    return found->name.c_str();
}

void selectDevice(const std::string& name) {
    auto& audio = detail::state();
    std::lock_guard lock(audio.mutex);
    detail::ensureContext(audio);
    const auto found = std::find_if(audio.devices.begin(), audio.devices.end(),
        [&name](const detail::Device& device) { return device.name == name; });
    if (found == audio.devices.end()) {
        throw std::out_of_range("audio playback device '" + name + "' was not found");
    }
    if (audio.engineInitialized && !audio.resources.empty()) {
        throw std::runtime_error(
            "cannot select an audio device while audio resources are loaded");
    }
    if (audio.engineInitialized) {
        ma_engine_uninit(&audio.engine);
        audio.engineInitialized = false;
    }
    audio.selectedDevice = found->id;
    audio.hasSelectedDevice = true;
    if (!audio.testNoDevice) detail::ensureEngine(audio);
}

} // namespace zplus::audio
