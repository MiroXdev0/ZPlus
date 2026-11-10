#pragma once

#include "Audio.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace zplus::audio::detail {

enum class ResourceKind {
    Music,
    Sound
};

struct SoundInstance {
    ma_sound sound{};
    bool initialized = false;
    bool paused = false;
    double pausedPosition = 0.0;
};

struct Resource {
    ResourceKind kind = ResourceKind::Music;
    std::string path;
    ma_sound sound{};
    bool initialized = false;
    bool paused = false;
    double pausedPosition = 0.0;
    double durationSeconds = 0.0;
    double volume = 1.0;
    bool looping = false;
    std::vector<std::unique_ptr<SoundInstance>> instances;
};

struct Device {
    ma_device_id id{};
    std::string name;
    bool isDefault = false;
};

struct AudioState {
    std::mutex mutex;
    ma_context context{};
    bool contextInitialized = false;
    ma_engine engine{};
    bool engineInitialized = false;
    bool testNoDevice = false;
    bool muted = false;
    double masterVolume = 1.0;
    std::deque<Device> devices;
    ma_device_id selectedDevice{};
    bool hasSelectedDevice = false;
    std::unordered_map<std::int64_t, std::unique_ptr<Resource>> resources;
    std::int64_t nextHandle = 1;
};

AudioState& state();
void ensureContext(AudioState& audio);
void refreshDevices(AudioState& audio);
void ensureEngine(AudioState& audio, bool noDevice = false);
void shutdownLocked(AudioState& audio);
Resource& checkedResource(AudioState& audio, std::int64_t handle);
const Resource& checkedResource(const AudioState& audio, std::int64_t handle);
void checkResult(ma_result result, const std::string& operation);
void validateVolume(double volume);
void validatePosition(double position);
void validateAudioFile(const std::string& path);
double audioFileDuration(const std::string& path);
void initializeForTestsWithoutDevice();

} // namespace zplus::audio::detail
