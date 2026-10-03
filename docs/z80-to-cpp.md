# From Z80 to C++

The translation follows the ROM routine by routine. Every C++ function names
the routine it comes from, with the address label of the listing:

```cpp
// L1B17 LINE-SCAN: check the syntax of the edit line.
void Machine::lineScan() {
```

`grep -n L1B17 src/core/*.cpp` finds the translation of the routine at
`L1B17` in `gw_rom.s`, and vice versa. Labels without an address (`NEWED`,
`IMPOSE`, `RENU`, ...) are routines from the GW spare section.

This document describes the patterns used.

## Registers

| Z80 | C++ |
|-----|-----|
| `A`, `BC`, `DE`, `HL` carrying values | parameters and return values, often named after the register (`b`, `c`, `hl`) |
| `HL` after MAKE-ROOM | `mrHL`, `mrDE`: the routine's documented exit registers |
| `IY` (always `$5C3A`) | the system variables in `mem[]`, by name: `mem[FLAGS]`, `word(CH_ADD)` |
| `IX` (tape descriptors) | a local `uint8_t hdr[17]` |
| the alternate register set | not needed |
| carry / zero flags as results | `bool` results, or small enums where both mattered (see below) |

Some routines return two flags, and their callers test either one. These
get explicit results:

| Routine | C++ |
|---------|-----|
| L28B2 LOOK-VARS: carry = not found, zero = "string or array" | `lookVars(hl, notFound, arrayZ)` |
| L198B EACH-STMT: zero = statement reached, carry = end of line | returns 0 / 1 / 2 |
| L204E PR-POSN-1: zero = more items; may drop its caller's return address | returns 0 / 1 / 2 |
| L2DA2 FP-TO-BC: carry = overflow, zero = positive | `fpToBc(overflow, negative)` |

## The restarts

| Restart | C++ |
|---------|-----|
| `RST 00` START | `startNew(false)` |
| `RST 08` ERROR-1 | `error(errNr)`: sets X_PTR and throws `BasicError` |
| `RST 10` PRINT-A | `printA(a)`: calls the current channel's output routine |
| `RST 18` GET-CHAR | `getChar()` |
| `RST 20` NEXT-CHAR | `nextChar()` |
| `RST 28` FP-CALC | C++ arithmetic on `calc` (see below) |
| `RST 30` BC-SPACES | `bcSpaces(n)` |
| `RST 38` MASK-INT | `interrupt()`, called regularly instead of 50 times a second |

## Stack tricks become exceptions

The ROM often changes its stack pointer to continue somewhere else. Each
case has a C++ equivalent:

| ROM | C++ |
|-----|-----|
| `RST 08`: `SP := ERR_SP`, return to the error handler | `throw BasicError{errNr}` |
| ERR_SP → MAIN-4 (the report) | `catch` in `mainLoop()` |
| ERR_SP → ED-ERROR (editing continues after a "rasp") | `catch` in `editor()` |
| ERR_SP → IN-VAR-1 (INPUT is edited again) | `catch` loop in `inItem1()` |
| ERR_SP → ED-FULL (lower screen full) | `catch` in `edCopy()` |
| ERR_SP → REPORT-G (no room for the line) | `catch` in `mainAdd()` |
| CHECK-END while checking syntax: drop two return addresses, go to STMT-NEXT | `throw SyntaxStatementEnd{}`, caught by the statement loop |
| UNSTACK-Z: return from the caller when checking syntax | `if (syntaxZ()) return;` |
| REM, IF: drop STMT-RET to end the line | `After::LineEnd` from the command dispatch |
| AUTO-LIST: `SP := LIST_SP` when the screen is full | `throw AutoListStop{}` |
| NEW: re-initialise and go to MAIN-1 | `throw RestartMain{}` |
| CLEAR: new RAMTOP, new stack | `gosubSp` is reset |
| GO SUB / RETURN: entries on the machine stack | three byte entries below RAMTOP (`gosubSp`), same layout |

Because errors are exceptions, `error()` is `[[noreturn]]` and C++ code after
it never runs, just as nothing after `RST 08` runs.

## Tables and indirect jumps

