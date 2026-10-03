// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The "Gosh Wonderful" additions in the spare ROM space ($386E onwards):
//
//   NEWED      the editor followed by the tokenizer, so keywords can be
//              typed letter by letter ("print" or "PRINT" or "pr.")
//   TOGGLE     STOP as a direct command switches to classic keyword entry
//   IMPOSE     forces 'L' mode unless classic entry is active
//   NEWREM     REM as a direct command: REM STREAMS, REM DELETE first last,
//              REM RENUMBER [start [step [first [last]]]], and a help screen

#include "machine.h"

#include <algorithm>
#include <cstring>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;

// NEWED: the new editor.
void Machine::newEd() {
    setFlag(TV_FLAG, TV_EDIT_CHANGED, false);
    impose();
    mem[ERR_NR] = 0xFF;
    if (!pendingCommands.empty()) {
        // A command queued by the application is "typed" in one go.
        std::string text = pendingCommands.front();
        pendingCommands.pop_front();
        for (uint8_t ch : text) addChar(ch);
        tokenize(word(E_LINE));
        return;
    }
    editor();
    if (flag(FLAGS2, F2_GW_CLASSIC)) return;                   // classic entry
    tokenize(word(E_LINE));
}

// IMPOSE: the K/L mode flags.
void Machine::impose() {
    mem[FLAGS] |= F_K_L_MODE | F_K_L_TRANSIENT;                // 'L' mode
    if (!flag(FLAGS2, F2_GW_CLASSIC)) return;
    if (flag(FLAGX, FX_INPUT_MODE)) return;
    mem[FLAGS] &= uint8_t(~(F_K_L_MODE | F_K_L_TRANSIENT));    // 'K' mode
}

// NEWTOK: search the edit line for one keyword and replace it by the token.
//   text      the keyword (upper case)
//   remTable  searching the REM command table: return at the first match
// Returns 1 with hl at the keyword when a REM table keyword was found,
// otherwise 0 when the end of the line is reached.
int Machine::newTok(uint16_t& hlOut, uint8_t token, const char* text, bool remTable) {
    std::string t(text);
    t.back() = char(uint8_t(t.back()) | 0x80);                 // inverted last character
    const bool firstRemPass = token == 0;
    uint16_t hl = word(E_LINE);
    uint16_t start = 0;
    size_t de = 0;
    uint8_t a = 0, b = 0, c = 0;                               // B, C: alpha flags, quotes
    auto ucase = [&]() {                                       // UCASE
        b = c;
        c = 0;
        if (!alpha(a)) return false;
        a &= 0xDF;
        c = 0x80;
        return true;
    };

char0:                                                         // CHAR0
    b = 0;
    c = 0;
char1:                                                         // CHAR1
    de = 0;
    for (;;) {                                                 // L3
        a = mem[hl];
        if (a == 0x0D || a == tok::REM) return 0;              // EOL
        if (a == '"') ++c;
        if (!(c & 1)) {                                        // NOQ
            bool isAlpha = ucase();
            // a keyword must not follow a letter ('INT' in 'PRINT')
            if (!(isAlpha && (b & 0x80)) && a == uint8_t(t[0])) break;
        }
        ++hl;                                                  // SKIP
    }
    start = hl;                                                // MATCH1
intra:                                                         // INTRA
    ++hl;
    a = mem[hl];
    ucase();
intra2:                                                        // INTRA2
    ++de;
    if (de >= t.size()) goto char1;
    if (a == uint8_t(t[de])) goto intra;
    if (t[de] == ' ') goto intra2;                             // "GOTO" for "GO TO"
    if (a == '.') goto chksp;                                  // abbreviation
    if (uint8_t(a | 0x80) != uint8_t(t[de])) goto char1;
    if (uint8_t(a | 0x80) < 0xC0) goto subst;                  // '<>', 'STR$', 'OPEN #'
chksp:                                                         // CHKSP
    ++hl;
    a = mem[hl];
    if (a == ' ') goto subst;                                  // the space goes too
    --hl;
    if (a == '$') goto char1;                                  // VAL in VAL$
    if (alpha(a)) goto char1;                                  // AT in ATN, FOR in FORMAT
subst: {                                                       // SUBST
        uint16_t from = start;
        do { --from; } while (mem[from] == ' ');               // BAKT: leading spaces
        ++from;
        reclaim1(from, hl);
        hl = from;
    }
    if (remTable) {
        hlOut = hl;
        return 1;
    }
    if (firstRemPass) {
        mem[hl] = tok::REM;                                    // only one REM per line
        return 0;
    }
    mem[hl] = token;
    goto char0;                                                // more of the same token
}

// The TOKENIZER: REM first, then COPY down to RND.
void Machine::tokenize(uint16_t start) {
    (void)start;
    uint16_t hl;
    newTok(hl, 0, "REM", false);
    for (int t = 0xFF; t >= 0xA5; --t) newTok(hl, uint8_t(t), tokenText(uint8_t(t)), false);
}

