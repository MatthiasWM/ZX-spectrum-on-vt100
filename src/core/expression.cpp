// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 8. EXPRESSION EVALUATION
//
// L24FB SCANNING evaluates an expression of any complexity with a
// priority-driven operator stack (the machine stack in the ROM, a vector
// here).  Each stacked operation is a 16 bit value: the priority in the high
// byte and the calculator literal in the low byte, where bit 7 means "numeric
// result" and bit 6 "numeric operand" - exactly as in the original.

#include "machine.h"

#include <cmath>
#include <cstring>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;

namespace {

// L2795 tbl-of-ops: operator character -> operation code.
int operatorCode(uint8_t a) {
    switch (a) {
    case '+': return 0xCF;
    case '-': return 0xC3;
    case '*': return 0xC4;
    case '/': return 0xC5;
    case '^': return 0xC6;
    case '=': return 0xCE;
    case '>': return 0xCC;
    case '<': return 0xCD;
    case tok::LE: return 0xC9;
    case tok::GE: return 0xCA;
    case tok::NE: return 0xCB;
    case tok::OR: return 0xC7;
    case tok::AND: return 0xC8;
    default: return -1;
    }
}

// L27B0 tbl-priors, indexed by operation code - $C3.
const uint8_t kPriorities[13] = {6, 8, 8, 10, 2, 3, 5, 5, 5, 5, 5, 5, 6};

} // namespace

