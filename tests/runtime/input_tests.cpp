#include "Input.hpp"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

extern "C" void zpi_set_exception_slot(std::uintptr_t* slot) noexcept;
extern "C" const char* zpi_read_managed() noexcept;
extern "C" const char* zpi_string_data(const char* value) noexcept;
extern "C" std::int64_t zpi_push_roots(
    const std::uintptr_t* slots, std::size_t count) noexcept;
extern "C" std::int64_t zpi_pop_roots() noexcept;
extern "C" std::int64_t zpi_collect_strings() noexcept;
extern "C" std::int64_t zpi_managed_string_count() noexcept;
extern "C" std::int64_t zpi_memory_shutdown() noexcept;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class FlushTrackingBuffer final : public std::stringbuf {
public:
    bool flushed = false;

protected:
    int sync() override {
        flushed = true;
        return std::stringbuf::sync();
    }
};

class ReadOrderBuffer final : public std::stringbuf {
public:
    ReadOrderBuffer(const bool& promptWasFlushed, bool& readBeforeFlush)
        : std::stringbuf("Ada Lovelace\n"),
          promptWasFlushed_(promptWasFlushed),
          readBeforeFlush_(readBeforeFlush) {}

protected:
    int_type underflow() override {
        if (!attemptedRead_) {
            attemptedRead_ = true;
            readBeforeFlush_ = !promptWasFlushed_;
        }
        return std::stringbuf::underflow();
    }

private:
    const bool& promptWasFlushed_;
    bool& readBeforeFlush_;
    bool attemptedRead_ = false;
};

class FailedFlushBuffer final : public std::stringbuf {
protected:
    int sync() override { return -1; }
};

void testPromptFlushesBeforeReading() {
    FlushTrackingBuffer outputBuffer;
    std::ostream output(&outputBuffer);
    bool readBeforeFlush = false;
    ReadOrderBuffer inputBuffer(outputBuffer.flushed, readBeforeFlush);
    std::istream input(&inputBuffer);

    zplus::runtime::input::writePrompt(output, "Name: ");
    const auto line = zplus::runtime::input::readLine(input);

    require(outputBuffer.flushed, "prompt output was not flushed");
    require(!readBeforeFlush, "input was requested before the prompt flush");
    require(outputBuffer.str() == "Name: ", "prompt text changed");
    require(line && *line == "Ada Lovelace", "line input changed");
}

void testPromptWriteFailureIsReported() {
    FailedFlushBuffer buffer;
    std::ostream output(&buffer);
    try {
        zplus::runtime::input::writePrompt(output, "Prompt: ");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()) == "standard output prompt write failed",
                "prompt write reported an unexpected error");
        return;
    }
    throw std::runtime_error("failed prompt flush was not reported");
}

void testManagedReadStringsSurviveAndCollect() {
    constexpr std::size_t readCount = 5000;
    std::string inputText = "held across reassignment\n";
    for (std::size_t index = 0; index < readCount; ++index) {
        inputText += "reassigned value " + std::to_string(index) + "\n";
    }
    std::istringstream input(inputText);
    std::streambuf* const originalInput = std::cin.rdbuf(input.rdbuf());
    std::uintptr_t exception = 0;
    std::uintptr_t roots[2] = {};
    zpi_set_exception_slot(&exception);
    require(zpi_push_roots(roots, 2) == 0,
            "could not install native input test roots");

    const char* const held = zpi_read_managed();
    roots[0] = reinterpret_cast<std::uintptr_t>(held);
    require(held && std::string_view(zpi_string_data(held)) ==
                "held across reassignment",
            "first managed input string was incorrect");

    for (std::size_t index = 0; index < readCount; ++index) {
        const char* const current = zpi_read_managed();
        roots[1] = reinterpret_cast<std::uintptr_t>(current);
        require(current != nullptr, "managed input returned a null string");
        require(zpi_collect_strings() == 2,
                "managed input collector retained reassigned strings");
        const std::string expected =
            "reassigned value " + std::to_string(index);
        require(std::string_view(zpi_string_data(current)) == expected,
                "managed input string changed after collection");
        require(std::string_view(zpi_string_data(held)) ==
                    "held across reassignment",
                "a still-rooted input string was invalidated");
        require(zpi_managed_string_count() == 2,
                "managed input heap grew with reassigned strings");
    }

    roots[0] = 0;
    require(zpi_collect_strings() == 1,
            "unreferenced managed input string was not reclaimed");
    require(zpi_pop_roots() == 0,
            "native input test root stack was unbalanced");
    require(zpi_memory_shutdown() == 0,
            "native input managed heap did not shut down cleanly");
    zpi_set_exception_slot(nullptr);
    std::cin.rdbuf(originalInput);
    require(exception == 0, "managed input stress test raised an exception");
}

} // namespace

int main() {
    try {
        testPromptFlushesBeforeReading();
        testPromptWriteFailureIsReported();
        testManagedReadStringsSurviveAndCollect();
        std::cout << "Terminal input helper tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
