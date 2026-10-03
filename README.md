# zxgw — the "Gosh Wonderful" ZX Spectrum BASIC in your terminal

zxgw is a C++ translation of the **"Gosh Wonderful" ZX Spectrum ROM**
(version 1.32, December 2004), an improved 48K Spectrum ROM with bug fixes, a
keyword tokenizer, RENUMBER, block DELETE and a stream lister. The commented
Z80 listing, [`gw_rom.s`](gw_rom.s), is the source of this translation.

It is not an emulator: there is no Z80. The BASIC interpreter, editor,
screen and printer routines, calculator and tape routines of the ROM were
translated routine by routine into C++. The original data structures are
kept: the 64K memory map, system variables, tokenized program lines, the
variables area and the GO SUB stack. As a result, programs that PEEK and POKE
system variables still work, and `.tap` files can be swapped with Spectrum
emulators.

```
10 FOR n=1 TO 3
20 CIRCLE 30*n,30,10*n
30 NEXT n
40 PRINT AT 1,24;"Hello";AT 2,24;INVERSE 1;"World"
RUN
                                          ⢀⣀⣀⣀⣀⣀⡀
                        Hello         ⡠⠔⠒⠉⠁     ⠈⠉⠒⠢⢄⡀
                        World      ⣀⠔⠉               ⠈⠢⢄
                         ⢀⡠⠔⠒⠒⠒⠒⠒⢤⣎                     ⠱⡀
                       ⡠⠊⠁      ⡰⠁ ⠉⠢⡀                   ⠈⢆
              ⣀⣀⣀     ⡔⠁       ⢰⠁    ⠈⢢                   ⠈⡆
           ⡠⠒⠉   ⠉⠢⡀ ⡜        ⢀⠇       ⢣                   ⠸⡀
          ⢰⠁       ⠑⣼         ⢸         ⡇                   ⡇
          ⢸         ⣿         ⢸         ⡇                   ⡇
          ⠈⢆       ⡰⠹⡀        ⢸        ⢀⠇                   ⡇
           ⠈⠒⠤⣀⣀⣀⠔⠊  ⠱⡀        ⢇      ⢀⠎                   ⡸
                      ⠑⢄       ⠘⡄    ⡠⠊                   ⢠⠃
                       ⠈⠢⢄⡀     ⠘⢄⣀⠤⠊                    ⡠⠃
                          ⠈⠑⠒⠒⠒⠒⠒⠉⠣⡀                   ⣀⠜
                                   ⠈⠑⢄⡀             ⢀⠤⠊
                                      ⠈⠑⠒⠤⢄⣀⣀⣀⣀⣀⡠⠤⠒⠊⠁

0 OK, 40:1
```

(The lines are typed in lower or upper case; the screen is the terminal's
size, here 64×18, and the circles are drawn with braille dots.)

## Features

- **The whole 48K BASIC**: every command, function and report of the ROM,
  with the original syntax checking, error positions (the flashing `?`),
  number formatting (`PRINT 1/3` gives `0.33333333`, `PRINT .05` gives `.05`)
  and quirks.
- **The GW additions**: keywords can be typed letter by letter in any case
  (`print`, `PRINT`, `pr.`), and `REM RENUMBER`, `REM DELETE`, `REM STREAMS`
  plus the `STOP` toggle to classic one-key keyword entry.
- **Adapts to the terminal**: the screen is as wide and as high as the
  window (instead of 32×24) and follows when it is resized.
- **Colours** with ANSI escape sequences: INK, PAPER, BRIGHT, FLASH (the
  cells really blink), INVERSE, OVER, BORDER (the lower screen), in 24 bit,
  256 or 16 colours.
- **Graphics with Unicode braille**: every character cell holds 2×4 square
  dots for PLOT, DRAW, CIRCLE and POINT, using the ROM's line and arc
  algorithms. Optionally the classic 256×176 coordinates are scaled to fit.
- **Files instead of tapes**: SAVE, LOAD, VERIFY and MERGE use `.tap` files
  (compatible with emulators) or plain text listings (`.bas`), with any path.
  CAT, ERASE, MOVE and FORMAT work on directories.
- **ZX Printer**: LPRINT, LLIST and COPY write to a text file.
- **Extensible**: new BASIC commands and functions can be added in C++.
- **A library**: the interpreter is a static library with a small API and a
  pluggable host, so other programs can embed it.
- **Portable C++17**: Linux, macOS, BSD and Windows 10+ (console with VT
  sequences).

## Building

