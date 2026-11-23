#include "WindowsWindow.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>

namespace zplus::graphics {
namespace {

constexpr wchar_t kWindowClass[] = L"ZPlusGraphicsWindow";

std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::invalid_argument("text is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

COLORREF nativeColor(Color color) {
    return RGB(color.red, color.green, color.blue);
}

} // namespace

struct PlatformApplication::Window {
    struct Surface {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previousBitmap = nullptr;
        int width = 0;
        int height = 0;
    };

    enum class CommandType { Text, Image, Line, Rectangle, Circle };
    struct Command {
        CommandType type;
        std::wstring text;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        int thickness = 1;
        bool filled = false;
        int fontSize = 16;
        Color color;
    };

    HWND handle = nullptr;
    Color background{};
    Color completedBackground{};
    std::vector<Command> commands;
    std::vector<Command> completedCommands;
    Surface completedSurface;
    Surface workingSurface;
    std::array<HFONT, 513> fonts{};
    bool frameDirty = true;
    bool renderFailed = false;
    std::vector<Event> events;
    bool closed = false;
};

PlatformApplication::PlatformApplication() {
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = windowProcedure;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    klass.hbrBackground = nullptr;
    klass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("cannot register Z+ graphics window class");
    }
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Gdiplus::Ok) {
        throw std::runtime_error("cannot initialize Windows image rendering");
    }
}

PlatformApplication::~PlatformApplication() {
    for (auto& window : windows_) {
        if (window->handle && IsWindow(window->handle)) DestroyWindow(window->handle);
        releaseSurfaces(*window);
        releaseFonts(*window);
    }
    if (gdiplusToken_ != 0) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

PlatformApplication::Window& PlatformApplication::get(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > windows_.size() ||
        !windows_[static_cast<std::size_t>(id - 1)]) {
        throw std::out_of_range("invalid graphics window handle");
    }
    return *windows_[static_cast<std::size_t>(id - 1)];
}

PlatformApplication::Window& PlatformApplication::getOpen(int id) {
    auto& window = get(id);
    if (window.closed) throw std::runtime_error("graphics window is closed");
    return window;
}

int PlatformApplication::createWindow(const std::string& title, int width, int height) {
    auto window = std::make_unique<Window>();
    auto* raw = window.get();
    const auto wideTitle = toWide(title);
    raw->handle = CreateWindowExW(
        0, kWindowClass, wideTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
        GetModuleHandleW(nullptr), raw);
    if (!raw->handle) throw std::runtime_error("cannot create native graphics window");
    windows_.push_back(std::move(window));
    return static_cast<int>(windows_.size());
}

void PlatformApplication::setTitle(int id, const std::string& title) {
    if (!SetWindowTextW(getOpen(id).handle, toWide(title).c_str())) {
        throw std::runtime_error("cannot set window title");
    }
}

