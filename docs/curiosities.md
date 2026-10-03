# Curiosities, caveats and differences

This document lists three things: original ROM behaviour that zxgw keeps on
purpose, what the "Gosh Wonderful" ROM changed compared with the standard 48K
ROM, and where zxgw necessarily differs from a real Spectrum.

## Quirks of the original, kept on purpose

These often surprise people who know other BASICs. They are what a real
Spectrum does, so zxgw does the same.

**Numbers**

- `PRINT 0.5` gives `0.5` but `PRINT 0.05` gives `.05`. PRINT-FP prints the
  leading zero only when no zeros follow the decimal point.
- Up to eight significant digits are printed. Whole numbers with nine or more
  digits switch to E format: `PRINT 12345678` gives `12345678`, but
  `PRINT 123456789` gives `1.2345679E+8`. Small numbers switch at
  0.00001: `PRINT 1E-5` gives `.00001`, `PRINT 1E-6` gives `1E-6`.
- `PRINT (-2)^2` is "A Invalid argument": the ROM computes `x^y` as
  `EXP (y*LN x)`, and LN of a negative number is invalid.
- `0^0` is 1, `0^-1` is "6 Number too big" (the ROM forces a division by
  zero there).
- `INT` rounds down: `INT -0.5` is -1.
- `x AND y` is `x` if `y` is not 0, else 0, and `a$ AND y` is `a$` or "".
  `x OR y` is 1 if `y` is not 0, else `x`. `NOT x` is 1 for 0, else 0.
- `RND` is the ROM's generator: after `RANDOMIZE 1` the first values are
  `.0022735596`, `0.17164612`, `0.87440491`.
- `PRINT AT -1,0` is the same as `AT 1,0`: STK-TO-BC drops the sign there.

**Program flow**

- `GO TO 25`, when there is no line 25, continues at the next line after it.
- `CONTINUE` after STOP resumes with the next statement, after a BREAK it
  repeats the interrupted one ("D BREAK - CONT repeats").
- "I FOR without NEXT" only appears when the body of a loop is skipped
  (`FOR i=1 TO 0`) and there is no NEXT to skip to.
- `NEXT x` for a variable not set by FOR is "1 NEXT without FOR".
- `CLEAR` does not RESTORE.
- At "scroll?", N, SPACE and STOP stop with "D BREAK - CONT repeats".

**Strings and variables**

- Variable names may contain spaces: `long name` and `longname` are the same.
- `a$(5 TO 3)` is the empty string, even if `a$` is shorter.
- Assigning to a slice or an array element pads or cuts the text to the
  length of the destination ("Procrustean assignment"):
  `LET a$="hatstand": LET a$(2 TO 4)="XY"` gives `hXY tand`.
- `DIM a$(3,5)`: `a$(2)` is a string of five characters.
- INPUT evaluates an expression: typing `2*3` assigns 6, typing a variable
  name assigns its value, and STOP (Alt+A) leaves the INPUT.
- DEF FN parameters live inside the program line (hidden five byte slots
  after each parameter), and FN searches the program for the DEF FN.
  Recursion overwrites the slots, as on the Spectrum.

**The editor**

- EDIT copies the line with the line number right aligned (`  10 PRINT`).
- `PRINT CODE INKEY$`, typed as a command, prints 13: ENTER is still held
  down when the command runs.
- With the GW tokenizer, a space typed after a keyword ending in `$` is kept,
  so `CHR$ 65` is listed as `CHR$  65` (the keyword has a trailing space).
  A keyword at the end of a line is listed with its trailing space
  (`40 PRINT `).

## What the "Gosh Wonderful" ROM changed

Most changes are in the spare space at the end of the ROM (`$386E` onwards),
others are small fixes in place (marked `;+` in the listing).

**Keyword entry.** The standard Spectrum enters keywords with single keys
('K' mode). The GW ROM lets you type everything letter by letter and turns
keywords into tokens when ENTER is pressed (the TOKENIZER called from
NEWED):

- upper, lower or mixed case: `print`, `Print`, `PRINT`;
- spaces inside keywords are optional: `goto`, `gosub`, `deffn`, `open#`;
- abbreviations with a dot: `pr.` is PRINT, `ra.` is RANDOMIZE;
- keywords are only recognised as whole words: not after a letter (`INT` in
  `PRINT`, `TO` in `TOTAL`) and not before a letter or `$` (`AT` in `ATN`,
  `VAL` in `VAL$`, `FOR` in `FORMAT`);
- nothing inside quotes and nothing after REM is tokenized;
- spaces before a keyword and one space after it are removed (except after
  keywords ending in `$`, `<>`, `#` ...);
- the keywords are tried in the order REM, then COPY down to RND. An
  abbreviation therefore picks the first keyword in that order that starts
  with the given letters: `p.` is PLOT, not PRINT, and `g.` is GO SUB, not
  GO TO. `INPUT` is found before `IN` gets a chance.

**STOP toggles classic entry.** STOP as a direct command switches between GW
entry ('L' cursor) and the classic 'K' mode, where the first key of a
statement is a keyword. In classic mode nothing is tokenized. It still reports
"9 STOP statement".

**REM commands.** REM as the first statement of a direct command:

