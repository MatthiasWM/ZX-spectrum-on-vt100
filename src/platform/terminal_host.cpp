// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The terminal host: draws the cell screen with ANSI escape sequences.
//
// Only cells that changed since the last frame are written.  The Spectrum
// colours are produced in 24 bit colour, the 256 colour palette (which
// contains the exact Spectrum levels 0, 0xD7 and 0xFF), 16 colours or no
// colour at all.  FLASH swaps ink and paper every 16 frames (0.32 s) like
// the ULA does.

#include "zxgw/terminal_host.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#include "terminal.h"

namespace zxgw {

namespace {

ColorMode detectColorMode() {
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm && (std::strstr(colorterm, "truecolor") || std::strstr(colorterm, "24bit")))
        return ColorMode::TrueColor;
    const char* term = std::getenv("TERM");
    if (term && std::strcmp(term, "dumb") == 0) return ColorMode::Mono;
    if (std::getenv("NO_COLOR")) return ColorMode::Mono;
#ifdef _WIN32
    return ColorMode::TrueColor;
#else
    return ColorMode::Ansi256;
#endif
}

// The ANSI colour number for a Spectrum colour (blue, red, green bits).
int ansiColour(int c) {
    static const int map[8] = {0, 4, 1, 5, 2, 6, 3, 7};
    return map[c & 7];
}

int cubeIndex(int colour, bool bright) {
    int level = bright ? 5 : 4;                                // 0xFF : 0xD7
    int r = (colour & 2) ? level : 0;
    int g = (colour & 4) ? level : 0;
    int b = (colour & 1) ? level : 0;
    return 16 + 36 * r + 6 * g + b;
}

} // namespace

TerminalHost::TerminalHost() : TerminalHost(Config()) {}

TerminalHost::TerminalHost(const Config& config)
    : config_(config), term_(new Terminal), start_(std::chrono::steady_clock::now()) {
    mode_ = config.color == ColorMode::Auto ? detectColorMode() : config.color;
    term_->open();
}

TerminalHost::~TerminalHost() {
    term_->restore();
}

void TerminalHost::screenSize(int& columns, int& rows) { term_->size(columns, rows); }

void TerminalHost::invalidate() { full_ = true; }

bool TerminalHost::interactive() { return Terminal::interactive(); }

std::string TerminalHost::sgr(const Cell& cell, bool flashPhase) const {
    int ink = cell.attr & 7;
    int paper = (cell.attr >> 3) & 7;
    bool bright = cell.attr & 0x40;
    bool swap = cell.inverse();
    if ((cell.attr & 0x80) && flashPhase) swap = !swap;
    if (swap) std::swap(ink, paper);
    char buf[64];
    switch (mode_) {
    case ColorMode::TrueColor: {
        Rgb f = spectrumColour(ink, bright), b = spectrumColour(paper, bright);
        std::snprintf(buf, sizeof(buf), "\x1b[0;38;2;%d;%d;%d;48;2;%d;%d;%dm", f.r, f.g, f.b, b.r, b.g, b.b);
        break;
    }
    case ColorMode::Ansi256:
        std::snprintf(buf, sizeof(buf), "\x1b[0;38;5;%d;48;5;%dm", cubeIndex(ink, bright), cubeIndex(paper, bright));
        break;
    case ColorMode::Ansi16:
        std::snprintf(buf, sizeof(buf), "\x1b[0;%d;%dm", (bright ? 90 : 30) + ansiColour(ink),
                      (bright ? 100 : 40) + ansiColour(paper));
        break;
    default:
        std::snprintf(buf, sizeof(buf), swap ? "\x1b[0;7m" : "\x1b[0m");
        break;
    }
    return buf;
}

