// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 7. BASIC LINE AND COMMAND INTERPRETATION
//
// Every statement is interpreted twice in the life of a line: once when the
// line is entered (syntax checking, FLAGS bit 7 reset) and every time it is
// executed (runtime).  The same routines serve both, guided by syntaxZ().

#include "machine.h"

#include <cmath>
#include <thread>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;

namespace {

// The routines that the parameter table addresses (classes 00, 03, 05).
enum Routine : uint8_t {
    R_NONE, R_GOTO, R_IF, R_GOSUB, R_STOP, R_RETURN, R_FOR, R_NEXT, R_PRINT,
    R_INPUT, R_DIM, R_REM, R_NEW, R_RUN, R_LIST, R_POKE, R_RANDOMIZE,
    R_CONTINUE, R_CLEAR, R_CLS, R_PLOT, R_PAUSE, R_READ, R_DATA, R_RESTORE,
    R_DRAW, R_COPY, R_LPRINT, R_LLIST, R_BEEP, R_CIRCLE, R_OUT, R_BORDER,
    R_DEF_FN, R_OPEN, R_CLOSE, R_CAT_ETC, R_CAT
};

// L1A48 offst-tbl and L1A7A parameter ("syntax") table.  Bytes below $20
// are command classes, above are required separators; the routine follows
// classes 00, 03 and 05.
struct SyntaxEntry {
    uint8_t params[8];
    Routine routine;
};

constexpr uint8_t END = 0xFF;   // end of the parameter list in this table

const SyntaxEntry kSyntax[50] = {
    /* CE DEF FN   */ {{0x05, END}, R_DEF_FN},
    /* CF CAT      */ {{0x05, END}, R_CAT},          // zxgw: optional directory
    /* D0 FORMAT   */ {{0x0A, 0x00, END}, R_CAT_ETC},
    /* D1 MOVE     */ {{0x0A, ',', 0x0A, 0x00, END}, R_CAT_ETC},
    /* D2 ERASE    */ {{0x0A, 0x00, END}, R_CAT_ETC},
    /* D3 OPEN #   */ {{0x06, ',', 0x0A, 0x00, END}, R_OPEN},
    /* D4 CLOSE #  */ {{0x06, 0x00, END}, R_CLOSE},
    /* D5 MERGE    */ {{0x0B, END}, R_NONE},
    /* D6 VERIFY   */ {{0x0B, END}, R_NONE},
    /* D7 BEEP     */ {{0x08, 0x00, END}, R_BEEP},
    /* D8 CIRCLE   */ {{0x09, 0x05, END}, R_CIRCLE},
    /* D9 INK      */ {{0x07, END}, R_NONE},
    /* DA PAPER    */ {{0x07, END}, R_NONE},
    /* DB FLASH    */ {{0x07, END}, R_NONE},
    /* DC BRIGHT   */ {{0x07, END}, R_NONE},
    /* DD INVERSE  */ {{0x07, END}, R_NONE},
    /* DE OVER     */ {{0x07, END}, R_NONE},
    /* DF OUT      */ {{0x08, 0x00, END}, R_OUT},
    /* E0 LPRINT   */ {{0x05, END}, R_LPRINT},
    /* E1 LLIST    */ {{0x05, END}, R_LLIST},
    /* E2 STOP     */ {{0x00, END}, R_STOP},
    /* E3 READ     */ {{0x05, END}, R_READ},
    /* E4 DATA     */ {{0x05, END}, R_DATA},
    /* E5 RESTORE  */ {{0x03, END}, R_RESTORE},
    /* E6 NEW      */ {{0x00, END}, R_NEW},
    /* E7 BORDER   */ {{0x06, 0x00, END}, R_BORDER},
    /* E8 CONTINUE */ {{0x00, END}, R_CONTINUE},
    /* E9 DIM      */ {{0x05, END}, R_DIM},
    /* EA REM      */ {{0x05, END}, R_REM},
    /* EB FOR      */ {{0x04, '=', 0x06, tok::TO, 0x06, 0x05, END}, R_FOR},
    /* EC GO TO    */ {{0x06, 0x00, END}, R_GOTO},
    /* ED GO SUB   */ {{0x06, 0x00, END}, R_GOSUB},
    /* EE INPUT    */ {{0x05, END}, R_INPUT},
    /* EF LOAD     */ {{0x0B, END}, R_NONE},
    /* F0 LIST     */ {{0x05, END}, R_LIST},
    /* F1 LET      */ {{0x01, '=', 0x02, END}, R_NONE},
    /* F2 PAUSE    */ {{0x06, 0x00, END}, R_PAUSE},
    /* F3 NEXT     */ {{0x04, 0x00, END}, R_NEXT},
    /* F4 POKE     */ {{0x08, 0x00, END}, R_POKE},
    /* F5 PRINT    */ {{0x05, END}, R_PRINT},
    /* F6 PLOT     */ {{0x09, 0x00, END}, R_PLOT},
    /* F7 RUN      */ {{0x03, END}, R_RUN},
    /* F8 SAVE     */ {{0x0B, END}, R_NONE},
    /* F9 RANDOMIZE*/ {{0x03, END}, R_RANDOMIZE},
    /* FA IF       */ {{0x06, tok::THEN, 0x05, END}, R_IF},
    /* FB CLS      */ {{0x00, END}, R_CLS},
    /* FC DRAW     */ {{0x09, 0x05, END}, R_DRAW},
    /* FD CLEAR    */ {{0x03, END}, R_CLEAR},
    /* FE RETURN   */ {{0x00, END}, R_RETURN},
    /* FF COPY     */ {{0x00, END}, R_COPY},
};

// How a statement ends: STMT-RET (normal), LINE-END (REM, IF false) or
// STMT-L-1 (the statement after THEN).
enum class After { Ret, LineEnd, StmtL1 };

} // namespace