// ---------------------------------------------------------------------------
// L24FB SCANNING
// ---------------------------------------------------------------------------
void Machine::scanning() {
    std::vector<uint16_t> ops;
    ops.push_back(0x0000);                                     // priority marker
    uint8_t a = getChar();
    uint16_t bc = 0;

operand:                                                       // S-LOOP-1
    switch (a) {
    case '+':                                                  // S-U-PLUS
        a = nextChar();
        goto operand;
    case '"': {                                                // S-QUOTE
        uint16_t p = uint16_t(word(CH_ADD) + 1);
        std::string s;
        for (;;) {                                             // S-QUOTE-S
            uint8_t ch = mem[p];
            if (ch == 0x0D) { setWord(CH_ADD, p); error(0x0B); }
            if (ch == '"') {
                if (mem[uint16_t(p + 1)] == '"') { s += '"'; p = uint16_t(p + 2); continue; }
                break;
            }
            s += char(ch);
            ++p;
        }
        setWord(CH_ADD, uint16_t(p + 1));
        setFlag(FLAGS, F_NUMERIC, false);                      // S-STRING
        if (!syntaxZ()) stackStr(s);
        goto cont2;
    }
    case '(':                                                  // S-BRACKET
        nextChar();
        scanning();
        if (getChar() != ')') error(0x0B);
        nextChar();
        goto cont2;
    case '.':
    case tok::BIN:
        sDecimal();
        goto numeric;
    case tok::FN:
        sFnSbrn();
        goto cont2;
    case tok::RND:
        if (!syntaxZ()) stack(rnd());
        nextChar();
        goto numeric;
    case tok::PI:
        if (!syntaxZ()) { double pi = M_PI; fp::normalize(pi); stack(pi); }
        nextChar();
        goto numeric;
    case tok::INKEY: {                                         // S-INKEY$
        a = nextChar();
        if (a == '#') {
            ops.push_back(0x105A);                             // read-in
            a = nextChar();
            goto operand;
        }
        setFlag(FLAGS, F_NUMERIC, false);
        if (!syntaxZ()) {
            int k = inkey();
            stackStr(k < 0 ? std::string() : std::string(1, char(k)));
        }
        goto cont2;
    }
    case tok::SCREEN:                                          // S-SCREEN$
        s2Coord();
        if (!syntaxZ()) sScrnS();
        nextChar();
        setFlag(FLAGS, F_NUMERIC, false);
        goto cont2;
    case tok::ATTR:                                            // S-ATTR
        s2Coord();
        if (!syntaxZ()) sAttrS();
        nextChar();
        goto numeric;
    case tok::POINT:                                           // S-POINT
        s2Coord();
        if (!syntaxZ()) pointSub();
        nextChar();
        goto numeric;
    default:
        break;
    }

    // S-ALPHNUM
    if (alphanum(a)) {
        if (a >= 'A') {                                        // S-LETTER
            if (extensionFunction()) goto cont2;
            uint16_t hl;
            bool notFound, arrayZ;
            uint8_t c = lookVars(hl, notFound, arrayZ);
            if (notFound) error(0x01);                         // REPORT-2
            if (arrayZ) {
                uint16_t elem = hl;
                stkVar(hl, c, elem);
                hl = elem;
            }
            if ((mem[FLAGS] & 0xC0) == 0xC0) stack(stackNum(uint16_t(hl + 1)));
            goto cont2;
        }
        sDecimal();
        goto numeric;
    }

    // S-NEGATE: the prefix operators and functions.
    if (a == '-') {
        ops.push_back(0x09DB);
    } else if (a == tok::VALS) {
        ops.push_back(0x1018);
    } else {
        if (a < tok::CODE) error(0x0B);
        int x = a - tok::CODE;
        if (x == 0x14) {
            ops.push_back(0x04F0);                             // NOT
        } else {
            if (x > 0x14) error(0x0B);
            uint8_t c = uint8_t(x + 0xDC);
            if (c < 0xDF) c &= 0xBF;                           // CODE, VAL, LEN: string operand
            if (c >= 0xEE) c &= 0x7F;                          // STR$, CHR$: string result
            ops.push_back(uint16_t(0x1000 | c));
        }
    }
    a = nextChar();                                            // S-PUSH-PO
    goto operand;

numeric:                                                       // S-NUMERIC
    setFlag(FLAGS, F_NUMERIC, true);

cont2:                                                         // S-CONT-2
    a = getChar();
    for (;;) {                                                 // S-CONT-3
        if (a != '(') break;
        if (flag(FLAGS, F_NUMERIC)) { bc = 0; goto sloop; }
        slicing();
        a = nextChar();
    }
    {                                                          // S-OPERTR
        int op = operatorCode(a);
        if (op < 0) bc = 0;
        else bc = uint16_t((kPriorities[op - 0xC3] << 8) | op);
    }

sloop:                                                         // S-LOOP
    for (;;) {
        uint16_t de = ops.back();
        ops.pop_back();
        uint8_t d = uint8_t(de >> 8);
        uint8_t b = uint8_t(bc >> 8);
        if (d < b) {                                           // S-TIGHTER
            ops.push_back(de);
            uint8_t c = uint8_t(bc);
            if (!flag(FLAGS, F_NUMERIC)) {
                // string operands: comparisons and + have string versions
                uint8_t lit = uint8_t((c & 0x3F) + 8);
                if (lit == 0x10) c = 0x50;                     // str-&-no
                else if (lit < 0x10) error(0x0B);              // e.g. a$ * b$
                else if (lit == 0x17) c = 0x17;                // strs-add
                else c = uint8_t(lit | 0x80);
            }
            ops.push_back(uint16_t((b << 8) | c));             // S-NEXT
            a = nextChar();
            goto operand;
        }
        if (d == 0) {
            getChar();
            return;
        }
        ops.push_back(bc);
        uint8_t e = uint8_t(de);
        if (e == 0xED && !flag(FLAGS, F_NUMERIC)) e = 0x99;    // USR "a"
        if (!syntaxZ()) {
            operation(uint8_t(e & 0x3F));
        } else if ((e ^ mem[FLAGS]) & 0x40) {                  // S-SYNTEST
            error(0x0B);
        }
        setFlag(FLAGS, F_NUMERIC, e & 0x80);                   // S-RUNTEST
        bc = ops.back();                                       // S-LOOPEND
        ops.pop_back();
    }
}

// L2522 S-2-COORD: "(x,y)" for SCREEN$, ATTR and POINT.
void Machine::s2Coord() {
    if (nextChar() != '(') error(0x0B);
    next2Num();
    if (getChar() != ')') error(0x0B);
}

// L2535 S-SCRN$-S: the character at a screen position.  The ROM compares
// the bitmap with the character set; here the cell knows its character.
void Machine::sScrnS() {
    int line, col;
    stkToLc(line, col);
    const Cell& cell = screen.at(line, col);
    std::string s;
    if (cell.graphic()) {
        if (cell.dots == 0) s = " ";
    } else if (cell.ch >= 0x20 && cell.ch < 0x80) {
        s = std::string(1, char(cell.ch));
    } else if (cell.ch == 0x80 || cell.ch == 0x8F) {
        s = " ";                                               // empty / inverse space
    }
    stackStr(s);
}

