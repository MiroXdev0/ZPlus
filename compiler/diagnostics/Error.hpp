#pragma once

#include <string>

namespace mpp::diagnostics {

struct Error {
    std::string message;
    int line = 0;
    int column = 0;
};

}