// ---------------------------------------------------------------------------
// L1B17 LINE-SCAN: check the syntax of the edit line.
// ---------------------------------------------------------------------------
void Machine::lineScan() {
    setFlag(FLAGS, F_RUNTIME, false);
    eLineNo();
    mem[SUBPPC] = 0;
    mem[ERR_NR] = 0xFF;
    runStatements(false);
}

// The statement loop: STMT-LOOP, STMT-L-1, STMT-RET, LINE-RUN, LINE-NEW,
// LINE-END, LINE-USE, NEXT-LINE and STMT-NEXT.
void Machine::runStatements(bool fromLineRun) {
    enum State { LINE_RUN, NEXT_LINE, STMT_LOOP, STMT_L1, STMT_RET, STMT_NEXT, LINE_END, LINE_USE };
    State s = fromLineRun ? LINE_RUN : STMT_L1;
    uint16_t hl = 0, de = 0;
    uint8_t a = 0;
    for (;;) {
        switch (s) {
        case LINE_RUN:                                         // L1B8A
            setWord(PPC, 0xFFFE);
            hl = uint16_t(word(WORKSP) - 1);
            de = uint16_t(word(E_LINE) - 1);
            a = mem[NSPPC];
            s = NEXT_LINE;
            break;

        case NEXT_LINE: {                                      // L1BD1
            setWord(NXTLIN, hl);
            setWord(CH_ADD, de);
            uint8_t d = a;
            mem[NSPPC] = 0xFF;
            --d;
            mem[SUBPPC] = d;
            if (d == 0) { s = STMT_LOOP; break; }
            ++d;
            int r = eachStmt(de, d, 0);
            if (r == 0 || (r == 2 && d == 0)) { s = STMT_NEXT; break; }
            error(0x16);                                       // REPORT-N
        }

        case STMT_LOOP:                                        // L1B28
            nextChar();
            [[fallthrough]];
        case STMT_L1: {                                        // L1B29
            setWork();
            if (++mem[SUBPPC] & 0x80) error(0x0B);
            a = getChar();
            if (a == 0x0D) { s = LINE_END; break; }
            if (a == ':') { s = STMT_LOOP; break; }
            After after = After::Ret;
            try {
                if (a < tok::DEF_FN) {
                    // zxgw: a statement starting with a name may be an
                    // extension command, otherwise "Nonsense in BASIC".
                    extensionCommand();
                } else {
                    nextChar();
                    uint8_t token = a;
                    curToken = token;
                    const SyntaxEntry& sx = kSyntax[token - tok::DEF_FN];
                    after = After::Ret;
                    for (int i = 0; sx.params[i] != END; ++i) {   // SCAN-LOOP
                        uint8_t p = sx.params[i];
                        if (p >= 0x20) { separator(p); continue; }
                        getChar();
                        bool callRoutine = false;
                        switch (p) {                           // class-tbl
                        case 0x00: checkEnd(); callRoutine = true; break;
                        case 0x01: class01(); break;
                        case 0x02: class02(); break;
                        case 0x03: fetchNum(); checkEnd(); callRoutine = true; break;
                        case 0x04: class04(); break;
                        case 0x05: callRoutine = true; break;
                        case 0x06: expt1Num(); break;
                        case 0x07: class07(token); break;
                        case 0x08: expt2Num(); break;
                        case 0x09: class09(); break;
                        case 0x0A: exptExp(); break;
                        case 0x0B: saveEtc(); break;
                        }
                        if (!callRoutine) continue;
                        switch (sx.routine) {
                        case R_GOTO: goToCmd(); break;
                        case R_IF: {
                            // L1CF0 IF: false skips the rest of the line.
                            if (!syntaxZ()) {
                                double v = popNum();
                                if (v == 0) { after = After::LineEnd; break; }
                            }
                            after = After::StmtL1;
                            break;
                        }
                        case R_GOSUB: goSubCmd(); break;
                        case R_STOP: stopCmd(); break;
                        case R_RETURN: returnCmd(); break;
                        case R_FOR: forCmd(); break;
                        case R_NEXT: nextCmd(); break;
                        case R_PRINT: printCmd(2); break;
                        case R_INPUT: inputCmd(); break;
                        case R_DIM: dimCmd(); break;
                        case R_REM: remCmd(); after = After::LineEnd; break;
                        case R_NEW: newCmd(); break;
                        case R_RUN: runCmd(); break;
                        case R_LIST: listCmd(2); break;
                        case R_POKE: pokeCmd(); break;
                        case R_RANDOMIZE: randomizeCmd(); break;
                        case R_CONTINUE: continueCmd(); break;
                        case R_CLEAR: clearCmd(); break;
                        case R_CLS: cls(); break;
                        case R_PLOT: plotCmd(); break;
                        case R_PAUSE: pauseCmd(); break;
                        case R_READ: readCmd(); break;
                        case R_DATA: dataCmd(); break;
                        case R_RESTORE: restoreCmd(); break;
                        case R_DRAW: drawCmd(); break;
                        case R_COPY: copyCmd(); break;
                        case R_LPRINT: lprintCmd(); break;
                        case R_LLIST: llistCmd(); break;
                        case R_BEEP: beepCmd(); break;
                        case R_CIRCLE: circleCmd(); break;
                        case R_OUT: outCmd(); break;
                        case R_BORDER: borderCmd(); break;
                        case R_DEF_FN: defFnCmd(); break;
                        case R_OPEN: openCmd(); break;
                        case R_CLOSE: closeCmd(); break;
                        case R_CAT_ETC: catEtc(token); break;
                        case R_CAT: catEtc(token); break;
                        case R_NONE: break;
                        }
                        break;
                    }
                }
            } catch (SyntaxStatementEnd&) {
                s = STMT_NEXT;                                 // CHECK-END while checking
                break;
            }
            if (after == After::LineEnd) { s = LINE_END; break; }
            if (after == After::StmtL1) { s = STMT_L1; break; }
            s = syntaxZ() ? STMT_NEXT : STMT_RET;
            break;
        }

        case STMT_RET: {                                       // L1B76
            statementTick();
            if (breakKey()) error(0x14);                       // REPORT-L
            if (mem[NSPPC] & 0x80) { s = STMT_NEXT; break; }
            hl = word(NEWPPC);
            if (bit15(hl)) { s = LINE_RUN; break; }            // a direct command
            // LINE-NEW
            uint16_t prev;
            bool exact;
            hl = lineAddr(hl, prev, exact);
            a = mem[NSPPC];
            if (exact) { s = LINE_USE; break; }
            if (a != 0) error(0x16);                           // REPORT-N
            if ((mem[hl] & 0xC0) == 0) { s = LINE_USE; break; }
            error(0xFF);                                       // REPORT-0 OK
        }

        case STMT_NEXT:                                        // L1BF4
            a = getChar();
            if (a == 0x0D) { s = LINE_END; break; }
            if (a == ':') { s = STMT_LOOP; break; }
            error(0x0B);

        case LINE_END:                                         // L1BB3
            if (syntaxZ()) return;
            hl = word(NXTLIN);
            if (mem[hl] & 0xC0) return;                        // end of program
            a = 0;
            [[fallthrough]];
        case LINE_USE: {                                       // L1BBF
            if (a == 0) a = 1;
            setWord(PPC, uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)]));
            uint16_t length = word(uint16_t(hl + 2));
            de = uint16_t(hl + 3);
            hl = uint16_t(hl + 4 + length);
            s = NEXT_LINE;
            break;
        }
        }
    }
}