// L2580 S-ATTR-S
void Machine::sAttrS() {
    int line, col;
    stkToLc(line, col);
    stack(screen.at(line, col).attr);
}

// GW STK_TO_LC: line and column, checked against the screen size.
void Machine::stkToLc(int& line, int& col) {
    int b, c, d, e;
    stkToBc(b, c, d, e);
    if (d < 0 || e < 0) error(0x0A);                           // BC_POSTVE
    col = b;
    line = c;
    if (col >= W) error(0x0A);
    if (line >= H) error(0x0A);
}

// L268D S-DECIMAL / S-BIN: a number in the program.  While checking the
// syntax the value is computed and stored in the line behind the number as
// CHR$ 14 plus five bytes; at runtime that hidden value is used.
void Machine::sDecimal() {
    if (syntaxZ()) {
        double v = decToFp();
        getChar();
        uint16_t pos = word(CH_ADD);
        makeRoom(pos, 6);
        uint16_t hl = uint16_t(mrHL + 1);
        mem[hl] = 0x0E;
        fp::write(v, &mem[uint16_t(hl + 1)]);
        tempPtr1(uint16_t(hl + 5));
    } else {
        uint16_t hl = word(CH_ADD);                            // S-STK-DEC
        getChar();
        hl = word(CH_ADD);
        do { ++hl; } while (mem[hl] != 0x0E);                  // S-SD-SKIP
        ++hl;
        stack(stackNum(hl));
        setWord(CH_ADD, uint16_t(hl + 5));
    }
}

// L27BD S-FN-SBRN: user defined functions.
void Machine::sFnSbrn() {
    if (syntaxZ()) {
        uint8_t a = nextChar();
        if (!alpha(a)) error(0x0B);
        a = nextChar();
        bool str = a == '$';
        if (str) a = nextChar();
        if (a != '(') error(0x0B);                             // SF-BRKT-1
        a = nextChar();
        if (a != ')') {
            for (;;) {                                         // SF-ARGMTS
                scanning();
                a = getChar();
                if (a != ',') break;
                nextChar();
            }
            if (a != ')') error(0x0B);                         // SF-BRKT-2
        }
        nextChar();                                            // SF-FLAG-6
        setFlag(FLAGS, F_NUMERIC, !str);
        return;
    }
    // SF-RUN
    uint8_t b = uint8_t(nextChar() & 0xDF);
    uint8_t c = uint8_t(nextChar() - '$');
    if (c == 0) nextChar();
    nextChar();                                                // SF-ARGMT1
    uint16_t argStart = word(CH_ADD);
    uint16_t hl = uint16_t(word(PROG) - 1);
    auto fnSkpovr = [&](uint16_t p) {                          // L28AB FN-SKPOVR
        do { ++p; } while (mem[p] < 0x21);
        return p;
    };
    uint16_t def;
    for (;;) {                                                 // SF-FND-DF
        uint8_t d = 0;
        uint16_t bcEnd = 0;
        if (!lookProg(hl, tok::DEF_FN, d, bcEnd)) error(0x18); // REPORT-P
        def = word(CH_ADD);
        uint16_t p = fnSkpovr(def);
        if ((mem[p] & 0xDF) == b) {                            // SF-CP-DEF
            p = fnSkpovr(p);
            if (uint8_t(mem[p] - '$') == c) {
                hl = p;
                break;
            }
        }
        // SF-NOT-FD: skip this definition
        uint8_t d2 = 2;
        eachStmt(uint16_t(def - 1), d2, 0);
        hl = word(CH_ADD);
    }
    // SF-VALUES
    if (c == 0) hl = fnSkpovr(hl);                             // past '$'
    setWord(CH_ADD, argStart);
    hl = fnSkpovr(hl);                                         // past '('
    uint16_t defArgs = hl;
    if (mem[hl] != ')') {
        for (;;) {                                             // SF-ARG-LP
            ++hl;
            uint8_t numeric = 0x40;
            if (mem[hl] != 0x0E) {
                --hl;
                hl = fnSkpovr(hl);
                ++hl;
                numeric = 0;
            }
            ++hl;                                              // SF-ARG-VL
            uint16_t slot = hl;
            scanning();
            if ((numeric ^ mem[FLAGS]) & 0x40) error(0x19);    // REPORT-Q
            if (numeric) {
                storeNum(slot, popNum());
            } else {
                // The ROM stores a string descriptor; the text is kept aside.
                std::string s = popStr();
                fnStrings[slot] = s;
                mem[slot] = 0;
                setWord(uint16_t(slot + 1), 0);
                setWord(uint16_t(slot + 3), uint16_t(s.size()));
            }
            hl = fnSkpovr(uint16_t(slot + 4));
            if (mem[hl] == ')') break;
            if (getChar() != ',') error(0x19);
            nextChar();
            hl = fnSkpovr(hl);
        }
    }
    // SF-R-BR-2
    if (getChar() != ')') error(0x19);
    uint16_t fnClose = word(CH_ADD);                           // SF-VALUE
    setWord(CH_ADD, hl);
    uint16_t oldDefadd = word(DEFADD);
    setWord(DEFADD, defArgs);
    nextChar();
    nextChar();
    scanning();
    setWord(CH_ADD, fnClose);
    setWord(DEFADD, oldDefadd);
    nextChar();
}

