#include "AudioInternal.hpp"

#include <cmath>
#include <stdexcept>

namespace zplus::audio::detail {

void checkResult(ma_result result, const std::string& operation) {
    if (result != MA_SUCCESS) {
        throw std::runtime_error(operation + " failed: " +
            ma_result_description(result));
    }
}

void validateVolume(double volume) {
    if (!std::isfinite(volume)) {
        throw std::invalid_argument("volume must be a finite number");
    }
}

void validatePosition(double position) {
    if (!std::isfinite(position) || position < 0.0) {
        throw std::invalid_argument("playback position must be a finite non-negative number");
    }
}

} // namespace zplus::audio::detail