// L1BEE CHECK-END: in syntax mode the statement is finished here.
void Machine::checkEnd() {
    if (!syntaxZ()) return;
    uint8_t a = getChar();
    if (a != 0x0D && a != ':') error(0x0B);                    // STMT-NEXT
    throw SyntaxStatementEnd{};
}

// L1B6F SEPARATOR
void Machine::separator(uint8_t c) {
    if (getChar() != c) error(0x0B);
    nextChar();
}

// ---------------------------------------------------------------------------
// The command classes.
// ---------------------------------------------------------------------------

// L1C1F CLASS-01: a variable is required (for an assignment).
void Machine::class01() {
    uint16_t hl;
    bool notFound, arrayZ;
    uint8_t c = lookVars(hl, notFound, arrayZ);
    varA1(hl, notFound, arrayZ, c);
}

// L1C22 VAR-A-1: remember the destination of an assignment in DEST,
// STRLEN and FLAGX.
void Machine::varA1(uint16_t hl, bool notFound, bool arrayZ, uint8_t c) {
    uint16_t bc = c;
    mem[FLAGX] = 0;
    if (notFound) {
        setFlag(FLAGX, FX_NEW_VARIABLE, true);
        if (arrayZ) error(0x01);                               // REPORT-2
    } else {
        // VAR-A-2
        if (arrayZ) {
            uint16_t elem = hl;
            stkVar(hl, c, elem);
            hl = elem;
        }
        if (!flag(FLAGS, F_NUMERIC)) {
            uint8_t kind = 0;
            if (!syntaxZ()) {
                CalcValue v = stkFetch();
                kind = v.kind;
                hl = v.start;
                bc = v.len;
            }
            mem[FLAGX] |= kind;
        }
    }
    setWord(STRLEN, bc);                                       // VAR-A-3
    setWord(DEST, hl);
}

// L1C4E CLASS-02: an expression, then the assignment.
void Machine::class02() {
    valFet1();
    checkEnd();
}

// L1C56 VAL-FET-1
void Machine::valFet1() { valFet2(mem[FLAGS]); }

// L1C59 VAL-FET-2: evaluate and assign; the type must match bit 6 of a.
void Machine::valFet2(uint8_t a) {
    scanning();
    uint8_t d = mem[FLAGS];
    if ((a ^ d) & 0x40) error(0x0B);
    if (d & F_RUNTIME) letCmd();
}

// L1C6C CLASS-04: a single character numeric variable (FOR, NEXT).
void Machine::class04() {
    uint16_t hl;
    bool notFound, arrayZ;
    uint8_t c = lookVars(hl, notFound, arrayZ);
    if (uint8_t((c | 0x9F) + 1) != 0) error(0x0B);
    varA1(hl, notFound, arrayZ, c);
}

