// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 2. KEYBOARD ROUTINES
//
// The Spectrum scans a 40 key matrix and decodes keys depending on the
// current mode (K, L, C, E, G) and the shift keys.  A terminal delivers
// characters instead, so K-DECODE (L0333) is translated to work on them:
//
//   letters, digits, symbols   as typed (they are already "decoded")
//   Alt/Option + key           SYMBOL SHIFT + key (e.g. Alt+Y = AND)
//   Ctrl+E or F-key            EXTENDED mode, then the next key
//   Ctrl+G / F9                GRAPHICS mode (block graphics and UDGs)
//   arrows, Backspace, Tab     the cursor keys, DELETE and EDIT
//
// In 'K' mode (classic keyword entry, see the GW STOP toggle) a letter gives
// the keyword printed on the Spectrum key.

#include "machine.h"

#include <cstring>

#include "tables.h"

namespace zxgw {

using namespace sv;

namespace {

// The digit that a shifted digit key produces on a US keyboard.
int shiftedDigit(uint32_t cp) {
    static const char shifted[] = ")!@#$%^&*(";
    for (int i = 0; i < 10; ++i)
        if (cp == uint32_t(shifted[i])) return i;
    return -1;
}

// Unicode to the Spectrum character set (32..127).
int spectrumChar(uint32_t cp) {
    if (cp == 0x00A3) return 0x60;             // pound
    if (cp == 0x00A9) return 0x7F;             // copyright
    if (cp == 0x2191) return 0x5E;             // up arrow
    if (cp >= 0x20 && cp < 0x7F) return int(cp);
    return -1;
}

// The 8 half rows of the keyboard matrix (port bits A8..A15).
const char* const kMatrix[8] = {
    "\x01ZXCV", "ASDFG", "QWERT", "12345", "09876", "POIUY", "\rLKJH", " \x02MNB"
};

} // namespace

// L0333 K-DECODE for terminal keys.  Returns the key code or -1.
int Machine::decodeKey(const KeyEvent& ev, bool lMode, uint8_t mode, bool forInkey) {
    switch (ev.code) {
    case KeyCode::Enter: return 0x0D;
    case KeyCode::Backspace: return 0x0C;
    case KeyCode::Left: return 0x08;
    case KeyCode::Right: return 0x09;
    case KeyCode::Down: return 0x0A;
    case KeyCode::Up: return 0x0B;
    case KeyCode::Edit: return 0x07;
    case KeyCode::CapsLock: return 0x06;
    case KeyCode::TrueVideo: return 0x04;
    case KeyCode::InvVideo: return 0x05;
    case KeyCode::Graphics: return 0x0F;
    case KeyCode::Extend: return 0x0E;
    case KeyCode::Break: return forInkey ? ' ' : -1;
    case KeyCode::Home:
    case KeyCode::End:
    case KeyCode::Delete: {
        if (forInkey) return -1;
        // Terminal extras for the editor, made of cursor keys and DELETE.
        uint16_t start = setDe(true);
        uint16_t cur = word(K_CUR);
        uint16_t end = cur;
        while (mem[end] != 0x0D) ++end;
        if (ev.code == KeyCode::Home) {
            for (uint16_t i = start; i < cur; ++i) codeQueue.push_back(0x08);
        } else if (ev.code == KeyCode::End) {
            for (uint16_t i = cur; i < end; ++i) codeQueue.push_back(0x09);
        } else if (cur < end) {
            codeQueue.push_back(0x09);
            codeQueue.push_back(0x0C);
        }
        return -1;
    }
    case KeyCode::Char:
        break;
    default:
        return -1;
    }

    uint32_t cp = ev.ch;
    bool upper = cp >= 'A' && cp <= 'Z';
    bool lower = cp >= 'a' && cp <= 'z';
    bool letter = upper || lower;
    bool digit = cp >= '0' && cp <= '9';
    int sdigit = shiftedDigit(cp);
    int letterIndex = upper ? int(cp - 'A') : lower ? int(cp - 'a') : -1;

    if (mode == 2 && !forInkey) {                              // G mode
        if (letter) {
            if (letterIndex <= 'U' - 'A') return 0x90 + letterIndex;   // GW K_GR_FIX
            return -1;
        }
        int dgt = digit ? int(cp - '0') : sdigit;
        if (dgt >= 0) {                                        // K-GRA-DGT
            if (dgt == 9) return 0x0F;                         // GRAPHICS
            if (dgt == 0) return 0x0C;                         // DELETE
            int code = 0x80 + (dgt & 7);
            if (!digit || ev.symbolShift) code ^= 0x0F;        // shifted: inverted
            return code;
        }
        return spectrumChar(cp);
    }

    if (mode == 1 && !forInkey) {                              // E mode
        if (letter) {                                          // K-E-LET
            if (lower && !ev.symbolShift) return kEUnshift[letterIndex];
            return kExtShift[letterIndex];
        }
        if (digit && ev.symbolShift) return kEDigits[cp - '0'];
        int dgt = digit ? int(cp - '0') : sdigit;
        if (dgt >= 0) {
            bool shifted = !digit;
            if (dgt >= 8) return (dgt - 6) - (shifted ? 2 : 0);   // K-8-&-9: BRIGHT / FLASH
            return 0x10 + dgt + (shifted ? 8 : 0);             // PAPER / INK
        }
        return spectrumChar(cp);
    }

    // KLC mode (and INKEY$)
    if (ev.symbolShift) {
        if (letter) return kSymCodes[letterIndex];             // SYM-CODES
        if (digit) {                                           // K-KLC-DGT
            int a = int(cp) - 0x10;
            if (a == 0x22) return '@';
            if (a == 0x20) return '_';
            return a;
        }
    }
    if (letter) {
        if (!lMode && !forInkey) return int(upper ? cp : cp - 32) + 0xA5;   // K-TOKENS
        if (flag(FLAGS2, F2_CAPS_LOCK) && lower) return int(cp - 32);
        return int(cp);
    }
    return spectrumChar(cp);
}

// INKEY$: the key held down at the moment (a terminal reports key presses,
// so a key counts as held for a short time after it arrived).
int Machine::inkey() {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastPoll).count() >= 2)
        pollKeys(0);
    if (std::chrono::steady_clock::now() >= heldUntil) return -1;
    return decodeKey(heldKey, true, 0, true);
}