// ---------------------------------------------------------------------------
// L28B2 LOOK-VARS: find a variable.  Returns the C register (the type bits
// and letter).  hl is the address of the variable, or of its name in the
// line if it was not found or syntax is checked.  notFound is the carry
// flag, arrayZ the zero flag ("string or array: call STK-VAR").
// ---------------------------------------------------------------------------
uint8_t Machine::lookVars(uint16_t& hlOut, bool& notFound, bool& arrayZ) {
    setFlag(FLAGS, F_NUMERIC, true);
    uint8_t a = getChar();
    if (!alpha(a)) error(0x0B);
    uint16_t p1 = word(CH_ADD);
    uint8_t c = uint8_t(a & 0x1F);
    a = nextChar();
    uint16_t p2 = word(CH_ADD);
    bool testFn = false;
    if (a != '(') {
        c |= 0x40;                                             // 010: string
        if (a == '$') {                                        // V-STR-VAR
            nextChar();
            setFlag(FLAGS, F_NUMERIC, false);
            testFn = true;
        } else {
            c |= 0x20;                                         // 011: numeric
            if (!alphanum(a)) {
                testFn = true;
            } else {
                while (alphanum(a)) {                          // V-CHAR
                    c &= 0xBF;                                 // 001: long name
                    a = nextChar();
                }
            }
        }
    }
    if (testFn && mem[DEFADD + 1] != 0 && !syntaxZ()) {        // V-TEST-FN
        // L2951 STK-F-ARG: the parameters of a DEF FN being evaluated.
        uint16_t hl = word(DEFADD);
        if (mem[hl] != ')') {
            for (;;) {                                         // SFA-LOOP
                uint8_t b = uint8_t(mem[hl] | 0x60);
                ++hl;
                if (mem[hl] != 0x0E) {
                    --hl;
                    do { ++hl; } while (mem[hl] < 0x21);       // FN-SKPOVR to '$'
                    ++hl;
                    b &= 0xDF;
                }
                if (b == c) {                                  // SFA-MATCH
                    if (!(c & 0x20)) {
                        uint16_t slot = uint16_t(hl + 1);
                        auto it = fnStrings.find(slot);
                        stackStr(it == fnStrings.end() ? std::string() : it->second);
                    }
                    hlOut = hl;
                    notFound = false;
                    arrayZ = false;
                    return c;
                }
                hl = uint16_t(hl + 5);
                do { ++hl; } while (mem[hl] < 0x21);
                if (mem[hl] == ')') break;
                do { ++hl; } while (mem[hl] < 0x21);
            }
        }
    }
    // V-RUN/SYN
    uint8_t b = c;
    uint16_t found = 0;
    if (syntaxZ()) {
        c = uint8_t((c & 0xE0) | 0x80);
    } else {
        uint16_t hl = word(VARS);                              // V-RUN
        for (;;) {                                             // V-EACH
            uint8_t v = uint8_t(mem[hl] & 0x7F);
            if (v == 0) { b |= 0x80; break; }                  // V-80-BYTE
            if (v == c) {
                bool t5 = v & 0x20, t6 = v & 0x40;
                if (!t5 || t6) { found = hl; goto vFound; }
                // long names: compare the remaining letters
                uint16_t de = p2, h = hl;
                for (;;) {                                     // V-MATCHES
                    ++h;
                    uint8_t ch;
                    do { ch = mem[de++]; } while (ch == ' ');  // V-SPACES
                    ch |= 0x20;
                    if (ch == mem[h]) continue;
                    ch |= 0x80;
                    if (ch != mem[h]) break;
                    if (!alphanum(mem[de])) { found = h; goto vFound; }
                    break;
                }
            }
            hl = nextOne(hl);                                  // V-NEXT
        }
    }
    // V-SYNTAX
    a = getChar();
    if (a != '(') b |= 0x20;
    hlOut = p1;
    notFound = b & 0x80;
    arrayZ = !(b & 0x20);
    return c;

vFound:                                                        // V-FOUND-2
    a = getChar();
    while (alphanum(a)) a = nextChar();                        // V-PASS
    hlOut = found;
    notFound = false;
    arrayZ = !(c & 0x20);
    return c;
}

