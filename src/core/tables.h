// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Constant tables from the ROM: keywords, keyboard maps and messages.

#ifndef ZXGW_TABLES_H
#define ZXGW_TABLES_H

#include <cstdint>

namespace zxgw {

// L0095 TKN-TABLE: keyword text for token codes $A5 (RND) .. $FF (COPY).
// Index 0 is RND.  The original table marks the last character by setting
// bit 7; here the strings are plain C strings.
extern const char* const kTokens[91];
inline const char* tokenText(uint8_t code) { return code >= 0xA5 ? kTokens[code - 0xA5] : ""; }

// L022C E-UNSHIFT: extended mode letters A..Z.
extern const uint8_t kEUnshift[26];
// L0246 EXT-SHIFT: extended mode with shift, letters A..Z.
extern const uint8_t kExtShift[26];
// L026A SYM-CODES: symbol shift letters A..Z.
extern const uint8_t kSymCodes[26];
// L0284 E-DIGITS: extended mode with symbol shift, digits 0..9.
extern const uint8_t kEDigits[10];
// L0260 CTL-CODES: caps shift digits 0..9.
extern const uint8_t kCtlCodes[10];

// L1391 rpt-mesgs: the report messages, index = report code 0..27.
extern const char* const kReports[28];
// L1539 copyright (character $7F is the copyright sign).
extern const char* const kCopyright;
// L09A1 tape-msgs (index 1..4 are "Program: " .. "Bytes: ").
extern const char* const kTapeMessages[5];
// L0CF8 scrl-mssg.
extern const char* const kScrollMessage;

// ROM character set, codes 32..127, 8 bytes each (charset.cpp).
extern const uint8_t kRomCharset[96 * 8];

} // namespace zxgw

#endif
