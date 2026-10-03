# The Spectrum in a terminal

How the hardware-related parts of the ROM were mapped to a text terminal.

## Screen size

The Spectrum screen has 24 lines of 32 characters, the bottom two being the
lower screen (for the editor, INPUT and reports). zxgw uses the whole terminal
window instead: `W` columns and `H` lines (16..250 columns, 4..250 lines). The
lower screen is still at the bottom and grows upwards when the edit line or
an INPUT prompt needs more lines, exactly like on the Spectrum.

Everything that depends on the screen size follows the window:

- `PRINT AT y,x` accepts lines `0..H-3` and columns `0..W-1`
  ("5 Out of screen" / "B Integer out of range" otherwise);
- `TAB n` moves to column `n` modulo `W`;
- the comma in PRINT moves to the next multiple of 16 columns (the Spectrum
  has two 16 column zones; a wider terminal has more);
- `SCREEN$ (y,x)` and `ATTR (y,x)` accept the whole window;
- "scroll?" appears after a screenful of lines;
- the automatic listing fills the upper screen.

When the window is resized while the editor waits, the upper screen keeps its
content at the top, the lower screen stays at the bottom and the print
positions are adjusted. A running program keeps the old size until it
returns to the editor. If the window is smaller than the screen (e.g. with
`-g 32x24` in a small window), the bottom part is shown, where the edit line
and the reports are.

`zxgw -g 32x24` gives the original layout.

## Character cells

Each of the W×H cells holds either

- a **character**: codes 32..127 and the block graphics 128..143, shown as
  Unicode characters, or
- **dots**: a 2×4 pattern shown as a Unicode braille character
  (U+2800..U+28FF),

plus a Spectrum attribute byte (FLASH, BRIGHT, PAPER, INK) and an "inverse"
flag.

