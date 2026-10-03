// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Windows console: virtual terminal processing for the output (Windows 10
// and later), ReadConsoleInputW for the keys.

#ifdef _WIN32

#include "terminal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <io.h>

#include <chrono>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace zxgw {

namespace {
const char* const kEnter = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[H\x1b[2J";
const char* const kLeave = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
} // namespace

struct Terminal::Impl {
    HANDLE in = INVALID_HANDLE_VALUE;
    HANDLE out = INVALID_HANDLE_VALUE;
    DWORD inMode = 0, outMode = 0;
    UINT codePage = 0;
    bool open = false;
    wchar_t highSurrogate = 0;
};

Terminal::Terminal() : impl_(new Impl) {
    impl_->in = GetStdHandle(STD_INPUT_HANDLE);
    impl_->out = GetStdHandle(STD_OUTPUT_HANDLE);
}

Terminal::~Terminal() {
    restore();
    delete impl_;
}

bool Terminal::interactive() {
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
}

void Terminal::open() {
    if (impl_->open) return;
    GetConsoleMode(impl_->in, &impl_->inMode);
    GetConsoleMode(impl_->out, &impl_->outMode);
    impl_->codePage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    DWORD inMode = impl_->inMode;
    inMode &= ~DWORD(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    inMode |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(impl_->in, inMode);
    SetConsoleMode(impl_->out, impl_->outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    write(kEnter);
    impl_->open = true;
}

void Terminal::restore() {
    if (!impl_->open) return;
    write(kLeave);
    SetConsoleMode(impl_->in, impl_->inMode);
    SetConsoleMode(impl_->out, impl_->outMode);
    SetConsoleOutputCP(impl_->codePage);
    impl_->open = false;
}

void Terminal::size(int& columns, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(impl_->out, &info)) {
        columns = info.srWindow.Right - info.srWindow.Left + 1;
        rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    } else {
        columns = 80;
        rows = 25;
    }
}

void Terminal::write(const std::string& bytes) {
    DWORD written = 0;
    const char* p = bytes.data();
    size_t n = bytes.size();
    while (n) {
        if (!WriteFile(impl_->out, p, DWORD(n), &written, nullptr) || written == 0) return;
        p += written;
        n -= written;
    }
}

bool Terminal::readKey(KeyEvent& ev, int timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        DWORD wait = INFINITE;
        if (timeoutMs >= 0) {
            long long used = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            wait = used >= timeoutMs ? 0 : DWORD(timeoutMs - used);
        }
        if (WaitForSingleObject(impl_->in, wait) != WAIT_OBJECT_0) return false;
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(impl_->in, &rec, 1, &n) || n == 0) return false;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        const KEY_EVENT_RECORD& k = rec.Event.KeyEvent;
        ev = KeyEvent();
        bool alt = (k.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
        bool ctrl = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        switch (k.wVirtualKeyCode) {
        case VK_LEFT: ev.code = KeyCode::Left; return true;
        case VK_RIGHT: ev.code = KeyCode::Right; return true;
        case VK_UP: ev.code = KeyCode::Up; return true;
        case VK_DOWN: ev.code = KeyCode::Down; return true;
        case VK_HOME: ev.code = KeyCode::Home; return true;
        case VK_END: ev.code = KeyCode::End; return true;
        case VK_DELETE: ev.code = KeyCode::Delete; return true;
        case VK_BACK: ev.code = KeyCode::Backspace; return true;
        case VK_RETURN: ev.code = KeyCode::Enter; return true;
        case VK_TAB: ev.code = KeyCode::Edit; return true;
        case VK_ESCAPE: ev.code = KeyCode::Break; return true;
        case VK_F1: ev.code = KeyCode::Edit; return true;
        case VK_F2: ev.code = KeyCode::CapsLock; return true;
        case VK_F3: ev.code = KeyCode::TrueVideo; return true;
        case VK_F4: ev.code = KeyCode::InvVideo; return true;
        case VK_F5: ev.code = KeyCode::Left; return true;
        case VK_F6: ev.code = KeyCode::Down; return true;
        case VK_F7: ev.code = KeyCode::Up; return true;
        case VK_F8: ev.code = KeyCode::Right; return true;
        case VK_F9: ev.code = KeyCode::Graphics; return true;
        case VK_F10: ev.code = KeyCode::Backspace; return true;
        case VK_F11: ev.code = KeyCode::Extend; return true;
        default: break;
        }
        wchar_t wc = k.uChar.UnicodeChar;
        if (wc == 0) continue;
        if (ctrl && !alt) {
            switch (k.wVirtualKeyCode) {
            case 'C': ev.code = KeyCode::Break; return true;
            case 'D': case 'Z': ev.code = KeyCode::Quit; return true;
            case 'E': ev.code = KeyCode::Extend; return true;
            case 'G': ev.code = KeyCode::Graphics; return true;
            case 'L': ev.code = KeyCode::Redraw; return true;
            default: continue;
            }
        }
        uint32_t cp = wc;
        if (wc >= 0xD800 && wc < 0xDC00) { impl_->highSurrogate = wc; continue; }
        if (wc >= 0xDC00 && wc < 0xE000) {
            cp = 0x10000 + ((uint32_t(impl_->highSurrogate) - 0xD800) << 10) + (wc - 0xDC00);
        }
        if (cp < 0x20) continue;
        ev.code = KeyCode::Char;
        ev.ch = cp;
        ev.symbolShift = alt;
        return true;
    }
}

} // namespace zxgw

#endif // _WIN32
