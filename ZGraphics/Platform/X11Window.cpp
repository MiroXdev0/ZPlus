#include "X11Window.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zplus::graphics {
namespace {

} // namespace

struct PlatformApplication::Impl {
    struct Command {
        enum class Type { Text, Line, Rectangle, Circle } type;
        std::string text;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        int thickness = 1;
        bool filled = false;
        int fontSize = 16;
        Color color;
    };
    struct WindowData {
        ::Window handle = 0;
        Color background{};
        Color completedBackground{};
        std::vector<Command> commands;
        std::vector<Command> completedCommands;
        std::deque<Event> events;
        GC gc = nullptr;
        Pixmap completedFrame = 0;
        Pixmap workingFrame = 0;
        unsigned int frameWidth = 0;
        unsigned int frameHeight = 0;
        Atom closeAtom = None;
        bool frameDirty = true;
        bool closed = false;
        bool shown = false;
    };

    Display* display = nullptr;
    int screen = 0;
    std::vector<WindowData> windows;
    std::unordered_map<unsigned int, unsigned long> colors;
    std::unordered_map<int, XFontSet> fonts;

    unsigned long nativeColor(Color color) {
        const unsigned int key = (static_cast<unsigned int>(color.red) << 16) |
                                 (static_cast<unsigned int>(color.green) << 8) |
                                 static_cast<unsigned int>(color.blue);
        const auto found = colors.find(key);
        if (found != colors.end()) return found->second;

        XColor value{};
        value.red = static_cast<unsigned short>(color.red * 257u);
        value.green = static_cast<unsigned short>(color.green * 257u);
        value.blue = static_cast<unsigned short>(color.blue * 257u);
        value.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(display, DefaultColormap(display, screen), &value)) {
            throw std::runtime_error("cannot allocate X11 color");
        }
        colors.emplace(key, value.pixel);
        return value.pixel;
    }

    XFontSet fontForSize(int size) {
        const auto found = fonts.find(size);
        if (found != fonts.end()) return found->second;
        char** missing = nullptr;
        int missingCount = 0;
        char* defaultString = nullptr;
        const std::string pattern = "-*-sans-*-*-*-*-*-" +
            std::to_string(size) + "-*-*-*-*-*-*";
        XFontSet font = XCreateFontSet(display, pattern.c_str(), &missing,
                                       &missingCount, &defaultString);
        if (missing) XFreeStringList(missing);
        fonts.emplace(size, font);
        return font;
    }

    WindowData& get(int id) {
        if (id <= 0 || static_cast<std::size_t>(id) > windows.size()) {
            throw std::out_of_range("invalid graphics window handle");
        }
        return windows[static_cast<std::size_t>(id - 1)];
    }

    WindowData& getOpen(int id) {
        auto& window = get(id);
        if (window.closed) throw std::runtime_error("graphics window is closed");
        return window;
    }

    void releaseFrames(WindowData& window) {
        if (window.completedFrame) XFreePixmap(display, window.completedFrame);
        if (window.workingFrame) XFreePixmap(display, window.workingFrame);
        window.completedFrame = 0;
        window.workingFrame = 0;
        window.frameWidth = 0;
        window.frameHeight = 0;
    }

    void releaseResources(WindowData& window) {
        releaseFrames(window);
        if (window.gc) XFreeGC(display, window.gc);
        window.gc = nullptr;
    }

    void render(WindowData& window, Drawable target, Color background,
                const std::vector<Command>& commands) {
        XSetForeground(display, window.gc, nativeColor(background));
        XFillRectangle(display, target, window.gc, 0, 0,
                       window.frameWidth, window.frameHeight);
        for (const auto& command : commands) {
            XSetForeground(display, window.gc, nativeColor(command.color));
            XSetLineAttributes(display, window.gc, static_cast<unsigned int>(command.thickness),
                               LineSolid, CapButt, JoinMiter);
            switch (command.type) {
                case Command::Type::Text: {
                    XFontSet font = fontForSize(command.fontSize);
                    if (!font) {
                        XDrawString(display, target, window.gc, command.x1,
                                    command.y1 + command.fontSize,
                                    command.text.data(), static_cast<int>(command.text.size()));
                    } else {
                        Xutf8DrawString(display, target, font, window.gc, command.x1,
                                        command.y1 + command.fontSize, command.text.data(),
                                        static_cast<int>(command.text.size()));
                    }
                    break;
                }
                case Command::Type::Line:
                    XDrawLine(display, target, window.gc, command.x1, command.y1,
                              command.x2, command.y2);
                    break;
                case Command::Type::Rectangle:
                    if (command.filled) {
                        XFillRectangle(display, target, window.gc, command.x1, command.y1,
                                       static_cast<unsigned int>(command.x2),
                                       static_cast<unsigned int>(command.y2));
                    } else {
                        XDrawRectangle(display, target, window.gc, command.x1, command.y1,
                                       static_cast<unsigned int>(command.x2),
                                       static_cast<unsigned int>(command.y2));
                    }
                    break;
                case Command::Type::Circle: {
                    const int diameter = command.x2 * 2;
                    if (command.filled) {
                        XFillArc(display, target, window.gc, command.x1 - command.x2,
                                 command.y1 - command.x2, static_cast<unsigned int>(diameter),
                                 static_cast<unsigned int>(diameter), 0, 360 * 64);
                    } else {
                        XDrawArc(display, target, window.gc, command.x1 - command.x2,
                                 command.y1 - command.x2, static_cast<unsigned int>(diameter),
                                 static_cast<unsigned int>(diameter), 0, 360 * 64);
                    }
                    break;
                }
            }
        }
    }

    bool ensureFrames(WindowData& window) {
        if (!window.handle || window.closed) return false;
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, window.handle, &attributes)) {
            throw std::runtime_error("cannot query X11 graphics window size");
        }
        if (attributes.width <= 0 || attributes.height <= 0) return false;
        const auto width = static_cast<unsigned int>(attributes.width);
        const auto height = static_cast<unsigned int>(attributes.height);
        if (window.completedFrame && window.workingFrame &&
            window.frameWidth == width && window.frameHeight == height) {
            return true;
        }

        const Pixmap completed = XCreatePixmap(display, window.handle, width, height,
                                                static_cast<unsigned int>(attributes.depth));
        if (!completed) throw std::runtime_error("cannot create X11 graphics backbuffer");
        const Pixmap working = XCreatePixmap(display, window.handle, width, height,
                                              static_cast<unsigned int>(attributes.depth));
        if (!working) {
            XFreePixmap(display, completed);
            throw std::runtime_error("cannot create X11 graphics working buffer");
        }

        const unsigned int oldWidth = window.frameWidth;
        const unsigned int oldHeight = window.frameHeight;
        window.frameWidth = width;
        window.frameHeight = height;
        try {
            render(window, completed, window.completedBackground, window.completedCommands);
        } catch (...) {
            window.frameWidth = oldWidth;
            window.frameHeight = oldHeight;
            XFreePixmap(display, completed);
            XFreePixmap(display, working);
            throw;
        }
        releaseFrames(window);
        window.completedFrame = completed;
        window.workingFrame = working;
        window.frameWidth = width;
        window.frameHeight = height;
        return true;
    }

    void copyCompletedFrame(WindowData& window) {
        if (!ensureFrames(window)) return;
        XCopyArea(display, window.completedFrame, window.handle, window.gc, 0, 0,
                  window.frameWidth, window.frameHeight, 0, 0);
        XFlush(display);
    }

    void dispatch(XEvent& event) {
        for (auto& window : windows) {
            if (window.handle != event.xany.window) continue;
            switch (event.type) {
                case Expose:
                    if (event.xexpose.count == 0) copyCompletedFrame(window);
                    break;
                case ButtonPress:
                    if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
                        window.events.push_back({Event::Type::MouseWheel, event.xbutton.x,
                                                 event.xbutton.y,
                                                 event.xbutton.button == Button4 ? 120 : -120, {}});
                    } else {
                        window.events.push_back({Event::Type::MouseDown, event.xbutton.x,
                                                 event.xbutton.y,
                                                 static_cast<int>(event.xbutton.button), {}});
                    }
                    break;
                case ButtonRelease:
                    if (event.xbutton.button != Button4 && event.xbutton.button != Button5) {
                        window.events.push_back({Event::Type::MouseUp, event.xbutton.x,
                                                 event.xbutton.y,
                                                 static_cast<int>(event.xbutton.button), {}});
                    }
                    break;
                case MotionNotify:
                    window.events.push_back({Event::Type::MouseMove, event.xmotion.x, event.xmotion.y, 0, {}});
                    break;
                case KeyPress: {
                    char buffer[64]{};
                    KeySym key = NoSymbol;
                    const int count = XLookupString(&event.xkey, buffer, sizeof(buffer), &key, nullptr);
                    window.events.push_back({Event::Type::KeyDown, 0, 0, static_cast<int>(key), {}});
                    if (count > 0) {
                        window.events.push_back({Event::Type::TextInput, 0, 0, 0,
                                                 std::string(buffer, static_cast<std::size_t>(count))});
                    }
                    break;
                }
                case KeyRelease: {
                    char buffer[64]{};
                    KeySym key = NoSymbol;
                    XLookupString(&event.xkey, buffer, sizeof(buffer), &key, nullptr);
                    window.events.push_back({Event::Type::KeyUp, 0, 0, static_cast<int>(key), {}});
                    break;
                }
                case FocusIn:
                    window.events.push_back({Event::Type::FocusGained, 0, 0, 0, {}});
                    break;
                case FocusOut:
                    window.events.push_back({Event::Type::FocusLost, 0, 0, 0, {}});
                    break;
                case ConfigureNotify:
                    window.events.push_back({Event::Type::Resized, event.xconfigure.width,
                                             event.xconfigure.height, 0, {}});
                    break;
                case ClientMessage:
                    if (static_cast<Atom>(event.xclient.data.l[0]) == window.closeAtom) {
                        window.events.push_back({Event::Type::Closed, 0, 0, 0, {}});
                        XDestroyWindow(display, window.handle);
                        releaseResources(window);
                        window.handle = 0;
                        window.closed = true;
                    }
                    break;
                case DestroyNotify:
                    releaseResources(window);
                    window.handle = 0;
                    window.closed = true;
                    break;
                default:
                    break;
            }
            return;
        }
    }

    void dispatchNext(XEvent event) {
        if (event.type == KeyRelease && XPending(display) > 0) {
            XEvent next{};
            XPeekEvent(display, &next);
            if (next.type == KeyPress &&
                next.xkey.window == event.xkey.window &&
                next.xkey.keycode == event.xkey.keycode &&
                next.xkey.time == event.xkey.time) {
                XNextEvent(display, &next);
                event = next;
            }
        }
        dispatch(event);
    }
};

