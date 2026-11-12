#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace zplus::graphics {

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
};

struct Event {
    enum class Type {
        None = 0,
        Closed = 1,
        MouseDown = 2,
        MouseUp = 3,
        MouseMove = 4,
        KeyDown = 5,
        TextInput = 6,
        KeyUp = 7,
        FocusGained = 8,
        FocusLost = 9,
        Resized = 10,
        MouseWheel = 11
    };

    Type type = Type::None;
    int x = 0;
    int y = 0;
    int key = 0;
    std::string text;
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int createWindow(const std::string& title, int width, int height);
    void setPosition(int window, int x, int y);
    void resize(int window, int width, int height);
    void setTitle(int window, const std::string& title);
    void setBackground(int window, Color color);
    void show(int window);
    void hide(int window);
    void minimize(int window);
    void maximize(int window);
    void restore(int window);
    void focus(int window);
    void close(int window);
    bool isOpen(int window);

    void drawText(int window, const std::string& text, int x, int y, Color color, int size);
    void drawImage(int window, const std::string& path, int x, int y, int width, int height);
    void drawLine(int window, int x1, int y1, int x2, int y2, Color color, int thickness);
    void drawRectangle(
        int window,
        int x,
        int y,
        int width,
        int height,
        Color color,
        bool filled,
        int thickness
    );
    void drawCircle(
        int window,
        int x,
        int y,
        int radius,
        Color color,
        bool filled,
        int thickness
    );
    void clear(int window);

    Event pollEvent(int window);
    Event waitEvent(int window);
    void update(int window);
    void setTargetFPS(int window, int framesPerSecond);
    int deltaTimeMs(int window) const;
    int framesPerSecond(int window) const;
    bool keyDown(int window, int key);
    bool keyPressed(int window, int key);
    bool keyReleased(int window, int key);
    int mouseX(int window) const;
    int mouseY(int window) const;
    bool mouseButtonDown(int window, int button);
    bool mouseButtonPressed(int window, int button);
    bool mouseButtonReleased(int window, int button);
    int run(int window);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Color parseColor(const std::string& value);

} // namespace zplus::graphics
