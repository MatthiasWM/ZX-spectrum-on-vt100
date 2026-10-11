


# GW Basic ROM Spectrum rebuild for C++ on console

gw.rom.s contains the Gosh Wunderful Basic ROM for the ZX Spectrum. The code is
written in Z80 assembly language and commented.

I want you to translate the entire ROM into C++ code, so it can run in a text
terminal.

## Scope

All functions of the ROM shall be translated and implemented in a way that makes
the most sense when using a terminal.

The code shall adapt to the curren terminal size.

Color ind text inversione is implemented with the matching escape sequences.

Keyboard input is read from the terminal. The arrow keys are used in the editor
window.

Graphics are implemented using unicode characters for 2x4 braille patterns,
asuuming the aspect ration is 2x1 instead of the original square spectrum
characters.

Loading and saving shall write into the file system, allowing platform path and
file names.

## Deliverables

Create a Git repo that we will upload to my github page.

Create a CMake based setup and a documented source tree using the given
Z80 code.

Document curiosities, caveats, and mappings from Z80 to C++ in a docs directroy.

Everything shall compile into one simple executable, and alternatively into a
static library that is easily linked to an existing app.

All code shall be platform independent wherever possible, so it can run on
any variant of Unix/Linux, MacOS, and Windows.

A way to extend the list of known basic commands, and documetation on how to use
it.

Create a LICENSE.md (MIT license), a README.md, and everything else so this
archive can be posted to Github.

## Further Instructions

If unsure, ask me. This may change the further we go.

Commit and push in relatively small steps, so I can follow your work and
go back to changes.

C++ should not be higher than C++17.