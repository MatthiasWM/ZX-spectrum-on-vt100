// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The Machine: a C++ translation of the "Gosh Wonderful" ZX Spectrum ROM.
//
// The class is implemented in several source files that follow the parts of
// the ROM listing (gw_rom.s):
//
//   machine.cpp     Part 1  restarts, errors, memory primitives
//                   Part 6  executive: START/NEW, main loop, channels,
//                           MAKE-ROOM, line and variable searching
//   keyboard.cpp    Part 2  keyboard decoding (terminal keys -> key codes)
//   tape.cpp        Part 4  SAVE, LOAD, VERIFY, MERGE (files instead of tape)
//   screen.cpp      Part 5  screen and printer output, scrolling, CLS
//   editor.cpp      Part 5  the line editor, KEY-INPUT, ED-COPY
//                   Part 6  LIST, AUTO-LIST, OUT-LINE
//   interpreter.cpp Part 7  LINE-SCAN, statement loop, command classes and
//                           most commands
//   graphics.cpp    Part 7  PLOT, DRAW, CIRCLE, POINT, BORDER, colours
//   sound.cpp       Part 3  BEEP
//   expression.cpp  Part 8  SCANNING, variables, slicing, LET, DIM
//   calculator.cpp  Parts 9 and 10  arithmetic and functions
//   gw.cpp          The "spare" section: tokenizer, REM commands
//                   (STREAMS, DELETE, RENUMBER), classic keyword toggle
//   extensions.cpp  Extension commands and functions
//
// Register usage of the original is mapped to C++ like this:
//   - BC/HL/DE that carry results become return values or reference
//     parameters, named after the register (b, c) where that helps to
//     compare with the listing.
//   - RST 08 (ERROR-1) throws BasicError.  The ROM's ERR_SP error handler
//     addresses become try/catch blocks.
//   - CHECK-END in syntax mode drops two return addresses and continues
//     with the next statement; this is SyntaxStatementEnd.
//   - The calculator stack becomes a std::vector of values.

#ifndef ZXGW_MACHINE_H
#define ZXGW_MACHINE_H

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "zxgw/extension.h"
#include "zxgw/host.h"
#include "zxgw/screen.h"
#include "zxgw/zxgw.h"
#include "sysvars.h"

namespace zxgw {

// RST 08: an error report.  errNr is the value stored in ERR_NR, i.e. the
// report code minus one ($FF = OK, $00 = "1 NEXT without FOR", ...).
struct BasicError {
    uint8_t errNr;
};

// CHECK-END while checking syntax: the statement is complete.
struct SyntaxStatementEnd {};

// LIST_SP: the automatic listing has filled the screen.
struct AutoListStop {};

// Leaving Machine::run().
struct QuitRequest {
    int exitCode;
};

// NEW and USR 0: continue at MAIN-1 without a report.
struct RestartMain {};

// ESC / Ctrl-C while editing an INPUT (a terminal addition): leave the INPUT
// with "H STOP in INPUT".
struct InputBreak {};

// An entry on the calculator stack.
struct CalcValue {
    bool isStr = false;
    double num = 0;
    std::string str;
    // Strings that are taken from memory (string variables, array elements
    // and slices of them) remember where they are.  The ROM keeps only this
    // descriptor; it is needed when assigning to a slice.
    bool inMem = false;
    uint16_t start = 0;
    uint16_t len = 0;
    uint8_t kind = 0;    // the A register of STK-STORE: 1 = complete simple string

    static CalcValue number(double n) { CalcValue v; v.num = n; return v; }
    static CalcValue string(std::string s) { CalcValue v; v.isStr = true; v.str = std::move(s); return v; }
};

// A tape image: blocks of a .tap file (flag byte + data + checksum).
struct TapeBlock {
    std::vector<uint8_t> data;   // including the flag byte, excluding checksum
};

class Machine {
public:
    Machine(Host& host, const Options& options);

    // ---- public facade ------------------------------------------------------
    int run();
    void queueCommand(const std::string& utf8);
    void requestQuit(int code);
    void addCommand(const std::string& name, CommandHandler handler);
    void addFunction(const std::string& name, FunctionHandler handler);

    uint8_t peekUser(uint16_t addr);           // PEEK (screen memory is mapped)
    void pokeUser(uint16_t addr, uint8_t v);   // POKE (screen memory is mapped)