void TerminalHost::present(const Screen& screen) {
    int w = screen.width(), h = screen.height();
    int tw, th;
    term_->size(tw, th);
    if (w != shownW_ || h != shownH_ || tw != termW_ || th != termH_ || full_) {
        termW_ = tw;
        termH_ = th;
        shown_.assign(size_t(w) * h, Cell());
        shownPhase_.assign(size_t(w) * h, false);
        shownW_ = w;
        shownH_ = h;
        term_->write("\x1b[0m\x1b[H\x1b[2J");
        lastSgr_.clear();
        full_ = true;
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_).count();
    bool phase = (ms / 320) % 2 == 1;
    std::string out;
    int curRow = -1, curCol = -1;
    // A window smaller than the screen shows its bottom part, where the
    // edit line and the reports are.
    int top = h > th ? h - th : 0;
    for (int r = top; r < h; ++r) {
        for (int c = 0; c < w && c < tw; ++c) {
            const Cell& cell = screen.at(r, c);
            size_t i = size_t(r) * w + c;
            bool cellPhase = (cell.attr & 0x80) ? phase : false;
            if (!full_ && shown_[i] == cell && shownPhase_[i] == cellPhase) continue;
            shown_[i] = cell;
            shownPhase_[i] = cellPhase;
            if (r != curRow || c != curCol) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", r - top + 1, c + 1);
                out += buf;
            }
            std::string s = sgr(cell, phase);
            if (s != lastSgr_) {
                out += s;
                lastSgr_ = s;
            }
            out += Screen::glyphUtf8(cell);
            curRow = r;
            curCol = c + 1;
        }
    }
    full_ = false;
    if (!out.empty()) term_->write(out);
}

bool TerminalHost::readKey(KeyEvent& event, int timeoutMs) {
    for (;;) {
        if (!term_->readKey(event, timeoutMs)) return false;
        if (event.code == KeyCode::Redraw) {
            invalidate();
            return true;
        }
        return true;
    }
}

void TerminalHost::beep(double frequencyHz, double seconds) {
    if (config_.bell && seconds >= 0.05) term_->write("\a");
    Host::beep(frequencyHz, seconds);
}

void TerminalHost::printerLine(const std::string& utf8Line) {
    if (config_.printerFile.empty()) return;
    std::ofstream out(config_.printerFile, std::ios::app);
    out << utf8Line << '\n';
}

// ---------------------------------------------------------------------------
// BatchHost
// ---------------------------------------------------------------------------
BatchHost::BatchHost(std::istream& in, std::ostream& out) : BatchHost(in, out, Config()) {}

BatchHost::BatchHost(std::istream& in, std::ostream& out, const Config& config)
    : in_(in), out_(out), config_(config) {}

void BatchHost::type(const std::string& utf8) {
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = (unsigned char)utf8[i];
        KeyEvent ev;
        if (c == '\n') {
            ev.code = KeyCode::Enter;
            ++i;
        } else {
            int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
            uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k < n && i + size_t(k) < utf8.size(); ++k)
                cp = (cp << 6) | ((unsigned char)utf8[i + size_t(k)] & 0x3F);
            i += size_t(n);
            if (cp < 0x20) continue;
            ev.code = KeyCode::Char;
            ev.ch = cp;
        }
        pending_.push_back(ev);
    }
}

void BatchHost::screenSize(int& columns, int& rows) {
    columns = config_.columns;
    rows = config_.rows;
}

void BatchHost::present(const Screen&) { out_.flush(); }

void BatchHost::newlineIfNeeded() {
    if (!atLineStart_) {
        out_ << '\n';
        atLineStart_ = true;
    }
}

bool BatchHost::readKey(KeyEvent& event, int timeoutMs) {
    // A poll only returns keys of the current line; a new line is read when
    // the machine actually waits for input.
    if (timeoutMs == 0) {
        if (pending_.empty()) return false;
        event = pending_.front();
        pending_.pop_front();
        return true;
    }
    if (pending_.empty() && !eof_) {
        std::string line;
        if (std::getline(in_, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            newlineIfNeeded();
            if (config_.echo) {
                out_ << line << '\n';
                justEchoed_ = true;
            }
            type(line + "\n");
        } else {
            eof_ = true;
        }
    }
    if (!pending_.empty()) {
        event = pending_.front();
        pending_.pop_front();
        return true;
    }
    if (eof_) {
        newlineIfNeeded();
        out_.flush();
        event = KeyEvent();
        event.code = KeyCode::Quit;
        return true;
    }
    return false;
}

void BatchHost::beep(double, double) {}

void BatchHost::printerLine(const std::string& utf8Line) {
    if (!config_.printerToOutput) return;
    newlineIfNeeded();
    out_ << utf8Line << '\n';
}

void BatchHost::transcript(Output where, const std::string& utf8) {
    // A line break right after the echoed input (the print position was at
    // the end of a line) would only add an empty line.
    bool skipNewline = justEchoed_;
    justEchoed_ = false;
    if (skipNewline && utf8 == "\n") return;
    if (where != last_) {
        newlineIfNeeded();
        last_ = where;
    }
    for (char c : utf8) {
        if (c == '\n') {
            out_ << '\n';
            atLineStart_ = true;
        } else {
            out_ << c;
            atLineStart_ = false;
        }
    }
}

} // namespace zxgw
