#include "Physics.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

double measure(std::int64_t objectCount) {
    const auto world = zplus::physics::createWorld(0.0, 9.8);
    for (std::int64_t i = 0; i < objectCount; ++i) {
        const double x = static_cast<double>(i % 50) * 3.0;
        const double y = static_cast<double>(i / 50) * 3.0;
        const auto body = zplus::physics::createBody(world, "dynamic", x, y);
        zplus::physics::createCircle(world, body, 0.5);
    }

    constexpr int steps = 120;
    const auto start = std::chrono::steady_clock::now();
    for (int step = 0; step < steps; ++step) {
        zplus::physics::update(world, 1.0 / 60.0);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    zplus::physics::destroyWorld(world);
    return std::chrono::duration<double, std::milli>(elapsed).count() / steps;
}

} // namespace

int main() {
    for (const std::int64_t count : {100, 500, 1000}) {
        std::cout << count << " bodies: " << measure(count)
                  << " ms/update (120 fixed steps)\n";
    }
}
