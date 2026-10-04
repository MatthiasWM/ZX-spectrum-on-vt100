// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 1 (restart routines) and Part 6 (executive routines) of the ROM.

#include "machine.h"

#include <algorithm>
#include <cstring>
#include <thread>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;
using Clock = std::chrono::steady_clock;

namespace {
int msSince(Clock::time_point t) {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}
} // namespace

Machine::Machine(Host& h, const Options& o) : host(h), opts(o) {
    batch = host.batch();
    started = lastInterrupt = lastPresent = lastPoll = throttleStart = Clock::now();
    heldUntil = started;
    syncGeometry(true);
    startNew(false);                                  // L0000 START
    if (opts.classicKeywords) setFlag(FLAGS2, F2_GW_CLASSIC, true);
}

// ===========================================================================
// Part 1. RESTART ROUTINES
// ===========================================================================

// L0008 ERROR-1: the error pointer is set to the position reached by CH_ADD
// so the editor can show the flashing '?' marker there.
void Machine::error(uint8_t errNr) {
    setWord(X_PTR, word(CH_ADD));
    error3(errNr);
}

// L0055 ERROR-3: store the code and unwind to the active error handler.
// SET-STK clears the calculator stack.
void Machine::error3(uint8_t errNr) {
    mem[ERR_NR] = errNr;
    setStk();
    calc.clear();
    throw BasicError{errNr};
}

// L0010 PRINT-A / L15F2 PRINT-A-2: output to the current channel.  The output
// routine address in the channel record selects the C++ function.
void Machine::printA(uint8_t a) {
    uint16_t routine = word(word(CURCHL));
    switch (routine) {
    case PRINT_OUT: printOut(a); break;
    case PO_TV_2:   poTv2(a); break;
    case PO_CONT:   poCont(a); break;
    case ADD_CHAR:  addChar(a); break;
    default:        error(0x12);                     // REPORT-J Invalid I/O device
    }
}

// L0018 GET-CHAR / L001C TEST-CHAR
uint8_t Machine::getChar() {
    uint16_t hl = word(CH_ADD);
    uint8_t a = mem[hl];
    while (skipOver(hl, a)) a = chAddPlus1();        // NEXT-CHAR loop
    return a;
}

// L0020 NEXT-CHAR
uint8_t Machine::nextChar() {
    uint8_t a = chAddPlus1();
    uint16_t hl = word(CH_ADD);
    while (skipOver(hl, a)) { a = chAddPlus1(); hl = word(CH_ADD); }
    return a;
}

// L0074 CH-ADD+1
uint8_t Machine::chAddPlus1() { return tempPtr1(word(CH_ADD)); }
// L0077 TEMP-PTR1
uint8_t Machine::tempPtr1(uint16_t hl) { return tempPtr2(uint16_t(hl + 1)); }
// L0078 TEMP-PTR2
uint8_t Machine::tempPtr2(uint16_t hl) { setWord(CH_ADD, hl); return mem[hl]; }

// L007D SKIP-OVER: returns true (carry) if the character is to be skipped.
// The colour controls 16..21 and AT/TAB 22..23 skip their parameters.
bool Machine::skipOver(uint16_t& hl, uint8_t a) {
    if (a >= 0x21) return false;
    if (a == 0x0D) return false;
    if (a < 0x10) return true;
    if (a >= 0x18) return true;
    ++hl;
    if (a >= 0x16) ++hl;
    setWord(CH_ADD, hl);                              // SKIPS
    return true;
}

// L0030 BC-SPACES / L169E RESERVE: make room at the end of the workspace.
// Returns the address of the first new location.
uint16_t Machine::bcSpaces(uint16_t bc) {
    uint16_t worksp = word(WORKSP);
    makeRoom(uint16_t(word(STKBOT) - 1), bc);
    setWord(WORKSP, worksp);                          // POINTERS may have moved it
    return uint16_t(mrHL + 2);
}

// ===========================================================================
// Part 6. EXECUTIVE ROUTINES
// ===========================================================================