| Command | Action |
|---------|--------|
| `REM` | a help screen ("REM COMMANDS V1.32") |
| `REM STREAMS` | lists streams -3 to 15 with their channels and the free memory |
| `REM DELETE first last` | deletes the lines `first` to `last`; both must exist |
| `REM RENUMBER [start [step [first [last]]]]` | defaults 100, 10, 1, 16383 |

RENUMBER changes the line numbers after GO TO, GO SUB, RESTORE, RUN, LIST,
LLIST and LINE when they are plain numbers at the end of a statement
(`GO TO 100`, but not `GO TO 100*a`). It refuses (silently) when new numbers
would pass 9999 or collide with other lines, and it sorts the lines when a
block was moved. A reference to a line after the last one becomes 9999.
Numbers are separated by spaces, not commas. Anything wrong shows the help
screen.

**Fixes and checks.** Among the changes in place:

- Line numbers in GO TO, GO SUB, RUN, RESTORE, LIST and SAVE ... LINE must be
  below 16384 ("B Integer out of range"; FIND_LINE).
- PLOT and POINT reject negative coordinates (BC_POSTVE). DRAW is relative
  and still takes them.
- SCREEN$ and ATTR check that the line is 0..23 and the column 0..31
  (STK_TO_LC).
- CLOSE # of a stream that is not open is "O Invalid stream". Before, it
  could crash.
- `SAVE "x" DATA a$()` for a simple string (not an array) is "C Nonsense in
  BASIC" (CHK_VAR).
- Cursor right skips the parameter of a colour control in the edit line
  (ED_FIX1).
- The colour commands (CLASS-07) and PLOT, DRAW and CIRCLE (CLASS-09) open
  the screen channel 'S'. The original only set a flag, which went wrong
  while another channel was current.
- `"2"+STR$ 0.5` gives `20.5`. The original left a value on the calculator
  stack and gave `0.5` (credit Tony Stratton, 1982).
- Number entry is more accurate (DEC-TO-FP multiplies, then divides), and
  `-65535-1` gives -65536 (ADDFIX).
- Backspace at the top of the screen (credit Frank O'Hara, 1982) and CHR$ 9
  (cursor right) work.
- In graphics mode only the letters A to U give UDGs (K_GR_FIX).
- After COPY, clearing the printer buffer no longer moves the screen print
  position (which could give "5 Out of screen").

## Differences from a real Spectrum

**There is no Z80.** Machine code cannot run. `USR address` returns the
address, which is what happens when the code is a single `RET`, with these
exceptions:

| USR | Effect |
|-----|--------|
| 0 | restart (like switching on) |
| 3435 | CLS |
| 3582 | scroll the screen up |
| 3756 | COPY |
| 4535 | NEW |
| 7962 | free memory: `PRINT 65536-USR 7962` |

`USR "a"` (the address of a UDG) works as usual. LOAD "" CODE loads
machine code into memory, but nothing can run it.

**Memory.** The ROM area contains only the character set (PEEK of other ROM
addresses gives 0, POKE is ignored). The display file and attribute area are
mapped to the cells of the top-left 32×24 characters. POKEing ERR_SP or
other tricks that depend on the Z80 stack have no effect.

**Arithmetic** is done with doubles rounded to the precision of the five byte
format after every operation. Results can differ from a Spectrum in the last
digit. SIN, EXP and the other functions are more accurate, and integer
powers (`2^3`) are exact. Whole numbers between -65535 and 65535 are always
stored in the small integer format, so PEEKing a variable's bytes can differ
when the Spectrum would have stored the same value in floating point form.

**Strings** are copied when they are evaluated. A DEF FN string parameter is
kept outside the program line; its hidden slot only holds the length.

**Display.** See [terminal.md](terminal.md): the screen has the size of the
window, characters and braille dots share cells, UDGs are dots, `OVER 1` on
characters follows simple rules, and a font set with CHARS is used by SCREEN$
and POINT but not for display.

**Keyboard.** Terminals report key presses, not releases, so INKEY$ and IN
see a key as held for 150 ms. Esc and Ctrl-C are BREAK. Esc also leaves an
INPUT with "H STOP in INPUT" (on a Spectrum you would type STOP). The key
click (PIP) and the rasp are silent.

**Time.** FRAMES runs on real time, also during BEEP (the Spectrum stops its
clock while beeping). Programs run far faster; `--throttle` slows them down.

**Ports.** OUT does nothing. IN reads the keyboard half rows on even ports;
other ports give 255.

**Tape.** There is no "Start tape, then press any key." and no waiting for a
tape. The ten character limit on names is gone. LOAD "" needs a current
tape file (from an earlier LOAD). Text listings (`.bas`) are an addition;
their lines are entered after the LOAD command has finished.

**Printer.** The ZX Printer prints text, not pixels. COPY copies the
characters and braille dots of the upper screen.

**RENUMBER** always writes the new line numbers into the line. When memory is
short, the GW ROM instead writes `VAL "nnnn"` to save space; zxgw does not.

**NMI.** The GW ROM repairs the NMI routine: a button on the NMI line gives a
warm restart with a report, unless a program has put zero into NMIADD. zxgw
has no NMI, because a terminal has no NMI button.