// L1C79 NEXT-2NUM
void Machine::next2Num() {
    nextChar();
    expt2Num();
}

// L1C7A EXPT-2NUM (CLASS-08)
void Machine::expt2Num() {
    expt1Num();
    if (getChar() != ',') error(0x0B);
    nextChar();
    expt1Num();
}

// L1C82 EXPT-1NUM (CLASS-06)
void Machine::expt1Num() {
    scanning();
    if (!flag(FLAGS, F_NUMERIC)) error(0x0B);
}

// L1C8C EXPT-EXP (CLASS-0A)
void Machine::exptExp() {
    scanning();
    if (flag(FLAGS, F_NUMERIC)) error(0x0B);
}

// L1C96 CLASS-07: INK, PAPER, FLASH, BRIGHT, INVERSE, OVER as commands.
void Machine::class07(uint8_t token) {
    if (!syntaxZ()) chanOpen(-2);                              // GW: channel 'S'
    coTemp4(token);
    checkEnd();
    setWord(ATTR_P, word(ATTR_T));
    uint8_t p = mem[P_FLAG];
    uint8_t rl = uint8_t((p << 1) | (p >> 7));
    mem[P_FLAG] = uint8_t(((rl ^ p) & 0xAA) ^ p);
}

// L1CBE CLASS-09: two coordinates with optional colour items (PLOT etc.).
void Machine::class09() {
    if (!syntaxZ()) {
        chanOpen(-2);                                          // GW: channel 'S'
        mem[MASK_T] |= 0xF8;
        setFlag(P_FLAG, 0x40, false);
        getChar();
    }
    coTemp2();                                                 // CL-09-1
    expt2Num();
}

// L1CDE FETCH-NUM (CLASS-03): a number or zero.
void Machine::fetchNum() {
    uint8_t a = getChar();
    if (a == 0x0D || a == ':') { useZero(); return; }
    expt1Num();
}

// L1CE6 USE-ZERO
void Machine::useZero() {
    if (syntaxZ()) return;
    stack(0);
}

// ---------------------------------------------------------------------------
// Commands.
// ---------------------------------------------------------------------------

// GW TOGGLE: STOP as a direct command switches between the GW keyword
// entry (keywords are typed as words) and the classic 'K' mode.
void Machine::stopCmd() {
    if (bit15(word(PPC))) {
        mem[FLAGS2] ^= F2_GW_CLASSIC;
        impose();
    }
    error(0x08);                                               // REPORT-9
}

// L1D03 FOR
void Machine::forCmd() {
    uint8_t a = getChar();
    if (a == tok::STEP) {
        nextChar();
        expt1Num();
        checkEnd();
    } else {
        checkEnd();                                            // F-USE-1
        stack(1);
    }
    // F-REORDER: v, l, s -> l, s, v
    CalcValue s = calc.back(); calc.pop_back();
    CalcValue l = calc.back(); calc.pop_back();
    CalcValue v = calc.back(); calc.pop_back();
    calc.push_back(l);
    calc.push_back(s);
    calc.push_back(v);
    uint16_t hl = letCmd();
    setWord(MEM, hl);
    --hl;
    uint8_t name = mem[hl];
    mem[hl] |= 0x80;
    hl = uint16_t(hl + 6);
    if (!(name & 0x80)) {
        makeRoom(hl, 13);
        hl = uint16_t(mrHL + 1);
    }
    // F-L-S
    double step = popNum();
    double limit = popNum();
    storeNum(hl, limit);
    storeNum(uint16_t(hl + 5), step);
    setWord(uint16_t(hl + 10), word(PPC));
    mem[uint16_t(hl + 12)] = uint8_t(mem[SUBPPC] + 1);
    if (!nextLoop(word(MEM))) return;
    // No loop: find the matching NEXT.
    uint8_t b = mem[STRLEN];
    setWord(NEWPPC, word(PPC));
    uint8_t d = uint8_t(-mem[SUBPPC]);
    hl = word(CH_ADD);
    for (;;) {                                                 // F-LOOP
        uint16_t bc = word(NXTLIN);
        bool found = lookProg(hl, tok::NEXT, d, bc);
        setWord(NXTLIN, bc);
        if (!found) error(0x11);                               // REPORT-I
        uint8_t c = uint8_t(nextChar() | 0x20);
        if (c == b) break;
        nextChar();
        hl = word(CH_ADD);
    }
    nextChar();                                                // F-FOUND
    mem[NSPPC] = uint8_t(1 - d);
}

// L1D86 LOOK-PROG: find the token e from hl on.  CH_ADD is left at the
// token and NEWPPC holds its line; bc is updated to the end of that line.
bool Machine::lookProg(uint16_t hl, uint8_t e, uint8_t& d, uint16_t& bc) {
    bool inLine = mem[hl] == ':';
    for (;;) {
        if (!inLine) {                                         // LOOK-P-1
            ++hl;
            if (mem[hl] & 0xC0) return false;
            setWord(NEWPPC, uint16_t((mem[hl] << 8) | mem[uint16_t(hl + 1)]));
            hl = uint16_t(hl + 3);
            bc = uint16_t(hl + word(uint16_t(hl - 1)));
            d = 0;
        }
        inLine = false;
        int r = eachStmt(hl, d, e);                            // LOOK-P-2
        if (r != 2) return true;
        hl = word(CH_ADD);
    }
}

