// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 5. The editor (L0F2C) and keyboard input (L10A8).
// Part 6. Listing: AUTO-LIST, LIST, OUT-LINE and friends.

#include "machine.h"

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;

// ---------------------------------------------------------------------------
// L0F2C EDITOR: edit the line in the editing area or the INPUT workspace.
// ERR_SP points to ED-ERROR while editing: errors cause a "rasp" and the
// editor simply continues (ED-AGAIN).
// ---------------------------------------------------------------------------
void Machine::editor() {
    for (;;) {                                                 // ED-AGAIN
        try {
            for (;;) {                                         // ED-LOOP
                uint8_t a = waitKey();
                // (the key click is not reproduced)
                if (edLoopKey(a)) break;                       // ED-ENTER
            }
        } catch (BasicError&) {
            // L107F ED-ERROR: while the keyboard is used, errors just give
            // a rasp (not reproduced) and editing continues.
            if (!flag(FLAGS2, F2_K_CHANNEL)) throw;            // ED-END
            mem[ERR_NR] = 0xFF;
            continue;
        }
        // L1026 ED-END: the previous error handler gets a pending error
        // (ED-STOP sets "H STOP in INPUT").
        if (mem[ERR_NR] != 0xFF) throw BasicError{mem[ERR_NR]};
        return;
    }
}

// L0F38 ED-LOOP: one key.  Returns true for ED-ENTER.
bool Machine::edLoopKey(uint8_t a) {
    if (a >= 0x18 || a < 0x07) { addChar(a); return false; }
    if (a < 0x10) return edKeys(a);
    // colour controls INK..OVER, AT and TAB need parameters
    uint16_t bc = 2;
    uint8_t d = a, e = 0;
    if (a >= 0x16) {
        bc = 3;
        if (!flag(FLAGX, FX_INPUT_LINE)) {                     // ED-IGNORE
            waitKey();
            waitKey();
            return true;
        }
        e = waitKey();
    }
    uint8_t param = waitKey();                                 // ED-CONTR
    uint16_t hl = word(K_CUR);
    mem[MODE] &= 0xFE;
    makeRoom(hl, bc);
    mem[hl] = d;
    mem[uint16_t(hl + 1)] = e;
    mem[mrDE] = param;                                         // ADD-CH-1
    setWord(K_CUR, uint16_t(mrDE + 1));
    return false;
}

// L0F81 ADD-CHAR: insert a character at the cursor.  This is also the
// output routine of channel 'R'.
void Machine::addChar(uint8_t a) {
    mem[MODE] &= 0xFE;                                         // 'L' mode
    uint16_t hl = word(K_CUR);
    makeRoom(hl, 1);                                           // ONE-SPACE
    mem[mrDE] = a;                                             // ADD-CH-1
    setWord(K_CUR, uint16_t(mrDE + 1));
}

// L0F92 ED-KEYS: editing keys 7..15.  Returns true for ED-ENTER.
bool Machine::edKeys(uint8_t a) {
    switch (a) {
    case 0x07: edEdit(); break;
    case 0x08: edLeft(); break;
    case 0x09: edRight(); break;
    case 0x0A: edDown(); return mem[ERR_NR] != 0xFF;          // ED-STOP -> ED-ENTER
    case 0x0B: edUp(); break;
    case 0x0C: edDelete(); break;
    case 0x0D: return true;                                    // ED-ENTER
    case 0x0E:                                                 // ED-SYMBOL
        if (!flag(FLAGX, FX_INPUT_LINE)) return true;
        addChar(a);
        break;
    case 0x0F: addChar(a); break;                              // ED-GRAPH
    default: break;
    }
    return false;
}

// L0FA9 ED-EDIT: bring the current line down for editing.
void Machine::edEdit() {
    if (flag(FLAGX, FX_INPUT_MODE)) { clearSp(); return; }
    uint16_t prev;
    bool exact;
    uint16_t hl = lineAddr(word(E_PPC), prev, exact);
    uint16_t n = lineNo(hl, prev);
    if (n == 0) { clearSp(); return; }
    // the line found (or the previous one)
    if (mem[hl] & 0xC0) hl = prev;
    testRoom(uint32_t(word(uint16_t(hl + 2))) + 10);
    clearSp();
    uint16_t curchl = word(CURCHL);
    chanOpen(-1);                                              // channel 'R'
    --mem[E_PPC + 1];                                          // GW: suppress the '>'
    uint8_t e = 1;
    outLine(hl, e);
    ++mem[E_PPC + 1];
    setWord(K_CUR, uint16_t(word(E_LINE) + 4));
    chanFlag(curchl);
}