You need CMake 3.16 or newer and a C++17 compiler (GCC 8+, Clang 7+ or
MSVC 2019+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # optional: runs the BASIC test programs
```

The build produces:

| File | What it is |
|------|------------|
| `build/zxgw` (`zxgw.exe`) | the command line application |
| `build/libzxgw.a` (`zxgw.lib`) | the static library |
| `build/zxgw-embed-example` | an example of embedding the library |

`cmake --install build` installs the program, the library, the headers and a
CMake package (`find_package(zxgw)` → target `zxgw::zxgw`).

## Running

```sh
zxgw                      # start with an empty program
zxgw game.tap             # LOAD "game.tap" (runs if saved with LINE)
zxgw -r prog.bas          # load a text listing and RUN it
zxgw -g 32x24             # the original screen size
echo 'PRINT 2+2' | zxgw   # batch mode: no terminal, plain text output
```

| Option | Meaning |
|--------|---------|
| `-r`, `--run` | RUN after loading the file |
| `-e`, `--exec LINE` | enter LINE as a direct command (repeatable) |
| `-b`, `--batch` | stdin lines are typed in, output is plain text (automatic when not on a terminal) |
| `--no-echo` | batch mode: do not copy the input lines to the output |
| `-g`, `--geometry WxH` | fixed screen size in characters |
| `-c`, `--color MODE` | `auto`, `truecolor`, `256`, `16` or `mono` |
| `-s`, `--scale-graphics` | PLOT/DRAW/CIRCLE in 256×176 Spectrum pixels, scaled to the terminal |
| `-k`, `--classic` | start in classic keyword entry ('K' mode) |
| `-p`, `--printer FILE` | ZX Printer output file (default `zxprinter.txt`) |
| `-t`, `--throttle N` | at most N statements per second (for games) |
| `--bell` | BEEP rings the terminal bell |
| `--dump-screen` | print the final screen as text when leaving |

Leave with `BYE` or Ctrl-D.

### Keys

| Terminal | Spectrum |
|----------|----------|
| letters, digits, symbols | as typed (keywords are recognised when ENTER is pressed) |
| ←, →, ↑, ↓ | cursor keys (↑/↓ move the program cursor `>`) |
| Backspace | DELETE |
| Delete, Home, End | delete right, start and end of line (terminal extras) |
| Enter | ENTER |
| Tab or F1 | EDIT (bring the current line down) |
| Esc or Ctrl-C | BREAK (and leaves an INPUT) |
| Alt/Option + key | SYMBOL SHIFT + key, e.g. Alt+Y = `AND`, Alt+A = `STOP` |
| Ctrl-E or F11 | EXTENDED mode for the next key |
| Ctrl-G or F9 | GRAPHICS mode (block graphics on 1–8, UDGs on A–U) |
| F2, F3, F4 | CAPS LOCK, TRUE VIDEO, INV VIDEO |
| Ctrl-L | redraw the screen |
| Ctrl-D | quit |

On macOS, set "Use Option as Meta key" in the terminal settings to use
Option as SYMBOL SHIFT.

### The GW extras

```
REM RENUMBER                  renumber everything: 100, 110, 120 ...
REM RENUMBER 1000 5 200 300   lines 200..300 become 1000, 1005, ...
REM DELETE 100 200            delete lines 100 to 200 (both must exist)
REM STREAMS                   list the streams, their channels and free memory
REM                           help
STOP                          (as a command) toggle classic keyword entry
```

## Embedding and extending

```cpp
#include <zxgw/zxgw.h>
#include <zxgw/terminal_host.h>

int main() {
    zxgw::TerminalHost host;
    zxgw::Spectrum spectrum(host);

    spectrum.addCommand("HELLO", [](zxgw::Statement& s) {
        std::string name = s.atEnd() ? "world" : s.string();
        s.end();                       // syntax check ends here
        s.print("Hello, " + name + "\r");
    });
    spectrum.addFunction("TWICE", [](zxgw::FunctionCall& f) {
        f.expectCount(1, 1);
        return zxgw::Value::num(2 * f.number(0));
    });
    return spectrum.run();
}
```

The `zxgw` application registers a few extensions this way: `BYE`, `CD`,
`UPPER$()`, `LOWER$()`, `INSTR()`, `MAX()`, `MIN()`, `TIME$()`, `DATE$()`
and `ENV$()`. See [docs/extending.md](docs/extending.md).

## Documentation

| Document | Contents |
|----------|----------|
| [docs/architecture.md](docs/architecture.md) | the source tree and how the ROM maps onto it |
| [docs/z80-to-cpp.md](docs/z80-to-cpp.md) | how Z80 code, registers, restarts and stack tricks were translated |
| [docs/terminal.md](docs/terminal.md) | screen geometry, colours, braille graphics, keyboard, printer |
| [docs/files.md](docs/files.md) | SAVE, LOAD, the `.tap` and `.bas` formats, CAT and friends |
| [docs/extending.md](docs/extending.md) | adding commands and functions, writing a host |
| [docs/curiosities.md](docs/curiosities.md) | ROM quirks kept on purpose, GW changes, caveats and differences |

## Licence and credits

The C++ translation is released under the MIT licence (see
[LICENSE.md](LICENSE.md)).

`gw_rom.s` and the data taken from it (the character set, keyword and message
tables) are part of the ZX Spectrum ROM, copyright Amstrad PLC. Amstrad allow
redistribution but keep the copyright; these parts are not covered by the MIT
licence.

The "Gosh Wonderful" ROM is generally attributed to Geoff Wearmouth (the
listing itself does not name its author). Its comments build on the
disassembly by Dr Ian Logan and Dr Frank O'Hara, with contributions credited in
the listing to Alvin Albrecht, Andy Styles, Andrew Owen and Dave Mills.
