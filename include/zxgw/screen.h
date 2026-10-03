// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The character-cell screen model.
//
// The original Spectrum has a 256x192 pixel bitmap with an 8x8 attribute
// grid.  In the terminal every 8x8 character cell becomes one terminal cell
// that holds either
//   - a character (ASCII, block graphic or keyword glyph), or
//   - a 2x4 pattern of "dots" rendered with a Unicode braille character.
// Each cell carries a Spectrum attribute byte (FLASH, BRIGHT, PAPER, INK).

#ifndef ZXGW_SCREEN_H
#define ZXGW_SCREEN_H

#include <cstdint>
#include <string>
#include <vector>

namespace zxgw {

// One terminal character cell.
struct Cell {
    uint8_t ch = ' ';       // Spectrum character code (32..143) for text cells
    uint8_t dots = 0;       // braille dot pattern for graphic cells, see Screen::dotBit()
    uint8_t attr = 0x38;    // Spectrum attribute: F B P P P I I I
    uint8_t flags = 0;      // kGraphic, kInverse

    enum : uint8_t {
        kGraphic = 0x01,    // the cell shows dots, not a character
        kInverse = 0x02     // printed with INVERSE 1 (ink and paper swapped)
    };

    bool graphic() const { return flags & kGraphic; }
    bool inverse() const { return flags & kInverse; }
    bool operator==(const Cell& o) const {
        return ch == o.ch && dots == o.dots && attr == o.attr && flags == o.flags;
    }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

// A rectangular grid of cells.  Row 0 is the top line of the terminal.
class Screen {
public:
    Screen() = default;
    Screen(int width, int height) { resize(width, height); }

    // Resize the grid, keeping the top left content.
    void resize(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    const Cell& at(int row, int col) const { return cells_[row * width_ + col]; }
    Cell& at(int row, int col) { ++version_; return cells_[row * width_ + col]; }

    // A counter that changes whenever the screen may have changed.
    uint64_t version() const { return version_; }
    void touch() { ++version_; }

    // The bit used for the dot at (dx, dy) inside a cell (dx 0..1, dy 0..3).
    static uint8_t dotBit(int dx, int dy) { return uint8_t(1u << (dy * 2 + dx)); }

    // UTF-8 text for the glyph of a cell (character, block graphic or braille).
    static std::string glyphUtf8(const Cell& c);

    // Unicode code point for a Spectrum character code (32..143).
    static uint32_t codePoint(uint8_t spectrumChar);

    // Unicode braille code point for a dot pattern.
    static uint32_t brailleCodePoint(uint8_t dots);

    // Encode a code point as UTF-8.
    static std::string utf8(uint32_t cp);

private:
    int width_ = 0;
    int height_ = 0;
    uint64_t version_ = 0;
    std::vector<Cell> cells_;
};

// The eight Spectrum colours as 24 bit RGB.  Index = colour number 0..7.
struct Rgb { uint8_t r, g, b; };
Rgb spectrumColour(int colour, bool bright);

} // namespace zxgw

#endif
