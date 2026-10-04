// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 4. CASSETTE HANDLING ROUTINES - with files instead of a cassette.
//
// SAVE, LOAD, VERIFY and MERGE keep the syntax and the logic of SAVE-ETC
// (L0605), LD-LOOK-H, LD-CONTRL, VR-CONTROL and ME-CONTRL.  The "tape" is a
// .tap file: a sequence of blocks, each a 17 byte header followed by the
// data, exactly as the ROM writes them, so files can be exchanged with
// emulators.
//
//   SAVE "name"            writes name.tap (".tap" is added if the name has
//                          no extension); any path is allowed
//   SAVE "name.bas"        writes the program as a text listing
//   LOAD "name"            reads name or name.tap and keeps it as the
//                          current tape: LOAD "" continues with the next
//                          block, like a real tape (e.g. LOAD "" CODE)
//   LOAD "x"               if no file "x" exists, the current tape is
//                          searched for a header named "x"
//   LOAD "name.bas"        reads a text listing; its lines are entered as
//                          if typed

#include "machine.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool isTextFile(const std::string& path) {
    std::string ext = lower(fs::path(path).extension().string());
    return ext == ".bas" || ext == ".txt";
}

// Display file address of pixel line y (0..191), byte column x.
uint16_t displayAddr(int y, int x) {
    return uint16_t(0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | x);
}

} // namespace

// ---------------------------------------------------------------------------
// The 8x8 bitmap of a cell and back.
// ---------------------------------------------------------------------------
void Machine::cellBitmap(int row, int col, uint8_t out[8]) const {
    const Cell& cell = screen.at(row, col);
    if (cell.graphic()) {
        for (int i = 0; i < 8; ++i) {
            uint8_t v = 0;
            int dy = i / 2;
            if (cell.dots & Screen::dotBit(0, dy)) v |= 0xF0;
            if (cell.dots & Screen::dotBit(1, dy)) v |= 0x0F;
            out[i] = v;
        }
    } else if (cell.ch >= 0x80 && cell.ch < 0x90) {
        uint8_t top = uint8_t(((cell.ch & 1) ? 0x0F : 0) | ((cell.ch & 2) ? 0xF0 : 0));
        uint8_t bottom = uint8_t(((cell.ch & 4) ? 0x0F : 0) | ((cell.ch & 8) ? 0xF0 : 0));
        for (int i = 0; i < 4; ++i) { out[i] = top; out[i + 4] = bottom; }
    } else {
        uint16_t a = uint16_t(word(CHARS) + cell.ch * 8);
        for (int i = 0; i < 8; ++i) out[i] = mem[uint16_t(a + i)];
    }
    if (cell.inverse())
        for (int i = 0; i < 8; ++i) out[i] = uint8_t(~out[i]);
}

// Like S-SCRN$-S, a bitmap that matches a character becomes that
// character; everything else becomes braille dots.
void Machine::setCellFromBitmap(int row, int col, const uint8_t in[8]) {
    Cell& cell = screen.at(row, col);
    uint16_t base = word(CHARS);
    for (int ch = 0x20; ch < 0x80; ++ch) {
        bool same = true, inverse = true;
        for (int i = 0; i < 8; ++i) {
            uint8_t g = mem[uint16_t(base + ch * 8 + i)];
            if (in[i] != g) same = false;
            if (in[i] != uint8_t(~g)) inverse = false;
        }
        if (same || inverse) {
            cell.ch = uint8_t(ch);
            cell.dots = 0;
            cell.flags = same ? 0 : Cell::kInverse;
            return;
        }
    }
    for (int n = 1; n < 16; ++n) {
        uint8_t top = uint8_t(((n & 1) ? 0x0F : 0) | ((n & 2) ? 0xF0 : 0));
        uint8_t bottom = uint8_t(((n & 4) ? 0x0F : 0) | ((n & 8) ? 0xF0 : 0));
        bool same = true;
        for (int i = 0; i < 8; ++i)
            if (in[i] != (i < 4 ? top : bottom)) same = false;
        if (same) {
            cell.ch = uint8_t(0x80 + n);
            cell.dots = 0;
            cell.flags = 0;
            return;
        }
    }
    cell.ch = ' ';
    cell.flags = Cell::kGraphic;
    cell.dots = downsampleGlyph(in);
}