// L11B7 NEW and L11CB START-NEW.
void Machine::startNew(bool fromNew) {
    uint16_t pRamt = 0xFFFF, raspPip = 0x0040, udg = 0xFF58, ramtop = 0xFF57;
    if (fromNew) {
        ramtop = word(RAMTOP);
        pRamt = word(P_RAMT);
        raspPip = word(RASP);
        udg = word(UDG);
        // RAM-FILL / RAM-READ: everything up to RAMTOP is cleared.
        std::fill(mem.begin() + ROM_END, mem.begin() + ramtop + 1, 0);
    } else {
        std::fill(mem.begin(), mem.end(), 0);
        // The ROM area: only the character set is provided (for PEEK).
        std::memcpy(&mem[CHARSET], kRomCharset, sizeof(kRomCharset));
        // The UDGs are a copy of the characters A..U.
        std::memcpy(&mem[0xFF58], &kRomCharset[('A' - 32) * 8], 21 * 8);
    }
    setWord(P_RAMT, pRamt);
    setWord(RASP, raspPip);
    setWord(UDG, udg);
    setWord(RAMTOP, ramtop);                          // RAM-SET

    // NMI_VECT
    setWord(CHARS, 0x3C00);
    mem[ramtop] = 0x3E;                               // GO SUB end marker
    gosubSp = uint16_t(ramtop - 1);
    setWord(ERR_SP, uint16_t(ramtop - 3));

    // SET_CHANS
    setWord(CHANS, CHANS_INIT);
    static const uint8_t initChan[21] = {             // L15AF init-chan
        0xF4, 0x09, 0xA8, 0x10, 'K',
        0xF4, 0x09, 0xC4, 0x15, 'S',
        0x81, 0x0F, 0xC4, 0x15, 'R',
        0xF4, 0x09, 0xC4, 0x15, 'P',
        0x80
    };
    std::memcpy(&mem[CHANS_INIT], initChan, sizeof(initChan));
    uint16_t hl = CHANS_INIT + sizeof(initChan);
    setWord(DATADD, uint16_t(hl - 1));
    setWord(PROG, hl);
    setWord(VARS, hl);
    mem[hl] = 0x80;
    setWord(E_LINE, uint16_t(hl + 1));
    mem[ATTR_P] = 0x38;
    mem[BORDCR] = 0x38;
    mem[REPDEL] = 0x23;
    mem[REPPER] = 0x05;
    mem[KSTATE] = 0xFF;
    mem[KSTATE + 4] = 0xFF;
    static const uint8_t initStrm[14] = {             // L15C6 init-strm
        0x01, 0x00, 0x06, 0x00, 0x0B, 0x00, 0x01, 0x00, 0x01, 0x00, 0x06, 0x00, 0x10, 0x00
    };
    std::memcpy(&mem[STRMS], initStrm, sizeof(initStrm));
    calc.clear();
    fnStrings.clear();
    clearPrb();
    mem[DF_SZ] = 2;
    cls();
    poMsg(kCopyright);
    setFlag(TV_FLAG, TV_CLEAR_LOWER, true);
    framesBase = 0;
    started = Clock::now();
}

// L11B7 NEW command.
void Machine::newCmd() {
    startNew(true);
    throw RestartMain{};                              // continue at MAIN-1
}

