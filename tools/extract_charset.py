#!/usr/bin/env python3
"""Extract the ZX Spectrum character set from gw_rom.s into src/core/charset.cpp.

The ROM listing contains the 96 character bitmaps (codes 32..127) at label
L3D00 as `DEFB %xxxxxxxx` lines.  This script turns them into a C++ table so
the translation does not need a binary ROM image.

Usage:  python3 tools/extract_charset.py [path/to/gw_rom.s] [output.cpp]
"""
import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "gw_rom.s"
dst = sys.argv[2] if len(sys.argv) > 2 else "src/core/charset.cpp"

lines = open(src, encoding="latin-1").read().split("\n")
start = next(i for i, l in enumerate(lines) if l.startswith("L3D00:"))
vals = []
for l in lines[start:]:
    if l.startswith("#end"):
        break
    m = re.search(r"DEFB\s+%([01]{8})", l)
    if m:
        vals.append(int(m.group(1), 2))
assert len(vals) == 96 * 8, len(vals)

out = [
    '// Generated from gw_rom.s (L3D00 "char-set") by tools/extract_charset.py.',
    "// The 96 character bitmaps for codes 32..127, 8 bytes each, top row first.",
    "",
    '#include "tables.h"',
    "",
    "namespace zxgw {",
    "",
    "const uint8_t kRomCharset[96 * 8] = {",
]
for c in range(96):
    row = ", ".join("0x%02X" % v for v in vals[c * 8:c * 8 + 8])
    out.append("    %s, // %d" % (row, c + 32))
out += ["};", "", "} // namespace zxgw", ""]
open(dst, "w").write("\n".join(out))
print("wrote", dst)
