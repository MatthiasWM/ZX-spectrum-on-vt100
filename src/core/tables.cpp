// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Constant tables from the ROM listing gw_rom.s.

#include "tables.h"

namespace zxgw {

// L0095 TKN-TABLE.
// "The previous 32 function-type words are printed without a leading space.
//  The following have a leading space if they begin with a letter."
const char* const kTokens[91] = {
    "RND", "INKEY$", "PI", "FN", "POINT", "SCREEN$", "ATTR", "AT", "TAB",
    "VAL$", "CODE", "VAL", "LEN", "SIN", "COS", "TAN", "ASN", "ACS", "ATN",
    "LN", "EXP", "INT", "SQR", "SGN", "ABS", "PEEK", "IN", "USR", "STR$",
    "CHR$", "NOT", "BIN",
    "OR", "AND", "<=", ">=", "<>", "LINE", "THEN", "TO", "STEP", "DEF FN",
    "CAT", "FORMAT", "MOVE", "ERASE", "OPEN #", "CLOSE #", "MERGE", "VERIFY",
    "BEEP", "CIRCLE", "INK", "PAPER", "FLASH", "BRIGHT", "INVERSE", "OVER",
    "OUT", "LPRINT", "LLIST", "STOP", "READ", "DATA", "RESTORE", "NEW",
    "BORDER", "CONTINUE", "DIM", "REM", "FOR", "GO TO", "GO SUB", "INPUT",
    "LOAD", "LIST", "LET", "PAUSE", "NEXT", "POKE", "PRINT", "PLOT", "RUN",
    "SAVE", "RANDOMIZE", "IF", "CLS", "DRAW", "CLEAR", "RETURN", "COPY"
};

// L022C E-UNSHIFT - the green keywords on the original keyboard.
const uint8_t kEUnshift[26] = {
    0xE3, 0xC4, 0xE0, 0xE4, 0xB4, 0xBC, 0xBD, 0xBB, 0xAF, 0xB0, 0xB1, 0xC0, 0xA7,
    0xA6, 0xBE, 0xAD, 0xB2, 0xBA, 0xE5, 0xA5, 0xC2, 0xE1, 0xB3, 0xB9, 0xC1, 0xB8
};

// L0246 EXT-SHIFT - the red keywords below the keys.
const uint8_t kExtShift[26] = {
    0x7E, 0xDC, 0xDA, 0x5C, 0xB7, 0x7B, 0x7D, 0xD8, 0xBF, 0xAE, 0xAA, 0xAB, 0xDD,
    0xDE, 0xDF, 0x7F, 0xB5, 0xD6, 0x7C, 0xD5, 0x5D, 0xDB, 0xB6, 0xD9, 0x5B, 0xD7
};

// L026A SYM-CODES - the red symbols on the letter keys.
const uint8_t kSymCodes[26] = {
    0xE2, 0x2A, 0x3F, 0xCD, 0xC8, 0xCC, 0xCB, 0x5E, 0xAC, 0x2D, 0x2B, 0x3D, 0x2E,
    0x2C, 0x3B, 0x22, 0xC7, 0x3C, 0xC3, 0x3E, 0xC5, 0x2F, 0xC9, 0x60, 0xC6, 0x3A
};

// L0284 E-DIGITS.
const uint8_t kEDigits[10] = {
    0xD0, 0xCE, 0xA8, 0xCA, 0xD3, 0xD4, 0xD1, 0xD2, 0xA9, 0xCF
};

// L0260 CTL-CODES.
const uint8_t kCtlCodes[10] = {
    0x0C, 0x07, 0x06, 0x04, 0x05, 0x08, 0x0A, 0x0B, 0x09, 0x0F
};

// L1391 rpt-mesgs.
const char* const kReports[28] = {
    "OK", "NEXT without FOR", "Variable not found", "Subscript wrong",
    "Out of memory", "Out of screen", "Number too big", "RETURN without GOSUB",
    "End of file", "STOP statement", "Invalid argument",
    "Integer out of range", "Nonsense in BASIC", "BREAK - CONT repeats",
    "Out of DATA", "Invalid file name", "No room for line", "STOP in INPUT",
    "FOR without NEXT", "Invalid I/O device", "Invalid colour",
    "BREAK into program", "RAMTOP no good", "Statement lost",
    "Invalid stream", "FN without DEF", "Parameter error", "Tape loading error"
};

// L1539 copyright.
const char* const kCopyright = "\x7F 1982 Sinclair Research Ltd";

// L09A1 tape-msgs.
const char* const kTapeMessages[5] = {
    "Start tape, then press any key.",
    "\rProgram: ", "\rNumber array: ", "\rCharacter array: ", "\rBytes: "
};

// L0CF8 scrl-mssg.
const char* const kScrollMessage = "scroll?";

} // namespace zxgw
