// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Parts 9 and 10. ARITHMETIC ROUTINES and the FLOATING-POINT CALCULATOR.
//
// The ROM implements its own five byte floating point arithmetic and a
// FORTH-like calculator language (RST 28).  Here the arithmetic is done
// with doubles, and every result is rounded to the precision and range of
// the five byte format (fp::normalize), so the visible behaviour matches:
// eight significant digits are printed, results beyond 1.7E38 give
// "6 Number too big", and the error conditions of the original functions
// are reproduced (e.g. a negative number raised to a power is
// "A Invalid argument", as the ROM computes x^y as EXP(y*LN x)).

#include "machine.h"

#include <cmath>

#include "fp.h"

namespace zxgw {

using namespace sv;

double Machine::popNum() {
    if (calc.empty()) return 0;
    double v = calc.back().num;
    calc.pop_back();
    return v;
}

std::string Machine::popStr() {
    if (calc.empty()) return std::string();
    std::string s = std::move(calc.back().str);
    calc.pop_back();
    return s;
}

// Round to the Spectrum format; overflow is "6 Number too big".
double Machine::checked(double x) {
    if (!fp::normalize(x)) error(0x05);
    return x;
}

double Machine::stackNum(uint16_t addr) const { return fp::read(&mem[addr]); }   // L33B4 STACK-NUM

void Machine::storeNum(uint16_t addr, double v) { fp::write(v, &mem[addr]); }

// L2DA2 FP-TO-BC: the last value rounded to an integer 0..65535.
uint16_t Machine::fpToBc(bool& overflow, bool& negative) {
    double v = popNum();
    double r = (v == std::floor(v) && std::fabs(v) <= 65535) ? v : std::floor(v + 0.5);
    negative = r < 0;
    double mag = std::fabs(r);
    overflow = mag > 65535;
    return overflow ? 0 : uint16_t(mag);
}

// L2DD5 FP-TO-A: the last value rounded to 0..255.
uint8_t Machine::fpToA(bool& overflow, bool& negative) {
    uint16_t bc = fpToBc(overflow, negative);
    if (bc > 0xFF) overflow = true;
    return uint8_t(bc);
}

// L2DE3 PRINT-FP
void Machine::printFp() {
    std::string s = fp::format(popNum());
    for (char ch : s) printA(uint8_t(ch));
}

// S-RND: SEED = (SEED+1)*75 mod 65537 - 1, the result is SEED/65536.
double Machine::rnd() {
    double s = word(SEED);
    double v = (s + 1) * 75;
    double r = v - std::floor(v / 65537) * 65537;              // n-mod-m
    double n = r - 1;
    setWord(SEED, uint16_t(n));
    return n / 65536;
}

// L35DE val / val$: the string is copied to the workspace, checked for
// syntax and then evaluated.
void Machine::valFunction(bool isValStr) {
    uint16_t chAdd = word(CH_ADD);
    std::string s = popStr();
    uint16_t de = bcSpaces(uint16_t(s.size() + 1));
    for (size_t i = 0; i < s.size(); ++i) mem[uint16_t(de + i)] = uint8_t(s[i]);
    mem[uint16_t(de + s.size())] = 0x0D;
    setWord(CH_ADD, de);
    setFlag(FLAGS, F_RUNTIME, false);
    scanning();
    if (getChar() != 0x0D) error(0x0B);
    bool numericResult = flag(FLAGS, F_NUMERIC);
    if (numericResult == isValStr) error(0x0B);                // V-RPORT-C
    setWord(CH_ADD, de);
    setFlag(FLAGS, F_RUNTIME, true);
    scanning();
    setWord(CH_ADD, chAdd);
}

// One calculator operation (the literal numbers are those of the ROM's
// table of addresses at L32D7).
void Machine::operation(uint8_t literal) {
    switch (literal) {
    // ---- binary numeric operations ------------------------------------
    case 0x03: { double b = popNum(), a = popNum(); stack(checked(a - b)); break; }   // subtract
    case 0x04: { double b = popNum(), a = popNum(); stack(checked(a * b)); break; }   // multiply
    case 0x05: {                                                                      // division
        double b = popNum(), a = popNum();
        if (b == 0) error(0x05);
        stack(checked(a / b));
        break;
    }
    case 0x06: {                                                                      // to-power
        double y = popNum(), x = popNum();
        if (x == 0) {                                          // XISO
            if (y == 0) stack(1);
            else if (y > 0) stack(0);
            else error(0x05);                                  // 1/0
            break;
        }
        if (x < 0) error(0x09);                                // LN of a negative number
        stack(checked(std::pow(x, y)));
        break;
    }
    case 0x07: { double b = popNum(), a = popNum(); stack(b != 0 ? 1 : a); break; }   // or
    case 0x08: { double b = popNum(), a = popNum(); stack(b != 0 ? a : 0); break; }   // no-&-no
    case 0x09: { double b = popNum(), a = popNum(); stack(a <= b ? 1 : 0); break; }   // no-l-eql
    case 0x0A: { double b = popNum(), a = popNum(); stack(a >= b ? 1 : 0); break; }   // no-gr-eq
    case 0x0B: { double b = popNum(), a = popNum(); stack(a != b ? 1 : 0); break; }   // nos-neql
    case 0x0C: { double b = popNum(), a = popNum(); stack(a > b ? 1 : 0); break; }    // no-grtr
    case 0x0D: { double b = popNum(), a = popNum(); stack(a < b ? 1 : 0); break; }    // no-less
    case 0x0E: { double b = popNum(), a = popNum(); stack(a == b ? 1 : 0); break; }   // nos-eql
    case 0x0F: { double b = popNum(), a = popNum(); stack(checked(a + b)); break; }   // addition

    // ---- string operations ---------------------------------------------
    case 0x10: { double b = popNum(); std::string a = popStr(); stackStr(b != 0 ? a : std::string()); break; } // str-&-no
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: {               // comparisons
        std::string b = popStr(), a = popStr();
        int r = a.compare(b);
        bool v = false;
        switch (literal) {
        case 0x11: v = r <= 0; break;                          // str-l-eql
        case 0x12: v = r >= 0; break;                          // str-gr-eq
        case 0x13: v = r != 0; break;                          // strs-neql
        case 0x14: v = r > 0; break;                           // str-grtr
        case 0x15: v = r < 0; break;                           // str-less
        case 0x16: v = r == 0; break;                          // strs-eql
        }
        stack(v ? 1 : 0);
        break;
    }
    case 0x17: { std::string b = popStr(), a = popStr(); stackStr(a + b); break; }     // strs-add
    case 0x18: valFunction(true); break;                                              // val$
    case 0x19: {                                                                      // usr-$
        std::string s = popStr();
        if (s.size() != 1) error(0x09);
        uint8_t ch = uint8_t(s[0]);
        int offset;
        if (alpha(ch)) {
            offset = ((ch - 1) * 8) & 0xFF;
        } else {
            if (ch < 0x90 || ch >= 0x90 + 0x15) error(0x09);
            offset = (ch - 0x90) * 8;
        }
        if (offset >= 0xA8) error(0x09);
        stack(uint16_t(word(UDG) + offset));
        break;
    }
    case 0x1A: {                                                                      // read-in
        uint8_t s = findInt1();
        if (s >= 16) error(0x0A);
        uint16_t curchl = word(CURCHL);
        chanOpen(s);
        uint8_t a;
        bool got = inputAd(a);
        chanFlag(curchl);
        stackStr(got ? std::string(1, char(a)) : std::string());
        break;
    }

    // ---- unary operations -------------------------------------------------
    case 0x1B: stack(-popNum()); break;                                               // negate
    case 0x1C: { std::string s = popStr(); stack(s.empty() ? 0 : uint8_t(s[0])); break; } // code
    case 0x1D: valFunction(false); break;                                             // val
    case 0x1E: { std::string s = popStr(); stack(double(s.size())); break; }          // len
    case 0x1F: stack(checked(std::sin(popNum()))); break;                             // sin
    case 0x20: stack(checked(std::cos(popNum()))); break;                             // cos
    case 0x21: {                                                                      // tan
        double x = popNum();
        double c = std::cos(x);
        if (c == 0) error(0x05);
        stack(checked(std::sin(x) / c));
        break;
    }
    case 0x22: case 0x23: {                                                           // asn, acs
        double x = popNum();
        if (1 - x * x < 0) error(0x09);
        stack(checked(literal == 0x22 ? std::asin(x) : std::acos(x)));
        break;
    }
    case 0x24: stack(checked(std::atan(popNum()))); break;                            // atn
    case 0x25: {                                                                      // ln
        double x = popNum();
        if (x <= 0) error(0x09);                               // REPORT-Ab
        stack(checked(std::log(x)));
        break;
    }
    case 0x26: {                                                                      // exp
        double r = std::exp(popNum());
        if (std::isinf(r)) error(0x05);
        stack(checked(r));
        break;
    }
    case 0x27: stack(std::floor(popNum())); break;                                    // int
    case 0x28: {                                                                      // sqr
        double x = popNum();
        if (x == 0) { stack(0); break; }
        if (x < 0) error(0x09);
        stack(checked(std::sqrt(x)));
        break;
    }
    case 0x29: { double x = popNum(); stack(x > 0 ? 1 : x < 0 ? -1 : 0); break; }     // sgn
    case 0x2A: stack(std::fabs(popNum())); break;                                     // abs
    case 0x2B: stack(peekUser(findInt2())); break;                                    // peek
    case 0x2C: stack(inPort(findInt2())); break;                                      // in
    case 0x2D: {                                                                      // usr-no
        uint16_t bc;
        usrCall(findInt2(), bc);
        stack(bc);
        break;
    }
    case 0x2E: stackStr(fp::format(popNum())); break;                                 // str$
    case 0x2F: {                                                                      // chr$
        bool over, neg;
        uint8_t a = fpToA(over, neg);
        if (over || neg) error(0x0A);                          // REPORT-Bd
        stackStr(std::string(1, char(a)));
        break;
    }
    case 0x30: stack(popNum() == 0 ? 1 : 0); break;                                   // not
    default:
        break;
    }
}

} // namespace zxgw