// L1DAB NEXT
void Machine::nextCmd() {
    if (flag(FLAGX, FX_NEW_VARIABLE)) error(0x01);             // REPORT-2
    uint16_t hl = word(DEST);
    if (!(mem[hl] & 0x80)) error(0x00);                        // REPORT-1
    ++hl;
    setWord(MEM, hl);
    double v = checked(stackNum(hl) + stackNum(uint16_t(hl + 10)));
    storeNum(hl, v);
    if (nextLoop(hl)) return;
    goTo2(word(uint16_t(hl + 15)), mem[uint16_t(hl + 17)]);
}

// L1DDA NEXT-LOOP: true (carry) if the loop is finished.
bool Machine::nextLoop(uint16_t memAddr) {
    double l = stackNum(uint16_t(memAddr + 5));
    double v = stackNum(memAddr);
    double s = stackNum(uint16_t(memAddr + 10));
    double diff = s < 0 ? checked(l - v) : checked(v - l);
    return diff > 0;
}

// L1DED READ
void Machine::readCmd() {
    for (;;) {
        class01();
        if (!syntaxZ()) {
            getChar();
            setWord(X_PTR, word(CH_ADD));
            uint16_t hl = word(DATADD);
            if (mem[hl] != ',') {
                uint8_t d = 0;
                uint16_t bc = 0;
                if (!lookProg(hl, tok::DATA, d, bc)) error(0x0D);  // REPORT-E
                hl = word(CH_ADD);
            }
            tempPtr1(hl);                                      // READ-1
            valFet1();
            getChar();
            setWord(DATADD, word(CH_ADD));
            uint16_t x = word(X_PTR);
            mem[X_PTR + 1] = 0;
            tempPtr2(x);
        }
        uint8_t a = getChar();                                 // READ-2
        if (a != ',') break;
        nextChar();                                            // READ-3
    }
    checkEnd();
}

// L1E27 DATA
void Machine::dataCmd() {
    if (!syntaxZ()) { passBy(tok::DATA); return; }
    for (;;) {                                                 // DATA-1
        scanning();
        if (getChar() != ',') checkEnd();
        nextChar();
    }
}

// L1E39 PASS-BY: skip a DATA or DEF FN statement at runtime.
void Machine::passBy(uint8_t token) {
    uint16_t hl = word(CH_ADD);
    while (mem[hl] != token) --hl;                             // CPDR
    --hl;
    uint8_t d = 2;
    eachStmt(hl, d, 0);
}

// L1E42 RESTORE
void Machine::restoreCmd() { restRun(findLine()); }

// L1E45 REST-RUN
void Machine::restRun(uint16_t line) {
    uint16_t prev;
    bool exact;
    uint16_t hl = lineAddr(line, prev, exact);
    setWord(DATADD, uint16_t(hl - 1));
}

// L1E4F RANDOMIZE
void Machine::randomizeCmd() {
    uint16_t bc = findInt2();
    if (bc == 0) {
        interrupt();
        bc = word(FRAMES);
    }
    setWord(SEED, bc);
}

// L1E5F CONTINUE
void Machine::continueCmd() { goTo2(word(OLDPPC), mem[OSPCC]); }

// L1E67 GO TO
void Machine::goToCmd() { goTo2(findLine(), 0); }

// L1E73 GO-TO-2
void Machine::goTo2(uint16_t line, uint8_t stmt) {
    setWord(NEWPPC, line);
    mem[NSPPC] = stmt;
}

// L1E7A OUT: there is no hardware behind the ports.
void Machine::outCmd() {
    uint8_t a;
    uint16_t bc;
    twoParam(a, bc);
}

// L1E80 POKE
void Machine::pokeCmd() {
    uint8_t a;
    uint16_t bc;
    twoParam(a, bc);
    pokeUser(bc, a);
}

// L1E85 TWO-PARAM: a byte (negative values allowed) and a word.
void Machine::twoParam(uint8_t& a, uint16_t& bc) {
    bool over, neg;
    a = fpToA(over, neg);
    if (over) error(0x0A);
    if (neg) a = uint8_t(-a);
    bc = findInt2();
}

// L1E94 FIND-INT1
uint8_t Machine::findInt1() {
    bool over, neg;
    uint8_t a = fpToA(over, neg);
    if (over || neg) error(0x0A);                              // REPORT-Bb
    return a;
}

// L1E99 FIND-INT2
uint16_t Machine::findInt2() {
    bool over, neg;
    uint16_t bc = fpToBc(over, neg);
    if (over || neg) error(0x0A);
    return bc;
}

// GW FIND_LINE: a line number 0..16383.
uint16_t Machine::findLine() {
    uint16_t bc = findInt2();
    if (bc >= 0x4000) error(0x0A);
    return bc;
}

// L1EA1 RUN
void Machine::runCmd() {
    goToCmd();
    restRun(0);
    clearRun(0);
}

// L1EAC CLEAR
void Machine::clearCmd() { clearRun(findInt2()); }

