#include "audio/Audio.hpp"
#include "audio/AudioInternal.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Function>
void requireFailure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}

} // namespace

int main() {
    try {
        const std::filesystem::path assets = ZAUDIO_TEST_ASSET_DIR;
        const auto wav = (assets / "tone.wav").string();
        const auto ogg = (assets / "tone.ogg").string();
        const auto mp3 = (assets / "tone.mp3").string();

        zplus::audio::detail::initializeForTestsWithoutDevice();
        require(zplus::audio::initialized(), "headless engine did not initialize");
        zplus::audio::initialize();
        require(zplus::audio::initialized(), "repeated initialization changed engine state");

        const auto musicWav = zplus::audio::loadMusic(wav);
        const auto musicOgg = zplus::audio::loadMusic(ogg);
        const auto musicMp3 = zplus::audio::loadMusic(mp3);
        const auto sound = zplus::audio::loadSound(wav);
        require(musicWav != musicOgg && musicOgg != musicMp3 &&
            musicMp3 != sound, "audio handles are not unique");
        require(zplus::audio::getDuration(musicWav) > 0.9,
            "WAV duration was not decoded");
        require(zplus::audio::getDuration(musicOgg) > 0.9,
            "OGG duration was not decoded");
        require(zplus::audio::getDuration(musicMp3) > 0.8,
            "MP3 duration was not decoded");

        zplus::audio::setVolume(sound, 0.25);
        require(std::abs(zplus::audio::getVolume(sound) - 0.25) < 0.001,
            "per-resource volume was not retained");
        zplus::audio::setVolume(sound, 2.0);
        require(zplus::audio::getVolume(sound) == 1.0,
            "volume was not clamped");
        requireFailure([&] {
            zplus::audio::setVolume(sound, std::numeric_limits<double>::infinity());
        }, "non-finite volume was accepted");
        zplus::audio::setLooping(musicWav, true);
        require(zplus::audio::isLooping(musicWav), "looping was not enabled");
        zplus::audio::setPlaybackPosition(musicWav, 0.2);
        require(std::abs(zplus::audio::getPlaybackPosition(musicWav) - 0.2) < 0.02,
            "music seek position was not retained");

        zplus::audio::play(musicWav);
        require(zplus::audio::isPlaying(musicWav), "music did not start");
        zplus::audio::pause(musicWav);
        require(zplus::audio::isPaused(musicWav) &&
            !zplus::audio::isPlaying(musicWav), "music did not pause");
        zplus::audio::resume(musicWav);
        require(zplus::audio::isPlaying(musicWav), "music did not resume");
        zplus::audio::stop(musicWav);
        require(zplus::audio::isStopped(musicWav), "music did not stop");

        zplus::audio::play(sound);
        zplus::audio::play(sound);
        require(zplus::audio::isPlaying(sound),
            "simultaneous sound instances did not start");
        zplus::audio::pause(sound);
        require(zplus::audio::isPaused(sound), "sound instances did not pause");
        zplus::audio::resume(sound);
        require(zplus::audio::isPlaying(sound), "sound instances did not resume");
        zplus::audio::stop(sound);
        require(zplus::audio::isStopped(sound), "sound instances did not stop");

        zplus::audio::setMasterVolume(0.4);
        zplus::audio::mute();
        require(zplus::audio::isMuted() &&
            zplus::audio::getMasterVolume() == 0.4 &&
            zplus::audio::getVolume(sound) == 1.0,
            "master mute changed per-resource volume");
        zplus::audio::unmute();
        require(!zplus::audio::isMuted(), "unmute did not restore master state");

        requireFailure([&] { zplus::audio::loadMusic((assets / "missing.wav").string()); },
            "missing audio file was accepted");
        requireFailure([&] { zplus::audio::loadSound((assets / "tone.flac").string()); },
            "unsupported audio format was accepted");
        const auto corrupt = std::filesystem::temp_directory_path() /
            "zplus-corrupt-audio.wav";
        {
            std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
            output << "not a valid WAV file";
        }
        requireFailure([&] { zplus::audio::loadSound(corrupt.string()); },
            "corrupt WAV file was accepted");
        std::filesystem::remove(corrupt);

        for (int cycle = 0; cycle < 100; ++cycle) {
            const auto handle = zplus::audio::loadSound(wav);
            zplus::audio::play(handle);
            zplus::audio::unload(handle);
            requireFailure([&] { zplus::audio::play(handle); },
                "unloaded handle remained usable");
        }
        zplus::audio::play(musicOgg);
        zplus::audio::unload(musicOgg);
        requireFailure([&] { zplus::audio::getDuration(musicOgg); },
            "unloaded active music handle remained usable");
        zplus::audio::unloadAll();
        requireFailure([&] { zplus::audio::getDuration(musicWav); },
            "unloadAll left a stale handle valid");

        const auto deviceCount = zplus::audio::deviceCount();
        if (deviceCount > 0) {
            const char* firstDeviceName = zplus::audio::deviceNameData(0);
            require(firstDeviceName != nullptr && std::string(firstDeviceName).size() > 0,
                "device name pointer was null or empty");
            const char* defaultDeviceName = zplus::audio::defaultDeviceData();
            require(defaultDeviceName != nullptr && std::string(defaultDeviceName).size() > 0,
                "default device pointer was null or empty");
        }
        requireFailure([&] { zplus::audio::selectDevice("invalid device"); },
            "unknown playback device was accepted");
        zplus::audio::shutdown();
        zplus::audio::shutdown();
        require(!zplus::audio::initialized(), "shutdown left the engine initialized");
        zplus::audio::detail::initializeForTestsWithoutDevice();
        require(zplus::audio::initialized(), "engine did not reinitialize after shutdown");
        const auto active = zplus::audio::loadSound(wav);
        zplus::audio::play(active);
        zplus::audio::shutdown();
        require(!zplus::audio::initialized(),
            "shutdown with active playback did not release the engine");

        std::cout << "Audio API tests passed (headless; no hardware playback tested)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio API test failed: " << error.what() << '\n';
        zplus::audio::shutdown();
        return 1;
    }
}