    // ---- memory -------------------------------------------------------------
    std::array<uint8_t, 65536> mem{};
    uint16_t word(uint16_t a) const { return uint16_t(mem[a] | (mem[uint16_t(a + 1)] << 8)); }
    void setWord(uint16_t a, uint16_t v) { mem[a] = uint8_t(v); mem[uint16_t(a + 1)] = uint8_t(v >> 8); }
    bool flag(uint16_t var, uint8_t bit) const { return (mem[var] & bit) != 0; }
    void setFlag(uint16_t var, uint8_t bit, bool on) { if (on) mem[var] |= bit; else mem[var] &= uint8_t(~bit); }

    // ---- Part 1: restarts ---------------------------------------------------
    [[noreturn]] void error(uint8_t errNr);   // L0008 ERROR-1 (sets X_PTR)
    [[noreturn]] void error3(uint8_t errNr);  // L0055 ERROR-3 (no X_PTR update)
    void printA(uint8_t a);                   // L0010 PRINT-A (RST 10)
    uint8_t getChar();                        // L0018 GET-CHAR (RST 18)
    uint8_t nextChar();                       // L0020 NEXT-CHAR (RST 20)
    uint8_t chAddPlus1();                     // L0074 CH-ADD+1
    uint8_t tempPtr1(uint16_t hl);            // L0077 TEMP-PTR1 (CH_ADD = hl+1)
    uint8_t tempPtr2(uint16_t hl);            // L0078 TEMP-PTR2 (CH_ADD = hl)
    bool skipOver(uint16_t& hl, uint8_t a);   // L007D SKIP-OVER
    uint16_t bcSpaces(uint16_t bc);           // L0030 BC-SPACES, returns DE

    // ---- Part 2: keyboard ---------------------------------------------------
    void interrupt();                         // L0038 MASK-INT (FRAMES, KEYBOARD)
    void pollKeys(int timeoutMs);
    void keyEvent(const KeyEvent& ev);
    int decodeKey(const KeyEvent& ev, bool lMode, uint8_t mode, bool forInkey); // L0333 K-DECODE
    int inkey();                              // INKEY$: key held now, or -1
    uint8_t inPort(uint16_t port);            // IN
    bool breakKey();                          // L1F54 BREAK-KEY

    // ---- Part 3: sound ------------------------------------------------------
    void beepCmd();                           // L03F8 BEEP

    // ---- Part 4: tape (files) -----------------------------------------------
    void saveEtc();                           // L0605 SAVE-ETC (CLASS-0B)
    void saveControl(const std::string& name, const uint8_t* hdr, uint16_t hl);   // L0970 SA-CONTRL
    void loadControl(int command, const std::string& name, const uint8_t* hdr, uint16_t hl); // L0767 LD-LOOK-H
    void mergeBlock(const std::vector<uint8_t>& bytes);   // L08B6 ME-CONTRL
    std::string resolveFile(const std::string& name, bool forSave);
    bool openTape(const std::string& path);
    void writeTap(const std::string& path, const std::vector<TapeBlock>& blocks);
    void loadText(const std::string& path, int command);
    std::string lineText(uint16_t p, uint16_t marker);
    void batchSyntaxError(uint16_t start);
    void saveText(const std::string& path);
    std::vector<std::string> listingLines();  // the program as a text listing
    void cellBitmap(int row, int col, uint8_t out[8]) const;
    void setCellFromBitmap(int row, int col, const uint8_t in[8]);
    void screenToMemory();                    // render cells into the display file
    void memoryToScreen();                    // the display file was loaded
    uint8_t peekDisplay(uint16_t addr);
    void pokeDisplay(uint16_t addr, uint8_t v);
    std::string tapePath;                     // the current "tape"
    std::vector<TapeBlock> tapeBlocks;
    size_t tapePos = 0;