// Render the top left 32x24 cells into the display file and attributes.
void Machine::screenToMemory() {
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 32; ++col) {
            if (row >= H || col >= W) continue;
            uint8_t g[8];
            cellBitmap(row, col, g);
            for (int i = 0; i < 8; ++i) mem[displayAddr(row * 8 + i, col)] = g[i];
            mem[uint16_t(ATTRS + row * 32 + col)] = screen.at(row, col).attr;
        }
}

// The display file was loaded: show it.
void Machine::memoryToScreen() {
    for (int row = 0; row < 24 && row < H; ++row)
        for (int col = 0; col < 32 && col < W; ++col) {
            uint8_t g[8];
            for (int i = 0; i < 8; ++i) g[i] = mem[displayAddr(row * 8 + i, col)];
            setCellFromBitmap(row, col, g);
            screen.at(row, col).attr = mem[uint16_t(ATTRS + row * 32 + col)];
        }
}

uint8_t Machine::peekDisplay(uint16_t addr) {
    int col = addr & 31;
    int y = ((addr >> 5) & 0xC0) | ((addr >> 8) & 0x07) | ((addr >> 2) & 0x38);
    int row = y / 8;
    if (row >= H || col >= W) return mem[addr];
    uint8_t g[8];
    cellBitmap(row, col, g);
    return g[y % 8];
}

void Machine::pokeDisplay(uint16_t addr, uint8_t v) {
    int col = addr & 31;
    int y = ((addr >> 5) & 0xC0) | ((addr >> 8) & 0x07) | ((addr >> 2) & 0x38);
    int row = y / 8;
    if (row >= H || col >= W) return;
    uint8_t g[8];
    cellBitmap(row, col, g);
    g[y % 8] = v;
    setCellFromBitmap(row, col, g);
}

// ---------------------------------------------------------------------------
// Files.
// ---------------------------------------------------------------------------

// The file for a name: as given, or with .tap added.  Empty if none exists.
std::string Machine::resolveFile(const std::string& name, bool forSave) {
    if (forSave) {
        if (fs::path(name).has_extension()) return name;
        return name + ".tap";
    }
    std::error_code ec;
    for (const std::string& candidate : {name, name + ".tap", name + ".TAP", name + ".bas"}) {
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return std::string();
}

bool Machine::openTape(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<TapeBlock> blocks;
    size_t p = 0;
    while (p + 2 <= data.size()) {
        size_t len = data[p] | (data[p + 1] << 8);
        p += 2;
        if (len < 2 || p + len > data.size()) break;
        TapeBlock b;
        b.data.assign(data.begin() + long(p), data.begin() + long(p + len - 1));   // without checksum
        blocks.push_back(std::move(b));
        p += len;
    }
    tapePath = path;
    tapeBlocks = std::move(blocks);
    tapePos = 0;
    return true;
}

void Machine::writeTap(const std::string& path, const std::vector<TapeBlock>& blocks) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) error(0x0E);                                     // Invalid file name
    for (const TapeBlock& b : blocks) {
        size_t len = b.data.size() + 1;
        uint8_t checksum = 0;
        for (uint8_t v : b.data) checksum ^= v;
        out.put(char(len & 0xFF));
        out.put(char(len >> 8));
        out.write(reinterpret_cast<const char*>(b.data.data()), long(b.data.size()));
        out.put(char(checksum));
    }
    if (!out) error(0x1A);                                     // Tape loading error
}