// L0FF3 ED-DOWN
void Machine::edDown() {
    if (flag(FLAGX, FX_INPUT_MODE)) {
        mem[ERR_NR] = 0x10;                                    // ED-STOP
        return;
    }
    lnFetch(E_PPC);
    edList();
}

// L1007 ED-LEFT
void Machine::edLeft() {
    bool atStart;
    uint16_t hl = edEdge(word(K_CUR), atStart);
    if (!atStart) setWord(K_CUR, hl);                          // ED-CUR
}

// L100C ED-RIGHT with the GW fix ED_FIX1: also skip a colour parameter.
void Machine::edRight() {
    uint16_t hl = word(K_CUR);
    uint8_t a = mem[hl];
    if (a == 0x0D) return;
    ++hl;                                                      // ED_BUMP
    setWord(K_CUR, hl);
    if (a >= 0x15) return;
    if (a < 0x10) return;
    setWord(K_CUR, uint16_t(hl + 1));
}

// L1015 ED-DELETE
void Machine::edDelete() {
    bool atStart;
    uint16_t hl = edEdge(word(K_CUR), atStart);
    if (atStart) return;
    reclaim2(hl, 1);                                           // POINTERS moves K_CUR
}

// L1031 ED-EDGE: the position left of the cursor, stepping over colour
// controls and their parameters.
uint16_t Machine::edEdge(uint16_t cursor, bool& atStart) {
    uint16_t de = setDe(true);
    atStart = cursor <= de;
    if (atStart) return cursor;
    for (;;) {                                                 // ED-EDGE-1
        uint16_t hl = uint16_t(de + 1);
        uint8_t a = mem[de];
        if ((a & 0xF0) == 0x10) {
            ++hl;
            if (a == 0x16 || a == 0x17) ++hl;
        }
        if (hl < cursor) { de = hl; continue; }                // ED-EDGE-2
        return de;
    }
}

// L1059 ED-UP
void Machine::edUp() {
    if (flag(FLAGX, FX_INPUT_MODE)) return;
    uint16_t prev;
    bool exact;
    uint16_t addr = lineAddr(word(E_PPC), prev, exact);
    uint16_t n = lineNo(prev, addr);
    setWord(E_PPC, n);                                         // LN-STORE
    edList();
}

// L106E ED-LIST
void Machine::edList() {
    autoList();
    chanOpen(0);
}

// L1097 CLEAR-SP: clear the editing area or the INPUT workspace.
void Machine::clearSp() {
    uint16_t hl;
    uint16_t de = setDe(false, &hl);                           // SET-HL
    --hl;
    reclaim1(de, hl);
    setWord(K_CUR, de);
    mem[MODE] = 0;
}

// L1190 SET-HL / L1195 SET-DE.  With carry: DE = start of the line.
// Without carry also HL = the last location (returned through hlOut).
uint16_t Machine::setDe(bool carry, uint16_t* hlOut) {
    uint16_t hl = uint16_t(word(WORKSP) - 1);
    uint16_t de = word(E_LINE);
    if (flag(FLAGX, FX_INPUT_MODE)) {
        de = word(WORKSP);
        if (!carry) hl = word(STKBOT);
    }
    if (hlOut) *hlOut = hl;
    return de;
}

// L11A7 REMOVE-FP: remove the hidden numbers from a line.
void Machine::removeFp(uint16_t hl) {
    for (;;) {
        if (mem[hl] == 0x0E) reclaim2(hl, 6);
        uint8_t a = mem[hl];
        ++hl;
        if (a == 0x0D) return;
    }
}

