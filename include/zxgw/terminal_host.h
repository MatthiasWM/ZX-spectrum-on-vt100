// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Hosts shipped with the library:
//
//   TerminalHost  an interactive ANSI/VT terminal (POSIX or Windows console)
//   BatchHost     plain stdin/stdout: input lines are typed into the
//                 machine, everything printed is written as text

#ifndef ZXGW_TERMINAL_HOST_H
#define ZXGW_TERMINAL_HOST_H

#include <chrono>
#include <cstdio>
#include <deque>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "zxgw/host.h"
#include "zxgw/screen.h"

namespace zxgw {

class Terminal;

enum class ColorMode { Auto, TrueColor, Ansi256, Ansi16, Mono };

class TerminalHost : public Host {
public:
    struct Config {
        ColorMode color = ColorMode::Auto;
        std::string printerFile = "zxprinter.txt";  // LPRINT, LLIST, COPY
        bool bell = false;                          // BEEP rings the terminal bell
    };

    TerminalHost();
    explicit TerminalHost(const Config& config);
    ~TerminalHost() override;

    void screenSize(int& columns, int& rows) override;
    void present(const Screen& screen) override;
    bool readKey(KeyEvent& event, int timeoutMs) override;
    void beep(double frequencyHz, double seconds) override;
    void printerLine(const std::string& utf8Line) override;

    // Forget what is on the terminal and repaint everything next time.
    void invalidate();

    // True if stdin and stdout are an interactive terminal.
    static bool interactive();

private:
    std::string sgr(const Cell& cell, bool flashPhase) const;

    Config config_;
    ColorMode mode_;
    std::unique_ptr<Terminal> term_;
    std::vector<Cell> shown_;
    std::vector<bool> shownPhase_;
    int shownW_ = 0, shownH_ = 0, termW_ = 0, termH_ = 0;
    std::string lastSgr_;
    bool full_ = true;
    std::chrono::steady_clock::time_point start_;
};

class BatchHost : public Host {
public:
    struct Config {
        int columns = 32;
        int rows = 24;
        bool echo = true;            // write the input lines to the output
        bool printerToOutput = true; // ZX Printer lines go to the output
    };

    BatchHost(std::istream& in, std::ostream& out);
    BatchHost(std::istream& in, std::ostream& out, const Config& config);

    // Feed additional input (typed before the stream is read).
    void type(const std::string& utf8);

    void screenSize(int& columns, int& rows) override;
    void present(const Screen& screen) override;
    bool readKey(KeyEvent& event, int timeoutMs) override;
    void beep(double frequencyHz, double seconds) override;
    void printerLine(const std::string& utf8Line) override;
    void transcript(Output where, const std::string& utf8) override;
    bool batch() const override { return true; }

private:
    void newlineIfNeeded();

    std::istream& in_;
    std::ostream& out_;
    Config config_;
    std::deque<KeyEvent> pending_;
    bool atLineStart_ = true;
    bool justEchoed_ = false;
    bool eof_ = false;
    Output last_ = Output::UpperScreen;
};

} // namespace zxgw

#endif