// ---------------------------------------------------------------------------
// The main execution loop: L12A2 MAIN-EXEC, MAIN-1 ... MAIN-9.
// ---------------------------------------------------------------------------
void Machine::mainLoop() {
    enum { MAIN_EXEC, MAIN_1, MAIN_2, MAIN_4 } state = MAIN_1;
    for (;;) {
        if (quit) return;
        switch (state) {
        case MAIN_EXEC:
            mem[DF_SZ] = 2;
            autoList();
            [[fallthrough]];
        case MAIN_1:
            setMin();
            [[fallthrough]];
        case MAIN_2: {
            chanOpen(0);
            newEd();                                  // NEWED, may throw QuitRequest
            try {
                lineScan();                           // L1B17 LINE-SCAN
            } catch (BasicError&) {
                // ERR_NR holds the code; X_PTR marks the position.
            }
            if (mem[ERR_NR] != 0xFF) {
                if (flag(FLAGS2, F2_K_CHANNEL)) {
                    removeFp(word(E_LINE));
                    if (batch) batchSyntaxError(word(E_LINE));
                    else pendingCommands.clear();            // stop loading a listing
                    mem[ERR_NR] = 0xFF;
                    state = MAIN_2;
                    continue;
                }
                state = MAIN_4;
                continue;
            }
            // MAIN-3
            mem[MODE] = 0;
            uint16_t bc = eLineNo();
            if (bc) {
                mainAdd(bc);
                state = mem[ERR_NR] == 0xFF ? MAIN_EXEC : MAIN_4;
                continue;
            }
            if (getChar() == 0x0D) { state = MAIN_EXEC; continue; }
            if (flag(FLAGS2, F2_CLEAR_MAIN)) clAll();
            clsLower();
            mem[SCR_CT] = uint8_t(H + 1 - mem[S_POSN + 1]);
            setFlag(FLAGS, F_RUNTIME, true);
            mem[ERR_NR] = 0xFF;
            mem[NSPPC] = 1;
            try {
                runStatements(true);                  // L1B8A LINE-RUN
            } catch (BasicError&) {
            } catch (RestartMain&) {
                state = MAIN_1;
                continue;
            }
            state = MAIN_4;
            continue;
        }
        case MAIN_4:
            // L1303 MAIN-4
            setFlag(FLAGS, F_NEW_KEY, false);
            runKeys.clear();
            if (flag(FLAGS2, F2_PRINTER_USED)) {
                setFlag(FLAGS, F_PRINTER, true);
                copyBuff();
            }
            mainG(uint8_t(mem[ERR_NR] + 1));
            state = MAIN_2;
            continue;
        }
    }
}

// L1313 MAIN-G: print the report "c message, line:statement".
void Machine::mainG(uint8_t a) {
    mem[FLAGX] = 0;
    mem[X_PTR + 1] = 0;
    setWord(DEFADD, 0);
    setWord(STRMS + 6, 1);                            // stream 0 is the keyboard
    setMin();
    setFlag(FLAGX, FX_INPUT_MODE, false);
    clsLower();
    setFlag(TV_FLAG, TV_CLEAR_LOWER, true);
    uint8_t b = a;
    outCode(uint8_t(a < 10 ? a : a + 7));             // MAIN-5
    printA(' ');                                      // TSTMSG prints the space
    poMsg(b < 28 ? kReports[b] : "?");
    poMsg(", ");
    outNum1(word(PPC));
    printA(':');
    outNum1(mem[SUBPPC]);
    clearSp();
    uint8_t e = uint8_t(mem[ERR_NR] + 1);
    if (e != 0) {
        if (e == 0x09 || e == 0x15) ++mem[SUBPPC];    // MAIN-6: STOP or BREAK
        // MAIN-7 / MAIN-8: remember where to CONTINUE.
        if (mem[NSPPC] & 0x80) {
            mem[OSPCC] = mem[SUBPPC];
            setWord(OLDPPC, word(PPC));
        } else {
            mem[OSPCC] = mem[NSPPC];
            setWord(OLDPPC, word(NEWPPC));
        }
    }
    mem[NSPPC] = 0xFF;                                // MAIN-9
    setFlag(FLAGS, F_K_L_MODE, false);
    present(true);
}

// L155D MAIN-ADD: add the edit line to the program.
void Machine::mainAdd(uint16_t bc) {
    setWord(E_PPC, bc);
    try {
        uint16_t de = word(CH_ADD);
        uint16_t length = uint16_t(word(WORKSP) - de - 1);
        uint16_t prev;
        bool exact;
        uint16_t hl = lineAddr(bc, prev, exact);
        if (exact) {
            uint16_t next = nextOne(hl);
            reclaim2(hl, uint16_t(next - hl));
        }
        if (length != 1) {                            // not just ENTER: insert
            uint16_t prog = word(PROG);
            uint16_t n = uint16_t(length + 4);
            makeRoom(uint16_t(hl - 1), n);
            setWord(PROG, prog);
            uint16_t dest = uint16_t(mrDE + 1);       // end of the new area
            uint16_t src = uint16_t(word(WORKSP) - 2);
            // copy the line backwards (LDDR) from the moved edit line
            for (uint16_t i = 0; i < length; ++i) mem[uint16_t(dest - i)] = mem[uint16_t(src - i)];
            uint16_t start = uint16_t(dest - length + 1 - 4);
            mem[start] = uint8_t(bc >> 8);
            mem[uint16_t(start + 1)] = uint8_t(bc);
            setWord(uint16_t(start + 2), length);
        }
    } catch (BasicError&) {
        mem[ERR_NR] = 0x0F;                           // L1555 REPORT-G No room for line
    }
}

