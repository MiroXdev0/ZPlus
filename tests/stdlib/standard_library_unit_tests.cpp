#include "StandardLibrary.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireThrows(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        const std::string text = "\xc3\xa9\xd0\x96\xf0\x9f\x99\x82";
        require(zplus::stdlib::stringLength(text) == 3, "Unicode code-point length failed");
        require(
            zplus::stdlib::substring(text, 1, 1) == "\xd0\x96",
            "Unicode substring failed");
        require(
            zplus::stdlib::stringUnary("reverse", text) ==
                "\xf0\x9f\x99\x82\xd0\x96\xc3\xa9",
            "Unicode reversal failed");
        require(
            zplus::stdlib::stringUnary("upper", "\xc3\xa9\xd0\xb6") ==
                "\xc3\x89\xd0\x96",
            "Unicode uppercase conversion failed");
        require(
            zplus::stdlib::stringUnary("lower", "\xc3\x89\xd0\x96") ==
                "\xc3\xa9\xd0\xb6",
            "Unicode lowercase conversion failed");
        require(
            zplus::stdlib::stringUnary("trim", "\xc2\xa0 \xc3\xa9 \xc2\xa0") ==
                "\xc3\xa9",
            "Unicode whitespace trimming failed");
        require(
            zplus::stdlib::stringBinary("contains", text, "\xd0\x96") == "true",
            "Unicode search failed");
        requireThrows(
            [] { static_cast<void>(zplus::stdlib::stringLength("\xc0\xaf")); },
            "overlong UTF-8 was accepted");
        requireThrows(
            [] { static_cast<void>(zplus::stdlib::substring("\xc3", 0, 1)); },
            "truncated UTF-8 was accepted");
        std::cout << "Unicode standard-library tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