// ---------------------------------------------------------------------------
// L0605 SAVE-ETC (CLASS-0B)
// ---------------------------------------------------------------------------
void Machine::saveEtc() {
    int command = curToken == tok::SAVE ? 0 : curToken == tok::LOAD ? 1
                : curToken == tok::VERIFY ? 2 : 3;
    exptExp();
    std::string name;
    if (!syntaxZ()) {
        name = popStr();
        if (command == 0 && name.empty()) error(0x0E);         // REPORT-Fa
    }
    uint8_t hdr[17];                                           // the first descriptor
    std::memset(hdr, ' ', 11);
    std::memset(hdr + 11, 0, 6);
    if (name.empty()) {
        hdr[1] = 0xFF;                                         // any name
    } else {
        std::string base = name;
        if (command == 0) base = fs::path(name).stem().string();
        for (size_t i = 0; i < base.size() && i < 10; ++i) hdr[1 + i] = uint8_t(base[i]);
    }
    uint16_t hl = 0;                                           // start of the data
    uint8_t a = getChar();
    if (a == tok::DATA) {                                      // SA-DATA
        if (command == 3) error(0x0B);                         // no MERGE of DATA
        nextChar();
        uint16_t vhl;
        bool notFound, arrayZ;
        uint8_t c = uint8_t(lookVars(vhl, notFound, arrayZ) | 0x80);
        if (notFound) {
            if (command != 1) error(0x01);                     // REPORT-2a
            vhl = 0;
        } else {
            if (!arrayZ) error(0x0B);                          // SA-V-OLD
            if (!syntaxZ()) {
                if (!(mem[vhl] & 0x80)) error(0x0B);           // GW CHK_VAR: no simple strings
                hdr[11] = mem[uint16_t(vhl + 1)];
                hdr[12] = mem[uint16_t(vhl + 2)];
                vhl = uint16_t(vhl + 3);
            }
        }
        hl = vhl;
        hdr[14] = c;                                           // SA-V-NEW
        hdr[0] = (c & 0x40) ? 2 : 1;
        if (nextChar() != ')') error(0x0B);                    // SA-DATA-1
        nextChar();
        checkEnd();
    } else if (a == tok::SCREEN) {                             // SA-SCR$
        if (command == 3) error(0x0B);
        nextChar();
        checkEnd();
        hdr[11] = 0x00;
        hdr[12] = 0x1B;
        hdr[13] = 0x00;
        hdr[14] = 0x40;
        hl = 0x4000;
        hdr[0] = 3;
    } else if (a == tok::CODE) {                               // SA-CODE
        if (command == 3) error(0x0B);
        a = nextChar();
        if (prStEnd(a)) {
            if (command == 0) error(0x0B);
            useZero();
            useZero();
        } else {
            expt1Num();                                        // SA-CODE-1
            if (getChar() == ',') {
                nextChar();                                    // SA-CODE-3
                expt1Num();
            } else {
                if (command == 0) error(0x0B);
                useZero();                                     // SA-CODE-2
            }
        }
        checkEnd();                                            // SA-CODE-4
        uint16_t len = findInt2();
        uint16_t start = findInt2();
        hdr[11] = uint8_t(len);
        hdr[12] = uint8_t(len >> 8);
        hdr[13] = uint8_t(start);
        hdr[14] = uint8_t(start >> 8);
        hdr[15] = 0x00;
        hdr[16] = 0x80;
        hl = start;
        hdr[0] = 3;                                            // SA-TYPE-3
    } else {
        if (a == tok::LINE) {                                  // SA-LINE
            if (command != 0) error(0x0B);
            nextChar();
            expt1Num();
            checkEnd();
            uint16_t line = findLine();
            hdr[13] = uint8_t(line);
            hdr[14] = uint8_t(line >> 8);
        } else {
            checkEnd();
            hdr[14] = 0x80;                                    // no auto-start
        }
        uint16_t total = uint16_t(word(E_LINE) - word(PROG) - 1);   // SA-TYPE-0
        uint16_t prog = uint16_t(word(VARS) - word(PROG));
        hdr[0] = 0;
        hdr[11] = uint8_t(total);
        hdr[12] = uint8_t(total >> 8);
        hdr[15] = uint8_t(prog);
        hdr[16] = uint8_t(prog >> 8);
        hl = word(PROG);
    }

    // SA-ALL
    if (command == 0) {
        saveControl(name, hdr, hl);
        return;
    }
    loadControl(command, name, hdr, hl);
}