// Batch mode: nobody can correct the line, so the error is reported in the
// transcript and the line is discarded.
void Machine::batchSyntaxError(uint16_t start) {
    uint8_t code = uint8_t(mem[ERR_NR] + 1);
    std::string msg;
    msg += char(code < 10 ? '0' + code : 'A' + code - 10);
    msg += ' ';
    msg += code < 28 ? kReports[code] : "?";
    msg += ": " + lineText(start, word(X_PTR)) + "\n";
    host.transcript(Output::LowerScreen, msg);
    clearSp();
    mem[X_PTR + 1] = 0;
}

// L15EF OUT-CODE
void Machine::outCode(uint8_t a) { printA(uint8_t(a + '0')); }

// L1601 CHAN-OPEN
void Machine::chanOpen(int stream) {
    uint16_t entry = uint16_t(STRMS + ((stream + 3) & 0xFF) * 2);
    uint16_t de = word(entry);
    if (de == 0) error(0x17);                         // REPORT-Oa Invalid stream
    chanFlag(uint16_t(word(CHANS) + de - 1));
}

// L1615 CHAN-FLAG
void Machine::chanFlag(uint16_t hl) {
    setWord(CURCHL, hl);
    setFlag(FLAGS2, F2_K_CHANNEL, false);
    switch (mem[uint16_t(hl + 4)]) {
    case 'K':                                         // L1634 CHAN-K
        setFlag(TV_FLAG, TV_LOWER, true);
        setFlag(FLAGS, F_NEW_KEY, false);
        setFlag(FLAGS2, F2_K_CHANNEL, true);
        setFlag(FLAGS, F_PRINTER, false);
        temps();
        break;
    case 'S':                                         // L1642 CHAN-S
        setFlag(TV_FLAG, TV_LOWER, false);
        setFlag(FLAGS, F_PRINTER, false);
        temps();
        break;
    case 'P':                                         // L164D CHAN-P
        setFlag(FLAGS, F_PRINTER, true);
        break;
    default:
        break;                                        // channel 'R'
    }
}

// L1655 MAKE-ROOM: create bc bytes at hl, moving everything up to STKEND.
// Afterwards mrHL = hl - 1 and mrDE = hl + bc - 1 (last new location).
void Machine::makeRoom(uint16_t hl, uint16_t bc) {
    testRoom(bc);
    uint16_t oldEnd = word(STKEND);
    pointers(hl, bc);
    uint32_t count = uint32_t(oldEnd) - hl + 1;
    std::memmove(&mem[uint32_t(hl) + bc], &mem[hl], count);
    mrHL = uint16_t(hl - 1);
    mrDE = uint16_t(hl + bc - 1);
}

// L1664 POINTERS: the fourteen dynamic pointers from VARS to STKEND that
// are above pos move by delta.
void Machine::pointers(uint16_t pos, int delta) {
    for (uint16_t p = VARS; p <= STKEND; p += 2) {
        uint16_t v = word(p);
        if (v > pos) setWord(p, uint16_t(v + delta));
    }
}

// L1695 LINE-NO: the line number at hl, or that of the previous line if hl
// is the end of the program, or zero.
uint16_t Machine::lineNo(uint16_t hl, uint16_t prevDe) {
    if (mem[hl] & 0xC0) {
        hl = prevDe;
        if (mem[hl] & 0xC0) return 0;                 // LINE-ZERO
    }
    return uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)]);
}