// ---------------------------------------------------------------------------
// L10A8 KEY-INPUT: the input routine of channel 'K'.
// Returns true with a key code in a.
// ---------------------------------------------------------------------------
bool Machine::keyInput(uint8_t& a) {
    if (flag(TV_FLAG, TV_EDIT_CHANGED)) edCopy();
    if (!flag(FLAGS, F_NEW_KEY)) return false;
    // KI_END
    a = mem[LAST_K];
    setFlag(FLAGS, F_NEW_KEY, false);
    if (flag(TV_FLAG, TV_CLEAR_LOWER)) clsLower();
    if (a >= 0x20) return true;                                // KEY-DONE2
    uint8_t c, control;
    if (a >= 0x10) {                                           // KEY-CONTR
        c = a & 7;
        control = (a & 0x08) ? 0x10 : 0x11;                    // INK : PAPER
    } else if (a >= 0x06) {                                    // KEY-M-CL
        if (a == 0x06) {
            mem[FLAGS2] ^= F2_CAPS_LOCK;
            setFlag(TV_FLAG, TV_EDIT_CHANGED, true);           // KEY-FLAG
            return false;
        }
        if (a < 0x0E) return true;                             // KEY-MODE
        uint8_t mode = uint8_t(a - 0x0D);                      // 1 = E, 2 = G
        mem[MODE] = mem[MODE] == mode ? 0 : mode;
        setFlag(TV_FLAG, TV_EDIT_CHANGED, true);
        return false;
    } else {
        c = a & 1;                                             // FLASH, BRIGHT, INVERSE
        control = uint8_t((a >> 1) + 0x12);
    }
    mem[K_DATA] = c;                                           // KEY-DATA
    setWord(uint16_t(word(CHANS) + 2), KEY_NEXT);              // KEY-CHAN
    a = control;
    return true;
}

// L111D ED-COPY: print the edit line in the lower screen.
void Machine::edCopy() {
    editing = true;
    temps();
    setFlag(TV_FLAG, TV_EDIT_CHANGED, false);
    setFlag(TV_FLAG, TV_CLEAR_LOWER, false);
    uint16_t oldSposnl = word(SPOSNL);
    uint16_t newPos;
    try {
        uint16_t oldEcho = word(ECHO_E);
        uint16_t start = setDe(true);
        setFlag(FLAGS, F_SUPPRESS_SPACE, true);                // OUT-LINE2
        uint16_t end = outLine3(start);
        outCurs(end);
        newPos = word(SPOSNL);
        temps();
        for (;;) {                                             // ED-BLANK
            int line = mem[SPOSNL + 1], col = mem[SPOSNL];
            int oldLine = oldEcho >> 8, oldCol = oldEcho & 0xFF;
            if (line < oldLine) break;
            if (line == oldLine && oldCol >= col) break;
            printOut(' ');                                     // ED-SPACES
        }
    } catch (BasicError&) {
        // L1167 ED-FULL: the lower screen is full.
        mem[ERR_NR] = 0xFF;
        newPos = word(SPOSNL);
    }
    clSet(oldSposnl >> 8, oldSposnl & 0xFF);                   // ED-C-END
    setWord(ECHO_E, newPos);
    mem[X_PTR + 1] = 0;
    editing = false;
}

// L15D4 WAIT-KEY
uint8_t Machine::waitKey() {
    if (!flag(TV_FLAG, TV_CLEAR_LOWER)) setFlag(TV_FLAG, TV_EDIT_CHANGED, true);
    struct QueueKeys {
        bool& flag, saved;
        explicit QueueKeys(bool& f) : flag(f), saved(f) { flag = true; }
        ~QueueKeys() { flag = saved; }
    } queueing(queueKeys);
    for (;;) {
        uint8_t a;
        if (inputAd(a)) return a;                              // WAIT-KEY1
        // End of input (Ctrl-D) once everything typed before it is used.
        if (quitPending && keyQueue.empty() && codeQueue.empty() && !flag(FLAGS, F_NEW_KEY)) {
            quitPending = false;
            throw QuitRequest{0};
        }
        if (quit) throw QuitRequest{exitCode};
        if (breakPending) {
            breakPending = false;
            // A terminal user expects ESC / Ctrl-C to leave an INPUT.
            if (flag(FLAGX, FX_INPUT_MODE) && flag(FLAGS2, F2_K_CHANNEL)) throw InputBreak{};
        }
        int oldW = W, oldH = H;
        syncGeometry(false);
        if (oldW != W || oldH != H) {
            setFlag(TV_FLAG, TV_EDIT_CHANGED, true);           // reprint the edit line
            present(true);
            continue;
        }
        idle(40);
    }
}

