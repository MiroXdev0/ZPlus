#include "../ZGraphics.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <chrono>
#include <deque>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include "../Platform/WindowsWindow.hpp"
#else
#include "../Platform/X11Window.hpp"
#endif

namespace zplus::graphics {

struct Application::Impl {
    PlatformApplication platform;
    struct InputState {
        std::deque<Event> queuedEvents;
        std::unordered_set<int> keys;
        std::unordered_set<int> pressedKeys;
        std::unordered_set<int> releasedKeys;
        std::unordered_set<int> mouseButtons;
        std::unordered_set<int> pressedMouseButtons;
        std::unordered_set<int> releasedMouseButtons;
        int mouseX = 0;
        int mouseY = 0;
        int deltaMs = 0;
        int fps = 0;
        int targetFps = 60;
        std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
    };
    std::unordered_map<int, InputState> input;

    InputState& state(int window) {
        const auto found = input.find(window);
        if (found == input.end()) throw std::out_of_range("invalid graphics window handle");
        return found->second;
    }

    void process(int window, const Event& event) {
        auto& current = state(window);
        switch (event.type) {
            case Event::Type::KeyDown:
                if (current.keys.insert(event.key).second) current.pressedKeys.insert(event.key);
                break;
            case Event::Type::KeyUp:
                if (current.keys.erase(event.key) != 0) current.releasedKeys.insert(event.key);
                break;
            case Event::Type::MouseDown:
                current.mouseX = event.x;
                current.mouseY = event.y;
                if (current.mouseButtons.insert(event.key).second) {
                    current.pressedMouseButtons.insert(event.key);
                }
                break;
            case Event::Type::MouseUp:
                current.mouseX = event.x;
                current.mouseY = event.y;
                if (current.mouseButtons.erase(event.key) != 0) {
                    current.releasedMouseButtons.insert(event.key);
                }
                break;
            case Event::Type::MouseMove:
            case Event::Type::MouseWheel:
                current.mouseX = event.x;
                current.mouseY = event.y;
                break;
            case Event::Type::FocusLost:
                current.releasedKeys.insert(current.keys.begin(), current.keys.end());
                current.keys.clear();
                current.releasedMouseButtons.insert(
                    current.mouseButtons.begin(), current.mouseButtons.end());
                current.mouseButtons.clear();
                current.pressedKeys.clear();
                current.pressedMouseButtons.clear();
                break;
            default:
                break;
        }
    }
};

Application::Application()
    : impl_(std::make_unique<Impl>()) {
}

Application::~Application() = default;

int Application::createWindow(const std::string& title, int width, int height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw std::invalid_argument("window dimensions must be between 1 and 16384");
    }
    const int window = impl_->platform.createWindow(title, width, height);
    impl_->input.emplace(window, Impl::InputState{});
    return window;
}

void Application::setPosition(int window, int x, int y) {
    impl_->platform.setPosition(window, x, y);
}

void Application::resize(int window, int width, int height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw std::invalid_argument("window dimensions must be between 1 and 16384");
    }
    impl_->platform.resize(window, width, height);
}

void Application::setTitle(int window, const std::string& title) {
    impl_->platform.setTitle(window, title);
}

void Application::setBackground(int window, Color color) {
    impl_->platform.setBackground(window, color);
}

void Application::show(int window) { impl_->platform.show(window); }
void Application::hide(int window) { impl_->platform.hide(window); }
void Application::minimize(int window) { impl_->platform.minimize(window); }
void Application::maximize(int window) { impl_->platform.maximize(window); }
void Application::restore(int window) { impl_->platform.restore(window); }
void Application::focus(int window) { impl_->platform.focus(window); }
void Application::close(int window) { impl_->platform.close(window); }
bool Application::isOpen(int window) { return impl_->platform.isOpen(window); }

void Application::drawText(
    int window, const std::string& text, int x, int y, Color color, int size
) {
    if (size <= 0 || size > 512) {
        throw std::invalid_argument("text size must be between 1 and 512");
    }
    impl_->platform.drawText(window, text, x, y, color, size);
}