// L16B0 SET-MIN
void Machine::setMin() {
    uint16_t hl = word(E_LINE);
    mem[hl] = 0x0D;
    setWord(K_CUR, hl);
    mem[uint16_t(hl + 1)] = 0x80;
    setWord(WORKSP, uint16_t(hl + 2));
    setWork();
}

// L16BF SET-WORK
void Machine::setWork() {
    setWord(STKBOT, word(WORKSP));
    setStk();
}

// L16C5 SET-STK
void Machine::setStk() {
    setWord(STKEND, word(STKBOT));
    setWord(MEM, MEMBOT);
    calc.clear();
}

// L16E5 CLOSE #
void Machine::closeCmd() {
    int s = findInt1();
    uint16_t bc;
    uint16_t hl = strData(s, bc);
    if (bc == 0) error(0x17);                         // GW CL_FIX: already closed
    // CLOSE-2 finds the channel letter; K, S and P have nothing to do.
    char letter = (char)mem[uint16_t(word(CHANS) + bc + 3)];
    if (letter != 'K' && letter != 'S' && letter != 'P') error(0x17);
    // Streams 0..3 get their initial channel back, others are closed.
    static const uint16_t initial[4] = {0x0001, 0x0001, 0x0006, 0x0010};
    setWord(hl, s < 4 ? initial[s] : 0);
}

// L171E STR-DATA: returns the address of the STRMS entry, bc = its offset.
uint16_t Machine::strData(int stream, uint16_t& bc) {
    if (stream >= 16) error(0x17);
    uint16_t hl = uint16_t(STRMS + (stream + 3) * 2);
    bc = word(hl);
    return hl;
}

// L1736 OPEN #  (the channel string and the stream are on the stack)
void Machine::openCmd() {
    CalcValue chan = calc.back();
    calc.pop_back();
    int s = findInt1();
    uint16_t bc;
    uint16_t hl = strData(s, bc);
    if (bc != 0) {
        char letter = (char)mem[uint16_t(word(CHANS) + bc + 3)];
        if (letter != 'K' && letter != 'S' && letter != 'P') error(0x17);
    }
    // OPEN-2
    if (chan.str.empty()) error(0x0E);                // REPORT-Fb Invalid file name
    uint8_t e;
    switch (chan.str[0] & 0xDF) {
    case 'K': e = 0x01; break;
    case 'S': e = 0x06; break;
    case 'P': e = 0x10; break;
    default: error(0x0E);
    }
    if (chan.str.size() != 1) error(0x0E);            // OPEN-END
    setWord(hl, e);
}

// ---------------------------------------------------------------------------
// Line and variable searching.
// ---------------------------------------------------------------------------

// L196E LINE-ADDR: address of the line, or of the following line.
// prev = address of the previous line; exact = line found.
uint16_t Machine::lineAddr(uint16_t lineNumber, uint16_t& prev, bool& exact) {
    uint16_t hl = word(PROG);
    prev = hl;
    for (;;) {
        int r = cpLines(hl, lineNumber);
        if (r >= 0) { exact = (r == 0); return hl; }
        prev = hl;
        hl = nextOne(hl);
    }
}

// L1980 CP-LINES: compare the line number at hl with bc.
// Returns <0 if the addressed line is lower ("carry"), 0 if equal.
int Machine::cpLines(uint16_t hl, uint16_t bc) {
    int a = mem[hl];
    if (a != (bc >> 8)) return a < (bc >> 8) ? -1 : 1;
    int l = mem[uint16_t(hl + 1)];
    if (l != (bc & 0xFF)) return l < (bc & 0xFF) ? -1 : 1;
    return 0;
}