    // ---- Part 5: screen and printer -----------------------------------------
    void printOut(uint8_t a);                 // L09F4 PRINT-OUT
    void poBack1();                           // L0A23 PO-BACK-1
    void poRight();                           // L0A3D PO-RIGHT
    void poEnter();                           // L0A4F PO-ENTER
    void poComma();                           // L0A5F PO-COMMA
    void poTv2(uint8_t a);                    // L0A6D PO-TV-2
    void poCont(uint8_t a);                   // L0A87 PO-CONT
    void poChange(uint16_t routine);          // L0A80 PO-CHANGE
    void poFill(int a);                       // L0AC3 PO-FILL
    void poAble(uint8_t a);                   // L0AD9 PO-ABLE
    void poStore(int b, int c);               // L0ADC PO-STORE
    void poFetch(int& b, int& c);             // L0B03 PO-FETCH
    void poAny(uint8_t a, int& b, int& c);    // L0B24 PO-ANY
    void prAll(const uint8_t* glyph, uint8_t code, int& b, int& c);  // L0B7F PR-ALL
    void poAttr(int row, int col);            // L0BDB PO-ATTR
    void poMsg(const char* text);             // L0C0A PO-MSG
    void poTokens(uint8_t token);             // L0C10 PO-TOKENS
    void poSave(uint8_t a);                   // L0C3B PO-SAVE
    void poScr(int& b, int c);                // L0C55 PO-SCR
    void poScr4(int b);                       // L0D02 PO-SCR-4
    void temps();                             // L0D4D TEMPS
    void cls();                               // L0D6B CLS
    void clsLower();                          // L0D6E CLS-LOWER
    void clChan();                            // L0D94 CL-CHAN
    void clAll();                             // L0DAF CL-ALL
    void clSet(int b, int c);                 // L0DD9 CL-SET
    void clScAll();                           // L0DFE CL-SC-ALL
    void clScroll(int b);                     // L0E00 CL-SCROLL
    void clLine(int b);                       // L0E44 CL-LINE
    void copyCmd();                           // L0EAC COPY
    void copyBuff();                          // L0ECD COPY-BUFF
    void clearPrb();                          // L0EDF CLEAR-PRB
    int rowOf(int b) const;                   // screen row of line b

    // ---- Part 5: the editor -------------------------------------------------
    void editor();                            // L0F2C EDITOR
    bool edLoopKey(uint8_t a);                // L0F38 ED-LOOP (true = ENTER)
    void addChar(uint8_t a);                  // L0F81 ADD-CHAR (also channel 'R')
    bool edKeys(uint8_t a);                   // L0F92 ED-KEYS (true = ENTER)
    void edEdit();                            // L0FA9 ED-EDIT
    void edDown();                            // L0FF3 ED-DOWN
    void edLeft();                            // L1007 ED-LEFT
    void edRight();                           // L100C ED-RIGHT (+ GW ED_FIX1)
    void edDelete();                          // L1015 ED-DELETE
    uint16_t edEdge(uint16_t cursor, bool& atStart); // L1031 ED-EDGE
    void edUp();                              // L1059 ED-UP
    void edList();                            // L106E ED-LIST
    void clearSp();                           // L1097 CLEAR-SP
    bool keyInput(uint8_t& a);                // L10A8 KEY-INPUT
    void edCopy();                            // L111D ED-COPY
    uint16_t setDe(bool carry, uint16_t* hlOut = nullptr); // L1190 SET-HL / L1195 SET-DE
    void removeFp(uint16_t hl);               // L11A7 REMOVE-FP
    uint8_t waitKey();                        // L15D4 WAIT-KEY
    bool inputAd(uint8_t& a);                 // L15E6 INPUT-AD
    uint8_t consIn();                         // GW CONS_IN