// L15E6 INPUT-AD: call the input routine of the current channel.
bool Machine::inputAd(uint8_t& a) {
    uint16_t routine = word(uint16_t(word(CURCHL) + 2));
    switch (routine) {
    case KEY_INPUT:
        return keyInput(a);
    case KEY_NEXT:                                             // L110D KEY-NEXT
        a = mem[K_DATA];
        setWord(uint16_t(word(CHANS) + 2), KEY_INPUT);
        return true;
    default:
        error(0x12);                                           // REPORT-J
    }
}

// GW CONS_IN: read a single key (ENTER or space and above), used for
// "scroll?" and the tape messages.
uint8_t Machine::consIn() {
    mem[LAST_K] = 0x08;
    setFlag(FLAGS, F_NEW_KEY, false);
    for (;;) {
        if (breakPending) {
            breakPending = false;
            mem[LAST_K] = ' ';
        }
        uint8_t a = mem[LAST_K];
        if (a == 0x0D || a >= 0x20) break;
        if (quitPending && keyQueue.empty()) {
            quitPending = false;
            throw QuitRequest{0};
        }
        setFlag(FLAGS, F_NEW_KEY, false);
        idle(40);
    }
    // KI_END
    uint8_t a = mem[LAST_K];
    setFlag(FLAGS, F_NEW_KEY, false);
    if (flag(TV_FLAG, TV_CLEAR_LOWER)) clsLower();
    return a;
}

// ===========================================================================
// Listing
// ===========================================================================

// L1795 AUTO-LIST: list the program around the current line.
void Machine::autoList() {
    try {
        mem[TV_FLAG] = TV_AUTOLIST;
        clAll();
        setFlag(TV_FLAG, TV_LOWER, true);
        clLine(mem[DF_SZ]);
        setFlag(TV_FLAG, TV_LOWER, false);
        setFlag(FLAGS2, F2_CLEAR_MAIN, true);
        uint16_t ePpc = word(E_PPC);
        uint16_t sTop = word(S_TOP);
        if (ePpc < sTop) {
            setWord(S_TOP, ePpc);                              // AUTO-L-2
        } else {
            uint16_t prev;
            bool exact;
            uint16_t cur = lineAddr(ePpc, prev, exact);
            uint16_t bc = uint16_t((H - 2) * W - cur);         // $02C0 = 22*32
            uint16_t hl = lineAddr(sTop, prev, exact);
            for (;;) {                                         // AUTO-L-1
                uint16_t next = nextOne(hl);
                if (uint32_t(hl) + bc > 0xFFFF) break;         // carry: line fits
                hl = next;
                setWord(S_TOP, uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)]));
            }
        }
        // AUTO-L-3
        uint16_t prev;
        bool exact;
        uint16_t hl = lineAddr(word(S_TOP), prev, exact);
        if (!exact) hl = prev;
        listAll(hl, 1);                                        // AUTO-L-4
        setFlag(TV_FLAG, TV_AUTOLIST, false);
    } catch (AutoListStop&) {
        // the screen is full and the current line is shown
    }
}

// L17F5 LLIST
void Machine::llistCmd() { listCmd(3); }

// L17F9 LIST
void Machine::listCmd(int stream) {
    mem[TV_FLAG] = 0;
    if (!syntaxZ()) chanOpen(stream);
    getChar();
    if (strAlter()) {
        uint8_t a = getChar();
        if (a == ';' || a == ',') {                            // LIST-2
            nextChar();
            expt1Num();
        } else {
            useZero();                                         // LIST-3
        }
    } else {
        fetchNum();                                            // LIST-4
    }
    checkEnd();                                                // LIST-5
    uint16_t bc = findLine();
    uint16_t line = uint16_t(((bc >> 8) & 0x3F) << 8 | (bc & 0xFF));
    setWord(E_PPC, line);
    uint16_t prev;
    bool exact;
    uint16_t hl = lineAddr(line, prev, exact);
    listAll(hl, 1);
}

// L1833 LIST-ALL
void Machine::listAll(uint16_t hl, uint8_t e) {
    for (;;) {
        if (!outLine(hl, e)) return;
        printA(0x0D);
        if (!flag(TV_FLAG, TV_AUTOLIST)) continue;
        if (mem[DF_SZ] != mem[S_POSN + 1]) continue;
        if (e == 0) return;
        lnFetch(S_TOP);
    }
}

