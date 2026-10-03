# Architecture

This document explains how the source tree is organised and how the parts of
the ROM listing (`gw_rom.s`) map onto it.

## Layers

```
 app/main.cpp            command line, options
      │
 zxgw::Spectrum          public facade (include/zxgw/zxgw.h)
      │
 zxgw::Machine           the translated ROM (src/core)
      │   ▲
      │   │ Host interface (include/zxgw/host.h)
      ▼   │
 TerminalHost / BatchHost (src/platform/terminal_host.cpp)
      │
 Terminal                raw keyboard, escape sequences, window size
                         (terminal_posix.cpp, terminal_win32.cpp)
```

The `Machine` never talks to the terminal itself. Everything the ROM did with
hardware goes through the `Host`:

| ROM / hardware | Host function |
|----------------|---------------|
| the ULA displaying the screen | `present(const Screen&)` |
| the keyboard matrix read by the interrupt | `readKey()` |
| the beeper | `beep()` |
| the ZX Printer | `printerLine()` |
| (new) plain text output for batch use | `transcript()` |

## Source tree

```
include/zxgw/
  zxgw.h                 Spectrum (the facade) and Options
  host.h                 the Host interface, KeyEvent
  screen.h               Cell and Screen: the character cell display
  extension.h            Statement, FunctionCall, Value, Report: extension API
  terminal_host.h        TerminalHost (ANSI terminal) and BatchHost (stdin/stdout)
  standard_extensions.h  BYE, CD, UPPER$ ... (used by the application)
src/core/
  machine.h              the Machine class: one member per ROM routine
  sysvars.h              memory map, system variables, tokens, ROM addresses
  machine.cpp            Part 1 restarts, Part 6 executive (main loop, channels,
                         MAKE-ROOM, line and variable search)
  keyboard.cpp           Part 2 keyboard decoding
  sound.cpp              Part 3 BEEP
  tape.cpp               Part 4 SAVE/LOAD/VERIFY/MERGE with files
  files.cpp              CAT, ERASE, MOVE, FORMAT on the file system
  screen.cpp             Part 5 output: PRINT-OUT, scrolling, CLS, printer
  editor.cpp             Part 5 editor and KEY-INPUT, Part 6 LIST/AUTO-LIST
  interpreter.cpp        Part 7 LINE-SCAN, statement loop, command classes,
                         commands
  graphics.cpp           Part 7 PLOT, DRAW, CIRCLE, POINT
  expression.cpp         Part 8 SCANNING, LOOK-VARS, STK-VAR, SLICING, LET, DIM
  calculator.cpp         Parts 9 and 10: arithmetic and functions
  fp.cpp, fp.h           the five byte number format and PRINT-FP
  gw.cpp                 the GW additions: tokenizer, REM commands, IMPOSE
  extensions.cpp         extension commands/functions, the Spectrum facade
  tables.cpp, tables.h   keyword, keyboard and message tables from the ROM
  charset.cpp            the ROM character set (generated, tools/)
  screen_model.cpp       Screen, Unicode mapping of the character set
src/platform/            terminal hosts and the platform terminal layer
src/ext/                 the standard extensions
app/main.cpp             the zxgw program
examples/embed/          embedding the library
tests/                   BASIC test programs (*.in) and expected output (*.out)
tools/                   extract_charset.py
```

## The memory model

The translation keeps the 64K address space as a byte array,
`Machine::mem`, laid out exactly like the 48K Spectrum:

```
$0000  ROM        only the character set at $3D00 is present (for PEEK)
$4000  display    mapped onto the cells of the top-left 32x24 characters
$5800  attributes mapped onto the cells of the top-left 32x24 characters
$5B00  printer buffer
$5C00  system variables      all of them, used by the C++ code
$5CB6  CHANS                 channel records (K, S, R, P)
       PROG                  the BASIC program, tokenized
       VARS                  variables in the ROM's formats
       E_LINE                the line being edited
       WORKSP                INPUT buffer and temporary work space
       STKBOT/STKEND         (the calculator stack is a C++ vector)
       ...                   free memory
       GO SUB stack          three bytes per GO SUB, just below RAMTOP
RAMTOP $FF57, UDGs $FF58-$FFFF
```

