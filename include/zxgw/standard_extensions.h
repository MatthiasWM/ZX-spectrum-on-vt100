// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// A small set of extension commands and functions that make sense in a
// terminal.  They are also a worked example of the extension API.
//
//   BYE [code]           leave zxgw
//   CD "dir"             change the working directory
//   UPPER$(a$)           upper case
//   LOWER$(a$)           lower case
//   INSTR(a$, b$[, n])   position of b$ in a$ (from position n), 0 if absent
//   MAX(a, b, ...)       largest number
//   MIN(a, b, ...)       smallest number
//   TIME$()              "hh:mm:ss"
//   DATE$()              "yyyy-mm-dd"
//   ENV$(name$)          an environment variable

#ifndef ZXGW_STANDARD_EXTENSIONS_H
#define ZXGW_STANDARD_EXTENSIONS_H

#include "zxgw/zxgw.h"

namespace zxgw {

void addStandardExtensions(Spectrum& spectrum);

} // namespace zxgw

#endif