// NEWREM: REM as the first statement of a direct command.
void Machine::gwRemCommand(uint16_t eLine) {
    mem[eLine] = tok::DATA;                                    // hide it from the tokenizer
    static const char* const remTable[3] = {"STREAMS", "DELETE ", "RENUMBER"};
    uint16_t hl = 0;
    int found = 0;
    for (int t = 2; t >= 0 && !found; --t) {
        if (newTok(hl, uint8_t(0xA5 + t), remTable[t], true)) found = 0xA5 + t;
    }
    if (!found) { helpScreen(); return; }
    // TKFOUND: read the numbers that follow
    tempPtr1(hl);
    clAll();
    std::vector<uint16_t> params;
    for (;;) {                                                 // SPC_LP
        uint8_t a = getChar();
        if (a == 0x0D) break;                                  // EOLR
        if (!numeric(a)) { helpScreen(); return; }             // UNWIND
        double v = intToFp();
        params.push_back(v > 65535 ? 65535 : uint16_t(v));
    }
    if (found == 0xA6) blockDelete(params);
    else if (found == 0xA5) streamsCmd();
    else renumberCmd(params);
}

// UNWIND: the help screen.
void Machine::helpScreen() {
    clAll();
    static const char* const lines[7][2] = {
        {"REM COMMANDS", ""}, {"\x06", ""}, {"V1.32", ""}, {"\x06", ""},
        {"STREAMS", ""}, {"DELETE ", "  first last"}, {"RENUMBER", " start step  first last"}
    };
    for (auto& l : lines) {
        poMsg(l[0]);
        poMsg(l[1]);
        printA(0x0D);
    }
}

// BDEL: REM DELETE first last - both lines must exist.
void Machine::blockDelete(const std::vector<uint16_t>& p) {
    if (p.size() != 2) { helpScreen(); return; }
    uint16_t prev;
    bool exact;
    uint16_t lastAddr = lineAddr(p[1], prev, exact);
    if (!exact) { helpScreen(); return; }
    uint16_t keep = nextOne(lastAddr);
    uint16_t firstAddr = lineAddr(p[0], prev, exact);
    if (!exact || keep < firstAddr) { helpScreen(); return; }
    reclaim1(firstAddr, keep);
}

// STRLST: REM STREAMS lists the streams and their channels.
void Machine::streamsCmd() {
    poMsg("\x14\x01 Streams\x06" "Free: ");
    long free = long(virtualSp()) - long(word(STKEND)) - 127;
    stack(free < 0 ? 0 : double(free));
    printFp();
    for (uint8_t c : {0x06, 0x14, 0x00, 0x0D}) printA(c);      // comma, INVERSE 0, ENTER
    for (int s = -3; s < 16; ++s) {
        if (s == 0) printA(0x0D);
        printA(0x0D);
        outNum1(uint16_t(s & 0xFF));
        printA(0x06);
        uint16_t offset = word(uint16_t(STRMS + (s + 3) * 2));
        if (offset) printA(mem[uint16_t(word(CHANS) + offset + 3)]);
    }
}