// L0970 SA-CONTRL: write the header and the data block to a file.
void Machine::saveControl(const std::string& name, const uint8_t* hdr, uint16_t hl) {
    std::string path = resolveFile(toUtf8(name), true);
    if (isTextFile(path)) {
        if (hdr[0] != 0) error(0x0E);                          // text is for programs only
        saveText(path);
        return;
    }
    uint16_t len = uint16_t(hdr[11] | (hdr[12] << 8));
    if (hdr[0] == 3) screenToMemory();                         // the display file may be saved
    std::vector<TapeBlock> blocks(2);
    blocks[0].data.push_back(0x00);
    blocks[0].data.insert(blocks[0].data.end(), hdr, hdr + 17);
    blocks[1].data.push_back(0xFF);
    for (uint32_t i = 0; i < len; ++i) blocks[1].data.push_back(mem[uint16_t(hl + i)]);
    writeTap(path, blocks);
}

// LD-LOOK-H and the LOAD, VERIFY and MERGE control routines.
void Machine::loadControl(int command, const std::string& name, const uint8_t* expected, uint16_t hl) {
    bool anyName = name.empty();
    std::string path = name.empty() ? std::string() : resolveFile(toUtf8(name), false);
    if (!path.empty()) {
        if (isTextFile(path)) {
            if (expected[0] != 0) error(0x0E);
            loadText(path, command);
            return;
        }
        if (!openTape(path)) error(0x0E);
        anyName = true;                                        // the file was named
    } else if (tapeBlocks.empty() || name.size() > 10) {
        // No such file, and no tape that could hold a header with this
        // name (tape names have at most ten characters).
        error(0x0E);
    }
    // Look for the header, starting at the current tape position.
    chanOpen(-2);
    mem[SCR_CT] = 3;
    size_t n = tapeBlocks.size();
    const TapeBlock* header = nullptr;
    const TapeBlock* data = nullptr;
    for (size_t tries = 0; tries < n; ++tries) {
        size_t i = tapePos % n;
        tapePos = i + 1;
        const TapeBlock& b = tapeBlocks[i];
        if (b.data.size() != 18 || b.data[0] != 0x00) continue;
        const uint8_t* h = &b.data[1];
        if (h[0] >= 4) continue;                               // LD-TYPE
        poMsg(kTapeMessages[1 + h[0]]);
        bool match = h[0] == expected[0];
        for (int c = 0; c < 10; ++c) {                         // LD-NAME
            if (!anyName && h[1 + c] != expected[1 + c]) match = false;
            printA(h[1 + c]);
        }
        if (!match) continue;
        printA(0x0D);
        if (tapePos < n && !tapeBlocks[tapePos].data.empty() && tapeBlocks[tapePos].data[0] == 0xFF) {
            header = &b;
            data = &tapeBlocks[tapePos];
            ++tapePos;
        }
        break;
    }
    if (!header || !data) error(0x1A);                         // REPORT-R
    const uint8_t* h = &header->data[1];
    std::vector<uint8_t> bytes(data->data.begin() + 1, data->data.end());
    uint16_t tapeLen = uint16_t(h[11] | (h[12] << 8));
    if (bytes.size() < tapeLen) error(0x1A);
    bytes.resize(tapeLen);

    if (h[0] == 3 || command == 2) {                           // VR-CONTROL
        uint16_t oldLen = uint16_t(expected[11] | (expected[12] << 8));
        if (oldLen) {
            if (tapeLen > oldLen) error(0x1A);
            if (tapeLen < oldLen && h[0] != 3) error(0x1A);
        }
        uint16_t dest = hl;
        if (dest == 0) dest = uint16_t(h[13] | (h[14] << 8));
        if (command == 2) {
            bool screenArea = dest < PRBUFF && dest + tapeLen > DISPLAY;
            if (screenArea) screenToMemory();
            for (uint32_t i = 0; i < tapeLen; ++i)
                if (mem[uint16_t(dest + i)] != bytes[i]) error(0x1A);
            return;
        }
        for (uint32_t i = 0; i < tapeLen; ++i) {
            uint16_t a = uint16_t(dest + i);
            if (a >= ROM_END) mem[a] = bytes[i];
        }
        if (dest < PRBUFF && uint32_t(dest) + tapeLen > DISPLAY) memoryToScreen();
        return;
    }

    if (command == 3) {                                        // ME-CONTRL
        mergeBlock(bytes);
        return;
    }

    // L0808 LD-CONTRL
    testRoom(uint32_t(tapeLen) + 5);
    if (h[0] != 0) {
        // an array: replace the variable
        if (hl) {
            uint16_t start = uint16_t(hl - 3);
            reclaim2(start, uint16_t(word(uint16_t(hl - 2)) + 3));
        }
        uint16_t pos = uint16_t(word(E_LINE) - 1);             // LD-DATA-1
        makeRoom(pos, uint16_t(tapeLen + 3));
        mem[pos] = expected[14];
        setWord(uint16_t(pos + 1), tapeLen);
        std::memcpy(&mem[uint16_t(pos + 3)], bytes.data(), tapeLen);
        return;
    }
    // L0873 LD-PROG
    uint16_t prog = word(PROG);
    reclaim1(prog, uint16_t(word(E_LINE) - 1));
    makeRoom(prog, tapeLen);
    setWord(VARS, uint16_t(prog + (h[15] | (h[16] << 8))));
    std::memcpy(&mem[prog], bytes.data(), tapeLen);
    fnStrings.clear();
    if (!(h[14] & 0xC0)) {                                     // auto-start
        setWord(NEWPPC, uint16_t(h[13] | (h[14] << 8)));
        mem[NSPPC] = 0;
    }
}

