#pragma once

#include <cstdint>
#include <string>

namespace zplus::audio {

void initialize();
bool initialized();
void shutdown();

std::int64_t loadMusic(const std::string& path);
std::int64_t loadSound(const std::string& path);
void unload(std::int64_t handle);
void unloadAll();

void play(std::int64_t handle);
void pause(std::int64_t handle);
void resume(std::int64_t handle);
void stop(std::int64_t handle);
bool isPlaying(std::int64_t handle);
bool isPaused(std::int64_t handle);
bool isStopped(std::int64_t handle);

void setVolume(std::int64_t handle, double volume);
double getVolume(std::int64_t handle);
void setLooping(std::int64_t handle, bool looping);
bool isLooping(std::int64_t handle);
void setPlaybackPosition(std::int64_t handle, double seconds);
double getPlaybackPosition(std::int64_t handle);
double getDuration(std::int64_t handle);

void setMasterVolume(double volume);
double getMasterVolume();
void mute();
void unmute();
bool isMuted();

std::int64_t deviceCount();
std::string deviceName(std::int64_t index);
const char* deviceNameData(std::int64_t index);
std::string defaultDevice();
const char* defaultDeviceData();
void selectDevice(const std::string& name);

} // namespace zplus::audio
