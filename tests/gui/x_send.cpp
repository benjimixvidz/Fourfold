// Sends synthetic mouse input to an X window (for driving the plugin GUI inside a host).
//
//   x-send WINDOW click X Y [BUTTON]
//   x-send WINDOW drag X Y DY          press, move DY pixels vertically, release
//   x-send WINDOW dragx X Y DX [STATE]  press, move DX pixels horizontally, release (STATE: 1 Shift, 4 Ctrl)
//   x-send WINDOW move X Y              pointer motion, no button
//   x-send WINDOW dblclick X Y          two left clicks within Rack's double-click time
//   x-send WINDOW key X Y KEYSYM [STATE] key press and release with the pointer at X Y (e.g. Up, Delete, z)
//   x-send find-child WINDOW W H       prints the deepest descendant window of size W x H

#include <X11/Xlib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

void send(Display* d, Window w, int type, int x, int y, unsigned button, unsigned state = 0)
{
    // Rack (GLFW) asks the server where the pointer is: tests/vcv/fake_pointer.c answers from this file.
    // Written aside then renamed, so a reader never sees a half-written file.
    if (const char* path = std::getenv("FAKE_POINTER")) {
        const std::string tmp = std::string(path) + ".tmp";
        if (FILE* f = std::fopen(tmp.c_str(), "w")) {
            std::fprintf(f, "%d %d\n", x, y);
            std::fclose(f);
            std::rename(tmp.c_str(), path);
        }
    }
    // Real timestamps: GLFW drops input-method duplicates of a key press by time, and time 0
    // would turn each synthetic press into press + repeat.
    const Time now = Time(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count() & 0x7fffffff);
    XEvent e{};
    if (type == MotionNotify) {
        const unsigned held = button ? Button1Mask : 0;
        e.xmotion = {MotionNotify, 0, True, d, w, 0, 0, now, x, y, x, y, held | state, 0, True};
        XSendEvent(d, w, False, PointerMotionMask | (held ? ButtonMotionMask | Button1MotionMask : 0), &e);
    } else if (type == KeyPress || type == KeyRelease) {
        e.xkey = {type, 0, True, d, w, 0, 0, now, x, y, x, y, state, button, True};
        XSendEvent(d, w, False, type == KeyPress ? KeyPressMask : KeyReleaseMask, &e);
    } else {
        e.xbutton.type = type;
        e.xbutton.display = d;
        e.xbutton.window = w;
        e.xbutton.x = x;
        e.xbutton.y = y;
        e.xbutton.state = state;
        e.xbutton.time = now;
        e.xbutton.button = button;
        e.xbutton.same_screen = True;
        XSendEvent(d, w, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &e);
    }
    XFlush(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

// Pointer motion, then a pause longer than a frame: a host handles a press where it last saw the pointer.
void hover(Display* d, Window w, int x, int y, unsigned state = 0)
{
    send(d, w, MotionNotify, x, y, 0, state);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

Window findChild(Display* d, Window w, unsigned width, unsigned height)
{
    Window root, parent, *children = nullptr;
    unsigned n = 0;
    if (!XQueryTree(d, w, &root, &parent, &children, &n))
        return 0;
    Window found = 0;
    // Deepest match first: the host's container often has the plugin window's size too.
    for (unsigned i = 0; i < n && !found; ++i) {
        found = findChild(d, children[i], width, height);
        XWindowAttributes a;
        if (!found && XGetWindowAttributes(d, children[i], &a) && unsigned(a.width) == width &&
            unsigned(a.height) == height)
            found = children[i];
    }
    if (children)
        XFree(children);
    return found;
}

} // namespace

int main(int argc, char** argv)
{
    Display* d = XOpenDisplay(nullptr);
    if (!d || argc < 3)
        return 2;
    if (!std::strcmp(argv[1], "find-child") && argc == 5) {
        const Window w = findChild(d, Window(std::strtoul(argv[2], nullptr, 0)), unsigned(std::atoi(argv[3])),
                                   unsigned(std::atoi(argv[4])));
        if (w)
            std::printf("0x%lx\n", w);
        XCloseDisplay(d);
        return w ? 0 : 1;
    }
    const Window w = Window(std::strtoul(argv[1], nullptr, 0));
    const int x = argc > 4 ? std::atoi(argv[3]) : 0, y = argc > 4 ? std::atoi(argv[4]) : 0;
    if (!std::strcmp(argv[2], "click") && argc >= 5) {
        const unsigned b = argc > 5 ? unsigned(std::atoi(argv[5])) : Button1;
        hover(d, w, x, y);
        send(d, w, ButtonPress, x, y, b);
        send(d, w, ButtonRelease, x, y, b);
    } else if (!std::strcmp(argv[2], "drag") && argc == 6) {
        const int dy = std::atoi(argv[5]);
        hover(d, w, x, y);
        send(d, w, ButtonPress, x, y, Button1);
        for (int i = 1; i <= 10; ++i)
            send(d, w, MotionNotify, x, y + dy * i / 10, 1);
        send(d, w, ButtonRelease, x, y + dy, Button1);
    } else if (!std::strcmp(argv[2], "dragx") && argc >= 6) {
        const int dx = std::atoi(argv[5]);
        const unsigned state = argc > 6 ? unsigned(std::atoi(argv[6])) : 0;
        hover(d, w, x, y, state);
        send(d, w, ButtonPress, x, y, Button1, state);
        for (int i = 1; i <= 10; ++i)
            send(d, w, MotionNotify, x + dx * i / 10, y, 1, state);
        send(d, w, ButtonRelease, x + dx, y, Button1, state);
    } else if (!std::strcmp(argv[2], "dblclick") && argc == 5) {
        hover(d, w, x, y);
        for (int i = 0; i < 2; ++i) {
            send(d, w, ButtonPress, x, y, Button1);
            send(d, w, ButtonRelease, x, y, Button1);
        }
    } else if (!std::strcmp(argv[2], "move") && argc == 5) {
        send(d, w, MotionNotify, x, y, 0);
    } else if (!std::strcmp(argv[2], "key") && argc >= 6) {
        const KeySym sym = XStringToKeysym(argv[5]);
        const unsigned state = argc > 6 ? unsigned(std::atoi(argv[6])) : 0;
        if (sym == NoSymbol)
            return 2;
        const unsigned code = XKeysymToKeycode(d, sym);
        hover(d, w, x, y, state);
        send(d, w, KeyPress, x, y, code, state);
        send(d, w, KeyRelease, x, y, code, state);
    } else {
        return 2;
    }
    XCloseDisplay(d);
    return 0;
}