// ---------------------------------------------------------------------------
// L2996 STK-VAR: subscripts and slices of strings and arrays.  For numeric
// arrays elem receives the address before the element (like the ROM's HL);
// strings are put on the calculator stack.
// ---------------------------------------------------------------------------
void Machine::stkVar(uint16_t hl, uint8_t c, uint16_t& elem) {
    bool syntax = c & 0x80;
    int b = 0;                         // dimensions (0 = 256 while checking)
    uint16_t de = 0;                   // pointer to the dimension sizes
    uint32_t data = 0;                 // the data pointer
    uint8_t a;

    auto pushRef = [&](uint16_t start, uint16_t len, uint8_t kind) {
        CalcValue v = CalcValue::string(std::string(&mem[start], &mem[start] + len));
        v.inMem = true;
        v.start = start;
        v.len = len;
        v.kind = kind;
        calc.push_back(v);
        setFlag(FLAGS, F_NUMERIC, false);
    };

    if (!syntax) {
        if (!(mem[hl] & 0x80)) {                               // SV-SIMPLE$
            pushRef(uint16_t(hl + 3), word(uint16_t(hl + 1)), 1);
            a = getChar();
            goto sliceQ;
        }
        hl = uint16_t(hl + 3);                                 // SV-ARRAYS
        b = mem[hl];
        de = hl;
        if (c & 0x40) {
            --b;
            if (b == 0) {                                      // one dimension: a string
                pushRef(uint16_t(hl + 3), word(uint16_t(hl + 1)), 0);
                a = getChar();
                goto sliceQ;
            }
            if (getChar() != '(') error(0x02);                 // REPORT-3
        }
    }

    // SV-COUNT / SV-LOOP
    for (;;) {
        nextChar();
        if (c == 0xC0) {                                       // string array, checking
            a = getChar();
            if (a == ')') goto dim;
            if (a == tok::TO) goto chAdd;
        }
        // SV-MULT
        uint16_t size = 0;
        if (!syntax) {
            size = word(uint16_t(de + 1));
            de = uint16_t(de + 2);
        }
        bool over;
        uint16_t index = intExp(size, over);
        if (over) error(0x02);
        if (!syntax) {
            data = data * size + (index - 1u);
            if (data > 0xFFFF) error3(0x03);
        }
        b = (b - 1) & 0xFF;
        if (b != 0) {                                          // SV-COMMA
            a = getChar();
            if (a == ',') continue;
            if (!syntax) error(0x02);
            if (c & 0x40) {                                    // SV-CLOSE
                if (a == ')') goto dim;
                if (a != tok::TO) error(0x0B);
                goto chAdd;
            }
            if (a != ')') error(0x0B);
            nextChar();
            return;
        }
        break;
    }
    if (syntax) error(0x0B);                                   // SV-RPT-C
    if (!(c & 0x40)) {                                         // a number
        if (getChar() != ')') error(0x02);
        nextChar();                                            // SV-NUMBER
        uint32_t addr = uint32_t(de) + data * 5;
        if (addr > 0xFFFF) error3(0x03);
        elem = uint16_t(addr);
        return;
    }
    {                                                          // SV-ELEM$
        uint16_t len = word(uint16_t(de + 1));
        de = uint16_t(de + 2);
        uint32_t start = uint32_t(de) + data * len + 1;
        if (start + len > 0x10000) error3(0x03);
        pushRef(uint16_t(start), len, 0);
        a = getChar();
        if (a == ')') goto dim;
        if (a != ',') error(0x02);
    }
slice:                                                         // SV-SLICE
    slicing();
dim:                                                           // SV-DIM
    a = nextChar();
sliceQ:                                                        // SV-SLICE?
    if (a == '(') goto slice;
    setFlag(FLAGS, F_NUMERIC, false);
    return;
chAdd:                                                         // SV-CH-ADD
    getChar();
    setWord(CH_ADD, uint16_t(word(CH_ADD) - 1));
    goto slice;
}

