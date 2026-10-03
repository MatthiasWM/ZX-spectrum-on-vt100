// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Minimal platform abstraction of a text terminal: raw keyboard input,
// output of escape sequences and the window size.  There are two
// implementations: POSIX (termios, Linux/macOS/BSD) and Win32 (console API
// with virtual terminal processing, Windows 10 and later).

#ifndef ZXGW_TERMINAL_H
#define ZXGW_TERMINAL_H

#include <string>

#include "zxgw/host.h"

namespace zxgw {

class Terminal {
public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // True if both input and output are an interactive terminal.
    static bool interactive();

    // Switch to raw input and the alternate screen; undone by restore()
    // and the destructor.
    void open();
    void restore();

    // The window size in characters.
    void size(int& columns, int& rows);

    // Write bytes (UTF-8 and escape sequences).
    void write(const std::string& bytes);

    // Wait up to timeoutMs (<0 = forever) for a key.
    bool readKey(KeyEvent& ev, int timeoutMs);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace zxgw

#endif
