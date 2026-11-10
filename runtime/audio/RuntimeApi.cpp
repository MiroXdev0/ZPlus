#include "Audio.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifdef ZAUDIO_RUNTIME_EXPORTS
#define ZAUDIO_API extern "C" __declspec(dllexport)
#else
#define ZAUDIO_API extern "C" __declspec(dllimport)
#endif
#else
#define ZAUDIO_API extern "C" __attribute__((visibility("default")))
#endif

namespace {

[[noreturn]] void fail(const char* message) noexcept {
    std::fprintf(stderr, "native Z+ audio runtime error: %s\n", message);
    std::fflush(stderr);
    std::exit(EXIT_FAILURE);
}

template<typename Result, typename Function>
Result guard(Function&& function) noexcept {
    try {
        return function();
    } catch (const std::exception& error) {
        fail(error.what());
    } catch (...) {
        fail("unknown audio runtime failure");
    }
}

std::string checkedText(const char* value, const char* name) {
    if (!value) throw std::invalid_argument(std::string(name) + " must not be null");
    return value;
}

double number(std::int64_t bits) {
    return std::bit_cast<double>(static_cast<std::uint64_t>(bits));
}

std::int64_t encodedNumber(double value) {
    return std::bit_cast<std::int64_t>(value);
}

std::int64_t integerNumber(std::int64_t bits, const char* argument) {
    const double decoded = number(bits);
    if (!std::isfinite(decoded) || std::trunc(decoded) != decoded ||
        decoded < -0x1p63 || decoded >= 0x1p63) {
        throw std::invalid_argument(
            std::string(argument) + " must be an integer in the supported range");
    }
    return static_cast<std::int64_t>(decoded);
}

} // namespace

ZAUDIO_API std::int64_t zpaudio_initialize() noexcept {
    return guard<std::int64_t>([] { zplus::audio::initialize(); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_initialized() noexcept {
    return guard<std::int64_t>([] { return zplus::audio::initialized() ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_shutdown() noexcept {
    return guard<std::int64_t>([] { zplus::audio::shutdown(); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_loadMusic(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::audio::loadMusic(checkedText(path, "path"));
    });
}

ZAUDIO_API std::int64_t zpaudio_loadSound(const char* path) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::audio::loadSound(checkedText(path, "path"));
    });
}

ZAUDIO_API std::int64_t zpaudio_unload(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::unload(handle); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_unloadAll() noexcept {
    return guard<std::int64_t>([] { zplus::audio::unloadAll(); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_play(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::play(handle); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_pause(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::pause(handle); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_resume(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::resume(handle); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_stop(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::stop(handle); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_isPlaying(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { return zplus::audio::isPlaying(handle) ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_isPaused(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { return zplus::audio::isPaused(handle) ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_isStopped(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { return zplus::audio::isStopped(handle) ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_setVolume(
    std::int64_t handle, std::int64_t value
) noexcept {
    return guard<std::int64_t>([&] { zplus::audio::setVolume(handle, number(value)); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_getVolume(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return encodedNumber(zplus::audio::getVolume(handle));
    });
}

ZAUDIO_API std::int64_t zpaudio_setLooping(
    std::int64_t handle, std::int64_t looping
) noexcept {
    return guard<std::int64_t>([&] {
        zplus::audio::setLooping(handle, looping != 0);
        return 0;
    });
}

ZAUDIO_API std::int64_t zpaudio_isLooping(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] { return zplus::audio::isLooping(handle) ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_setPlaybackPosition(
    std::int64_t handle, std::int64_t value
) noexcept {
    return guard<std::int64_t>([&] {
        zplus::audio::setPlaybackPosition(handle, number(value));
        return 0;
    });
}

ZAUDIO_API std::int64_t zpaudio_getPlaybackPosition(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return encodedNumber(zplus::audio::getPlaybackPosition(handle));
    });
}

ZAUDIO_API std::int64_t zpaudio_getDuration(std::int64_t handle) noexcept {
    return guard<std::int64_t>([&] {
        return encodedNumber(zplus::audio::getDuration(handle));
    });
}

ZAUDIO_API std::int64_t zpaudio_setMasterVolume(std::int64_t value) noexcept {
    return guard<std::int64_t>([&] {
        zplus::audio::setMasterVolume(number(value));
        return 0;
    });
}

ZAUDIO_API std::int64_t zpaudio_getMasterVolume() noexcept {
    return guard<std::int64_t>([] {
        return encodedNumber(zplus::audio::getMasterVolume());
    });
}

ZAUDIO_API std::int64_t zpaudio_mute() noexcept {
    return guard<std::int64_t>([] { zplus::audio::mute(); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_unmute() noexcept {
    return guard<std::int64_t>([] { zplus::audio::unmute(); return 0; });
}

ZAUDIO_API std::int64_t zpaudio_isMuted() noexcept {
    return guard<std::int64_t>([] { return zplus::audio::isMuted() ? 1 : 0; });
}

ZAUDIO_API std::int64_t zpaudio_deviceCount() noexcept {
    return guard<std::int64_t>([] {
        return encodedNumber(static_cast<double>(zplus::audio::deviceCount()));
    });
}

ZAUDIO_API const char* zpaudio_deviceName(std::int64_t index) noexcept {
    return guard<const char*>([&] {
        return zplus::audio::deviceNameData(integerNumber(index, "device index"));
    });
}

ZAUDIO_API const char* zpaudio_defaultDevice() noexcept {
    return guard<const char*>([] { return zplus::audio::defaultDeviceData(); });
}

ZAUDIO_API std::int64_t zpaudio_selectDevice(const char* name) noexcept {
    return guard<std::int64_t>([&] {
        zplus::audio::selectDevice(checkedText(name, "name"));
        return 0;
    });
}