    // ---- Part 6: executive --------------------------------------------------
    void startNew(bool fromNew);              // L11CB START-NEW
    void newCmd();                            // L11B7 NEW
    void mainLoop();                          // L12A2 MAIN-EXEC .. MAIN-9
    void mainG(uint8_t a);                    // L1313 MAIN-G
    void mainAdd(uint16_t lineNumber);        // L155D MAIN-ADD
    void outCode(uint8_t a);                  // L15EF OUT-CODE
    void chanOpen(int stream);                // L1601 CHAN-OPEN
    void chanFlag(uint16_t hl);               // L1615 CHAN-FLAG
    void makeRoom(uint16_t hl, uint16_t bc);  // L1655 MAKE-ROOM
    uint16_t mrHL = 0, mrDE = 0;              // HL and DE after MAKE-ROOM
    void pointers(uint16_t pos, int delta);   // L1664 POINTERS
    uint16_t lineNo(uint16_t hl, uint16_t prevDe); // L1695 LINE-NO
    void setMin();                            // L16B0 SET-MIN
    void setWork();                           // L16BF SET-WORK
    void setStk();                            // L16C5 SET-STK
    void closeCmd();                          // L16E5 CLOSE #
    uint16_t strData(int stream, uint16_t& bc); // L171E STR-DATA
    void openCmd();                           // L1736 OPEN #
    void catEtc(uint8_t command);             // L1793 CAT, ERASE, FORMAT, MOVE
    void autoList();                          // L1795 AUTO-LIST
    void llistCmd();                          // L17F5 LLIST
    void listCmd(int stream = 2);             // L17F9 LIST
    void listAll(uint16_t hl, uint8_t e);     // L1833 LIST-ALL
    bool outLine(uint16_t& hl, uint8_t& e);   // L1855 OUT-LINE
    void outLine2(uint16_t hl);               // L187D OUT-LINE2
    uint16_t outLine3(uint16_t hl);           // L1881 OUT-LINE3
    static uint16_t numberSkip(const Machine& mm, uint16_t hl); // L18B6 NUMBER
    void outFlash(uint8_t a);                 // L18C1 OUT-FLASH
    void outCurs(uint16_t de);                // L18E1 OUT-CURS
    void lnFetch(uint16_t var);               // L190F LN-FETCH
    void outChar(uint8_t a);                  // L1937 OUT-CHAR
    uint16_t lineAddr(uint16_t lineNumber, uint16_t& prev, bool& exact); // L196E LINE-ADDR
    int cpLines(uint16_t hl, uint16_t bc);    // L1980 CP-LINES
    // L198B EACH-STMT: 0 = d'th statement reached, 1 = token e found,
    // 2 = end of line reached (d has been decremented once more).
    int eachStmt(uint16_t hl, uint8_t& d, uint8_t e);
    uint16_t nextOne(uint16_t hl);            // L19B8 NEXT-ONE
    void reclaim1(uint16_t de, uint16_t hl);  // L19E5 RECLAIM-1
    void reclaim2(uint16_t hl, uint16_t bc);  // L19E8 RECLAIM-2
    uint16_t eLineNo();                       // L19FB E-LINE-NO
    void outNum1(uint16_t bc);                // L1A1B OUT-NUM-1
    void outNum2(uint16_t hl);                // L1A28 OUT-NUM-2
    void usrCall(uint16_t addr, uint16_t& bc);// USR

    // ---- Part 7: interpreter ------------------------------------------------
    void lineScan();                          // L1B17 LINE-SCAN
    void runStatements(bool fromLineRun);     // STMT-LOOP ... LINE-END
    bool syntaxZ() const { return (mem[sv::FLAGS] & sv::F_RUNTIME) == 0; } // L2530 SYNTAX-Z
    void checkEnd();                          // L1BEE CHECK-END
    void separator(uint8_t c);                // L1B6F SEPARATOR
    uint8_t curToken = 0;                     // the command being executed
    void class01();                           // L1C1F CLASS-01
    void varA1(uint16_t hl, bool notFound, bool arrayZ, uint8_t c); // L1C22 VAR-A-1
    void class02();                           // L1C4E CLASS-02
    void valFet1();                           // L1C56 VAL-FET-1
    void valFet2(uint8_t a);                  // L1C59 VAL-FET-2
    void class04();                           // L1C6C CLASS-04
    void next2Num();                          // L1C79 NEXT-2NUM
    void expt2Num();                          // L1C7A EXPT-2NUM (CLASS-08)
    void expt1Num();                          // L1C82 EXPT-1NUM (CLASS-06)
    void exptExp();                           // L1C8C EXPT-EXP (CLASS-0A)
    void class07(uint8_t token);              // L1C96 CLASS-07
    void class09();                           // L1CBE CLASS-09
    void fetchNum();                          // L1CDE FETCH-NUM (CLASS-03)
    void useZero();                           // L1CE6 USE-ZERO
    void stopCmd();                           // GW TOGGLE (STOP)
    void forCmd();                            // L1D03 FOR
    bool lookProg(uint16_t hl, uint8_t e, uint8_t& d, uint16_t& bc); // L1D86 LOOK-PROG
    void nextCmd();                           // L1DAB NEXT
    bool nextLoop(uint16_t memAddr);          // L1DDA NEXT-LOOP (true = finished)
    void readCmd();                           // L1DED READ
    void dataCmd();                           // L1E27 DATA
    void passBy(uint8_t token);               // L1E39 PASS-BY
    void restoreCmd();                        // L1E42 RESTORE
    void restRun(uint16_t line);              // L1E45 REST-RUN
    void randomizeCmd();                      // L1E4F RANDOMIZE
    void continueCmd();                       // L1E5F CONTINUE
    void goToCmd();                           // L1E67 GO TO
    void goTo2(uint16_t line, uint8_t stmt);  // L1E73 GO-TO-2
    void outCmd();                            // L1E7A OUT
    void pokeCmd();                           // L1E80 POKE
    void twoParam(uint8_t& a, uint16_t& bc);  // L1E85 TWO-PARAM
    uint8_t findInt1();                       // L1E94 FIND-INT1
    uint16_t findInt2();                      // L1E99 FIND-INT2
    uint16_t findLine();                      // GW FIND_LINE
    void runCmd();                            // L1EA1 RUN
    void clearCmd();                          // L1EAC CLEAR
    void clearRun(uint16_t ramtop);           // L1EAF CLEAR-RUN
    void goSubCmd();                          // L1EED GO SUB
    void testRoom(uint32_t bc);               // L1F05 TEST-ROOM
    uint16_t freeMem();                       // L1F1A free-mem
    void returnCmd();                         // L1F23 RETURN
    void pauseCmd();                          // L1F3A PAUSE
    void defFnCmd();                          // L1F60 DEF FN
    void lprintCmd();                         // L1FC9 LPRINT
    void printCmd(int stream);                // L1FCD PRINT
    void print2();                            // L1FDF PRINT-2
    void printCr();                           // L1FF5 PRINT-CR
    void prItem1();                           // L1FFC PR-ITEM-1
    static bool prEndZ(uint8_t a);            // L2045 PR-END-Z
    static bool prStEnd(uint8_t a);           // L2048 PR-ST-END
    int prPosn1();                            // L204E PR-POSN-1 (0 none, 1 more, 2 end)
    bool strAlter();                          // L2070 STR-ALTER (true = stream given)
    void inputCmd();                          // L2089 INPUT
    void inItem1();                           // L20C1 IN-ITEM-1
    void inAssign();                          // L21B9 IN-ASSIGN
    bool inChanK();                           // L21D6 IN-CHAN-K
    void coTemp2();                           // L21E2 CO-TEMP-2
    bool coTemp3();                           // L21F2 CO-TEMP-3 (true = colour item)
    void coTemp4(uint8_t token);              // L21FC CO-TEMP-4
    void coTemp5(uint8_t control, uint8_t d); // L2211 CO-TEMP-5
    void borderCmd();                         // L2294 BORDER
    void remCmd();                            // GW NEWREM
    void extensionCommand();                  // zxgw: a statement starting with a name