// IN: port $xxFE reads the keyboard half rows selected by the high byte.
uint8_t Machine::inPort(uint16_t port) {
    if (port & 1) return 0xFF;                                 // nothing attached
    pollKeys(0);
    uint8_t result = 0xFF;
    if (std::chrono::steady_clock::now() >= heldUntil) return result;
    // Find the matrix keys for the held key.
    std::vector<std::pair<int, int>> keys;                     // (half row, bit)
    auto addChar = [&](char c) {
        for (int r = 0; r < 8; ++r) {
            const char* p = std::strchr(kMatrix[r], c);
            if (p && *p) keys.push_back({r, int(p - kMatrix[r])});
        }
    };
    const KeyEvent& ev = heldKey;
    switch (ev.code) {
    case KeyCode::Enter: addChar('\r'); break;
    case KeyCode::Left: addChar('\x01'); addChar('5'); break;
    case KeyCode::Down: addChar('\x01'); addChar('6'); break;
    case KeyCode::Up: addChar('\x01'); addChar('7'); break;
    case KeyCode::Right: addChar('\x01'); addChar('8'); break;
    case KeyCode::Backspace: addChar('\x01'); addChar('0'); break;
    case KeyCode::Edit: addChar('\x01'); addChar('1'); break;
    case KeyCode::Graphics: addChar('\x01'); addChar('9'); break;
    case KeyCode::Break: addChar('\x01'); addChar(' '); break;
    case KeyCode::Char: {
        uint32_t cp = ev.ch;
        if (ev.symbolShift) addChar('\x02');
        if (cp >= 'a' && cp <= 'z') {
            addChar(char(cp - 32));
        } else if ((cp >= 'A' && cp <= 'Z')) {
            addChar(char(cp));
            if (!ev.symbolShift) addChar('\x01');
        } else if ((cp >= '0' && cp <= '9') || cp == ' ') {
            addChar(char(cp));
        } else {
            // A symbol: SYMBOL SHIFT plus the key that carries it.
            int sc = spectrumChar(cp);
            for (int i = 0; i < 26 && sc >= 0; ++i)
                if (kSymCodes[i] == sc) { addChar('\x02'); addChar(char('A' + i)); sc = -1; }
            static const char digitSym[] = "_!@#$%&'()";
            for (int i = 0; i < 10 && sc >= 0; ++i)
                if (digitSym[i] == char(sc)) { addChar('\x02'); addChar(char('0' + i)); sc = -1; }
        }
        break;
    }
    default:
        break;
    }
    uint8_t high = uint8_t(port >> 8);
    for (auto& k : keys)
        if (!(high & (1 << k.first))) result &= uint8_t(~(1 << k.second));
    return result;
}

} // namespace zxgw