// RENU: REM RENUMBER [start [step [first [last]]]]
// Defaults 100, 10, 1, 16383.  Line numbers after GO TO, GO SUB, RESTORE,
// RUN, LIST, LLIST and LINE are updated when they are plain numbers.
void Machine::renumberCmd(std::vector<uint16_t> p) {
    if (p.size() > 4) { helpScreen(); return; }
    uint16_t newStart = p.size() > 0 ? p[0] : 100;
    uint16_t step = p.size() > 1 ? p[1] : 10;
    uint16_t first = p.size() > 2 ? p[2] : 1;
    uint16_t lastX = p.size() > 3 ? p[3] : 16383;
    if (step == 0) return;                                     // RBAK3
    uint16_t newEnd = 0;

    // DO_RENUM / DO_DUMMY: assign the new numbers (or just compute NEWEND).
    auto doRenum = [&](bool dummy, uint16_t lastLn) {
        uint16_t hl = word(PROG);
        uint16_t de = newStart;
        while (mem[hl] < 0x40) {                               // LN_LP
            if (cpLines(hl, first) >= 0) {
                int r = cpLines(hl, lastLn);
                if (r <= 0) {                                  // REN_LN
                    if (!dummy) {
                        mem[hl] = uint8_t(de >> 8);
                        mem[uint16_t(hl + 1)] = uint8_t(de);
                    }
                    newEnd = de;
                    de = uint16_t(de + step);
                }
            }
            hl = uint16_t(hl + 4 + word(uint16_t(hl + 2)));   // ADVANCE
        }
    };

    doRenum(true, lastX);
    if (newEnd >= 10000) return;                               // RBAK3

    // Does the renumbered section clash with other lines?
    uint16_t prev;
    bool exact;
    uint16_t aEnd = lineAddr(newEnd, prev, exact);
    bool inSitu = exact;
    if (!inSitu) {
        uint16_t aStart = lineAddr(newStart, prev, exact);
        if (exact || aStart < aEnd) inSitu = true;
    }
    if (inSitu) {                                              // IN_SITU
        uint16_t a = lineAddr(uint16_t(lastX + 1), prev, exact);
        uint16_t following = uint16_t((mem[a] << 8) | mem[uint16_t(a + 1)]);
        if (newEnd >= following) return;
        uint16_t prevFirst, prevNew;
        lineAddr(first, prevFirst, exact);
        lineAddr(newStart, prevNew, exact);
        if (prevNew < prevFirst) return;
    }

    // RENU3: update the line numbers used by GO TO etc.
    static const uint8_t tokTab[7] = {tok::GO_TO, tok::GO_SUB, tok::RESTORE, tok::RUN,
                                      tok::LIST, tok::LLIST, tok::LINE};
    uint16_t hl = word(PROG);
    while (!(mem[hl] & 0xC0)) {                                // LP1
        uint16_t lineStart = hl;
        uint16_t p2 = uint16_t(hl + 4);
        for (;;) {                                             // LKP1
            uint8_t a = mem[p2];
            if (a == '"') {                                    // Q2: skip quoted text
                do { ++p2; } while (mem[p2] != '"' && mem[p2] != 0x0D);
                if (mem[p2] == 0x0D) break;
                ++p2;
                continue;
            }
            if (a == tok::REM) break;                          // NEWLINE
            if (a == 0x0E) { p2 = uint16_t(p2 + 6); continue; }   // NUMBER
            if (a == 0x0D) break;
            if (std::find(std::begin(tokTab), std::end(tokTab), a) == std::end(tokTab)) {
                ++p2;
                continue;
            }
            // CHKDIG: a token that may be followed by a line number
            uint16_t q = uint16_t(p2 + 1);
            while (mem[q] == ' ') ++q;
            if (!numeric(mem[q])) { p2 = q; continue; }
            uint16_t digits = q;
            while (numeric(mem[q])) ++q;
            if (mem[q] != 0x0E || mem[uint16_t(q + 1)] != 0) { p2 = q; continue; }
            uint16_t numPos = q;                               // the hidden number
            uint16_t bc = word(uint16_t(q + 3));
            uint16_t r = uint16_t(q + 6);                      // SKPOVR
            while (mem[r] < 0x21 && mem[r] != 0x0D) ++r;
            if (mem[r] != 0x0D && mem[r] != ':') { p2 = r; continue; }
            // DOIT: the new number of the line referred to
            uint16_t target = lineAddr(bc, prev, exact);
            uint16_t number;
            if (mem[target] >= 0x40) {
                number = 9999;
            } else {
                uint16_t lineNumber = uint16_t((mem[target] << 8) | mem[uint16_t(target + 1)]);
                if (bc > lastX || bc < first) { p2 = r; continue; }   // SANE
                uint16_t saveEnd = newEnd;
                doRenum(true, lineNumber);
                number = newEnd;
                newEnd = saveEnd;
            }
            // DTINS: replace the digits and the hidden value.
            std::string text = std::to_string(number);
            int oldLen = numPos - digits;
            int delta = int(text.size()) - oldLen;
            if (delta > 0) makeRoom(numPos, uint16_t(delta));
            else if (delta < 0) reclaim2(uint16_t(numPos + delta), uint16_t(-delta));
            for (size_t i = 0; i < text.size(); ++i) mem[uint16_t(digits + i)] = uint8_t(text[i]);
            numPos = uint16_t(digits + text.size());
            fp::write(number, &mem[uint16_t(numPos + 1)]);
            setWord(uint16_t(lineStart + 2), uint16_t(word(uint16_t(lineStart + 2)) + delta));
            p2 = uint16_t(numPos + 6);
        }
        hl = uint16_t(lineStart + 4 + word(uint16_t(lineStart + 2)));
    }

    // RENU4: the real renumbering, then sort the lines (BUBBLE).
    doRenum(false, lastX);
    struct Line { uint16_t number; std::vector<uint8_t> bytes; };
    std::vector<Line> lines;
    uint16_t prog = word(PROG), vars = word(VARS);
    for (uint16_t q = prog; q < vars;) {
        uint16_t len = uint16_t(4 + word(uint16_t(q + 2)));
        Line l;
        l.number = uint16_t((mem[q] << 8) | mem[uint16_t(q + 1)]);
        l.bytes.assign(&mem[q], &mem[q] + len);
        lines.push_back(std::move(l));
        q = uint16_t(q + len);
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& x, const Line& y) { return x.number < y.number; });
    uint16_t q = prog;
    for (const Line& l : lines) {
        std::memcpy(&mem[q], l.bytes.data(), l.bytes.size());
        q = uint16_t(q + l.bytes.size());
    }
}

} // namespace zxgw