// L1EAF CLEAR-RUN
void Machine::clearRun(uint16_t bc) {
    if (bc == 0) bc = word(RAMTOP);
    reclaim1(word(VARS), uint16_t(word(E_LINE) - 1));
    fnStrings.clear();
    cls();
    uint32_t hl = uint32_t(word(STKEND)) + 50;
    if (hl >= bc) error(0x15);                                 // REPORT-M
    if (word(P_RAMT) < bc) error(0x15);
    setWord(RAMTOP, bc);                                       // CLEAR-2
    mem[bc] = 0x3E;
    gosubSp = uint16_t(bc - 1);
    setWord(ERR_SP, uint16_t(bc - 3));
}

// L1EED GO SUB: the GO SUB stack lives just below RAMTOP, three bytes per
// entry: line number (low, high) and statement.
void Machine::goSubCmd() {
    gosubSp = uint16_t(gosubSp - 3);
    setWord(gosubSp, word(PPC));
    mem[uint16_t(gosubSp + 2)] = uint8_t(mem[SUBPPC] + 1);
    setWord(ERR_SP, uint16_t(gosubSp - 2));
    goToCmd();
    testRoom(0x14);
}

// L1F23 RETURN
void Machine::returnCmd() {
    uint16_t de = word(gosubSp);
    if ((de >> 8) == 0x3E) error(0x06);                        // REPORT-7
    uint8_t stmt = mem[uint16_t(gosubSp + 2)];
    gosubSp = uint16_t(gosubSp + 3);
    setWord(ERR_SP, uint16_t(gosubSp - 2));
    goTo2(de, stmt);
}

// L1F3A PAUSE: n frames (1/50 s) or until a key is pressed; 0 = no limit.
void Machine::pauseCmd() {
    uint16_t n = findInt2();
    present(true);
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        interrupt();
        if (flag(FLAGS, F_NEW_KEY) || breakPending) break;     // PAUSE-2
        int wait = 40;
        if (n) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            long left = long(n) * 20 - (long)elapsed;
            if (left <= 0) break;
            if (left < wait) wait = int(left);
        }
        idle(wait);
    }
    setFlag(FLAGS, F_NEW_KEY, false);                          // PAUSE-END
}

// L1F54 BREAK-KEY: ESC or Ctrl-C in the terminal.
bool Machine::breakKey() {
    if (breakPending) {
        breakPending = false;
        return true;
    }
    return false;
}

// L1F60 DEF FN
void Machine::defFnCmd() {
    if (!syntaxZ()) { passBy(tok::DEF_FN); return; }
    setFlag(FLAGS, F_NUMERIC, true);                           // DEF-FN-1
    uint8_t a = getChar();
    if (!alpha(a)) error(0x0B);
    a = nextChar();
    if (a == '$') {
        setFlag(FLAGS, F_NUMERIC, false);
        a = nextChar();
    }
    if (a != '(') error(0x0B);                                 // DEF-FN-2
    a = nextChar();
    if (a != ')') {
        for (;;) {                                             // DEF-FN-3
            if (!alpha(a)) error(0x0B);
            uint16_t de = word(CH_ADD);
            a = nextChar();
            if (a == '$') {
                de = word(CH_ADD);
                a = nextChar();
            }
            makeRoom(de, 6);                                   // DEF-FN-5
            mem[uint16_t(mrHL + 2)] = 0x0E;
            if (a != ',') break;
            a = nextChar();
        }
    }
    if (a != ')') error(0x0B);                                 // DEF-FN-6
    a = nextChar();
    if (a != '=') error(0x0B);
    nextChar();
    uint8_t flags = mem[FLAGS];
    scanning();
    if ((flags ^ mem[FLAGS]) & 0x40) error(0x0B);              // DEF-FN-7
    checkEnd();
}

// L1FC9 LPRINT
void Machine::lprintCmd() { printCmd(3); }

// L1FCD PRINT
void Machine::printCmd(int stream) {
    if (!syntaxZ()) chanOpen(stream);
    temps();
    print2();
    checkEnd();
}

// L1FDF PRINT-2
void Machine::print2() {
    uint8_t a = getChar();
    if (!prEndZ(a)) {
        for (;;) {                                             // PRINT-3
            int r = prPosn1();
            if (r == 2) return;
            if (r == 1) continue;
            prItem1();
            r = prPosn1();
            if (r == 2) return;
            if (r == 1) continue;
            break;
        }
    }
    a = getChar();
    if (a == ')') return;                                      // PRINT-4
    printCr();
}

// L1FF5 PRINT-CR
void Machine::printCr() {
    if (syntaxZ()) return;                                     // UNSTACK-Z
    printA(0x0D);
}

// L1FFC PR-ITEM-1
void Machine::prItem1() {
    uint8_t a = getChar();
    if (a == tok::AT) {
        next2Num();
        if (syntaxZ()) return;
        int b, c, d, e;
        stkToBc(b, c, d, e);
        printA(0x16);                                          // PR-AT-TAB
        printA(uint8_t(c));
        printA(uint8_t(b));
        return;
    }
    if (a == tok::TAB) {                                       // PR-ITEM-2
        nextChar();
        expt1Num();
        if (syntaxZ()) return;
        uint16_t bc = findInt2();
        printA(0x17);
        printA(uint8_t(bc));
        printA(uint8_t(bc >> 8));
        return;
    }
    if (coTemp3()) return;                                     // PR-ITEM-3
    if (strAlter()) return;
    scanning();
    if (syntaxZ()) return;
    if (!flag(FLAGS, F_NUMERIC)) {
        CalcValue v = stkFetch();
        for (uint8_t ch : v.str) printA(ch);                   // PR-STRING
    } else {
        printFp();
    }
}