// L1855 OUT-LINE: print the BASIC line at hl.  Returns false at the end of
// the program.  e: 1 while the current line (E_PPC) has not been printed.
bool Machine::outLine(uint16_t& hl, uint8_t& e) {
    uint16_t bc = word(E_PPC);
    int r = cpLines(hl, bc);
    uint8_t d = '>';
    if (r != 0) {
        d = 0;
        e = r < 0 ? 1 : 0;
    }
    mem[BREG] = e;                                             // OUT-LINE1
    if (mem[hl] >= 0x40) return false;
    outNum2(hl);
    hl = uint16_t(hl + 4);
    setFlag(FLAGS, F_SUPPRESS_SPACE, false);
    if (d) {
        printA(d);
        setFlag(FLAGS, F_SUPPRESS_SPACE, true);                // OUT-LINE2
    }
    hl = outLine3(hl);
    return true;
}

// L187D OUT-LINE2
void Machine::outLine2(uint16_t hl) {
    setFlag(FLAGS, F_SUPPRESS_SPACE, true);
    outLine3(hl);
}

// L1881 OUT-LINE3 .. OUT-LINE6: print up to and including the ENTER.
// Returns the address after the ENTER.
uint16_t Machine::outLine3(uint16_t hl) {
    setFlag(FLAGS2, F2_IN_QUOTES, false);
    impose();                                                  // GW
    for (;;) {                                                 // OUT-LINE4
        if (word(X_PTR) == hl) outFlash('?');                  // the error marker
        outCurs(hl);                                           // OUT-LINE5
        uint8_t a = mem[hl];
        if (a == 0x0E) { hl = uint16_t(hl + 6); a = mem[hl]; } // NUMBER
        ++hl;
        if (a == 0x0D) return hl;                              // OUT-LINE6
        outChar(a);
    }
}

// L18B6 NUMBER: step over a hidden number.
uint16_t Machine::numberSkip(const Machine& mm, uint16_t hl) {
    if (mm.mem[hl] != 0x0E) return hl;
    return uint16_t(hl + 6);
}

// L18C1 OUT-FLASH: print a flashing character (cursor or error marker).
void Machine::outFlash(uint8_t a) {
    uint16_t tm = word(ATTR_T);
    uint8_t pflag = mem[P_FLAG];
    mem[ATTR_T] |= 0x80;
    mem[MASK_T] &= 0x7F;
    mem[P_FLAG] = 0;
    printOut(a);
    mem[P_FLAG] = pflag;
    setWord(ATTR_T, tm);
}

// L18E1 OUT-CURS: print the cursor if this is the cursor position.
void Machine::outCurs(uint16_t de) {
    if (word(K_CUR) != de) return;
    uint8_t mode = mem[MODE];
    uint8_t a;
    if (mode) {
        a = uint8_t('C' + 2 * mode);                           // 'E' or 'G'
    } else {
        setFlag(FLAGS, F_K_L_MODE, false);                     // OUT-C-1
        a = 'K';
        if (flag(FLAGS, F_K_L_TRANSIENT)) {
            setFlag(FLAGS, F_K_L_MODE, true);
            a = 'L';
            if (flag(FLAGS2, F2_CAPS_LOCK)) a = 'C';
        }
    }
    outFlash(a);                                               // OUT-C-2
}

// L190F LN-FETCH: the number of the line after the one in var.
void Machine::lnFetch(uint16_t var) {
    uint16_t prev;
    bool exact;
    uint16_t hl = lineAddr(uint16_t(word(var) + 1), prev, exact);
    uint16_t n = lineNo(hl, prev);
    if (flag(FLAGX, FX_INPUT_MODE)) return;                    // LN-STORE
    setWord(var, n);
}

// L1937 OUT-CHAR: print a character of a BASIC line and track the
// K/L mode for the cursor.
void Machine::outChar(uint8_t a) {
    if (numeric(a) || a < 0x21) { printA(a); return; }         // OUT-CH-3
    impose();                                                  // GW (was RES 2)
    if (a == tok::THEN) { printA(a); return; }
    if (a == ':') {
        if (flag(FLAGX, FX_INPUT_MODE) || flag(FLAGS2, F2_IN_QUOTES)) {
            setFlag(FLAGS, F_K_L_TRANSIENT, true);             // OUT-CH-2
        }
        printA(a);
        return;
    }
    if (a == '"') mem[FLAGS2] ^= F2_IN_QUOTES;                 // OUT-CH-1
    setFlag(FLAGS, F_K_L_TRANSIENT, true);                     // OUT-CH-2
    printA(a);
}

} // namespace zxgw