// L2A52 SLICING: a$( TO ), a$(n), a$(n TO m) ...
void Machine::slicing() {
    CalcValue s;
    bool syntax = syntaxZ();
    if (!syntax) s = stkFetch();
    uint8_t a = nextChar();
    if (a == ')') {                                            // SL-STORE
        if (!syntax) {
            s.kind = 0;
            calc.push_back(s);
            setFlag(FLAGS, F_NUMERIC, false);
        }
        return;
    }
    bool over = false;
    uint16_t len = uint16_t(s.str.size());
    uint16_t from = 1, to = len;
    a = getChar();
    if (a != tok::TO) {
        bool o;
        from = intExp(len, o);
        over |= o;
        a = getChar();
        if (a != tok::TO) {
            if (a != ')') error(0x0B);                         // SL-RPT-C
            to = from;
            goto define;
        }
    }
    a = nextChar();                                            // SL-SECOND
    if (a != ')') {
        bool o;
        to = intExp(len, o);
        over |= o;
        if (getChar() != ')') error(0x0B);
    }
define:                                                        // SL-DEFINE
    setFlag(FLAGS, F_NUMERIC, false);
    if (syntax) return;
    {
        CalcValue r = CalcValue::string("");
        if (to >= from) {
            if (over) error(0x02);                             // REPORT-3
            uint16_t n = uint16_t(to - from + 1);
            r.str = s.str.substr(from - 1u, n);
            if (s.inMem) {
                r.inMem = true;
                r.start = uint16_t(s.start + from - 1);
                r.len = n;
            }
        } else if (s.inMem) {
            r.inMem = true;
            r.start = uint16_t(s.start + from - 1);
            r.len = 0;
        }
        r.kind = 0;
        calc.push_back(r);
    }
}

// L2ACC INT-EXP1: a subscript; over if it is 0 or above the limit.
uint16_t Machine::intExp(uint16_t limit, bool& over) {
    expt1Num();
    over = false;
    if (syntaxZ()) return 0;
    uint16_t bc = findInt2();
    over = bc == 0 || bc > limit;
    return bc;
}

