#include "AudioInternal.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace zplus::audio::detail {
namespace {

std::string lowercaseExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

} // namespace

void validateAudioFile(const std::string& path) {
    if (path.empty()) throw std::invalid_argument("audio file path must not be empty");
    const std::string extension = lowercaseExtension(path);
    if (extension != ".wav" && extension != ".ogg" && extension != ".mp3") {
        throw std::invalid_argument(
            "unsupported audio format for '" + path + "'; expected WAV, OGG Vorbis, or MP3");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::runtime_error("audio file not found or unreadable: '" + path + "'");
    }
}

double audioFileDuration(const std::string& path) {
    ma_decoder decoder{};
    checkResult(ma_decoder_init_file(path.c_str(), nullptr, &decoder),
        "initialize decoder for '" + path + "'");
    struct DecoderScope {
        ma_decoder& decoder;
        ~DecoderScope() { ma_decoder_uninit(&decoder); }
    } cleanup{decoder};

    ma_uint64 frames = 0;
    checkResult(ma_decoder_get_length_in_pcm_frames(&decoder, &frames),
        "query audio file length for '" + path + "'");
    if (decoder.outputSampleRate == 0) {
        throw std::runtime_error("audio decoder returned an invalid sample rate");
    }
    return static_cast<double>(frames) /
        static_cast<double>(decoder.outputSampleRate);
}

} // namespace zplus::audio::detail