PlatformApplication::PlatformApplication()
    : impl_(std::make_unique<Impl>()) {
    impl_->display = XOpenDisplay(nullptr);
    if (!impl_->display) {
        throw std::runtime_error("cannot connect to the X11 display; check DISPLAY");
    }
    impl_->screen = DefaultScreen(impl_->display);
}

PlatformApplication::~PlatformApplication() {
    if (!impl_) return;
    for (auto& window : impl_->windows) {
        impl_->releaseResources(window);
        if (window.handle) XDestroyWindow(impl_->display, window.handle);
    }
    for (const auto& [size, font] : impl_->fonts) {
        (void)size;
        if (font) XFreeFontSet(impl_->display, font);
    }
    for (const auto& [key, pixel] : impl_->colors) {
        (void)key;
        XFreeColors(impl_->display, DefaultColormap(impl_->display, impl_->screen),
                    &pixel, 1, 0);
    }
    if (impl_->display) XCloseDisplay(impl_->display);
}

int PlatformApplication::createWindow(const std::string& title, int width, int height) {
    auto& state = *impl_;
    const unsigned long black = BlackPixel(state.display, state.screen);
    Impl::WindowData window;
    window.handle = XCreateSimpleWindow(state.display, RootWindow(state.display, state.screen),
                                        0, 0, static_cast<unsigned int>(width),
                                        static_cast<unsigned int>(height), 0, black, black);
    if (!window.handle) throw std::runtime_error("cannot create native X11 window");
    window.gc = XCreateGC(state.display, window.handle, 0, nullptr);
    if (!window.gc) {
        XDestroyWindow(state.display, window.handle);
        throw std::runtime_error("cannot create X11 drawing context");
    }
    XStoreName(state.display, window.handle, title.c_str());
    XSelectInput(state.display, window.handle,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 KeyPressMask | KeyReleaseMask | StructureNotifyMask | FocusChangeMask);
    window.closeAtom = XInternAtom(state.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(state.display, window.handle, &window.closeAtom, 1);
    state.windows.push_back(std::move(window));
    return static_cast<int>(state.windows.size());
}

void PlatformApplication::setTitle(int id, const std::string& title) {
    auto& window = impl_->getOpen(id);
    XStoreName(impl_->display, window.handle, title.c_str());
}

void PlatformApplication::setPosition(int id, int x, int y) {
    auto& window = impl_->getOpen(id);
    XMoveWindow(impl_->display, window.handle, x, y);
    XFlush(impl_->display);
}

void PlatformApplication::resize(int id, int width, int height) {
    auto& window = impl_->getOpen(id);
    XResizeWindow(impl_->display, window.handle, static_cast<unsigned int>(width),
                  static_cast<unsigned int>(height));
    XFlush(impl_->display);
}

void PlatformApplication::setBackground(int id, Color color) {
    auto& window = impl_->getOpen(id);
    window.background = color;
    window.frameDirty = true;
}

void PlatformApplication::show(int id) {
    auto& window = impl_->getOpen(id);
    present(id);
    XMapRaised(impl_->display, window.handle);
    window.shown = true;
    XFlush(impl_->display);
}

void PlatformApplication::hide(int id) {
    auto& window = impl_->getOpen(id);
    XUnmapWindow(impl_->display, window.handle);
    window.shown = false;
}

void PlatformApplication::minimize(int id) {
    XIconifyWindow(impl_->display, impl_->getOpen(id).handle, impl_->screen);
}

void PlatformApplication::maximize(int id) {
    auto& window = impl_->getOpen(id);
    Atom state = XInternAtom(impl_->display, "_NET_WM_STATE", False);
    Atom horizontal = XInternAtom(impl_->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom vertical = XInternAtom(impl_->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window.handle;
    event.xclient.message_type = state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = static_cast<long>(horizontal);
    event.xclient.data.l[2] = static_cast<long>(vertical);
    event.xclient.data.l[3] = 1;
    XSendEvent(impl_->display, DefaultRootWindow(impl_->display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

void PlatformApplication::restore(int id) {
    auto& window = impl_->getOpen(id);
    XMapRaised(impl_->display, window.handle);
    XFlush(impl_->display);
}

void PlatformApplication::focus(int id) {
    auto& window = impl_->getOpen(id);
    XSetInputFocus(impl_->display, window.handle, RevertToParent, CurrentTime);
    XFlush(impl_->display);
}

bool PlatformApplication::isOpen(int id) {
    return !impl_->get(id).closed;
}

void PlatformApplication::close(int id) {
    auto& window = impl_->get(id);
    if (!window.closed) {
        window.events.push_back({Event::Type::Closed, 0, 0, 0, {}});
        XDestroyWindow(impl_->display, window.handle);
        impl_->releaseResources(window);
        window.handle = 0;
        window.closed = true;
    }
}

void PlatformApplication::drawText(
    int id, const std::string& text, int x, int y, Color color, int size
) {
    auto& window = impl_->getOpen(id);
    window.commands.push_back({Impl::Command::Type::Text, text, x, y, 0, 0, 1, false, size, color});
    window.frameDirty = true;
}

void PlatformApplication::drawLine(
    int id, int x1, int y1, int x2, int y2, Color color, int thickness
) {
    auto& window = impl_->getOpen(id);
    window.commands.push_back({Impl::Command::Type::Line, {}, x1, y1, x2, y2, thickness, false, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::drawRectangle(
    int id, int x, int y, int width, int height, Color color, bool filled, int thickness
) {
    auto& window = impl_->getOpen(id);
    window.commands.push_back({Impl::Command::Type::Rectangle, {}, x, y, width, height,
                               thickness, filled, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::drawCircle(
    int id, int x, int y, int radius, Color color, bool filled, int thickness
) {
    auto& window = impl_->getOpen(id);
    window.commands.push_back({Impl::Command::Type::Circle, {}, x, y, radius, 0,
                               thickness, filled, 16, color});
    window.frameDirty = true;
}

void PlatformApplication::clear(int id) {
    auto& window = impl_->getOpen(id);
    window.commands.clear();
    window.frameDirty = true;
}

void PlatformApplication::present(int id) {
    auto& window = impl_->get(id);
    if (window.closed || !window.handle) return;
    if (!window.frameDirty) return;
    if (impl_->ensureFrames(window)) {
        impl_->render(window, window.workingFrame, window.background, window.commands);
        window.completedCommands = window.commands;
        window.completedBackground = window.background;
        window.frameDirty = false;
        std::swap(window.completedFrame, window.workingFrame);
        impl_->copyCompletedFrame(window);
        return;
    }
    window.completedCommands = window.commands;
    window.completedBackground = window.background;
    window.frameDirty = false;
}

Event PlatformApplication::pollEvent(int id) {
    while (XPending(impl_->display) > 0) {
        XEvent event{};
        XNextEvent(impl_->display, &event);
        impl_->dispatchNext(event);
    }
    auto& events = impl_->get(id).events;
    if (events.empty()) return {};
    Event result = std::move(events.front());
    events.pop_front();
    return result;
}

Event PlatformApplication::waitEvent(int id) {
    auto& target = impl_->get(id);
    while (target.events.empty() && !target.closed) {
        XEvent event{};
        XNextEvent(impl_->display, &event);
        impl_->dispatchNext(event);
    }
    return pollEvent(id);
}

int PlatformApplication::run(int id) {
    auto& target = impl_->get(id);
    present(id);
    while (!target.closed) {
        (void)waitEvent(id);
    }
    return 0;
}

} // namespace zplus::graphics