// L2045 PR-END-Z
bool Machine::prEndZ(uint8_t a) { return a == ')' || prStEnd(a); }

// L2048 PR-ST-END
bool Machine::prStEnd(uint8_t a) { return a == 0x0D || a == ':'; }

// L204E PR-POSN-1: ';' ',' or "'".  Returns 0 if there is none, 1 if one
// was handled, 2 if one was handled and the statement ends there (the ROM
// drops the caller's return address in that case).
int Machine::prPosn1() {
    uint8_t a = getChar();
    if (a == ';') {
    } else if (a == ',') {
        if (!syntaxZ()) printA(0x06);
    } else if (a == '\'') {
        printCr();                                             // PR-POSN-2
    } else {
        return 0;
    }
    a = nextChar();                                            // PR-POSN-3
    return prEndZ(a) ? 2 : 1;
}

// L2070 STR-ALTER: "#stream".  Returns true if a stream was given.
bool Machine::strAlter() {
    if (getChar() != '#') return false;
    nextChar();
    expt1Num();
    if (syntaxZ()) return true;
    uint8_t s = findInt1();
    if (s >= 16) error(0x17);                                  // REPORT-Oa
    chanOpen(s);
    return true;
}

// L2089 INPUT
void Machine::inputCmd() {
    if (!syntaxZ()) {
        clsLower();
        chanOpen(1);                                           // GW
    }
    mem[TV_FLAG] = TV_LOWER;                                   // INPUT-1
    try {
        inItem1();
    } catch (InputBreak&) {
        error(0x10);                                           // zxgw: ESC in INPUT
    }
    checkEnd();
    // The lower screen may have grown into the upper screen.
    int c = mem[S_POSN], b = mem[S_POSN + 1];
    int dfsz = mem[DF_SZ];
    if (dfsz >= b) {
        c = W + 1;
        b = dfsz;
    }
    mem[S_POSN] = uint8_t(c);                                  // INPUT-2
    mem[S_POSN + 1] = uint8_t(b);
    mem[SCR_CT] = uint8_t(H + 1 - b);
    setFlag(TV_FLAG, TV_LOWER, false);
    clSet(b, c);
    clsLower();
}

// L20C1 IN-ITEM-1
void Machine::inItem1() {
    for (;;) {
        for (;;) {
            int r = prPosn1();
            if (r == 2) return;
            if (r == 0) break;
        }
        uint8_t a = getChar();
        bool isLine = false;
        if (a == '(') {
            nextChar();
            print2();
            if (getChar() != ')') error(0x0B);
            nextChar();
        } else if (a == tok::LINE || alpha(a)) {
            if (a == tok::LINE) {                              // IN-ITEM-2
                nextChar();
                class01();
                setFlag(FLAGX, FX_INPUT_LINE, true);
                if (flag(FLAGS, F_NUMERIC)) error(0x0B);
                isLine = true;
            } else {                                           // IN-ITEM-3
                class01();
                setFlag(FLAGX, FX_INPUT_LINE, false);
            }
            if (!syntaxZ()) {                                  // IN-PROMPT
                setWork();
                setFlag(FLAGX, FX_INPUT_NUMERIC, false);
                setFlag(FLAGX, FX_INPUT_MODE, true);
                uint16_t bc = 1;
                if (!isLine) {
                    if (!flag(FLAGS, F_NUMERIC)) bc = 3;
                    mem[FLAGX] |= uint8_t(mem[FLAGS] & 0x40);
                }
                uint16_t de = bcSpaces(bc);                    // IN-PR-2
                uint16_t hl = uint16_t(de + bc - 1);
                mem[hl] = 0x0D;
                if (bc == 3) {
                    mem[de] = '"';
                    --hl;
                    mem[hl] = '"';
                }
                setWord(K_CUR, hl);                            // IN-PR-3
                uint16_t chAdd = word(CH_ADD);
                if (isLine) {
                    editor();                                  // IN-VAR-3
                } else {
                    for (;;) {                                 // IN-VAR-1
                        try {
                            removeFp(word(WORKSP));
                            mem[ERR_NR] = 0xFF;
                            editor();
                            setFlag(FLAGS, F_RUNTIME, false);
                            inAssign();
                            break;
                        } catch (BasicError&) {
                            if (!flag(FLAGS2, F2_K_CHANNEL)) throw;
                            if (batch) {
                                // nobody can correct the input: start again
                                batchSyntaxError(word(WORKSP));
                                if (bc == 3) {
                                    addChar('"');
                                    addChar('"');
                                    setWord(K_CUR, uint16_t(word(K_CUR) - 1));
                                }
                            }
                        }
                    }
                }
                // IN-VAR-4
                mem[K_CUR + 1] = 0;
                if (inChanK()) {
                    edCopy();
                    clSet(mem[ECHO_E + 1], mem[ECHO_E]);
                }
                // IN-VAR-5
                setFlag(FLAGX, FX_INPUT_MODE, false);
                bool wasLine = flag(FLAGX, FX_INPUT_LINE);
                setFlag(FLAGX, FX_INPUT_LINE, false);
                if (!wasLine) {
                    setWord(X_PTR, chAdd);                     // kept up to date by POINTERS
                    setFlag(FLAGS, F_RUNTIME, true);
                    inAssign();
                    uint16_t hl2 = word(X_PTR);
                    mem[X_PTR + 1] = 0;
                    setWord(CH_ADD, hl2);
                } else {
                    // IN-VAR-6: the whole line is the string.
                    uint16_t start = word(WORKSP);
                    uint16_t len = uint16_t(word(STKBOT) - start - 1);
                    CalcValue v = CalcValue::string(std::string(&mem[start], &mem[start] + len));
                    calc.push_back(v);
                    setFlag(FLAGS, F_NUMERIC, false);
                    letCmd();
                }
            }
        } else {
            prItem1();                                         // IN-NEXT-1
        }
        // IN-NEXT-2
        if (prPosn1() != 1) return;
    }
}