void Application::drawLine(
    int window, int x1, int y1, int x2, int y2, Color color, int thickness
) {
    if (thickness <= 0 || thickness > 256) {
        throw std::invalid_argument("line thickness must be between 1 and 256");
    }
    impl_->platform.drawLine(window, x1, y1, x2, y2, color, thickness);
}

void Application::drawRectangle(
    int window, int x, int y, int width, int height, Color color, bool filled, int thickness
) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("rectangle dimensions must be positive");
    }
    if (!filled && (thickness <= 0 || thickness > 256)) {
        throw std::invalid_argument("outline thickness must be between 1 and 256");
    }
    impl_->platform.drawRectangle(window, x, y, width, height, color, filled, thickness);
}

void Application::drawCircle(
    int window, int x, int y, int radius, Color color, bool filled, int thickness
) {
    if (radius <= 0 || radius > 8192) {
        throw std::invalid_argument("circle radius must be between 1 and 8192");
    }
    if (!filled && (thickness <= 0 || thickness > 256)) {
        throw std::invalid_argument("outline thickness must be between 1 and 256");
    }
    impl_->platform.drawCircle(window, x, y, radius, color, filled, thickness);
}

void Application::clear(int window) { impl_->platform.clear(window); }
Event Application::pollEvent(int window) {
    auto& state = impl_->state(window);
    if (!state.queuedEvents.empty()) {
        Event event = std::move(state.queuedEvents.front());
        state.queuedEvents.pop_front();
        return event;
    }
    Event event = impl_->platform.pollEvent(window);
    if (event.type != Event::Type::None) impl_->process(window, event);
    return event;
}

Event Application::waitEvent(int window) {
    auto& state = impl_->state(window);
    if (!state.queuedEvents.empty()) {
        Event event = std::move(state.queuedEvents.front());
        state.queuedEvents.pop_front();
        return event;
    }
    Event event = impl_->platform.waitEvent(window);
    if (event.type != Event::Type::None) impl_->process(window, event);
    return event;
}

void Application::update(int window) {
    auto& state = impl_->state(window);
    if (state.targetFps > 0) {
        const auto frameDuration = std::chrono::microseconds(1000000 / state.targetFps);
        std::this_thread::sleep_until(state.lastFrame + frameDuration);
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.lastFrame).count();
    state.deltaMs = static_cast<int>(std::clamp<std::int64_t>(elapsed, 0, 250));
    state.fps = elapsed > 0 ? static_cast<int>(1000 / elapsed) : state.targetFps;
    state.lastFrame = now;
    state.pressedKeys.clear();
    state.releasedKeys.clear();
    state.pressedMouseButtons.clear();
    state.releasedMouseButtons.clear();

    impl_->platform.present(window);
    for (;;) {
        Event event = impl_->platform.pollEvent(window);
        if (event.type == Event::Type::None) break;
        impl_->process(window, event);
        state.queuedEvents.push_back(std::move(event));
    }
}

void Application::setTargetFPS(int window, int framesPerSecond) {
    if (framesPerSecond < 0 || framesPerSecond > 1000) {
        throw std::invalid_argument("target FPS must be between 0 and 1000");
    }
    impl_->state(window).targetFps = framesPerSecond;
}

int Application::deltaTimeMs(int window) const {
    return impl_->input.at(window).deltaMs;
}

int Application::framesPerSecond(int window) const {
    return impl_->input.at(window).fps;
}

bool Application::keyDown(int window, int key) {
    return impl_->state(window).keys.contains(key);
}

bool Application::keyPressed(int window, int key) {
    return impl_->state(window).pressedKeys.contains(key);
}

bool Application::keyReleased(int window, int key) {
    return impl_->state(window).releasedKeys.contains(key);
}

int Application::mouseX(int window) const {
    return impl_->input.at(window).mouseX;
}

int Application::mouseY(int window) const {
    return impl_->input.at(window).mouseY;
}

bool Application::mouseButtonDown(int window, int button) {
    return impl_->state(window).mouseButtons.contains(button);
}

bool Application::mouseButtonPressed(int window, int button) {
    return impl_->state(window).pressedMouseButtons.contains(button);
}

bool Application::mouseButtonReleased(int window, int button) {
    return impl_->state(window).releasedMouseButtons.contains(button);
}

int Application::run(int window) { return impl_->platform.run(window); }

} // namespace zplus::graphics
