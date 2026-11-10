#pragma once

#include "../ZGraphics.hpp"

#include <memory>

namespace zplus::graphics {

class PlatformApplication {
public:
    PlatformApplication();
    ~PlatformApplication();

    int createWindow(const std::string&, int, int);
    void setPosition(int, int, int);
    void resize(int, int, int);
    void setTitle(int, const std::string&);
    void setBackground(int, Color);
    void show(int);
    void hide(int);
    void minimize(int);
    void maximize(int);
    void restore(int);
    void focus(int);
    void close(int);
    bool isOpen(int);
    void drawText(int, const std::string&, int, int, Color, int);
    void drawLine(int, int, int, int, int, Color, int);
    void drawRectangle(int, int, int, int, int, Color, bool, int);
    void drawCircle(int, int, int, int, Color, bool, int);
    void clear(int);
    void present(int);
    Event pollEvent(int);
    Event waitEvent(int);
    int run(int);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace zplus::graphics