// L21B9 IN-ASSIGN
void Machine::inAssign() {
    setWord(CH_ADD, word(WORKSP));
    uint8_t a = getChar();
    if (a == tok::STOP) {                                      // IN-STOP
        if (syntaxZ()) return;
        error(0x10);                                           // REPORT-H
    }
    valFet2(mem[FLAGX]);
    if (getChar() != 0x0D) error(0x0B);                        // REPORT-Cb
}

// L21D6 IN-CHAN-K
bool Machine::inChanK() { return mem[uint16_t(word(CURCHL) + 4)] == 'K'; }

// L21E2 CO-TEMP-2: colour items before the coordinates of PLOT etc.
void Machine::coTemp2() {
    for (;;) {
        if (!coTemp3()) return;
        uint8_t a = getChar();
        if (a != ',' && a != ';') error(0x0B);
        nextChar();                                            // CO-TEMP-1
    }
}

// L21F2 CO-TEMP-3: an embedded colour item; true if one was found.
bool Machine::coTemp3() {
    uint8_t a = getChar();
    if (a < tok::INK || a > tok::OVER) return false;
    nextChar();
    coTemp4(a);
    return true;
}

// L21FC CO-TEMP-4: evaluate the parameter and send the control code.
void Machine::coTemp4(uint8_t token) {
    uint8_t control = uint8_t(token - 0xC9);
    expt1Num();
    if (syntaxZ()) return;
    uint8_t d = findInt1();
    printA(control);
    printA(d);
}

// L2211 CO-TEMP-5: the colour system variables ATTR_T, MASK_T and P_FLAG.
void Machine::coTemp5(uint8_t control, uint8_t d) {
    auto coChange = [&](uint16_t addr, uint8_t value, uint8_t mask) {   // CO-CHANGE
        mem[addr] = uint8_t(((value ^ mem[addr]) & mask) ^ mem[addr]);
    };
    switch (control) {
    case 0x10:                                                 // INK
    case 0x11: {                                               // PAPER (CO-TEMP-7)
        bool ink = control == 0x10;
        uint8_t b = ink ? 0x07 : 0x38;
        uint8_t c = ink ? d : uint8_t(d << 3);
        if (d >= 10) error(0x13);                              // REPORT-K
        if (d >= 8) {                                          // CO-TEMP-9
            uint8_t t = mem[ATTR_T];
            if (d == 8) c = t;                                 // transparent
            else c = (uint8_t(~(t | b)) & 0x24) ? b : 0;       // 9: contrast
        }
        coChange(ATTR_T, c, b);                                // CO-TEMP-B
        coChange(MASK_T, d > 7 ? 0xFF : 0x00, b);
        coChange(P_FLAG, d > 8 ? 0xFF : 0x00, ink ? 0x10 : 0x40);
        break;
    }
    case 0x12:                                                 // FLASH
    case 0x13: {                                               // BRIGHT (CO-TEMP-C)
        if (d != 8 && d >= 2) error(0x13);
        uint8_t b = control == 0x12 ? 0x80 : 0x40;
        coChange(ATTR_T, d == 1 ? 0xFF : 0x00, b);             // CO-TEMP-E
        coChange(MASK_T, d == 8 ? 0xFF : 0x00, b);
        break;
    }
    default: {                                                 // INVERSE, OVER (CO-TEMP-6)
        if (d >= 2) error(0x13);
        coChange(P_FLAG, d ? 0xFF : 0x00, control == 0x14 ? 0x04 : 0x01);
        break;
    }
    }
}

// L2294 BORDER
void Machine::borderCmd() {
    uint8_t a = findInt1();
    if (a >= 8) error(0x13);
    uint8_t attr = uint8_t(a << 3);
    if (!(attr & 0x20)) attr ^= 0x07;                          // white ink on dark paper
    mem[BORDCR] = attr;
}

// GW NEWREM and the REM command.
void Machine::remCmd() {
    if (!syntaxZ() && bit15(word(PPC))) {
        uint16_t e = word(E_LINE);
        if (mem[e] == tok::REM) gwRemCommand(e);
    }
    // L1BB2 REM: the rest of the line is ignored.
}

// zxgw: a statement that starts with a name.
void Machine::extensionCommand() {
    uint16_t end;
    uint16_t start = word(CH_ADD);
    std::string name = readName(start, end);
    auto it = commands.find(name);
    if (name.empty() || it == commands.end()) error(0x0B);     // REPORT-C
    setWord(CH_ADD, uint16_t(end - 1));
    nextChar();
    Statement st(*this);
    it->second(st);
    checkEnd();
}

} // namespace zxgw
