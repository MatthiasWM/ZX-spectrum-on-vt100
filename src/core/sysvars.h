// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Memory map, system variables and ROM entry points.
//
// The translation keeps the Spectrum memory layout so that programs which
// PEEK and POKE system variables keep working, and so that the program and
// variables areas have exactly the original format (which is also what is
// written to .tap files).  The names are those used in the ROM listing.

#ifndef ZXGW_SYSVARS_H
#define ZXGW_SYSVARS_H

#include <cstdint>

namespace zxgw {
namespace sv {

// ---- memory map ------------------------------------------------------------
constexpr uint16_t ROM_END     = 0x4000;  // first RAM address
constexpr uint16_t DISPLAY     = 0x4000;  // display file (6144 bytes)
constexpr uint16_t ATTRS       = 0x5800;  // attribute file (768 bytes)
constexpr uint16_t PRBUFF      = 0x5B00;  // printer buffer (256 bytes)
constexpr uint16_t SYSVARS     = 0x5C00;
constexpr uint16_t CHANS_INIT  = 0x5CB6;  // channel information after START
constexpr uint16_t CHARSET     = 0x3D00;  // ROM character set (chr$ 32)

// ---- system variables (address = ROM label) --------------------------------
constexpr uint16_t KSTATE  = 0x5C00;
constexpr uint16_t LAST_K  = 0x5C08;
constexpr uint16_t REPDEL  = 0x5C09;
constexpr uint16_t REPPER  = 0x5C0A;
constexpr uint16_t DEFADD  = 0x5C0B;
constexpr uint16_t K_DATA  = 0x5C0D;
constexpr uint16_t TVDATA  = 0x5C0E;
constexpr uint16_t STRMS   = 0x5C10;  // 19 streams * 2 bytes ($FD..$0F)
constexpr uint16_t CHARS   = 0x5C36;
constexpr uint16_t RASP    = 0x5C38;
constexpr uint16_t PIP     = 0x5C39;
constexpr uint16_t ERR_NR  = 0x5C3A;
constexpr uint16_t FLAGS   = 0x5C3B;
constexpr uint16_t TV_FLAG = 0x5C3C;
constexpr uint16_t ERR_SP  = 0x5C3D;
constexpr uint16_t LIST_SP = 0x5C3F;
constexpr uint16_t MODE    = 0x5C41;
constexpr uint16_t NEWPPC  = 0x5C42;
constexpr uint16_t NSPPC   = 0x5C44;
constexpr uint16_t PPC     = 0x5C45;
constexpr uint16_t SUBPPC  = 0x5C47;
constexpr uint16_t BORDCR  = 0x5C48;
constexpr uint16_t E_PPC   = 0x5C49;
constexpr uint16_t VARS    = 0x5C4B;
constexpr uint16_t DEST    = 0x5C4D;
constexpr uint16_t CHANS   = 0x5C4F;
constexpr uint16_t CURCHL  = 0x5C51;
constexpr uint16_t PROG    = 0x5C53;
constexpr uint16_t NXTLIN  = 0x5C55;
constexpr uint16_t DATADD  = 0x5C57;
constexpr uint16_t E_LINE  = 0x5C59;
constexpr uint16_t K_CUR   = 0x5C5B;
constexpr uint16_t CH_ADD  = 0x5C5D;
constexpr uint16_t X_PTR   = 0x5C5F;
constexpr uint16_t WORKSP  = 0x5C61;
constexpr uint16_t STKBOT  = 0x5C63;
constexpr uint16_t STKEND  = 0x5C65;
constexpr uint16_t BREG    = 0x5C67;
constexpr uint16_t MEM     = 0x5C68;
constexpr uint16_t FLAGS2  = 0x5C6A;
constexpr uint16_t DF_SZ   = 0x5C6B;
constexpr uint16_t S_TOP   = 0x5C6C;
constexpr uint16_t OLDPPC  = 0x5C6E;
constexpr uint16_t OSPCC   = 0x5C70;
constexpr uint16_t FLAGX   = 0x5C71;
constexpr uint16_t STRLEN  = 0x5C72;
constexpr uint16_t T_ADDR  = 0x5C74;
constexpr uint16_t SEED    = 0x5C76;
constexpr uint16_t FRAMES  = 0x5C78;
constexpr uint16_t UDG     = 0x5C7B;
constexpr uint16_t COORDS  = 0x5C7D;
constexpr uint16_t P_POSN  = 0x5C7F;
constexpr uint16_t PR_CC   = 0x5C80;
constexpr uint16_t ECHO_E  = 0x5C82;
constexpr uint16_t DF_CC   = 0x5C84;
constexpr uint16_t DFCCL   = 0x5C86;
constexpr uint16_t S_POSN  = 0x5C88;  // column, then line
constexpr uint16_t SPOSNL  = 0x5C8A;  // column, then line
constexpr uint16_t SCR_CT  = 0x5C8C;
constexpr uint16_t ATTR_P  = 0x5C8D;
constexpr uint16_t MASK_P  = 0x5C8E;
constexpr uint16_t ATTR_T  = 0x5C8F;
constexpr uint16_t MASK_T  = 0x5C90;
constexpr uint16_t P_FLAG  = 0x5C91;
constexpr uint16_t MEMBOT  = 0x5C92;  // calculator memory mem-0..mem-5
constexpr uint16_t NMIADD  = 0x5CB0;
constexpr uint16_t RAMTOP  = 0x5CB2;
constexpr uint16_t P_RAMT  = 0x5CB4;

// ---- ROM routine addresses -------------------------------------------------
// The channel records in the CHANS area hold the addresses of the input and
// output service routines.  The C++ code dispatches on these addresses, so
// they act as handles for the corresponding C++ functions.
constexpr uint16_t PRINT_OUT = 0x09F4;  // screen / printer output
constexpr uint16_t PO_TV_2   = 0x0A6D;  // second operand of AT/TAB
constexpr uint16_t PO_CONT   = 0x0A87;  // last operand of a control code
constexpr uint16_t ADD_CHAR  = 0x0F81;  // channel 'R': insert into edit line
constexpr uint16_t KEY_INPUT = 0x10A8;  // channel 'K' input
constexpr uint16_t KEY_NEXT  = 0x110D;  // colour parameter after a control key
constexpr uint16_t REPORT_J  = 0x15C4;  // "Invalid I/O device"

// Selected routines that USR may call (see Machine::usrCall()).
constexpr uint16_t START     = 0x0000;
constexpr uint16_t CLS       = 0x0D6B;
constexpr uint16_t CL_SC_ALL = 0x0DFE;
constexpr uint16_t COPY      = 0x0EAC;
constexpr uint16_t NEW       = 0x11B7;
constexpr uint16_t FREE_MEM  = 0x1F1A;

// ---- bits of FLAGS, TV_FLAG, FLAGS2 and FLAGX -------------------------------
constexpr uint8_t F_SUPPRESS_SPACE = 0x01; // FLAGS bit 0: no leading space
constexpr uint8_t F_PRINTER        = 0x02; // FLAGS bit 1: printer in use
constexpr uint8_t F_K_L_TRANSIENT  = 0x04; // FLAGS bit 2: L mode (transient)
constexpr uint8_t F_K_L_MODE       = 0x08; // FLAGS bit 3: L mode
constexpr uint8_t F_NEW_KEY        = 0x20; // FLAGS bit 5: a new key was pressed
constexpr uint8_t F_NUMERIC        = 0x40; // FLAGS bit 6: numeric result
constexpr uint8_t F_RUNTIME        = 0x80; // FLAGS bit 7: not checking syntax

constexpr uint8_t TV_LOWER         = 0x01; // TV_FLAG bit 0: lower screen in use
constexpr uint8_t TV_EDIT_CHANGED  = 0x08; // TV_FLAG bit 3: mode changed, reprint
constexpr uint8_t TV_AUTOLIST      = 0x10; // TV_FLAG bit 4: automatic listing
constexpr uint8_t TV_CLEAR_LOWER   = 0x20; // TV_FLAG bit 5: clear lower screen

constexpr uint8_t F2_CLEAR_MAIN    = 0x01; // FLAGS2 bit 0: clear main screen
constexpr uint8_t F2_PRINTER_USED  = 0x02; // FLAGS2 bit 1: printer buffer used
constexpr uint8_t F2_IN_QUOTES     = 0x04; // FLAGS2 bit 2: inside quotes
constexpr uint8_t F2_CAPS_LOCK     = 0x08; // FLAGS2 bit 3: CAPS LOCK
constexpr uint8_t F2_K_CHANNEL     = 0x10; // FLAGS2 bit 4: channel K in use
constexpr uint8_t F2_GW_CLASSIC    = 0x20; // FLAGS2 bit 5: GW "new flag", classic K mode

constexpr uint8_t FX_SIMPLE_STRING = 0x01; // FLAGX bit 0: complete simple string
constexpr uint8_t FX_NEW_VARIABLE  = 0x02; // FLAGX bit 1: new variable
constexpr uint8_t FX_INPUT_MODE    = 0x20; // FLAGX bit 5: INPUT mode
constexpr uint8_t FX_INPUT_NUMERIC = 0x40; // FLAGX bit 6: numeric INPUT
constexpr uint8_t FX_INPUT_LINE    = 0x80; // FLAGX bit 7: INPUT LINE

} // namespace sv

// Tokens (character codes of keywords).
namespace tok {
constexpr uint8_t RND = 0xA5, INKEY = 0xA6, PI = 0xA7, FN = 0xA8, POINT = 0xA9,
    SCREEN = 0xAA, ATTR = 0xAB, AT = 0xAC, TAB = 0xAD, VALS = 0xAE, CODE = 0xAF,
    VAL = 0xB0, LEN = 0xB1, SIN = 0xB2, USR = 0xC0, STRS = 0xC1, CHRS = 0xC2,
    NOT = 0xC3, BIN = 0xC4, OR = 0xC5, AND = 0xC6, LE = 0xC7, GE = 0xC8,
    NE = 0xC9, LINE = 0xCA, THEN = 0xCB, TO = 0xCC, STEP = 0xCD, DEF_FN = 0xCE,
    CAT = 0xCF, FORMAT = 0xD0, MOVE = 0xD1, ERASE = 0xD2, OPEN = 0xD3,
    CLOSE = 0xD4, MERGE = 0xD5, VERIFY = 0xD6, BEEP = 0xD7, CIRCLE = 0xD8,
    INK = 0xD9, PAPER = 0xDA, FLASH = 0xDB, BRIGHT = 0xDC, INVERSE = 0xDD,
    OVER = 0xDE, OUT = 0xDF, LPRINT = 0xE0, LLIST = 0xE1, STOP = 0xE2,
    READ = 0xE3, DATA = 0xE4, RESTORE = 0xE5, NEW = 0xE6, BORDER = 0xE7,
    CONTINUE = 0xE8, DIM = 0xE9, REM = 0xEA, FOR = 0xEB, GO_TO = 0xEC,
    GO_SUB = 0xED, INPUT = 0xEE, LOAD = 0xEF, LIST = 0xF0, LET = 0xF1,
    PAUSE = 0xF2, NEXT = 0xF3, POKE = 0xF4, PRINT = 0xF5, PLOT = 0xF6,
    RUN = 0xF7, SAVE = 0xF8, RANDOMIZE = 0xF9, IF = 0xFA, CLS = 0xFB,
    DRAW = 0xFC, CLEAR = 0xFD, RETURN = 0xFE, COPY = 0xFF;
}

} // namespace zxgw

#endif