So a BASIC line is stored as line number (big endian), length, tokens and
characters, with a hidden `CHR$ 14` and five byte number after every numeric
literal; variables are stored with the ROM's name bytes and value formats.
That's why `PEEK 23635+256*PEEK 23636` gives the start of the program,
`POKE 23658,8` switches on CAPS LOCK, `PEEK 23672` counts frames and `SAVE`
writes exactly the bytes a Spectrum would.

What is **not** in memory:

- The calculator stack: values are `CalcValue` objects (a double or a
  `std::string`) in `Machine::calc`. Strings are copied when they are
  evaluated. Only string slices that are assignment targets keep their
  address (DEST/STRLEN), as in the ROM.
- The screen: the display file and attributes are a `Screen` of character
  cells. PEEK and POKE of the display file and attribute area are translated
  to and from the cells of the top-left 32×24 characters.
- The machine stack: there is no Z80 stack. The GO SUB stack is stored in
  memory below RAMTOP in the ROM's format, and free memory is computed as if
  the machine stack were below it.

## How a line is entered and run

The control flow is the one of the ROM's main loop (`Machine::mainLoop`, from
L12A2 MAIN-EXEC):

1. **MAIN-2**: `newEd()` (GW `NEWED`) runs the editor (`editor()`, L0F2C) until
   ENTER. Each key arrives through the channel 'K' input routine
   (`keyInput()`, L10A8). After every change, `edCopy()` (L111D) prints the
   edit line in the lower screen with the flashing cursor.
2. The GW **tokenizer** (`tokenize()`) replaces typed keywords by tokens,
   unless classic keyword entry is on.
3. **LINE-SCAN** (`lineScan()`, L1B17) checks the syntax. This runs the same
   statement and expression routines as at runtime, with FLAGS bit 7 reset.
   While checking, every number in the line gets its hidden five byte value
   (`sDecimal()`, L268D) and DEF FN parameters get their hidden slots.
4. If there is an error, the `?` marker is shown at the error position and
   the editor is re-entered (MAIN-2).
5. A line with a number is inserted into the program (`mainAdd()`, L155D) and
   the program is listed (`autoList()`, L1795). Otherwise the line is run as a
   direct command (`runStatements(true)`, L1B8A LINE-RUN).
6. Errors raised while running end at **MAIN-4/MAIN-G** (`mainG()`), which
   prints the report, for example `2 Variable not found, 20:1`.

`runStatements()` holds the statement loop (STMT-LOOP, STMT-RET, LINE-NEW,
LINE-END, NEXT-LINE...) as a small state machine. For each statement it walks
through the ROM's **syntax table** (L1A48 offst-tbl / L1A7A, in
`interpreter.cpp`): command classes 00..0B parse the operands, then the
command routine runs.

## Expressions

`scanning()` is SCANNING (L24FB). It uses an operator stack of
(priority, operation) pairs, just like the ROM uses the machine stack. The
operation code has bit 7 set for a numeric result and bit 6 for a numeric
operand. While checking syntax, these bits find type mismatches
("Nonsense in BASIC"). At runtime, `operation()` executes the calculator
literal on the value stack.

## Timing, keys and the display

The 50 Hz interrupt (`interrupt()`, L0038) is emulated:

- FRAMES (23672) is derived from a monotonic clock, so it counts 1/50 s.
- Keys from the host are decoded (K-DECODE) and placed in LAST_K with
  FLAGS bit 5, as the KEYBOARD routine did.

`statementTick()` runs after every statement (at STMT-RET). Every few
milliseconds it polls the keyboard (BREAK, INKEY$) and presents the screen if
it changed. When the machine waits (editor, PAUSE, INPUT), `idle()` keeps
presenting the screen regularly, so FLASH can blink.

## Tests

`tests/*.in` are typed into `zxgw --batch --geometry 32x24`. The complete
output (input echo, printed text, reports) must equal `tests/*.out`. The
tests are registered with CTest; `tests/run_test.cmake` runs each one.