void PlatformApplication::setPosition(int id, int x, int y) {
    if (!SetWindowPos(getOpen(id).handle, nullptr, x, y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        throw std::runtime_error("cannot set window position");
    }
}

void PlatformApplication::resize(int id, int width, int height) {
    if (!SetWindowPos(getOpen(id).handle, nullptr, 0, 0, width, height,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        throw std::runtime_error("cannot resize window");
    }
}

void PlatformApplication::setBackground(int id, Color color) {
    auto& window = getOpen(id);
    window.background = color;
    window.frameDirty = true;
}

void PlatformApplication::show(int id) {
    auto& window = getOpen(id);
    present(id);
    ShowWindow(window.handle, SW_SHOW);
    UpdateWindow(window.handle);
}

void PlatformApplication::hide(int id) { ShowWindow(getOpen(id).handle, SW_HIDE); }
void PlatformApplication::minimize(int id) { ShowWindow(getOpen(id).handle, SW_MINIMIZE); }
void PlatformApplication::maximize(int id) { ShowWindow(getOpen(id).handle, SW_MAXIMIZE); }
void PlatformApplication::restore(int id) { ShowWindow(getOpen(id).handle, SW_RESTORE); }

void PlatformApplication::focus(int id) {
    const HWND handle = getOpen(id).handle;
    if (!SetForegroundWindow(handle)) throw std::runtime_error("cannot focus window");
    SetFocus(handle);
}

bool PlatformApplication::isOpen(int id) {
    const auto& window = get(id);
    return !window.closed && window.handle && IsWindow(window.handle);
}

void PlatformApplication::close(int id) {
    auto& window = get(id);
    if (window.handle && IsWindow(window.handle)) DestroyWindow(window.handle);
}

void PlatformApplication::drawText(
    int id, const std::string& text, int x, int y, Color color, int size
) {
    auto& window = getOpen(id);
    window.commands.push_back({Window::CommandType::Text, toWide(text), x, y, 0, 0,
                               1, false, size, color});
    window.frameDirty = true;
}

void PlatformApplication::drawImage(
    int id, const std::string& path, int x, int y, int width, int height
) {
    auto& window = getOpen(id);
    Window::Command command{};
    command.type = Window::CommandType::Image;
    command.text = toWide(path);
    command.x1 = x;
    command.y1 = y;
    command.x2 = width;
    command.y2 = height;
    command.color = {255, 255, 255, 255};
    window.commands.push_back(std::move(command));
    window.frameDirty = true;
}

void PlatformApplication::drawLine(
    int id, int x1, int y1, int x2, int y2, Color color, int thickness
) {
    auto& window = getOpen(id);
    window.commands.push_back({Window::CommandType::Line, {}, x1, y1, x2, y2, thickness, false, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::drawRectangle(
    int id, int x, int y, int width, int height, Color color, bool filled, int thickness
) {
    auto& window = getOpen(id);
    window.commands.push_back({Window::CommandType::Rectangle, {}, x, y, width, height,
                               thickness, filled, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::drawCircle(
    int id, int x, int y, int radius, Color color, bool filled, int thickness
) {
    auto& window = getOpen(id);
    window.commands.push_back({Window::CommandType::Circle, {}, x, y, radius, 0,
                               thickness, filled, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::clear(int id) {
    auto& window = getOpen(id);
    window.commands.clear();
    window.frameDirty = true;
}

void PlatformApplication::present(int id) {
    auto& window = get(id);
    if (window.closed || !window.handle || !IsWindow(window.handle)) return;
    if (window.renderFailed) {
        throw std::runtime_error("cannot render or present the graphics frame");
    }
    if (!window.frameDirty) return;

    const bool hasSurface = ensureSurfaces(window);
    if (window.renderFailed) {
        throw std::runtime_error("cannot create the graphics backbuffer");
    }
    if (hasSurface && !render(window, window.workingSurface.dc, false)) {
        window.renderFailed = true;
        throw std::runtime_error("cannot render the graphics frame");
    }

    window.completedCommands = window.commands;
    window.completedBackground = window.background;
    window.frameDirty = false;

    if (hasSurface) {
        std::swap(window.completedSurface, window.workingSurface);
        bool copied = true;
        if (!IsIconic(window.handle)) {
            HDC dc = GetDC(window.handle);
            copied = dc &&
                BitBlt(dc, 0, 0, window.completedSurface.width,
                       window.completedSurface.height, window.completedSurface.dc,
                       0, 0, SRCCOPY) != 0;
            if (dc) ReleaseDC(window.handle, dc);
        }
        if (!copied) {
            window.renderFailed = true;
            window.frameDirty = true;
            throw std::runtime_error("cannot present the graphics frame");
        }
    }
}

Event PlatformApplication::pollEvent(int id) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    auto& events = get(id).events;
    if (events.empty()) return {};
    Event event = std::move(events.front());
    events.erase(events.begin());
    return event;
}

Event PlatformApplication::waitEvent(int id) {
    auto& window = getOpen(id);
    while (window.events.empty() && !window.closed) {
        MSG message{};
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            window.closed = true;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return pollEvent(id);
}

int PlatformApplication::run(int id) {
    auto& target = get(id);
    if (!target.handle || target.closed) return 0;
    present(id);
    MSG message{};
    while (!target.closed) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                target.closed = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!target.closed) MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
    return 0;
}

void PlatformApplication::releaseSurface(HDC& dc, HBITMAP& bitmap, HGDIOBJ& previousBitmap) {
    if (dc && previousBitmap) SelectObject(dc, previousBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    dc = nullptr;
    bitmap = nullptr;
    previousBitmap = nullptr;
}

void PlatformApplication::releaseSurfaces(Window& window) {
    releaseSurface(window.completedSurface.dc, window.completedSurface.bitmap,
                   window.completedSurface.previousBitmap);
    releaseSurface(window.workingSurface.dc, window.workingSurface.bitmap,
                   window.workingSurface.previousBitmap);
    window.completedSurface.width = 0;
    window.completedSurface.height = 0;
    window.workingSurface.width = 0;
    window.workingSurface.height = 0;
}

void PlatformApplication::releaseFonts(Window& window) {
    for (HFONT& font : window.fonts) {
        if (font) DeleteObject(font);
        font = nullptr;
    }
}

bool PlatformApplication::render(Window& window, HDC dc, bool completed) {
    RECT bounds{};
    if (!GetClientRect(window.handle, &bounds)) return false;
    const Color backgroundColor = completed ? window.completedBackground : window.background;
    const auto& commands = completed ? window.completedCommands : window.commands;
    HBRUSH background = CreateSolidBrush(nativeColor(backgroundColor));
    if (!background) return false;
    const bool backgroundDrawn = FillRect(dc, &bounds, background) != 0;
    DeleteObject(background);
    if (!backgroundDrawn || SetBkMode(dc, TRANSPARENT) == 0) return false;

    for (const auto& command : commands) {
        const COLORREF color = nativeColor(command.color);
        const int penStyle = command.filled ? PS_NULL : PS_SOLID;
        HPEN pen = CreatePen(penStyle, command.thickness, color);
        if (!pen) return false;
        HGDIOBJ oldPen = SelectObject(dc, pen);
        if (!oldPen || oldPen == HGDI_ERROR) {
            DeleteObject(pen);
            return false;
        }
        HBRUSH brush = command.filled
            ? CreateSolidBrush(color)
            : static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        if (!brush) {
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            return false;
        }
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        if (!oldBrush || oldBrush == HGDI_ERROR) {
            SelectObject(dc, oldPen);
            if (command.filled) DeleteObject(brush);
            DeleteObject(pen);
            return false;
        }
        bool succeeded = true;

        switch (command.type) {
            case Window::CommandType::Text: {
                SetTextColor(dc, color);
                HFONT& font = window.fonts[static_cast<std::size_t>(command.fontSize)];
                if (!font) {
                    font = CreateFontW(-command.fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                }
                if (!font) {
                    succeeded = false;
                } else {
                    HGDIOBJ oldFont = SelectObject(dc, font);
                    if (!oldFont || oldFont == HGDI_ERROR) {
                        succeeded = false;
                    } else {
                        RECT textBounds{command.x1, command.y1, bounds.right, bounds.bottom};
                        succeeded = command.text.empty() ||
                            DrawTextW(dc, command.text.c_str(),
                                      static_cast<int>(command.text.size()), &textBounds,
                                      DT_LEFT | DT_TOP | DT_WORDBREAK) != 0;
                        SelectObject(dc, oldFont);
                    }
                }
                break;
            }
            case Window::CommandType::Image: {
                Gdiplus::Image image(command.text.c_str());
                if (image.GetLastStatus() != Gdiplus::Ok) {
                    succeeded = false;
                } else {
                    Gdiplus::Graphics imageGraphics(dc);
                    succeeded = imageGraphics.DrawImage(
                        &image, command.x1, command.y1,
                        command.x2, command.y2) == Gdiplus::Ok;
                }
                break;
            }
            case Window::CommandType::Line:
                succeeded = MoveToEx(dc, command.x1, command.y1, nullptr) != 0 &&
                            LineTo(dc, command.x2, command.y2) != 0;
                break;
            case Window::CommandType::Rectangle:
                succeeded = Rectangle(dc, command.x1, command.y1,
                                      command.x1 + command.x2,
                                      command.y1 + command.y2) != 0;
                break;
            case Window::CommandType::Circle:
                succeeded = Ellipse(dc, command.x1 - command.x2, command.y1 - command.x2,
                                    command.x1 + command.x2, command.y1 + command.x2) != 0;
                break;
        }
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        if (command.filled) DeleteObject(brush);
        if (!succeeded) return false;
    }
    return true;
}

bool PlatformApplication::ensureSurfaces(Window& window) {
    RECT bounds{};
    if (!GetClientRect(window.handle, &bounds)) {
        window.renderFailed = true;
        return false;
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || IsIconic(window.handle)) return false;
    if (window.completedSurface.width == width && window.completedSurface.height == height &&
        window.workingSurface.width == width && window.workingSurface.height == height) {
        return true;
    }

    HDC windowDC = GetDC(window.handle);
    if (!windowDC) {
        window.renderFailed = true;
        return false;
    }
    auto createSurface = [windowDC, width, height](Window::Surface& surface) {
        surface.dc = CreateCompatibleDC(windowDC);
        if (!surface.dc) return false;
        surface.bitmap = CreateCompatibleBitmap(windowDC, width, height);
        if (!surface.bitmap) return false;
        surface.previousBitmap = SelectObject(surface.dc, surface.bitmap);
        if (!surface.previousBitmap || surface.previousBitmap == HGDI_ERROR) return false;
        surface.width = width;
        surface.height = height;
        return true;
    };

    Window::Surface completed;
    Window::Surface working;
    const bool created = createSurface(completed) && createSurface(working);
    ReleaseDC(window.handle, windowDC);
    if (!created || !render(window, completed.dc, true)) {
        releaseSurface(completed.dc, completed.bitmap, completed.previousBitmap);
        releaseSurface(working.dc, working.bitmap, working.previousBitmap);
        window.renderFailed = true;
        return false;
    }
    releaseSurfaces(window);
    window.completedSurface = completed;
    window.workingSurface = working;
    return true;
}

bool PlatformApplication::paintCompletedFrame(Window& window, HDC dc) {
    RECT bounds{};
    if (!GetClientRect(window.handle, &bounds)) {
        window.renderFailed = true;
        return false;
    }
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
        IsIconic(window.handle)) {
        return true;
    }
    if (!ensureSurfaces(window)) return false;
    const BOOL copied = BitBlt(dc, 0, 0, window.completedSurface.width,
                               window.completedSurface.height,
                               window.completedSurface.dc, 0, 0, SRCCOPY);
    return copied != 0;
}

LRESULT CALLBACK PlatformApplication::windowProcedure(
    HWND handle, UINT message, WPARAM wParam, LPARAM lParam
) {
    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    if (!window) return DefWindowProcW(handle, message, wParam, lParam);

    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle, &paint);
            if (!paintCompletedFrame(*window, dc)) window->renderFailed = true;
            EndPaint(handle, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            window->events.push_back({Event::Type::MouseDown, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 1, {}});
            return 0;
        case WM_LBUTTONUP:
            window->events.push_back({Event::Type::MouseUp, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 1, {}});
            return 0;
        case WM_RBUTTONDOWN:
            window->events.push_back({Event::Type::MouseDown, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 2, {}});
            return 0;
        case WM_RBUTTONUP:
            window->events.push_back({Event::Type::MouseUp, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 2, {}});
            return 0;
        case WM_MBUTTONDOWN:
            window->events.push_back({Event::Type::MouseDown, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 3, {}});
            return 0;
        case WM_MBUTTONUP:
            window->events.push_back({Event::Type::MouseUp, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 3, {}});
            return 0;
        case WM_MOUSEMOVE:
            window->events.push_back({Event::Type::MouseMove, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, {}});
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(handle, &point);
            window->events.push_back({Event::Type::MouseWheel, point.x, point.y,
                                      GET_WHEEL_DELTA_WPARAM(wParam), {}});
            return 0;
        }
        case WM_KEYDOWN:
            window->events.push_back({Event::Type::KeyDown, static_cast<int>(wParam), 0,
                                      static_cast<int>(wParam), {}});
            return 0;
        case WM_KEYUP:
            window->events.push_back({Event::Type::KeyUp, static_cast<int>(wParam), 0,
                                      static_cast<int>(wParam), {}});
            return 0;
        case WM_SETFOCUS:
            window->events.push_back({Event::Type::FocusGained, 0, 0, 0, {}});
            return 0;
        case WM_KILLFOCUS:
            window->events.push_back({Event::Type::FocusLost, 0, 0, 0, {}});
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                window->events.push_back({Event::Type::Resized, LOWORD(lParam),
                                          HIWORD(lParam), 0, {}});
            }
            return 0;
        case WM_CHAR:
        {
            const wchar_t character = static_cast<wchar_t>(wParam);
            char utf8[4]{};
            const int count = WideCharToMultiByte(CP_UTF8, 0, &character, 1, utf8,
                                                   static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            window->events.push_back({Event::Type::TextInput, 0, 0, 0,
                                      std::string(utf8, static_cast<std::size_t>(count))});
            return 0;
        }
        case WM_CLOSE:
            window->events.push_back({Event::Type::Closed, 0, 0, 0, {}});
            DestroyWindow(handle);
            return 0;
        case WM_DESTROY:
            releaseSurfaces(*window);
            releaseFonts(*window);
            window->closed = true;
            window->handle = nullptr;
            return 0;
        default:
            return DefWindowProcW(handle, message, wParam, lParam);
    }
}

} // namespace zplus::graphics