// ---------------------------------------------------------------------------
// L2AFF LET: the assignment.  Returns the address of a numeric value.
// ---------------------------------------------------------------------------
uint16_t Machine::letCmd() {
    uint16_t hl = word(DEST);
    if (flag(FLAGX, FX_NEW_VARIABLE)) {
        uint16_t bc = 5;
        uint8_t a;
        for (;;) {                                             // L-EACH-CH
            ++bc;
            for (;;) {                                         // L-NO-SP
                ++hl;
                a = mem[hl];
                if (a == ' ') continue;
                if (a > ' ') break;
                if (a < 0x10 || a >= 0x16) goto spaces;
                ++hl;                                          // colour parameter
            }
            if (alphanum(a)) continue;                         // L-TEST-CH
            if (a == '$') {                                    // L-NEW$
                lString(uint8_t(mem[word(DEST)] & 0xDF));
                return 0;
            }
            break;
        }
    spaces: {                                                  // L-SPACES
            uint8_t len = uint8_t(bc);
            makeRoom(uint16_t(word(E_LINE) - 1), bc);
            uint16_t first = uint16_t(mrHL + 1);
            uint16_t de = first;
            hl = word(DEST);
            uint8_t mask = 0;
            int n = len - 6;
            if (n != 0) {
                uint8_t ch = 0;
                for (int i = 0; i < n; ++i) {                  // L-CHAR
                    do { ++hl; ch = mem[hl]; } while (ch < 0x21);
                    ch |= 0x20;
                    ++de;
                    mem[de] = ch;
                }
                mem[de] = uint8_t(ch | 0x80);
                mask = 0xC0;
            }
            mem[first] = uint8_t((mask ^ mem[word(DEST)]) | 0x20);   // L-SINGLE / L-FIRST
            uint16_t value = uint16_t(word(E_LINE) - 1 - 5);   // L-NUMERIC
            storeNum(value, popNum());
            return value;
        }
    }
    if (flag(FLAGS, F_NUMERIC)) {                              // L-EXISTS
        uint16_t value = uint16_t(hl + 1);
        storeNum(value, popNum());
        return value;
    }
    // L-DELETE$
    uint16_t bc = word(STRLEN);
    if (flag(FLAGX, FX_SIMPLE_STRING)) {                       // L-ADD$
        uint16_t old = uint16_t(hl - 3);
        lString(mem[old]);
        reclaim2(old, uint16_t(bc + 3));
        return 0;
    }
    // Procrustean assignment to a slice or an array element: the new text
    // is cut or padded with spaces to the length of the destination.
    std::string s = popStr();
    for (uint16_t i = 0; i < bc; ++i)
        mem[uint16_t(hl + i)] = i < s.size() ? uint8_t(s[i]) : uint8_t(' ');
    return hl;
}

// L2BC6 L-STRING: add a string variable at the end of the variables area.
void Machine::lString(uint8_t letter) {
    std::string s = popStr();
    uint16_t len = uint16_t(s.size());
    uint16_t pos = uint16_t(word(E_LINE) - 1);
    makeRoom(pos, uint16_t(len + 3));
    mem[pos] = letter;
    setWord(uint16_t(pos + 1), len);
    if (len) std::memcpy(&mem[uint16_t(pos + 3)], s.data(), len);
}

// L2BF1 STK-FETCH: the last value from the calculator stack.
CalcValue Machine::stkFetch() {
    if (calc.empty()) return CalcValue::string("");
    CalcValue v = calc.back();
    calc.pop_back();
    if (!v.isStr) { v.isStr = true; v.str.clear(); }
    if (!v.inMem) v.len = uint16_t(v.str.size());
    return v;
}

// ---------------------------------------------------------------------------
// L2C02 DIM
// ---------------------------------------------------------------------------
void Machine::dimCmd() {
    uint16_t hl;
    bool notFound, arrayZ;
    uint8_t c = lookVars(hl, notFound, arrayZ);
    if (!arrayZ) error(0x0B);                                  // D-RPORT-C
    if (syntaxZ()) {
        c &= 0xBF;                                             // check as numeric
        uint16_t elem;
        stkVar(hl, c, elem);
        checkEnd();
    }
    if (!notFound) reclaim2(hl, uint16_t(nextOne(hl) - hl));   // D-RUN
    c |= 0x80;                                                 // D-LETTER
    uint32_t total = (c & 0x40) ? 1 : 5;
    std::vector<uint16_t> sizes;
    uint8_t a;
    do {                                                       // D-NO-LOOP
        nextChar();
        bool over;
        uint16_t n = intExp(0xFFFF, over);
        if (over) error(0x02);                                 // REPORT-3
        sizes.push_back(n);
        total *= n;
        if (total > 0xFFFF) error3(0x03);
        a = getChar();
    } while (a == ',');
    if (a != ')') error(0x0B);
    nextChar();
    uint32_t space = total + 2 * sizes.size() + 4;
    if (space > 0xFFFF || sizes.size() > 255) error3(0x03);
    makeRoom(uint16_t(word(E_LINE) - 1), uint16_t(space));
    uint16_t p = uint16_t(mrHL + 1);
    mem[p] = c;
    setWord(uint16_t(p + 1), uint16_t(space - 3));
    mem[uint16_t(p + 3)] = uint8_t(sizes.size());
    uint16_t q = uint16_t(p + 4);
    for (uint16_t n : sizes) { setWord(q, n); q = uint16_t(q + 2); }
    std::memset(&mem[q], (c & 0x40) ? ' ' : 0, total);         // DIM-CLEAR
}