    // ---- Part 7: graphics ---------------------------------------------------
    void pointSub();                          // L22CB POINT-SUB
    void plotCmd();                           // L22DC PLOT
    void plotSub(int x, int y);               // L22E5 PLOT-SUB
    void stkToBc(int& b, int& c, int& d, int& e); // L2307 STK-TO-BC
    void circleCmd();                         // L2320 CIRCLE
    void drawCmd();                           // L2382 DRAW
    int cdPrms1(double z, double angle, double mems[6]); // L247D CD-PRMS1
    void drawLine();                          // L24B7 DRAW-LINE
    void drawLineTo(double dx, double dy);
    void bcPostve(int& x, int& y);            // GW BC_POSTVE
    int dotsW() const;                        // pixel space width
    int dotsH() const;                        // pixel space height
    bool mapPixel(int x, int y, int& row, int& col, int& dx, int& dy) const;
    bool pixelSet(int row, int col, int dx, int dy) const;
    int coordsX = 0, coordsY = 0;             // COORDS (wider than a byte here)

    // ---- Part 8: expressions ------------------------------------------------
    void scanning();                          // L24FB SCANNING
    void s2Coord();                           // L2522 S-2-COORD
    void sScrnS();                            // L2535 S-SCRN$-S
    void sAttrS();                            // L2580 S-ATTR-S
    void stkToLc(int& line, int& col);        // GW STK_TO_LC
    void sDecimal();                          // L268D S-DECIMAL / S-BIN
    void sFnSbrn();                           // L27BD S-FN-SBRN
    uint8_t lookVars(uint16_t& hl, bool& notFound, bool& arrayZ); // L28B2 LOOK-VARS
    void stkVar(uint16_t hl, uint8_t c, uint16_t& elem); // L2996 STK-VAR
    void slicing();                           // L2A52 SLICING
    uint16_t intExp(uint16_t limit, bool& over); // L2ACC INT-EXP1
    uint16_t letCmd();                        // L2AFF LET (returns the value address)
    void lString(uint8_t letter);             // L2BC6 L-STRING
    CalcValue stkFetch();                     // L2BF1 STK-FETCH
    void dimCmd();                            // L2C02 DIM
    static bool alpha(uint8_t a);             // L2C8D ALPHA
    static bool alphanum(uint8_t a);          // L2C88 ALPHANUM
    static bool numeric(uint8_t a);           // L2D1B NUMERIC (true = digit)
    double decToFp();                         // L2C9B DEC-TO-FP
    double intToFp();                         // L2D3B INT-TO-FP
    bool extensionFunction();                 // zxgw extension functions
    std::string readName(uint16_t hl, uint16_t& end) const;