| Spectrum | Terminal |
|----------|----------|
| 32..126 | ASCII, except |
| 94 `↑` | `↑` (typed as `^`) |
| 96 `£` | `£` (typed as `` ` `` or `£`) |
| 127 `©` | `©` |
| 128..143 block graphics | the Unicode quadrants `▘▝▀▖▌▞▛▗▚▐▜▄▙▟█` |
| 144..164 user defined graphics | braille dots made from the UDG bitmap |
| 165..255 keywords | the keyword text, with the ROM's spacing |

**UDGs** are shown with dots: each braille dot stands for a block of 4×2
pixels of the 8×8 bitmap and is set when at least three of its eight pixels
are set. The bitmap is read when the character is printed, as on the
Spectrum: redefining a UDG later does not change what is already on the
screen.

## Graphics with braille dots

A braille character has 2×4 dots, and a terminal cell is about twice as high
as it is wide, so the dots are square. PLOT, DRAW, CIRCLE and POINT work on
these dots:

- the pixel space is `2*W` × `4*(H-2)`, with (0,0) at the bottom left of the
  upper screen (on an 80×24 terminal: 160×88 pixels, on 128×46: 256×176,
  exactly the Spectrum's);
- coordinates outside are "B Integer out of range", as on the Spectrum;
- with `--scale-graphics`, PLOT and DRAW use the classic 256×176 coordinates
  and they are scaled (by the same factor in both directions, so circles stay
  round) onto the available dots.

DRAW-LINE and the arc and circle algorithms are those of the ROM, so lines
have the same steps and circles are the same polygons.

**Text and dots in one cell.** On a Spectrum, text and pixels mix freely in
the 8×8 cells. In a terminal a cell shows either a character or dots:

- PLOT into a character cell turns it into an empty dot cell first (the
  character is lost);
- PRINT into a dot cell replaces the dots by the character;
- POINT on a character cell answers from the character's bitmap (4×2 pixels
  per dot), so collision tests against text still work.

**OVER and INVERSE.** For dots they behave exactly as in PLOT-SUB (OVER 1
toggles, INVERSE 1 resets). Characters cannot be combined with XOR, so
`PRINT OVER 1` follows simple rules: a space changes nothing, an inverse
space inverts the cell, a character printed over an empty cell or over itself
behaves as XOR would, and any other character replaces the old one.

## Colours

Attributes are output with SGR escape sequences. INK, PAPER and BRIGHT give
the Spectrum colours (normal `$D7`, bright `$FF` levels):

| Mode (`-c`) | Output |
|-------------|--------|
| `truecolor` | 24 bit colours: exact Spectrum RGB values |
| `256` | the 6×6×6 cube of the 256 colour palette, which contains the exact Spectrum levels |
| `16` | ANSI colours 30..37 / 90..97 (the terminal's palette) |
| `mono` | no colours, only inverse video |
| `auto` | `truecolor` if `COLORTERM` says so, `mono` for `TERM=dumb` or `NO_COLOR`, else `256` |

**FLASH** is done by zxgw itself, not with the blink attribute that many
terminals ignore: every 0.32 seconds (16 frames, like the ULA), ink and paper
of flashing cells are swapped. The cursor (`K`, `L`, `C`, `E`, `G`) and the
error marker `?` flash too.

**BORDER** sets the colour of the lower screen (BORDCR), as on the Spectrum;
there is no border around the terminal.

## Keyboard

The ROM decodes a 40 key matrix with CAPS SHIFT, SYMBOL SHIFT and the cursor
modes. A terminal sends characters, so K-DECODE was rewritten for them (see
`keyboard.cpp`):

| Terminal | Spectrum |
|----------|----------|
| characters | the character (already "decoded" by the terminal keyboard) |
| Alt/Option + letter | SYMBOL SHIFT + letter: `STOP`, `*`, `?`, `STEP`, `>=`, `TO`, `THEN`, `↑`, `AT`, `-`, `+`, `=`, `.`, `,`, `;`, `"`, `<=`, `<`, `NOT`, `>`, `OR`, `/`, `<>`, `£`, `AND`, `:` for A..Z |
| Alt + digit | SYMBOL SHIFT + digit: `_ ! @ # $ % & ' ( )` |
| arrows | CAPS SHIFT + 5, 6, 7, 8 |
| Backspace / F10 | DELETE (CAPS SHIFT + 0) |
| Tab / F1 | EDIT (CAPS SHIFT + 1) |
| F2, F3, F4 | CAPS LOCK, TRUE VIDEO, INV VIDEO |
| F5..F8 | the cursor keys |
| Ctrl-G / F9 | GRAPHICS (CAPS SHIFT + 9) |
| Ctrl-E / F11 | EXTENDED mode (CAPS SHIFT + SYMBOL SHIFT) |
| Esc, Ctrl-C | BREAK (CAPS SHIFT + SPACE) |
| Delete, Home, End | extras of the terminal editor |
| Ctrl-L | redraw |
| Ctrl-D | quit (end of input) |

**Modes.** The cursor shows the mode as on the Spectrum:

- `L` (or `C` with CAPS LOCK) is the normal mode of the GW ROM: everything is
  typed letter by letter and the tokenizer finds the keywords.
- `K` appears in classic keyword entry (`STOP` as a direct command, or
  `--classic`): the first letter of a statement gives the keyword printed on
  that Spectrum key (`p` = PRINT, `g` = GO TO, ...).
- `E` (Ctrl-E): the next letter gives the green keyword above the key
  (lower case letter) or the red one below it (upper case or with Alt); digits
  give colour controls in the line: `0`..`7` PAPER, the shifted digits
  `)!@#$%^&` INK, `8`/`9` BRIGHT 0/1, `*`/`(` FLASH 0/1; Alt+digit gives
  `FORMAT DEF FN FN LINE OPEN# CLOSE# MOVE ERASE POINT CAT`.
- `G` (Ctrl-G): `1`..`8` give the block graphics (shifted: inverted), `A`..`U`
  the UDGs, `9` leaves the mode, `0` deletes.

**Type-ahead.** Keys typed while the editor is busy, or pasted text, are
queued and handed over one at a time, so pasting a listing works. While a
program runs, the latest key counts (LAST_K), as on the real keyboard.

**INKEY$ and IN.** A terminal reports key presses, not key releases.
A key counts as "held down" for 150 ms after it arrives, or until the next
key. INKEY$ reports that key. `IN 65278` and the other keyboard ports report
it in the keyboard matrix (with CAPS SHIFT or SYMBOL SHIFT where needed),
so programs that read the ports directly work too. Holding a key down relies
on the terminal's auto-repeat: between the first press and the first repeat
(typically a third of a second) the key appears released.

## Time and speed

FRAMES (23672..23674) counts 1/50 seconds of real time. PAUSE, BEEP and
RANDOMIZE use it. Programs run much faster than on a Spectrum (millions of
statements per second); `--throttle N` limits the speed for games that rely
on the original speed.

## Sound

BEEP computes the frequency and checks the parameters like the ROM and then
calls `Host::beep()`. The terminal host waits for the duration (and with
`--bell` rings the terminal bell); an application embedding zxgw can play the
tone.

## The ZX Printer

LPRINT, LLIST and COPY print to the 'P' channel. Each line is passed to
`Host::printerLine()` as UTF-8 text; the terminal host appends it to
`zxprinter.txt` (option `-p`), the batch host writes it to stdout. The
printer is 32 characters wide. COPY copies the upper screen as text, with
braille characters for the graphics.

## Batch mode

Without a terminal (or with `-b`) zxgw reads lines from stdin and types them
in, followed by ENTER. Everything printed is written to stdout as plain text,
and the screen positions are reproduced as far as plain text allows (AT
moves to a new line). Syntax errors, which would leave the line in the
editor, are reported as text and the line is dropped. "scroll?" never
appears. At the end of the input zxgw quits. This is used by the tests and
makes zxgw usable in pipelines:

```sh
printf '10 FOR i=1 TO 3: PRINT i: NEXT i\nRUN\n' | zxgw --no-echo
```
