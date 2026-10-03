// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// POSIX terminal: termios raw mode, poll() for keys with a timeout and a
// parser for the usual VT100/xterm escape sequences.

#ifndef _WIN32

#include "terminal.h"

#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace zxgw {

namespace {

Terminal* g_active = nullptr;
struct termios g_saved;
bool g_raw = false;

const char* const kEnter = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[H\x1b[2J";
const char* const kLeave = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";

void writeAll(const char* p, size_t n) {
    while (n) {
        ssize_t w = ::write(STDOUT_FILENO, p, n);
        if (w <= 0) return;
        p += w;
        n -= size_t(w);
    }
}

void emergencyRestore() {
    if (!g_raw) return;
    writeAll(kLeave, std::strlen(kLeave));
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
    g_raw = false;
}

void onSignal(int sig) {
    emergencyRestore();
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

struct Terminal::Impl {
    std::string in;          // bytes read but not parsed yet
    bool open = false;

    bool fill(int timeoutMs) {
        struct pollfd p;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        int r = poll(&p, 1, timeoutMs);
        if (r <= 0) return false;
        char buf[256];
        ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
            in += char(0x04);    // end of input: like Ctrl-D
            return true;
        }
        in.append(buf, size_t(n));
        return true;
    }

    // Parse one key from the buffer.  Returns 0 = need more bytes,
    // 1 = key parsed, 2 = bytes consumed without a key.
    int parse(KeyEvent& ev, bool final) {
        if (in.empty()) return 0;
        unsigned char c = (unsigned char)in[0];
        ev = KeyEvent();
        if (c == 0x1B) {
            if (in.size() == 1) {
                if (!final) return 0;
                in.erase(0, 1);
                ev.code = KeyCode::Break;                     // ESC alone
                return 1;
            }
            unsigned char c2 = (unsigned char)in[1];
            if (c2 == '[' || c2 == 'O') {
                size_t i = 2;
                while (i < in.size() && ((unsigned char)in[i] < 0x40 || (unsigned char)in[i] > 0x7E)) ++i;
                if (i >= in.size()) {
                    if (!final) return 0;
                    in.erase(0, 2);
                    return 2;
                }
                std::string params = in.substr(2, i - 2);
                char fin = in[i];
                in.erase(0, i + 1);
                int p1 = std::atoi(params.c_str());
                switch (fin) {
                case 'A': ev.code = KeyCode::Up; return 1;
                case 'B': ev.code = KeyCode::Down; return 1;
                case 'C': ev.code = KeyCode::Right; return 1;
                case 'D': ev.code = KeyCode::Left; return 1;
                case 'H': ev.code = KeyCode::Home; return 1;
                case 'F': ev.code = KeyCode::End; return 1;
                case 'P': return fkey(ev, 1);
                case 'Q': return fkey(ev, 2);
                case 'R': return fkey(ev, 3);
                case 'S': return fkey(ev, 4);
                case '~':
                    switch (p1) {
                    case 1: case 7: ev.code = KeyCode::Home; return 1;
                    case 4: case 8: ev.code = KeyCode::End; return 1;
                    case 3: ev.code = KeyCode::Delete; return 1;
                    case 11: return fkey(ev, 1);
                    case 12: return fkey(ev, 2);
                    case 13: return fkey(ev, 3);
                    case 14: return fkey(ev, 4);
                    case 15: return fkey(ev, 5);
                    case 17: return fkey(ev, 6);
                    case 18: return fkey(ev, 7);
                    case 19: return fkey(ev, 8);
                    case 20: return fkey(ev, 9);
                    case 21: return fkey(ev, 10);
                    case 23: return fkey(ev, 11);
                    case 24: return fkey(ev, 12);
                    default: return 2;
                    }
                default:
                    return 2;
                }
            }
            if (c2 == 0x1B) {                                 // ESC ESC
                in.erase(0, 1);
                ev.code = KeyCode::Break;
                return 1;
            }
            // Alt/Option + key = SYMBOL SHIFT
            in.erase(0, 1);
            int r = parse(ev, final);
            if (r == 1 && ev.code == KeyCode::Char) ev.symbolShift = true;
            if (r == 0) in.insert(in.begin(), char(0x1B));
            return r;
        }
        if (c < 0x20 || c == 0x7F) {
            in.erase(0, 1);
            switch (c) {
            case 0x7F: case 0x08: ev.code = KeyCode::Backspace; return 1;
            case 0x0D:
                if (!in.empty() && in[0] == '\n') in.erase(0, 1);
                ev.code = KeyCode::Enter;
                return 1;
            case 0x0A: ev.code = KeyCode::Enter; return 1;
            case 0x09: ev.code = KeyCode::Edit; return 1;
            case 0x03: ev.code = KeyCode::Break; return 1;
            case 0x04: ev.code = KeyCode::Quit; return 1;
            case 0x05: ev.code = KeyCode::Extend; return 1;
            case 0x07: ev.code = KeyCode::Graphics; return 1;
            case 0x0C: ev.code = KeyCode::Redraw; return 1;
            default: return 2;
            }
        }
        // UTF-8
        int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (int(in.size()) < n) {
            if (!final) return 0;
            in.clear();
            return 2;
        }
        uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)in[size_t(k)] & 0x3F);
        in.erase(0, size_t(n));
        ev.code = KeyCode::Char;
        ev.ch = cp;
        return 1;
    }

    // Function keys: F1..F10 are CAPS SHIFT + 1..0, F11 is extended mode.
    static int fkey(KeyEvent& ev, int n) {
        static const KeyCode map[13] = {
            KeyCode::None, KeyCode::Edit, KeyCode::CapsLock, KeyCode::TrueVideo, KeyCode::InvVideo,
            KeyCode::Left, KeyCode::Down, KeyCode::Up, KeyCode::Right, KeyCode::Graphics,
            KeyCode::Backspace, KeyCode::Extend, KeyCode::None
        };
        ev.code = map[n];
        return ev.code == KeyCode::None ? 2 : 1;
    }
};

Terminal::Terminal() : impl_(new Impl) {}

Terminal::~Terminal() {
    restore();
    delete impl_;
}

bool Terminal::interactive() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

void Terminal::open() {
    if (impl_->open) return;
    if (tcgetattr(STDIN_FILENO, &g_saved) == 0) {
        struct termios raw = g_saved;
        raw.c_iflag &= tcflag_t(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_cflag |= CS8;
        raw.c_lflag &= tcflag_t(~(ECHO | ICANON | IEXTEN | ISIG));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        g_raw = true;
    }
    g_active = this;
    signal(SIGTERM, onSignal);
    signal(SIGHUP, onSignal);
    signal(SIGQUIT, onSignal);
    std::atexit(emergencyRestore);
    write(kEnter);
    impl_->open = true;
}

void Terminal::restore() {
    if (!impl_->open) return;
    write(kLeave);
    if (g_raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
    g_raw = false;
    impl_->open = false;
    g_active = nullptr;
}

void Terminal::size(int& columns, int& rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        columns = ws.ws_col;
        rows = ws.ws_row;
    } else {
        columns = 80;
        rows = 24;
    }
}

void Terminal::write(const std::string& bytes) { writeAll(bytes.data(), bytes.size()); }

bool Terminal::readKey(KeyEvent& ev, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    for (;;) {
        int r = impl_->parse(ev, false);
        if (r == 1) return true;
        if (r == 2) continue;
        // need more bytes: an incomplete escape sequence gets a short grace
        bool pending = !impl_->in.empty();
        int wait;
        if (pending) {
            wait = 30;
        } else if (timeoutMs < 0) {
            wait = -1;
        } else {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left < 0) left = 0;
            wait = int(left);
        }
        if (!impl_->fill(wait)) {
            if (pending) {
                r = impl_->parse(ev, true);
                if (r == 1) return true;
                continue;
            }
            return false;
        }
    }
}

} // namespace zxgw

#endif // !_WIN32