// L198B EACH-STMT: step through the statements of a line, either to the
// d'th statement or to the token e.  CH_ADD follows the scan.
int Machine::eachStmt(uint16_t hl, uint8_t& d, uint8_t e) {
    setWord(CH_ADD, hl);
    bool quotes = false;                              // C register
    for (;;) {
        if (--d == 0) return 0;                       // EACH-S-1
        uint8_t a = nextChar();
        if (a == e) return 1;
        hl = word(CH_ADD);
        for (;;) {
            if (a == 0x0E) { hl = uint16_t(hl + 6); a = mem[hl]; }  // EACH-S-3 / NUMBER
            setWord(CH_ADD, hl);
            if (a == '"') quotes = !quotes;
            if ((a == ':' || a == tok::THEN) && !quotes) break;     // EACH-S-5
            if (a == 0x0D) { --d; return 2; }                       // EACH-S-6
            ++hl;                                                   // EACH-S-2
            a = mem[hl];
        }
    }
}

// L19B8 NEXT-ONE: the next line or the next variable.
uint16_t Machine::nextOne(uint16_t hl) {
    uint8_t a = mem[hl];
    if (a < 0x40) {                                   // a BASIC line
        return uint16_t(hl + 4 + word(uint16_t(hl + 2)));
    }
    if (!(a & 0x20)) {                                // strings and arrays
        return uint16_t(hl + 3 + word(uint16_t(hl + 1)));
    }
    uint16_t bc = (a & 0x80) && (a & 0x40) ? 0x12 : 5; // FOR-NEXT variable : number
    if ((a & 0x40) == 0) {                            // long name: skip letters
        uint16_t p = hl;
        do { ++p; } while (!(mem[p] & 0x80));
        return uint16_t(p + 1 + bc);
    }
    return uint16_t(hl + 1 + bc);
}

// L19E5 RECLAIM-1: reclaim the bytes from de up to (excluding) hl.
void Machine::reclaim1(uint16_t de, uint16_t hl) { reclaim2(de, uint16_t(hl - de)); }

// L19E8 RECLAIM-2: reclaim bc bytes at hl.
void Machine::reclaim2(uint16_t hl, uint16_t bc) {
    if (bc == 0) return;
    uint16_t oldEnd = word(STKEND);
    pointers(hl, -int(bc));
    uint32_t count = uint32_t(oldEnd) + 1 - (uint32_t(hl) + bc);
    std::memmove(&mem[hl], &mem[uint32_t(hl) + bc], count);
}

// L19FB E-LINE-NO: the line number of the edit line, 0 for a command.
uint16_t Machine::eLineNo() {
    setWord(CH_ADD, uint16_t(word(E_LINE) - 1));
    nextChar();
    double v = intToFp();
    if (v > 9999) {
        error(0x0B);                                  // REPORT-C
    }
    return uint16_t(v);
}

// L1A1B OUT-NUM-1: print bc without leading zeros (direct command: 0).
void Machine::outNum1(uint16_t bc) {
    if (bit15(bc)) { outCode(0); return; }
    std::string s = std::to_string(bc);
    for (char ch : s) outCode(uint8_t(ch - '0'));
}

// L1A28 OUT-NUM-2: print the line number at hl, right aligned in 4 places
// (OUT-SP-NO prints a space for each leading zero).
void Machine::outNum2(uint16_t hl) {
    unsigned v = unsigned((mem[hl] << 8) | mem[uint16_t(hl + 1)]);
    bool started = false;
    for (unsigned d : {1000u, 100u, 10u}) {
        unsigned digit = v / d;
        v -= digit * d;
        if (digit == 0 && !started) {
            outChar(' ');                             // OUT-SP-2
        } else {
            started = true;
            outCode(uint8_t(digit));
        }
    }
    outCode(uint8_t(v));
}

// L1F05 TEST-ROOM: there must be room for bc bytes plus 80 bytes below the
// machine stack.  The calculator stack counts five bytes per entry.
void Machine::testRoom(uint32_t bc) {
    uint32_t end = uint32_t(word(STKEND)) + 5 * calc.size() + bc + 80;
    if (end >= virtualSp()) error3(0x03);             // REPORT-4 Out of memory
}

// L1F1A free-mem: PRINT 65536-USR 7962 gives the free memory.
uint16_t Machine::freeMem() {
    uint32_t end = uint32_t(word(STKEND)) + 5 * calc.size() + 80;
    return uint16_t(end - virtualSp());
}

