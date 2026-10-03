// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 7 (graphics). PLOT, DRAW, CIRCLE and POINT.
//
// The Spectrum has 256x176 pixels above the lower screen; (0,0) is the
// bottom left.  In the terminal each character cell holds 2x4 braille dots,
// so the pixel space is (2 * columns) x (4 * (rows - 2)).  With the option
// scaleGraphics the classic 256x176 space is scaled onto those dots.
// The line and arc algorithms are those of the ROM (DRAW-LINE is a
// Bresenham variant, CIRCLE and arcs are polygons built with a rotation
// formula), so drawings have the same shapes.

#include "machine.h"

#include <algorithm>
#include <cmath>

#include "fp.h"

namespace zxgw {

using namespace sv;

int Machine::dotsW() const { return opts.scaleGraphics ? 256 : W * 2; }
int Machine::dotsH() const { return opts.scaleGraphics ? 176 : (H - 2) * 4; }

// Map a pixel to a cell and the dot inside it.
bool Machine::mapPixel(int x, int y, int& row, int& col, int& dx, int& dy) const {
    if (x < 0 || y < 0 || x >= dotsW() || y >= dotsH()) return false;
    int px = x, py = y;
    if (opts.scaleGraphics) {
        double s = std::min(W * 2 / 256.0, (H - 2) * 4 / 176.0);
        px = int(x * s);
        py = int(y * s);
    }
    int rows = H - 2;
    row = rows - 1 - py / 4;
    dy = 3 - py % 4;
    col = px / 2;
    dx = px % 2;
    return row >= 0 && row < H && col >= 0 && col < W;
}

// Is the dot set?  For character cells the glyph decides.
bool Machine::pixelSet(int row, int col, int dx, int dy) const {
    const Cell& cell = screen.at(row, col);
    bool on;
    if (cell.graphic()) {
        on = cell.dots & Screen::dotBit(dx, dy);
    } else {
        uint8_t g[8];
        if (cell.ch >= 0x80 && cell.ch < 0x90) {
            uint8_t top = uint8_t(((cell.ch & 1) ? 0x0F : 0) | ((cell.ch & 2) ? 0xF0 : 0));
            uint8_t bottom = uint8_t(((cell.ch & 4) ? 0x0F : 0) | ((cell.ch & 8) ? 0xF0 : 0));
            for (int i = 0; i < 4; ++i) { g[i] = top; g[i + 4] = bottom; }
        } else {
            uint16_t a = uint16_t(word(CHARS) + cell.ch * 8);
            for (int i = 0; i < 8; ++i) g[i] = mem[uint16_t(a + i)];
        }
        on = false;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                if (g[dy * 2 + y] & (0x80 >> (dx * 4 + x))) on = true;
    }
    return cell.inverse() ? !on : on;
}

// L2307 STK-TO-BC: two numbers from the stack as magnitudes and signs.
// b/d come from the last value, c/e from the one before.
void Machine::stkToBc(int& b, int& c, int& d, int& e) {
    bool over, neg;
    b = fpToBc(over, neg);
    if (over) error(0x0A);                                     // REPORT-Bc
    d = neg ? -1 : 1;
    c = fpToBc(over, neg);
    if (over) error(0x0A);
    e = neg ? -1 : 1;
}

// GW BC_POSTVE: PLOT and POINT require positive coordinates.
void Machine::bcPostve(int& x, int& y) {
    int b, c, d, e;
    stkToBc(b, c, d, e);
    if (d < 0 || e < 0) error(0x0A);
    y = b;
    x = c;
}

// L22CB POINT-SUB
void Machine::pointSub() {
    int x, y;
    bcPostve(x, y);
    if (y >= dotsH() || x >= dotsW()) error(0x0A);             // PIXEL-ADD
    int row, col, dx, dy;
    bool on = mapPixel(x, y, row, col, dx, dy) && pixelSet(row, col, dx, dy);
    stack(on ? 1 : 0);
}

// L22DC PLOT
void Machine::plotCmd() {
    int x, y;
    bcPostve(x, y);
    plotSub(x, y);
    temps();
}

// L22E5 PLOT-SUB: set, reset (INVERSE 1) or toggle (OVER 1) a pixel and
// colour its cell.
void Machine::plotSub(int x, int y) {
    if (y >= dotsH() || x >= dotsW() || x < 0 || y < 0) error(0x0A);
    setWord(COORDS, uint16_t((x & 0xFF) | ((y & 0xFF) << 8)));
    coordsX = x;
    coordsY = y;
    int row, col, dx, dy;
    if (!mapPixel(x, y, row, col, dx, dy)) return;
    Cell& cell = screen.at(row, col);
    if (!cell.graphic()) {
        cell.flags = Cell::kGraphic;
        cell.dots = 0;
        cell.ch = ' ';
    }
    uint8_t bit = Screen::dotBit(dx, dy);
    uint8_t p = mem[P_FLAG];
    uint8_t dots = cell.dots;
    if (!(p & 0x01)) dots &= uint8_t(~bit);                    // OVER 0: blank the pixel
    if (!(p & 0x04)) dots ^= bit;                              // INVERSE 0: switch it
    cell.dots = dots;
    poAttr(row, col);
}

// L2320 CIRCLE
void Machine::circleCmd() {
    if (getChar() != ',') error(0x0B);
    nextChar();
    expt1Num();
    checkEnd();
    double r = std::fabs(popNum());
    double y = popNum();
    double x = popNum();
    if (r < 1) {                                               // a dot
        stack(x);
        stack(y);
        plotCmd();
        return;
    }
    double mems[6];
    mems[5] = checked(2 * M_PI);                               // C-R-GRE-1
    int count = cdPrms1(r, mems[5], mems);
    double hc = checked(r * mems[1]);                          // half chord
    if (hc < 0.5) {
        stack(x);
        stack(y);
        plotCmd();
        return;
    }
    // C-ARC-GE1
    double sy = checked(y - hc);
    double sx = checked(x + r);
    double rx = 0, ry = checked(2 * hc);
    // COORDS are set with FIND-INT1
    stack(sy);
    stack(sx);
    int cx = findInt1();
    int cy = findInt1();
    if (opts.scaleGraphics || dotsW() <= 256) {
        // nothing
    }
    coordsX = cx;
    coordsY = cy;
    setWord(COORDS, uint16_t((cx & 0xFF) | ((cy & 0xFF) << 8)));
    // DRW-STEPS
    double tx = sx, ty = sy, ax = sx, ay = sy;
    int b = count - 1;
    bool first = true;
    while (b > 0) {
        if (!first) {                                          // ARC-LOOP
            double nrx = checked(checked(rx * mems[3]) - checked(ry * mems[4]));
            double nry = checked(checked(rx * mems[4]) + checked(ry * mems[3]));
            rx = nrx;
            ry = nry;
        }
        first = false;
        ax = checked(ax + rx);                                 // ARC-START
        ay = checked(ay + ry);
        drawLineTo(checked(ax - coordsX), checked(ay - coordsY));
        --b;
    }
    drawLineTo(checked(tx - coordsX), checked(ty - coordsY));  // ARC-END
    temps();
}

// L2382 DRAW
void Machine::drawCmd() {
    if (getChar() != ',') {
        checkEnd();
        double y = popNum();
        double x = popNum();
        drawLineTo(x, y);                                      // LINE-DRAW
        temps();
        return;
    }
    nextChar();                                                // DR-3-PRMS
    expt1Num();
    checkEnd();
    double mems[6];
    double A = popNum();
    double y = popNum();
    double x = popNum();
    mems[5] = A;
    double s = checked(std::sin(checked(A * 0.5)));
    if (s == 0) {                                              // a full turn: a line
        drawLineTo(x, y);
        temps();
        return;
    }
    // DR-SIN-NZ
    double D = std::fabs(checked(checked(std::fabs(x) + std::fabs(y)) / s));
    if (D < 1) {
        drawLineTo(x, y);
        temps();
        return;
    }
    int count = cdPrms1(D, A, mems);                           // DR-PRMS
    double f = checked(mems[1] / s);
    double xx = checked(x * f);
    double yy = checked(y * f);
    double angle = checked(checked(A - mems[0]) * 0.5);
    double sa = checked(std::sin(angle));
    double ca = checked(std::cos(angle));
    double xr = checked(checked(yy * sa) + checked(xx * ca));
    double yr = checked(checked(yy * ca) - checked(xx * sa));
    if (checked(std::fabs(yr) + std::fabs(xr)) < 1) {
        drawLineTo(x, y);
        temps();
        return;
    }
    double tx = checked(x + coordsX);
    double ty = checked(y + coordsY);
    double ax = coordsX, ay = coordsY;
    double rx = xr, ry = yr;
    int b = count - 1;                                         // DRW-STEPS
    bool first = true;
    while (b > 0) {
        if (!first) {                                          // ARC-LOOP
            double nrx = checked(checked(rx * mems[3]) - checked(ry * mems[4]));
            double nry = checked(checked(rx * mems[4]) + checked(ry * mems[3]));
            rx = nrx;
            ry = nry;
        }
        first = false;
        ax = checked(ax + rx);                                 // ARC-START
        ay = checked(ay + ry);
        drawLineTo(checked(ax - coordsX), checked(ay - coordsY));
        --b;
    }
    drawLineTo(checked(tx - coordsX), checked(ty - coordsY));  // ARC-END
    temps();
}

// L247D CD-PRMS1: number of straight lines for an arc or circle and the
// constants of the rotation formula.
int Machine::cdPrms1(double z, double angle, double mems[6]) {
    double n = std::fabs(checked(angle / checked(2 / checked(std::sqrt(z)))));
    stack(n);
    bool over, neg;
    int a = fpToA(over, neg);
    if (over) a = 252;                                         // USE-252
    else {
        a = (a & 0xFC) + 4;
        if (a > 255) a = 252;
    }
    double step = checked(angle / a);                          // DRAW-SAVE
    mems[4] = checked(std::sin(step));
    double s2 = checked(std::sin(checked(step * 0.5)));
    mems[1] = s2;
    mems[0] = step;
    mems[3] = checked(1 - checked(2 * checked(s2 * s2)));      // double angle formula
    return a;
}

// L24B7 DRAW-LINE: a line to the relative position on the stack.
void Machine::drawLine() {
    double y = popNum();
    double x = popNum();
    drawLineTo(x, y);
}

void Machine::drawLineTo(double dxv, double dyv) {
    stack(dxv);
    stack(dyv);
    int b, c, d, e;
    stkToBc(b, c, d, e);                                       // b = |y|, c = |x|
    int major, minor, hvX, hvY;
    if (c >= b) {                                              // DL-X-GE-Y
        if (c == 0) return;
        minor = b;
        major = c;
        hvX = e;
        hvY = 0;
    } else {
        minor = c;
        major = b;
        hvX = 0;
        hvY = d;
    }
    int acc = major >> 1;                                      // DL-LARGER
    for (int i = 0; i < major; ++i) {                          // D-L-LOOP
        acc += minor;
        int sx, sy;
        if (acc >= major) {                                    // D-L-DIAG
            acc -= major;
            sx = e;
            sy = d;
        } else {                                               // D-L-HR-VT
            sx = hvX;
            sy = hvY;
        }
        int nx = coordsX + sx, ny = coordsY + sy;              // D-L-STEP
        if (nx < 0 || nx >= dotsW()) error(0x0A);              // D-L-RANGE
        plotSub(nx, ny);
    }
}

} // namespace zxgw