| ROM | C++ |
|-----|-----|
| L1A48 offset table, L1A7A parameter table | `kSyntax[]` in `interpreter.cpp`, same class codes and separators |
| L1C01 class table (CLASS-00 .. CLASS-0B) | `switch` in `runStatements()` |
| L2596 scanning function table | `switch` at the start of `scanning()` |
| L2795 operator table, L27B0 priorities | `operatorCode()`, `kPriorities[]` |
| L32D7 calculator address table | `operation(literal)`, same literal numbers |
| L0A11 control character table | `switch` in `printOut()` |
| L0FA0 editing keys table | `switch` in `edKeys()` |
| channel records holding routine addresses | the addresses are kept (`PRINT_OUT` = `$09F4`, `KEY_INPUT` = `$10A8`, ...) and dispatched with a `switch` in `printA()` and `inputAd()` |

Keeping the routine addresses in the channel records means PO-CHANGE still
works: after `AT` or `INK` is printed, the channel's output address becomes
`PO_TV_2`/`PO_CONT` until the operands have arrived, exactly as before.

## The calculator

The ROM computes with its own five byte floating point format and a FORTH-like
language entered with `RST 28`:

```
RST 28H          ; FP-CALC          x, y.
DEFB $C0         ; st-mem-0
DEFB $02         ; delete
DEFB $31         ; duplicate
...
DEFB $38         ; end-calc
```

These sequences are written as C++ expressions on doubles. After each
operation the result goes through `fp::normalize()`, which rounds it to the
32 bit mantissa of the Spectrum format and gives report 6 when it exceeds
about 1.7E38. Values stored in memory (variables, hidden numbers in lines,
FOR loop limits) use the original five byte layout (`fp::read`, `fp::write`),
including the small integer form `00 ss lo hi 00`.

PRINT-FP (L2DE3) is reimplemented in `fp::format()` with the same algorithm:
integer digits by BCD conversion, fraction digits by multiplying a 32 bit
fixed point fraction by ten, rounding at eight digits, the E format rules
and the ROM's quirks (`0.5` but `.05`).

## Memory routines

MAKE-ROOM (L1655), POINTERS (L1664) and RECLAIM-1/2 (L19E5) are translated
literally: the same fourteen dynamic pointers from VARS to STKEND are
adjusted when they are above the position, and the memory move behaves like
`LDDR`/`LDIR`. The callers use the documented exit values (`mrHL`, `mrDE`).
This matters because some callers rely on details. For example, DEF FN
creates its hidden value slots with MAKE-ROOM *at* the parameter letter and
then writes the marker one byte later. This works because LDDR leaves a stale
copy of the letter in place, and the C++ `memmove` does the same.

## Screen positions

The ROM counts print positions backwards: line 24 ($18) is the top line and
column 33 ($21) is the left edge, both stored in S_POSN, SPOSNL and P_POSN.
The translation keeps this, so the original arithmetic carries over. The
constants of the 32×24 screen are replaced by the terminal size:

| ROM constant | Meaning | C++ |
|--------------|---------|-----|
| `$18` (24) | number of lines | `H` |
| `$17` (23) | lines to scroll | `H - 1` |
| `$19` (25) | one past the top | `H + 1` |
| `$16` (22) | last line for AT | `H - 2` |
| `$21` (33) | leftmost column | `W + 1` |
| `$20` (32) | columns | `W` (or the printer width) |
| `$1F` (31) | last column for AT | `W - 1` |
| `$02C0` (704) | characters in the upper screen (AUTO-LIST) | `(H - 2) * W` |
| `$AF` (175), 255 | highest pixel coordinates | `dotsH() - 1`, `dotsW() - 1` |
| 16 (comma tab stops) | half of 32 | 16 (tab stops every 16 columns) |

CL-ADDR and PO-ATTR, which turn a line and column into a display file
address, are replaced by `rowOf()` and direct access to the screen cell.

## What was not translated

These parts of the ROM drive hardware that does not exist in a terminal:

- the tape signal routines (SA-BYTES, LD-BYTES, LD-EDGE ...): replaced by
  reading and writing `.tap` blocks;
- the keyboard matrix scan (KEY-SCAN, K-TEST): replaced by decoding terminal
  keys (`decodeKey()`, keeping K-DECODE's modes and tables);
- the beeper timing loop (BEEPER): the frequency and duration are computed as
  in BEEP and passed to the host;
- the RAM test, the NMI routine and the interrupt mode setup;
- the floating point arithmetic internals (addition, multiplication,
  division, the series generator for SIN, EXP, LN ...): replaced by doubles
  with Spectrum rounding.

Machine code cannot run: `USR` with an address returns the address (as if the
code were a single `RET`), except for a few known ROM entry points (see
[curiosities.md](curiosities.md)).