// L2C8D ALPHA
bool Machine::alpha(uint8_t a) { return (a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z'); }
// L2C88 ALPHANUM
bool Machine::alphanum(uint8_t a) { return numeric(a) || alpha(a); }
// L2D1B NUMERIC
bool Machine::numeric(uint8_t a) { return a >= '0' && a <= '9'; }

// L2C9B DEC-TO-FP: read a number from the program text.
double Machine::decToFp() {
    uint8_t a = mem[word(CH_ADD)];
    if (a == tok::BIN) {
        uint32_t v = 0;
        for (;;) {                                             // BIN-DIGIT
            a = nextChar();
            if (a != '0' && a != '1') break;
            v = v * 2 + uint32_t(a - '0');
            if (v > 0xFFFF) error(0x05);                       // REPORT-6
        }
        return double(v);                                      // BIN-END
    }
    double v = 0;
    bool fraction = false;
    if (a == '.') {                                            // DECIMAL
        a = nextChar();
        if (!numeric(a)) error(0x0B);
        fraction = true;
    } else {
        v = intToFp();
        a = mem[word(CH_ADD)];
        if (a == '.') {
            a = nextChar();
            fraction = numeric(a);
        }
    }
    if (fraction) {                                            // DEC-STO-1
        double m = 1;
        for (;;) {                                             // NXT-DGT-1
            a = getChar();
            if (!numeric(a)) break;
            m = checked(m * 10);
            v = checked(v + checked(double(a - '0') / m));
            nextChar();
        }
    }
    a = mem[word(CH_ADD)];                                     // E-FORMAT
    if (a != 'E' && a != 'e') return v;
    bool negative = false;
    a = nextChar();                                            // SIGN-FLAG
    if (a == '+') {
        a = nextChar();
    } else if (a == '-') {
        negative = true;
        a = nextChar();
    }
    if (!numeric(a)) error(0x0B);                              // ST-E-PART
    double e = intToFp();
    if (e > 127) error(0x05);
    double r = fp::eToFp(v, negative ? -int(e) : int(e));
    return checked(r);
}

// L2D3B INT-TO-FP: digits at CH_ADD.
double Machine::intToFp() {
    double v = 0;
    uint8_t a = mem[word(CH_ADD)];
    while (numeric(a)) {                                       // NXT-DGT-2
        v = checked(v * 10 + (a - '0'));
        a = chAddPlus1();
    }
    return v;
}

// zxgw: extension functions NAME(...) and NAME$(...).
bool Machine::extensionFunction() {
    uint16_t start = word(CH_ADD);
    uint16_t end;
    std::string name = readName(start, end);
    if (name.size() < 2) return false;
    uint16_t p = end;
    while (mem[p] == ' ') ++p;
    bool str = false;
    if (mem[p] == '$') {
        str = true;
        ++p;
        while (mem[p] == ' ') ++p;
    }
    if (mem[p] != '(') return false;
    auto it = functions.find(str ? name + "$" : name);
    if (it == functions.end()) return false;
    setWord(CH_ADD, p);
    uint8_t a = nextChar();
    std::vector<Value> args;
    if (a != ')') {
        for (;;) {
            scanning();
            if (!syntaxZ()) {
                if (flag(FLAGS, F_NUMERIC)) args.push_back(Value::num(popNum()));
                else args.push_back(Value::str(popStr()));
            }
            a = getChar();
            if (a == ',') { nextChar(); continue; }
            if (a != ')') error(0x0B);
            break;
        }
    }
    nextChar();
    if (!syntaxZ()) {
        FunctionCall call(*this, std::move(args));
        Value r = it->second(call);
        if (r.isString != str) error(0x19);                    // Parameter error
        if (str) stackStr(r.string);
        else stack(checked(r.number));
    }
    setFlag(FLAGS, F_NUMERIC, !str);
    return true;
}

// The name of an extension command or function at hl (letters and digits),
// upper case.  end is the address after the name.
std::string Machine::readName(uint16_t hl, uint16_t& end) const {
    std::string name;
    if (alpha(mem[hl])) {
        while (alphanum(mem[hl])) {
            uint8_t ch = mem[hl];
            if (ch >= 'a' && ch <= 'z') ch = uint8_t(ch - 32);
            name += char(ch);
            ++hl;
        }
    }
    end = hl;
    return name;
}

} // namespace zxgw
