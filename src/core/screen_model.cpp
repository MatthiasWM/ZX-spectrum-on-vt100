// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The character cell screen and the mapping of the Spectrum character set
// to Unicode.

#include "zxgw/screen.h"

#include <algorithm>

namespace zxgw {

void Screen::resize(int width, int height) {
    std::vector<Cell> cells(size_t(width) * height);
    for (int r = 0; r < std::min(height, height_); ++r)
        for (int c = 0; c < std::min(width, width_); ++c)
            cells[size_t(r) * width + c] = cells_[size_t(r) * width_ + c];
    cells_.swap(cells);
    width_ = width;
    height_ = height;
    ++version_;
}

std::string Screen::utf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += char(cp);
    } else if (cp < 0x800) {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
    return s;
}

// The Spectrum character set differs from ASCII in three places, and the
// sixteen 2x2 block graphics map to the Unicode quadrant characters.
uint32_t Screen::codePoint(uint8_t c) {
    // Bits of the mosaic code: 1 = top right, 2 = top left,
    // 4 = bottom right, 8 = bottom left (see L0B3E PO-GR-2).
    static const uint32_t blocks[16] = {
        0x0020, 0x259D, 0x2598, 0x2580, 0x2597, 0x2590, 0x259A, 0x259C,
        0x2596, 0x259E, 0x258C, 0x259B, 0x2584, 0x259F, 0x2599, 0x2588
    };
    if (c == 0x5E) return 0x2191;      // up arrow (power)
    if (c == 0x60) return 0x00A3;      // pound sign
    if (c == 0x7F) return 0x00A9;      // copyright sign
    if (c >= 0x20 && c < 0x7F) return c;
    if (c >= 0x80 && c < 0x90) return blocks[c - 0x80];
    return '?';
}

// Braille dots are numbered 1-2-3-7 down the left column and 4-5-6-8 down
// the right column; our dot bit is (row * 2 + column).
uint32_t Screen::brailleCodePoint(uint8_t dots) {
    static const uint8_t brailleBit[8] = {
        0x01, 0x08,   // row 0: dot 1, dot 4
        0x02, 0x10,   // row 1: dot 2, dot 5
        0x04, 0x20,   // row 2: dot 3, dot 6
        0x40, 0x80    // row 3: dot 7, dot 8
    };
    uint32_t b = 0;
    for (int i = 0; i < 8; ++i)
        if (dots & (1 << i)) b |= brailleBit[i];
    return 0x2800 + b;
}

std::string Screen::glyphUtf8(const Cell& c) {
    if (c.graphic()) return c.dots ? utf8(brailleCodePoint(c.dots)) : std::string(" ");
    return utf8(codePoint(c.ch));
}

Rgb spectrumColour(int colour, bool bright) {
    uint8_t v = bright ? 0xFF : 0xD7;
    colour &= 7;
    return Rgb{uint8_t((colour & 2) ? v : 0), uint8_t((colour & 4) ? v : 0),
               uint8_t((colour & 1) ? v : 0)};
}

} // namespace zxgw