    // ---- Parts 9/10: calculator ---------------------------------------------
    std::vector<CalcValue> calc;
    void stack(double n) { calc.push_back(CalcValue::number(n)); }
    void stackStr(std::string s) { calc.push_back(CalcValue::string(std::move(s))); }
    double popNum();
    std::string popStr();
    double checked(double x);                 // round; report 6 on overflow
    void operation(uint8_t literal);          // one calculator literal
    uint16_t fpToBc(bool& overflow, bool& negative); // L2DA2 FP-TO-BC
    uint8_t fpToA(bool& overflow, bool& negative);   // L2DD5 FP-TO-A
    void printFp();                           // L2DE3 PRINT-FP
    double stackNum(uint16_t addr) const;     // L33B4 STACK-NUM
    void storeNum(uint16_t addr, double v);
    double rnd();                             // S-RND
    void valFunction(bool isValStr);          // L35DE val / val$

    // ---- GW additions -------------------------------------------------------
    void newEd();                             // NEWED: editor + tokenizer
    void tokenize(uint16_t start);            // the TOKENIZER
    int newTok(uint16_t& hl, uint8_t token, const char* text, bool remTable); // NEWTOK
    void impose();                            // IMPOSE
    void gwRemCommand(uint16_t eLine);        // NEWREM
    void helpScreen();                        // UNWIND
    void streamsCmd();                        // STRLST
    void blockDelete(const std::vector<uint16_t>& params); // BDEL
    void renumberCmd(std::vector<uint16_t> params);       // RENU

    // ---- state --------------------------------------------------------------
    Host& host;
    Options opts;
    Screen screen;
    int W = 32;                               // columns (the ROM's 32)
    int H = 24;                               // rows (the ROM's 24)
    bool batch = false;
    bool editing = false;                     // ED-COPY running: no transcript
    bool quit = false;
    int exitCode = 0;
    std::deque<std::string> pendingCommands;  // Spectrum encoded lines
    std::map<uint16_t, std::string> fnStrings;   // DEF FN string parameters

    // keyboard and timing
    KeyEvent heldKey;
    std::chrono::steady_clock::time_point heldUntil;
    bool breakPending = false;
    bool quitPending = false;
    bool queueKeys = false;                   // type-ahead while editing
    std::deque<KeyEvent> keyQueue;            // typed while editing (type-ahead, paste)
    std::deque<KeyEvent> runKeys;             // pressed while a program runs
    std::deque<uint8_t> codeQueue;            // decoded codes (Home/End/Delete)
    std::chrono::steady_clock::time_point started, lastInterrupt, lastPresent, lastPoll;
    uint64_t framesBase = 0;
    uint64_t presentedVersion = ~0ull;
    uint64_t statements = 0;
    std::chrono::steady_clock::time_point throttleStart;
    void present(bool force = false);
    void idle(int ms);                        // wait, present, poll keys
    void statementTick();                     // between statements

    // printer
    std::vector<Cell> printerLine;

    // GO SUB stack below RAMTOP, three bytes per entry
    uint16_t gosubSp = 0;
    uint16_t virtualSp() const { return uint16_t(gosubSp - 4); }

    // extensions
    std::map<std::string, CommandHandler> commands;
    std::map<std::string, FunctionHandler> functions;

    void transcriptChar(uint8_t code);
    bool transcribing() const { return !editing && !(mem[sv::TV_FLAG] & sv::TV_AUTOLIST); }
    void syncGeometry(bool force);
    void resizeScreen(int w, int h);
};

// Downsample an 8x8 bitmap to a 2x4 braille dot pattern.
uint8_t downsampleGlyph(const uint8_t* glyph);

// A line of a text listing to Spectrum characters (\a..\u, \{n} escapes).
std::string fromListing(const std::string& utf8);

// The Unicode text of a Spectrum character (UDGs and tokens are expanded).
std::string spectrumCharToUtf8(uint8_t c);

} // namespace zxgw

#endif