// L08B6 ME-CONTRL: merge a program block into the program and variables.
void Machine::mergeBlock(const std::vector<uint8_t>& bytes) {
    // Copy into the workspace, followed by an end marker, as the ROM does.
    uint16_t len = uint16_t(bytes.size());
    uint16_t ws = bcSpaces(uint16_t(len + 1));
    std::memcpy(&mem[ws], bytes.data(), len);
    mem[uint16_t(ws + len)] = 0x80;
    uint16_t hl = ws;
    uint16_t de = word(PROG);
    // ME-ENTER: insert the item at hl (workspace) at de, replacing if asked.
    auto meEnter = [&](bool replace, bool variable) {
        if (replace) {
            setWord(X_PTR, hl);
            uint16_t next = nextOne(de);
            reclaim2(de, uint16_t(next - de));
            hl = word(X_PTR);
        }
        uint16_t next = nextOne(hl);                           // ME-ENT-1
        uint16_t bc = uint16_t(next - hl);
        setWord(X_PTR, hl);
        uint16_t prog = word(PROG);
        uint16_t dest;
        if (variable) {
            makeRoom(de, bc);                                  // ME-ENT-2
            dest = de;
        } else {
            makeRoom(uint16_t(de - 1), bc);
            dest = de;
        }
        setWord(PROG, prog);
        uint16_t src = word(X_PTR);
        std::memmove(&mem[dest], &mem[src], bc);
        reclaim2(src, bc);
        de = uint16_t(dest + bc);
        hl = src;
    };
    for (;;) {                                                 // ME-NEW-LP
        if (mem[hl] & 0xC0) break;
        for (;;) {                                             // ME-OLD-LP
            int r = cpLines(de, uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)]));
            if (r >= 0) break;
            de = nextOne(de);
        }
        bool same = cpLines(de, uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)])) == 0;
        meEnter(same, false);
    }
    for (;;) {                                                 // ME-VAR-LP
        uint8_t c = mem[hl];
        if (c == 0x80) break;
        uint16_t v = word(VARS);
        bool replace = false;
        for (;;) {                                             // ME-OLD-VP
            if (mem[v] == 0x80) break;
            if (mem[v] == c) {
                if ((c & 0xE0) != 0xA0) { replace = true; break; }
                uint16_t p = v, q = hl;
                bool same = true;
                for (;;) {
                    ++p;
                    ++q;
                    if (mem[p] != mem[q]) { same = false; break; }
                    if (mem[p] & 0x80) break;
                }
                if (same) { replace = true; break; }
            }
            v = nextOne(v);
        }
        de = v;
        meEnter(replace, true);
    }
}

