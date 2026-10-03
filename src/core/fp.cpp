// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The Spectrum five byte number format and PRINT-FP.

#include "fp.h"

#include <cmath>

namespace zxgw {
namespace fp {

namespace {

// Split a normalized, non-zero value into exponent byte and 32 bit mantissa
// (with the leading bit set).
void split(double x, int& expByte, uint32_t& mant) {
    int e;
    double m = std::frexp(std::fabs(x), &e);          // 0.5 <= m < 1
    uint64_t M = (uint64_t)std::floor(std::ldexp(m, 32) + 0.5);
    if (M >= 0x100000000ull) { M >>= 1; ++e; }
    expByte = e + 128;
    mant = (uint32_t)M;
}

} // namespace

bool normalize(double& x) {
    if (std::isnan(x)) return false;
    if (x == 0) { x = 0; return true; }
    if (std::isinf(x)) return false;
    int expByte;
    uint32_t mant;
    split(x, expByte, mant);
    if (expByte > 255) return false;
    if (expByte < 1) { x = 0; return true; }
    double r = std::ldexp((double)mant, expByte - 128 - 32);
    x = x < 0 ? -r : r;
    return true;
}

double read(const uint8_t* p) {
    if (p[0] == 0) {
        // L2D7F INT-FETCH: small integer in two's complement.
        int v = p[2] | (p[3] << 8);
        if (p[1] == 0xFF) return double(v - 65536);
        return double(v);
    }
    uint32_t mant = (uint32_t(p[1] | 0x80) << 24) | (uint32_t(p[2]) << 16) |
                    (uint32_t(p[3]) << 8) | p[4];
    double r = std::ldexp((double)mant, p[0] - 128 - 32);
    return (p[1] & 0x80) ? -r : r;
}

void write(double x, uint8_t* p) {
    if (!normalize(x)) x = x < 0 ? -1.7014118346e38 : 1.7014118346e38;
    if (x == std::floor(x) && std::fabs(x) <= 65535) {
        // L2D8E INT-STORE.
        int v = (int)x;
        p[0] = 0;
        p[1] = v < 0 ? 0xFF : 0x00;
        int stored = v < 0 ? v + 65536 : v;
        p[2] = uint8_t(stored & 0xFF);
        p[3] = uint8_t(stored >> 8);
        p[4] = 0;
        return;
    }
    int expByte;
    uint32_t mant;
    split(x, expByte, mant);
    p[0] = uint8_t(expByte);
    p[1] = uint8_t(((mant >> 24) & 0x7F) | (x < 0 ? 0x80 : 0));
    p[2] = uint8_t(mant >> 16);
    p[3] = uint8_t(mant >> 8);
    p[4] = uint8_t(mant);
}

double eToFp(double x, int e) {
    // L2D4F E-TO-FP multiplies or divides by powers of ten built from the
    // bits of the exponent.  Doing the same keeps the rounding behaviour
    // close to the original.
    bool divide = e < 0;
    unsigned n = (unsigned)(divide ? -e : e);
    double ten = 10;
    while (n) {
        if (n & 1) {
            x = divide ? x / ten : x * ten;
            normalize(x);
        }
        n >>= 1;
        if (n) { ten = ten * ten; normalize(ten); }
    }
    return x;
}

// ---------------------------------------------------------------------------
// L2DE3 PRINT-FP
//
// The ROM converts the integer part with a BCD shift loop and the fraction
// part by repeated multiplication of a 32 bit fixed point value by ten, then
// rounds to eight significant digits.  The decisions below follow the
// labels of the original routine.
// ---------------------------------------------------------------------------
std::string format(double x) {
    std::string out;
    normalize(x);
    if (x < 0) { out += '-'; x = -x; }               // PF-NEGTVE
    if (x == 0) { out += '0'; return out; }

    int digits[24] = {0};    // mem-3 / mem-4 digit buffer
    int count = 0;           // MEM-5-1st: number of digits
    int lead = 0;            // MEM-5-2nd: digits before the decimal point
    bool roundUp = false;
    double fraction = 0;
    bool roundNow = false;

    auto appendInt = [&](uint64_t v) {               // PF-BITS / PF-DIGITS
        char buf[24];
        int n = 0;
        while (v) { buf[n++] = char(v % 10); v /= 10; }
        while (n) { digits[count++] = buf[--n]; ++lead; }
    };

    for (;;) {                                       // PF-LOOP
        double ix = std::floor(x);
        double f = x - ix;
        if (ix < 65536) {
            if (ix == 0) {
                // PF-SMALL: x is a fraction; scale it so that one digit is
                // left of the point.
                int expByte;
                uint32_t mant;
                split(f, expByte, mant);
                int a = expByte - 0x7E;
                int d = (int)std::fabs(std::floor(a * 0.30103));  // LOG(2^A)
                lead -= d;
                double s = eToFp(f, d);
                double si = std::floor(s);
                fraction = s - si;
                normalize(fraction);
                int digit = (int)si;
                digits[0] = digit;
                int c = digit != 0 ? 1 : 0;
                count = c;
                lead += c;
                break;
            }
            appendInt((uint64_t)ix);
            fraction = f;
            break;
        }
        int expByte;
        uint32_t mant;
        split(ix, expByte, mant);
        int e = expByte - 0x80;
        if (e < 28) {                                // PF-MEDIUM
            appendInt((uint64_t)ix);
            if (count >= 9) {
                count = 8;
                roundUp = digits[8] > 4;
                roundNow = true;
            }
            fraction = f;
            break;
        }
        // PF-LARGE: scale down by a power of ten and try again.
        int n = (int)std::floor(e * 0.30103) - 7;
        lead += n;
        x = eToFp(ix, -n);
    }

    if (!roundNow) {
        // PF-FRACTN: make a 32 bit fixed point fraction (SHIFT-FP).
        uint32_t F = 0;
        if (fraction != 0) {
            int expByte;
            uint32_t mant;
            split(fraction, expByte, mant);
            int s = 0x80 - expByte;
            if (s == 0) {
                F = mant;
            } else if (s < 33) {
                uint64_t m = mant;
                bool lastOut = (m >> (s - 1)) & 1;
                F = (uint32_t)(m >> s);
                if (lastOut) ++F;                    // ADD-BACK
            }
        }
        while (count < 8) {                          // PF-FRN-LP / PF-FR-DGT
            uint64_t t = (uint64_t)F * 10;
            digits[count++] = int(t >> 32);
            F = (uint32_t)t;
        }
        roundUp = (F & 0x80000000u) != 0;
    }

    // PF-ROUND: round and drop trailing zeros.
    {
        int b = count;
        int i = count;
        bool carry = roundUp;
        bool done = false;
        while (b > 0) {
            --i;
            int d = digits[i] + (carry ? 1 : 0);
            digits[i] = d;
            if (d == 0) {
                carry = false;
            } else if (d < 10) {
                count = b;
                done = true;
                break;
            } else {
                carry = true;
            }
            --b;
        }
        if (!done) {
            digits[0] = 1;
            count = 1;
            ++lead;
        }
    }

    // PF-COUNT: output in plain or E format.
    auto outDigits = [&](int b, int c, int pos) {    // PF-E-SBRN
        int idx = pos;
        int a = -b;
        if (a < 0) {
            // PF-OUT-LP: b leading digits (zeros if we run out of digits)
            for (; b > 0; --b) {
                int d = 0;
                if (c) { d = digits[idx++]; --c; }
                out += char('0' + d);
            }
        } else {
            b = a;
        }
        if (c == 0) return;                          // PF-DC-OUT
        out += '.';
        for (int z = 0; z < b; ++z) out += '0';      // PF-DEC-0$
        while (c) { out += char('0' + digits[idx++]); --c; }
    };

    unsigned ub = (unsigned)(lead & 0xFF);
    if (ub < 9 || ub >= 0xFC) {                      // PF-NOT-E
        if (lead == 0) out += '0';
        outDigits(lead, count, 0);
    } else {                                         // PF-E-FRMT
        int e = lead - 1;
        outDigits(1, count, 0);
        out += 'E';
        out += e < 0 ? '-' : '+';
        out += std::to_string(e < 0 ? -e : e);
    }
    return out;
}

} // namespace fp
} // namespace zxgw
