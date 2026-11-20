#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace {

double* volatile allocationObserver = nullptr;

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void observeAllocation(double* value) {
    allocationObserver = value;
}

double arithmetic(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        checksum += std::fmod(i * 3 + 7, 97);
    }
    return checksum;
}

double mixed(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        const double value = std::fmod(i, 10) + 0.5;
        checksum += (value * 1.5 + 0.25) * 2;
    }
    return checksum;
}

double transform(double value) {
    return std::fmod(value * 7 + 3, 101);
}

double functions(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        checksum += transform(std::fmod(i, 1000));
    }
    return checksum;
}

double branching(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        checksum += std::fmod(i, 2) == 0 ? i * 3 + 1 : i * 2 - 1;
    }
    return checksum;
}

double combinedTransform(double value) {
    return std::fmod(value * 7 + 11, 103);
}

double combined(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        const double value = combinedTransform(std::fmod(i, 1000));
        checksum += std::fmod(i, 3) == 0
            ? value * 2
            : std::fmod(value + std::fmod(i, 17), 89);
    }
    return checksum;
}

double arrays(std::uint64_t iterations) {
    std::array<double, 8> values{};
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        const auto index = static_cast<std::size_t>(
            std::fmod(i, static_cast<double>(values.size())));
        values[index] += std::fmod(i, 7);
    }
    double checksum = 0;
    for (double value : values) checksum += value;
    return checksum;
}

struct Counter {
    double value;
};

double structs(std::uint64_t iterations) {
    Counter counter{0};
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        counter.value += std::fmod(i, 7) + 1;
    }
    return counter.value;
}

double memory(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        auto transient = std::make_unique<double[]>(3);
        transient[0] = i;
        transient[1] = std::fmod(i, 11) + 2;
        transient[2] = i + 1;
        observeAllocation(transient.get());
        checksum += transient[1];
    }
    return checksum;
}

double tightLoop(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        checksum += i * 3 + 1;
    }
    return checksum;
}

double recursiveSum(double value) {
    if (value == 0) return 0;
    return value + recursiveSum(value - 1);
}

double recursion(std::uint64_t iterations) {
    double checksum = 0;
    for (double i = 0; i < static_cast<double>(iterations); i += 1) {
        checksum += recursiveSum(std::fmod(i, 12));
    }
    return checksum;
}

std::uint64_t defaultIterations(std::string_view workload) {
    if (workload == "mixed" || workload == "arrays" ||
        workload == "structs" || workload == "memory") {
        return 10'000;
    }
    if (workload == "tight_loop") return 1'000'000;
    return 50'000;
}

double run(std::string_view workload, std::uint64_t iterations) {
    if (workload == "arithmetic") return arithmetic(iterations);
    if (workload == "mixed") return mixed(iterations);
    if (workload == "functions") return functions(iterations);
    if (workload == "branching") return branching(iterations);
    if (workload == "combined") return combined(iterations);
    if (workload == "arrays") return arrays(iterations);
    if (workload == "structs") return structs(iterations);
    if (workload == "memory") return memory(iterations);
    if (workload == "tight_loop") return tightLoop(iterations);
    if (workload == "recursion") return recursion(iterations);
    return std::nan("");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::fprintf(
            stderr,
            "usage: cpp_reference workload [iterations] [repetitions]\n");
        return 2;
    }

    const std::string_view workload(argv[1]);
    const std::uint64_t iterations = argc >= 3
        ? std::strtoull(argv[2], nullptr, 10)
        : defaultIterations(workload);
    const std::uint64_t repetitions = argc >= 4
        ? std::strtoull(argv[3], nullptr, 10)
        : 1;
    if (iterations == 0 || repetitions == 0) return 2;

    double checksum = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t repeat = 0; repeat < repetitions; ++repeat) {
        checksum += run(workload, iterations);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (!std::isfinite(checksum)) return 3;

    const auto elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    std::printf("CHECKSUM %.*s %.0f\n", static_cast<int>(workload.size()),
                workload.data(), checksum);
    std::printf("STEADY_NS %lld\n", static_cast<long long>(elapsedNs));
    return 0;
}