// ---------------------------------------------------------------------------
// Text listings.
// ---------------------------------------------------------------------------

// The program as text: what LIST shows, one line per program line.
// A BASIC line (from p up to the ENTER) as UTF-8 text with the spacing of
// LIST.  A '?' is inserted at the marker address (the error position).
std::string Machine::lineText(uint16_t p, uint16_t marker) {
    std::string text;
    for (;; ++p) {
        if (p == marker) text += '?';
        uint8_t c = mem[p];
        if (c == 0x0D) break;
        if (c == 0x0E) { p = uint16_t(p + 5); continue; }
        if (c >= 0xA5) {
            // keywords: the same spacing as LIST
            const char* t = tokenText(c);
            bool leading = (c - 0xA5) >= 32 && t[0] >= 'A';
            if (leading && !text.empty() && text.back() != ' ') text += ' ';
            text += t;
            char last = t[std::strlen(t) - 1];
            if ((last == '$' || last >= 'A') && (c - 0xA5) >= 3 && mem[uint16_t(p + 1)] != ' ') text += ' ';
        } else if (c >= 0x90) {
            text += "\\";                                      // UDG as \a .. \u
            text += char('a' + c - 0x90);
        } else if (c >= 0x80) {
            text += Screen::utf8(Screen::codePoint(c));
        } else if (c == 0x5E) {
            text += '^';
        } else if (c == 0x60) {
            text += "\xC2\xA3";                                // pound
        } else if (c == 0x7F) {
            text += "\xC2\xA9";                                // copyright
        } else if (c < 0x20 || c == '\\') {
            text += "\\{" + std::to_string(c) + "}";
        } else {
            text += char(c);
        }
    }
    return text;
}

void Machine::saveText(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) error(0x0E);
    for (const std::string& line : listingLines()) out << line << '\n';
}

// The program as the lines of a text listing.
std::vector<std::string> Machine::listingLines() {
    std::vector<std::string> lines;
    uint16_t hl = word(PROG);
    while (mem[hl] < 0x40) {
        unsigned number = unsigned((mem[hl] << 8) | mem[uint16_t(hl + 1)]);
        uint16_t len = word(uint16_t(hl + 2));
        std::string text = lineText(uint16_t(hl + 4), 0);
        // drop the space after a keyword at the end of the line
        if (!text.empty() && text.back() == ' ' && mem[uint16_t(hl + 4 + len - 2)] >= 0xA5) text.pop_back();
        lines.push_back(std::to_string(number) + ' ' + text);
        hl = uint16_t(hl + 4 + len);
    }
    return lines;
}

// Read a listing.  The lines are entered as if they were typed, so they are
// tokenized and checked like any other line; a line with an error stays in
// the editor.  LOAD replaces the program, MERGE adds to it.
void Machine::loadText(const std::string& path, int command) {
    std::ifstream in(path);
    if (!in) error(0x0E);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i == line.size()) continue;
        if (line[i] < '0' || line[i] > '9') continue;          // only numbered lines
        lines.push_back(line.substr(i));
    }
    if (command == 2) {                                        // VERIFY: compare listings
        if (listingLines() != lines) error(0x1A);
        return;
    }
    if (command == 1) {
        reclaim1(word(PROG), uint16_t(word(E_LINE) - 1));      // like LD-PROG
        fnStrings.clear();
    }
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) pendingCommands.push_front(fromListing(*it));
}

} // namespace zxgw
