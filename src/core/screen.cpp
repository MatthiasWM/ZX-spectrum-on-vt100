// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 5. SCREEN AND PRINTER HANDLING ROUTINES (output side).
//
// Print positions are kept exactly like the ROM does it, in the system
// variables S_POSN (upper screen), SPOSNL (lower screen) and P_POSN
// (printer):
//   line   B counts down from H (the top line) - the ROM uses $18 = 24
//   column C counts down from W+1 (the left edge) - the ROM uses $21 = 33
// so that the original arithmetic carries over with the constants 24 and
// 32 replaced by the actual terminal size H and W.

#include "machine.h"

#include "tables.h"

namespace zxgw {

using namespace sv;

// Downsample an 8x8 bitmap to the 2x4 dots of a braille cell.  Each dot
// stands for a block of 4x2 pixels and is set when at least three of the
// eight pixels are set (or any pixel for very sparse bitmaps).
uint8_t downsampleGlyph(const uint8_t* glyph) {
    auto build = [&](int threshold) {
        uint8_t dots = 0;
        for (int dy = 0; dy < 4; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                int n = 0;
                for (int y = 0; y < 2; ++y) {
                    uint8_t row = glyph[dy * 2 + y];
                    for (int x = 0; x < 4; ++x)
                        if (row & (0x80 >> (dx * 4 + x))) ++n;
                }
                if (n >= threshold) dots |= Screen::dotBit(dx, dy);
            }
        return dots;
    };
    uint8_t d = build(3);
    if (!d) d = build(1);
    return d;
}

namespace {

bool isBlank(const Cell& c) {
    return c.graphic() ? c.dots == 0 && !c.inverse() : (c.ch == ' ' && !c.inverse());
}

} // namespace

// The screen row of line b: CL-SET and CL-ADDR combined.
int Machine::rowOf(int b) const {
    if (mem[TV_FLAG] & TV_LOWER) return 2 * H - b - mem[DF_SZ];
    return H - b;
}

// ---------------------------------------------------------------------------
// L09F4 PRINT-OUT
// ---------------------------------------------------------------------------
void Machine::printOut(uint8_t a) {
    if (a >= 0x20) { poAble(a); return; }
    if (a < 0x06 || a >= 0x18) { poAble('?'); return; }        // PO-QUEST
    switch (a) {                                               // ctlchrtab
    case 0x06: poComma(); break;
    case 0x08: poBack1(); break;
    case 0x09: poRight(); break;
    case 0x0D: poEnter(); break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
        mem[TVDATA] = a;                                       // PO-1-OPER
        poChange(PO_CONT);
        break;
    case 0x16: case 0x17:
        mem[TVDATA] = a;                                       // PO-2-OPER
        poChange(PO_TV_2);
        break;
    default:
        poAble('?');
        break;
    }
}

// L0A23 PO-BACK-1: cursor left, up a line at the left edge.
void Machine::poBack1() {
    int b, c;
    poFetch(b, c);
    bool printer = flag(FLAGS, F_PRINTER);
    int lw = printer ? opts.printerWidth : W;
    ++c;
    if (c == lw + 2) {
        if (printer) {
            c = lw + 1;                                        // PO-BACK-2
        } else {
            ++b;
            c = 2;
            if (b == H + 1) {                                  // GW fix: $19
                --b;
                c = W + 1;
            }
        }
    }
    clSet(b, c);                                               // PO-BACK-3
}

// L0A3D PO-RIGHT: print a space with OVER 1 (GW: through PO-ABLE so the
// position is stored).
void Machine::poRight() {
    uint8_t p = mem[P_FLAG];
    mem[P_FLAG] = 0x01;
    poAble(' ');
    mem[P_FLAG] = p;
}

// L0A4F PO-ENTER
void Machine::poEnter() {
    if (flag(FLAGS, F_PRINTER)) { copyBuff(); return; }
    int b, c;
    poFetch(b, c);
    c = W + 1;
    poScr(b, c);
    --b;
    clSet(b, c);
    if (transcribing()) host.transcript(flag(TV_FLAG, TV_LOWER) ? Output::LowerScreen : Output::UpperScreen, "\n");
}

// L0A5F PO-COMMA: move to the next 16 column tab stop.
void Machine::poComma() {
    int b, c;
    poFetch(b, c);
    int lw = flag(FLAGS, F_PRINTER) ? opts.printerWidth : W;
    int col = (lw + 1 - c) % lw;
    int next = (col / 16 + 1) * 16;
    int n = next >= lw ? lw - col : next - col;
    if (n <= 0) return;
    setFlag(FLAGS, F_SUPPRESS_SPACE, true);
    while (n--) poSave(' ');
}

// L0A6D PO-TV-2: first operand of AT or TAB.
void Machine::poTv2(uint8_t a) {
    mem[TVDATA + 1] = a;
    poChange(PO_CONT);
}

// L0A80 PO-CHANGE: alter the output address of the current channel.
void Machine::poChange(uint16_t routine) { setWord(word(CURCHL), routine); }

// L0A87 PO-CONT: the last operand of a control code has arrived.
void Machine::poCont(uint8_t a) {
    poChange(PRINT_OUT);
    uint8_t control = mem[TVDATA];
    uint8_t first = mem[TVDATA + 1];
    if (control < 0x16) { coTemp5(control, a); return; }       // CO-TEMP-5
    if (control == 0x17) { poFill(first); return; }            // PO-TAB
    // AT line, column (x0A9B)
    int line = first, col = a;
    if (col > W - 1 && !flag(FLAGS, F_PRINTER)) error(0x0A);
    if (flag(FLAGS, F_PRINTER)) {
        if (col > opts.printerWidth - 1) error(0x0A);
        clSet(0, opts.printerWidth + 1 - col);                 // PO-AT-SET
        return;
    }
    int c = W + 1 - col;
    if (line > H - 2) error(0x0A);                             // PO-AT-ERR
    int aa = H - 1 - line;
    int b = aa + 1;
    if (flag(TV_FLAG, TV_LOWER)) {
        poScr(b, c);
        return;
    }
    if (aa < mem[DF_SZ]) error(0x04);                          // REPORT-5
    if (transcribing()) {
        // A plain text transcript cannot move up; start a new line instead.
        int oldB = mem[S_POSN + 1];
        if (b != oldB) host.transcript(Output::UpperScreen, "\n");
    }
    clSet(b, c);
}

// L0AC3 PO-FILL: print spaces up to column a (TAB).
void Machine::poFill(int a) {
    int b, c;
    poFetch(b, c);
    int lw = flag(FLAGS, F_PRINTER) ? opts.printerWidth : W;
    int col = lw + 1 - c;
    int n = ((a - col) % lw + lw) % lw;
    if (n == 0) return;
    setFlag(FLAGS, F_SUPPRESS_SPACE, true);
    while (n--) poSave(' ');                                   // PO-SPACE
}

// L0AD9 PO-ABLE: print a character and store the new position.
void Machine::poAble(uint8_t a) {
    int b, c;
    poFetch(b, c);
    poAny(a, b, c);
    poStore(b, c);
}

// L0ADC PO-STORE
void Machine::poStore(int b, int c) {
    if (flag(FLAGS, F_PRINTER)) {
        mem[P_POSN] = uint8_t(c);
    } else if (flag(TV_FLAG, TV_LOWER)) {
        mem[SPOSNL] = uint8_t(c);
        mem[SPOSNL + 1] = uint8_t(b);
        mem[ECHO_E] = uint8_t(c);
        mem[ECHO_E + 1] = uint8_t(b);
    } else {
        mem[S_POSN] = uint8_t(c);
        mem[S_POSN + 1] = uint8_t(b);
    }
}

// L0B03 PO-FETCH
void Machine::poFetch(int& b, int& c) {
    if (flag(FLAGS, F_PRINTER)) {
        b = 0;
        c = mem[P_POSN];
    } else if (flag(TV_FLAG, TV_LOWER)) {
        c = mem[SPOSNL];
        b = mem[SPOSNL + 1];
    } else {
        c = mem[S_POSN];
        b = mem[S_POSN + 1];
    }
}

// L0B24 PO-ANY: characters 32-255.
void Machine::poAny(uint8_t a, int& b, int& c) {
    if (a < 0x80) {                                            // PO-CHAR
        const uint8_t* glyph = &mem[uint16_t(word(CHARS) + a * 8)];
        setFlag(FLAGS, F_SUPPRESS_SPACE, a == ' ');
        prAll(glyph, a, b, c);
        return;
    }
    if (a < 0x90) {                                            // PO-GR-1
        uint8_t g[8];
        uint8_t top = uint8_t(((a & 1) ? 0x0F : 0) | ((a & 2) ? 0xF0 : 0));
        uint8_t bottom = uint8_t(((a & 4) ? 0x0F : 0) | ((a & 8) ? 0xF0 : 0));
        for (int i = 0; i < 4; ++i) { g[i] = top; g[i + 4] = bottom; }
        prAll(g, a, b, c);
        return;
    }
    if (a >= 0xA5) {                                           // PO-T
        poTokens(a);
        poFetch(b, c);
        return;
    }
    const uint8_t* glyph = &mem[uint16_t(word(UDG) + (a - 0x90) * 8)];  // PO-T&UDG
    setFlag(FLAGS, F_SUPPRESS_SPACE, false);
    prAll(glyph, a, b, c);
}

// L0B7F PR-ALL: lay down a character at the print position.
void Machine::prAll(const uint8_t* glyph, uint8_t code, int& b, int& c) {
    bool printer = flag(FLAGS, F_PRINTER);
    int lw = printer ? opts.printerWidth : W;
    if (c == 1) {                                              // past the right edge
        --b;
        c = lw + 1;
        if (printer) copyBuff();
        else if (transcribing()) host.transcript(flag(TV_FLAG, TV_LOWER) ? Output::LowerScreen : Output::UpperScreen, "\n");
    }
    if (c == lw + 1) poScr(b, c);                              // PR-ALL-1
    bool over = mem[P_FLAG] & 0x01;
    bool inverse = mem[P_FLAG] & 0x04;
    if (printer) {
        setFlag(FLAGS2, F2_PRINTER_USED, true);
        int col = lw + 1 - c;
        if (col >= 0 && col < (int)printerLine.size()) {
            Cell& cell = printerLine[col];
            if (code >= 0x90) {
                cell.flags = Cell::kGraphic;
                cell.dots = downsampleGlyph(glyph);
                if (inverse) cell.dots = uint8_t(~cell.dots);
            } else if (!over || isBlank(cell)) {
                cell.flags = inverse ? Cell::kInverse : 0;
                cell.ch = code;
            }
        }
        --c;
        return;
    }
    int row = rowOf(b);
    int col = W + 1 - c;
    if (row >= 0 && row < H && col >= 0 && col < W) {
        Cell& cell = screen.at(row, col);
        if (code >= 0x90) {
            // A UDG: shown as braille dots made from its bitmap.
            uint8_t dots = downsampleGlyph(glyph);
            if (inverse) dots = uint8_t(~dots);
            if (over && cell.graphic()) {
                cell.dots ^= dots;
            } else {
                cell.flags = Cell::kGraphic;
                cell.dots = dots;
            }
        } else if (!over) {
            cell.flags = inverse ? Cell::kInverse : 0;
            cell.ch = code;
            cell.dots = 0;
        } else if (code == ' ') {
            // OVER 1 with a space leaves the cell alone, an inverse space
            // inverts it.
            if (inverse) {
                if (cell.graphic()) cell.dots = uint8_t(~cell.dots);
                else cell.flags ^= Cell::kInverse;
            }
        } else if (isBlank(cell)) {
            cell.flags = inverse ? Cell::kInverse : 0;
            cell.ch = code;
            cell.dots = 0;
        } else if (!cell.graphic() && cell.ch == code && cell.inverse() == inverse) {
            cell.flags = 0;                                    // XOR with itself
            cell.ch = ' ';
        } else {
            cell.flags = inverse ? Cell::kInverse : 0;         // cannot overlay glyphs
            cell.ch = code;
            cell.dots = 0;
        }
        poAttr(row, col);
        if (transcribing()) transcriptChar(code);
    }
    --c;
}

// L0BDB PO-ATTR: apply ATTR_T/MASK_T and INK/PAPER 9 to a cell.
void Machine::poAttr(int row, int col) {
    Cell& cell = screen.at(row, col);
    uint8_t t = mem[ATTR_T], mask = mem[MASK_T];
    uint8_t a = uint8_t(((cell.attr ^ t) & mask) ^ t);
    if (mem[P_FLAG] & 0x40) {                                  // PAPER 9
        a &= 0xC7;
        if (!(a & 0x04)) a ^= 0x38;
    }
    if (mem[P_FLAG] & 0x10) {                                  // INK 9
        a &= 0xF8;
        if (!(a & 0x20)) a ^= 0x07;
    }
    cell.attr = a;
}

// L0C0A PO-MSG: print a message from a table (no leading or trailing space).
void Machine::poMsg(const char* text) {
    for (const char* p = text; *p; ++p) poSave(uint8_t(*p));
}

// L0C10 PO-TOKENS: print a keyword with the leading and trailing spaces.
void Machine::poTokens(uint8_t token) {
    int index = token - 0xA5;
    const char* text = tokenText(token);
    // PO-SEARCH: the 32 function words and '<=' etc. have no leading space.
    if (index >= 0x20 && text[0] >= 'A' && !flag(FLAGS, F_SUPPRESS_SPACE)) poSave(' ');
    size_t n = 0;
    for (const char* p = text; *p; ++p, ++n) poSave(uint8_t(*p));
    char last = text[n - 1];
    if (last != '$' && last < 'A') return;
    if (index < 3) return;                                     // RND, INKEY$, PI
    poSave(' ');                                               // PO-TR-SP
}

// L0C3B PO-SAVE: print through RST 10 recursively.
void Machine::poSave(uint8_t a) { printA(a); }

// ---------------------------------------------------------------------------
// L0C55 PO-SCR: test for scrolling before printing on line b.
// ---------------------------------------------------------------------------
void Machine::poScr(int& b, int c) {
    if (flag(FLAGS, F_PRINTER)) return;
    if (flag(TV_FLAG, TV_LOWER)) {                             // PO-SCR-4
        poScr4(b);
        clSet(b, c);
        return;
    }
    int dfsz = mem[DF_SZ];
    if (b < dfsz) error(0x04);                                 // REPORT-5
    if (b != dfsz) { clSet(b, c); return; }
    if (flag(TV_FLAG, TV_AUTOLIST)) {
        if (uint8_t(mem[BREG] - 1) != 0) {
            // The automatic listing has shown the current line: stop it.
            chanOpen(0);
            setFlag(TV_FLAG, TV_AUTOLIST, false);
            throw AutoListStop{};
        }
    } else {
        // PO-SCR-2
        if (batch) mem[SCR_CT] = 2;
        if (--mem[SCR_CT] == 0) {
            mem[SCR_CT] = uint8_t(H - b);
            uint16_t attrMask = word(ATTR_T);
            uint8_t pflag = mem[P_FLAG];
            chanOpen(-3);
            poMsg(kScrollMessage);
            setFlag(TV_FLAG, TV_CLEAR_LOWER, true);
            setFlag(FLAGS, F_K_L_MODE, true);
            setFlag(FLAGS, F_NEW_KEY, false);
            uint8_t k = consIn();
            if (k == ' ' || k == tok::STOP || (k | 0x20) == 'n') error(0x0C);   // REPORT-D
            chanOpen(-2);
            mem[P_FLAG] = pflag;
            setWord(ATTR_T, attrMask);
        }
    }
    // PO-SCR-3: scroll the whole display.
    clScAll();
    b = dfsz + 1;
    int upperRow = H - b, lastRow = H - 1;
    uint8_t aLast = screen.at(lastRow, 0).attr;
    uint8_t cUpper = screen.at(upperRow, 0).attr;
    for (int col = 0; col < W; ++col) {                        // PO-SCR-3A
        screen.at(upperRow, col).attr = aLast;
        screen.at(lastRow, col).attr = cUpper;
    }
    clSet(b, W + 1);
}

// L0D02 PO-SCR-4: the lower screen grows upwards.
void Machine::poScr4(int b) {
    if (b < 2) error(0x04);
    int a = b + mem[DF_SZ] - (H + 1);
    if (a >= 0) return;
    int n = -a;
    uint16_t attrMask = word(ATTR_T);
    uint8_t pflag = mem[P_FLAG];
    temps();
    while (n--) {                                              // PO-SCR-4A
        int old = mem[DF_SZ];
        mem[DF_SZ] = uint8_t(old + 1);
        int count = old;
        if (old + 1 >= mem[S_POSN + 1]) {
            ++mem[S_POSN + 1];
            count = H - 1;                                     // GW: $17
        }
        clScroll(count);                                       // PO-SCR-4B
    }
    mem[P_FLAG] = pflag;
    setWord(ATTR_T, attrMask);
    setFlag(TV_FLAG, TV_LOWER, false);
    clSet(mem[S_POSN + 1], mem[S_POSN]);
    setFlag(TV_FLAG, TV_LOWER, true);
}

// L0D4D TEMPS: copy the permanent colours to the temporary ones.
void Machine::temps() {
    uint8_t pflag = mem[P_FLAG];
    uint8_t a = 0;
    if (flag(TV_FLAG, TV_LOWER)) {
        mem[ATTR_T] = mem[BORDCR];
        mem[MASK_T] = 0;
    } else {
        mem[ATTR_T] = mem[ATTR_P];
        mem[MASK_T] = mem[MASK_P];
        a = uint8_t((pflag >> 1) | (pflag << 7));              // RRCA
    }
    mem[P_FLAG] = uint8_t(((a ^ pflag) & 0x55) ^ pflag);       // TEMPS-2
}

// L0D6B CLS
void Machine::cls() {
    clAll();
    clsLower();
}

// L0D6E CLS-LOWER
void Machine::clsLower() {
    setFlag(TV_FLAG, TV_CLEAR_LOWER, false);
    setFlag(TV_FLAG, TV_LOWER, true);
    temps();
    int dfsz = mem[DF_SZ];
    clLine(dfsz);
    // the lines above the bottom two get the permanent colours (CLS-1)
    for (int row = H - dfsz; row < H - 2; ++row)
        for (int col = 0; col < W; ++col) screen.at(row, col).attr = mem[ATTR_P];
    mem[DF_SZ] = 2;
    clChan();
}

// L0D94 CL-CHAN: reset channel 'K' to its normal routines.
void Machine::clChan() {
    chanOpen(-3);
    uint16_t hl = word(CURCHL);
    setWord(hl, PRINT_OUT);
    setWord(uint16_t(hl + 2), KEY_INPUT);
    clSet(H - 1, W + 1);
}

// L0DAF CL-ALL: clear the whole display.
void Machine::clAll() {
    setWord(COORDS, 0);
    coordsX = coordsY = 0;
    setFlag(FLAGS2, F2_CLEAR_MAIN, false);
    clChan();
    chanOpen(-2);
    temps();
    clLine(H);
    setWord(word(CURCHL), PRINT_OUT);
    mem[SCR_CT] = 1;
    clSet(H, W + 1);
}

// L0DD9 CL-SET: the display address is implicit, just store the position.
void Machine::clSet(int b, int c) { poStore(b, c); }

// L0DFE CL-SC-ALL: scroll all but the top line... i.e. the whole display.
void Machine::clScAll() { clScroll(H - 1); }

// L0E00 CL-SCROLL: move the bottom b lines up by one line (the line above
// them is lost) and clear the bottom line.
void Machine::clScroll(int b) {
    int top = H - b - 1;
    if (top < 0) top = 0;
    for (int row = top; row < H - 1; ++row)
        for (int col = 0; col < W; ++col) screen.at(row, col) = screen.at(row + 1, col);
    clLine(1);
}

// L0E44 CL-LINE: clear the bottom b lines.
void Machine::clLine(int b) {
    uint8_t attr = flag(TV_FLAG, TV_LOWER) ? mem[BORDCR] : mem[ATTR_P];
    for (int row = H - b; row < H; ++row) {
        if (row < 0) continue;
        for (int col = 0; col < W; ++col) {
            Cell& cell = screen.at(row, col);
            cell = Cell();
            cell.attr = attr;
        }
    }
}

// ---------------------------------------------------------------------------
// The ZX Printer.  Lines go to the host as text.
// ---------------------------------------------------------------------------

// L0EAC COPY: the upper screen is sent to the printer.
void Machine::copyCmd() {
    for (int row = 0; row < H - 2; ++row) {
        std::string line;
        for (int col = 0; col < W; ++col) line += Screen::glyphUtf8(screen.at(row, col));
        while (!line.empty() && line.back() == ' ') line.pop_back();
        host.printerLine(line);
    }
    clearPrb();
}

// L0ECD COPY-BUFF: print the printer buffer.
void Machine::copyBuff() {
    std::string line;
    for (const Cell& c : printerLine) line += Screen::glyphUtf8(c);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    host.printerLine(line);
    clearPrb();
}

// L0EDF CLEAR-PRB
void Machine::clearPrb() {
    setFlag(FLAGS, F_PRINTER, true);                           // GW: CHAN-P
    printerLine.assign(size_t(opts.printerWidth), Cell());
    std::fill(mem.begin() + PRBUFF, mem.begin() + PRBUFF + 256, 0);
    setFlag(FLAGS2, F2_PRINTER_USED, false);
    clSet(0, opts.printerWidth + 1);
}

// ---------------------------------------------------------------------------
// Transcript of printed text for batch mode.
// ---------------------------------------------------------------------------
void Machine::transcriptChar(uint8_t code) {
    Output where = flag(TV_FLAG, TV_LOWER) ? Output::LowerScreen : Output::UpperScreen;
    if (code >= 0x90) host.transcript(where, Screen::utf8(0x2588));
    else host.transcript(where, Screen::utf8(Screen::codePoint(code)));
}

std::string spectrumCharToUtf8(uint8_t c) {
    if (c >= 0xA5) return tokenText(c);
    if (c >= 0x90) return std::string("[") + char('A' + c - 0x90) + "]";
    if (c < 0x20) return "?";
    return Screen::utf8(Screen::codePoint(c));
}

} // namespace zxgw
