// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The Spectrum five byte number format.
//
// Values are computed with C++ doubles but every result is rounded to the
// precision of the Spectrum format (32 bit mantissa, exponent -127..+127)
// and stored in memory in the original five byte layout:
//
//   small integer  00 ss ll hh 00      -65535..65535, ss = 00 or FF (sign)
//   floating point ee mm mm mm mm      value = 0.1mmm... * 2^(ee-128),
//                                       bit 7 of the first mantissa byte is
//                                       the sign (the leading 1 is implied)

#ifndef ZXGW_FP_H
#define ZXGW_FP_H

#include <cstdint>
#include <string>

namespace zxgw {
namespace fp {

// Round x to Spectrum precision.  Returns false if the magnitude is too
// big for the format (report 6 "Number too big").  Tiny values become 0.
bool normalize(double& x);

// Read / write the five byte form.
double read(const uint8_t* p);
void write(double x, uint8_t* p);

// L2DE3 PRINT-FP: the text of a number exactly as the ROM prints it,
// e.g. "0.5", ".05", "1E+9", "1.2345679E+8".
std::string format(double x);

// L2C9B DEC-TO-FP helpers: x * 10^e as done by E-TO-FP (L2D4F).
double eToFp(double x, int e);

} // namespace fp
} // namespace zxgw

#endif