// USR with a number: there is no Z80, so a few well known ROM routines are
// available and everything else returns at once with BC = the address,
// exactly what happens when USR calls a RET instruction.
void Machine::usrCall(uint16_t addr, uint16_t& bc) {
    bc = addr;
    switch (addr) {
    case START:
        startNew(false);
        throw RestartMain{};
    case NEW:
        newCmd();
        break;
    case CLS:
        cls();
        break;
    case CL_SC_ALL:
        clScAll();
        break;
    case COPY:
        copyCmd();
        break;
    case FREE_MEM:
        bc = freeMem();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// The interrupt, timing and the host.
// ---------------------------------------------------------------------------

// Bring the screen geometry in line with the options or the host.
void Machine::syncGeometry(bool force) {
    int w = opts.columns, h = opts.rows;
    if (w <= 0 || h <= 0) {
        int hw = 0, hh = 0;
        host.screenSize(hw, hh);
        if (w <= 0) w = hw;
        if (h <= 0) h = hh;
    }
    w = std::clamp(w, 16, 250);
    h = std::clamp(h, 4, 250);
    if (force || w != W || h != H) resizeScreen(w, h);
}

// A new window size.  The upper screen stays at the top and the lower
// screen at the bottom.  The ROM counts lines from the bottom and columns
// from the right, so the print positions move by the change in size.
void Machine::resizeScreen(int w, int h) {
    int oldW = W, oldH = H;
    W = w;
    H = h;
    if (word(CHANS) == 0) {                                    // before START
        screen.resize(W, H);
        return;
    }
    int dfsz = std::min<int>(mem[DF_SZ], std::min(oldH, H) - 1);
    Screen old = screen;
    screen = Screen(W, H);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            Cell& cell = screen.at(r, c);
            cell.attr = r >= H - dfsz ? mem[BORDCR] : mem[ATTR_P];
        }
    for (int r = 0; r < std::min(oldH - dfsz, H - dfsz); ++r)
        for (int c = 0; c < std::min(oldW, W); ++c) screen.at(r, c) = old.at(r, c);
    for (int k = 1; k <= dfsz; ++k)
        for (int c = 0; c < std::min(oldW, W); ++c) screen.at(H - k, c) = old.at(oldH - k, c);
    mem[DF_SZ] = uint8_t(dfsz);
    int dW = W - oldW, dH = H - oldH;
    auto adjust = [&](uint16_t var, int minLine) {
        int c = mem[var] + dW, b = mem[var + 1] + dH;
        c = std::clamp(c, 1, W + 1);
        b = std::clamp(b, minLine, H);
        mem[var] = uint8_t(c);
        mem[var + 1] = uint8_t(b);
    };
    adjust(S_POSN, dfsz + 1);
    adjust(SPOSNL, 1);
    adjust(ECHO_E, 1);
    screen.touch();
}

// L0038 MASK-INT: the 50 Hz interrupt updates FRAMES and reads the keyboard.
void Machine::interrupt() {
    auto now = Clock::now();
    uint64_t frames = framesBase +
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count() / 20;
    mem[FRAMES] = uint8_t(frames);
    mem[FRAMES + 1] = uint8_t(frames >> 8);
    mem[FRAMES + 2] = uint8_t(frames >> 16);
    lastInterrupt = now;
    // L02BF KEYBOARD.  Keys typed while editing (type-ahead, paste) are
    // queued and handed to the editor one at a time; while a program runs,
    // the latest key wins, like on the real keyboard.
    if (!queueKeys) {
        while (!runKeys.empty()) {
            KeyEvent ev = runKeys.front();
            runKeys.pop_front();
            int code = decodeKey(ev, flag(FLAGS, F_K_L_MODE), mem[MODE], false);
            if (code < 0) continue;
            mem[LAST_K] = uint8_t(code);
            setFlag(FLAGS, F_NEW_KEY, true);
        }
        return;
    }
    runKeys.clear();
    while (!flag(FLAGS, F_NEW_KEY)) {
        if (!codeQueue.empty()) {
            mem[LAST_K] = codeQueue.front();
            codeQueue.pop_front();
            setFlag(FLAGS, F_NEW_KEY, true);
            break;
        }
        if (keyQueue.empty()) break;
        KeyEvent ev = keyQueue.front();
        keyQueue.pop_front();
        int code = decodeKey(ev, flag(FLAGS, F_K_L_MODE), mem[MODE], false);
        if (code < 0) continue;
        mem[LAST_K] = uint8_t(code);
        setFlag(FLAGS, F_NEW_KEY, true);
    }
}

void Machine::pollKeys(int timeoutMs) {
    KeyEvent ev;
    if (host.readKey(ev, timeoutMs)) {
        keyEvent(ev);
        while (host.readKey(ev, 0)) keyEvent(ev);
    }
    lastPoll = Clock::now();
}

void Machine::keyEvent(const KeyEvent& ev) {
    switch (ev.code) {
    case KeyCode::Quit:
        quitPending = true;
        return;
    case KeyCode::Redraw:
        present(true);
        return;
    case KeyCode::Break:
        breakPending = true;
        break;
    default:
        break;
    }
    heldKey = ev;
    heldUntil = Clock::now() + std::chrono::milliseconds(150);
    if (ev.code == KeyCode::Break) return;
    if (queueKeys) {
        keyQueue.push_back(ev);
    } else {
        runKeys.push_back(ev);
        while (runKeys.size() > 64) runKeys.pop_front();
    }
}

void Machine::present(bool force) {
    if (force || screen.version() != presentedVersion || msSince(lastPresent) > 100) {
        host.present(screen);
        presentedVersion = screen.version();
        lastPresent = Clock::now();
    }
}

// Wait for something to happen (keys, time), keeping the display alive.
void Machine::idle(int ms) {
    present(false);
    // Keys still waiting in the queue are handled before new ones are read.
    pollKeys(queueKeys && (!keyQueue.empty() || !codeQueue.empty()) ? 0 : ms);
    interrupt();
}

// Called at STMT-RET after every statement.
void Machine::statementTick() {
    ++statements;
    if (opts.throttle > 0) {
        double due = double(statements) * 1000.0 / opts.throttle;
        int ahead = (int)(due - msSince(throttleStart));
        if (ahead > 2) std::this_thread::sleep_for(std::chrono::milliseconds(ahead));
    }
    if ((statements & 63) == 0 || opts.throttle > 0) {
        int since = msSince(lastPoll);
        if (since >= 10) {
            pollKeys(0);
            interrupt();
        }
        if (quit) throw QuitRequest{exitCode};
        if (screen.version() != presentedVersion && msSince(lastPresent) >= 30) present(true);
    }
}

void Machine::queueCommand(const std::string& utf8) { pendingCommands.push_back(fromUtf8(utf8)); }

void Machine::requestQuit(int code) {
    quit = true;
    exitCode = code;
}

int Machine::run() {
    try {
        mainLoop();
    } catch (QuitRequest& q) {
        exitCode = q.exitCode;
    }
    present(true);
    return exitCode;
}

// PEEK and POKE: the attribute file maps onto the top left 32x24 cells.
uint8_t Machine::peekUser(uint16_t addr) {
    if (addr >= DISPLAY && addr < ATTRS) return peekDisplay(addr);
    if (addr >= ATTRS && addr < PRBUFF) {
        int i = addr - ATTRS, row = i / 32, col = i % 32;
        if (row < H && col < W) return screen.at(row, col).attr;
    }
    if (addr == FRAMES || addr == FRAMES + 1 || addr == FRAMES + 2) interrupt();
    if (addr == LAST_K) { pollKeys(0); interrupt(); }
    return mem[addr];
}

void Machine::pokeUser(uint16_t addr, uint8_t v) {
    if (addr < ROM_END) return;                       // ROM
    mem[addr] = v;
    if (addr >= DISPLAY && addr < ATTRS) pokeDisplay(addr, v);
    if (addr >= ATTRS && addr < PRBUFF) {
        int i = addr - ATTRS, row = i / 32, col = i % 32;
        if (row < H && col < W) screen.at(row, col).attr = v;
    }
}

} // namespace zxgw
