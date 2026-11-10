#include "ZGraphics.hpp"

#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void require(bool condition) {
    if (!condition) throw std::runtime_error("ZGraphics core test failed");
}

#ifdef _WIN32
static COLORREF pixelAt(HWND window, int x, int y) {
    HDC dc = GetDC(window);
    require(dc != nullptr);
    const COLORREF pixel = GetPixel(dc, x, y);
    ReleaseDC(window, dc);
    require(pixel != CLR_INVALID);
    return pixel;
}
#endif

int main() {
    using zplus::graphics::Color;
    using zplus::graphics::parseColor;

    const Color color = parseColor("#15aBef");
    require(color.red == 0x15);
    require(color.green == 0xab);
    require(color.blue == 0xef);

    bool invalidColorRejected = false;
    try {
        (void)parseColor("blue");
    } catch (const std::invalid_argument&) {
        invalidColorRejected = true;
    }
    require(invalidColorRejected);

#ifdef _WIN32
    zplus::graphics::Application app;
    bool invalidDimensionsRejected = false;
    try {
        (void)app.createWindow("invalid", 0, 100);
    } catch (const std::invalid_argument&) {
        invalidDimensionsRejected = true;
    }
    require(invalidDimensionsRejected);

    const int window = app.createWindow("ZGraphics core state test", 320, 240);
    require(app.isOpen(window));
    app.setPosition(window, 20, 30);
    app.resize(window, 320, 240);
    app.setTargetFPS(window, 1000);
    app.update(window);
    require(app.deltaTimeMs(window) >= 0);
    require(app.framesPerSecond(window) > 0);
    require(!app.keyDown(window, 65));
    require(!app.keyPressed(window, 65));
    require(!app.keyReleased(window, 65));
    require(!app.mouseButtonDown(window, 1));
    require(!app.mouseButtonPressed(window, 1));
    require(!app.mouseButtonReleased(window, 1));

    const HWND nativeWindow = FindWindowW(
        L"ZPlusGraphicsWindow", L"ZGraphics core state test");
    require(nativeWindow != nullptr);
    require(PostMessageW(nativeWindow, WM_KEYDOWN, 'A', 0));
    app.update(window);
    require(app.keyDown(window, 'A'));
    require(app.keyPressed(window, 'A'));
    require(PostMessageW(nativeWindow, WM_KEYUP, 'A', 0));
    app.update(window);
    require(!app.keyDown(window, 'A'));
    require(app.keyReleased(window, 'A'));

    const LPARAM mousePosition = MAKELPARAM(12, 34);
    require(PostMessageW(nativeWindow, WM_MOUSEMOVE, 0, mousePosition));
    require(PostMessageW(nativeWindow, WM_LBUTTONDOWN, MK_LBUTTON, mousePosition));
    app.update(window);
    require(app.mouseX(window) == 12);
    require(app.mouseY(window) == 34);
    require(app.mouseButtonDown(window, 1));
    require(app.mouseButtonPressed(window, 1));
    require(PostMessageW(nativeWindow, WM_LBUTTONUP, 0, mousePosition));
    app.update(window);
    require(!app.mouseButtonDown(window, 1));
    require(app.mouseButtonReleased(window, 1));

    require(PostMessageW(nativeWindow, WM_KEYDOWN, 'D', 0));
    app.update(window);
    require(app.keyDown(window, 'D'));
    require(PostMessageW(nativeWindow, WM_KILLFOCUS, 0, 0));
    app.update(window);
    require(!app.keyDown(window, 'D'));
    require(app.keyReleased(window, 'D'));
    require(app.waitEvent(window).type != zplus::graphics::Event::Type::None);

    bool invalidFpsRejected = false;
    try {
        app.setTargetFPS(window, 1001);
    } catch (const std::invalid_argument&) {
        invalidFpsRejected = true;
    }
    require(invalidFpsRejected);

    const int defaultWindow = app.createWindow("ZGraphics default background test", 120, 80);
    app.show(defaultWindow);
    const HWND defaultNativeWindow = FindWindowW(
        L"ZPlusGraphicsWindow", L"ZGraphics default background test");
    require(defaultNativeWindow != nullptr);
    require(pixelAt(defaultNativeWindow, 10, 10) == RGB(0, 0, 0));
    app.close(defaultWindow);

    app.clear(window);
    app.setBackground(window, {16, 32, 48, 255});
    app.drawRectangle(window, 10, 10, 40, 30, {220, 30, 40, 255}, true, 1);
    app.show(window);
    require(pixelAt(nativeWindow, 5, 5) == RGB(16, 32, 48));
    require(pixelAt(nativeWindow, 20, 20) == RGB(220, 30, 40));

    app.clear(window);
    app.setBackground(window, {24, 48, 72, 255});
    app.drawRectangle(window, 10, 10, 40, 30, {40, 200, 80, 255}, true, 1);
    require(InvalidateRect(nativeWindow, nullptr, TRUE));
    UpdateWindow(nativeWindow);
    require(pixelAt(nativeWindow, 5, 5) == RGB(16, 32, 48));
    require(pixelAt(nativeWindow, 20, 20) == RGB(220, 30, 40));
    app.update(window);
    require(pixelAt(nativeWindow, 5, 5) == RGB(24, 48, 72));
    require(pixelAt(nativeWindow, 20, 20) == RGB(40, 200, 80));

    app.resize(window, 360, 280);
    UpdateWindow(nativeWindow);
    app.update(window);
    require(pixelAt(nativeWindow, 5, 5) == RGB(24, 48, 72));
    require(pixelAt(nativeWindow, 20, 20) == RGB(40, 200, 80));
    app.minimize(window);
    app.update(window);
    app.restore(window);
    app.update(window);
    UpdateWindow(nativeWindow);
    require(pixelAt(nativeWindow, 5, 5) == RGB(24, 48, 72));
    require(pixelAt(nativeWindow, 20, 20) == RGB(40, 200, 80));

    app.setBackground(window, color);
    app.drawText(window, "Z+ test", 8, 8, {0, 0, 0, 255}, 16);
    app.drawLine(window, 0, 0, 20, 20, {0, 0, 0, 255}, 1);
    app.drawRectangle(window, 10, 10, 40, 30, color, true, 1);
    app.drawCircle(window, 80, 80, 15, color, false, 2);
    app.clear(window);
    app.close(window);
    require(!app.isOpen(window));
#endif
    return 0;
}
