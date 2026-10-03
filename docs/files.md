# Files: SAVE, LOAD, VERIFY, MERGE and friends

The Spectrum saves to cassette tape. zxgw keeps the syntax and the logic of
the tape commands (SAVE-ETC at L0605, LD-LOOK-H, LD-CONTRL, VR-CONTROL,
ME-CONTRL), but reads and writes files.

## Names

The name in a tape command is a file name or path in the format of the
operating system (`"game"`, `"games/chess.tap"`, `"C:\\BASIC\\x.tap"`). The
Spectrum limit of ten characters does not apply.

| Command | File |
|---------|------|
| `SAVE "name"` | `name.tap` (`.tap` is added when the name has no extension) |
| `SAVE "name.tap"` | `name.tap` |
| `SAVE "name.bas"` (or `.txt`) | a text listing |
| `LOAD "name"` | the first of `name`, `name.tap`, `name.TAP`, `name.bas` that exists |

The ten character name inside the tape header is the file name without
directory and extension.

## The tape

A `.tap` file is a sequence of blocks, each a header block (17 bytes: type,
name, length, two parameters) and a data block, in the format every Spectrum
emulator understands. zxgw writes one header and one data block per SAVE.

LOAD treats the file it opened as the **current tape**, and the position after
the loaded block as the tape position:

- `LOAD "name"` opens `name.tap` and loads the first block of the right type
  from it (the name in the header does not matter, the file name selected it).
- `LOAD ""` (any name) continues on the current tape with the next block of
  the right type, for example the `LOAD "" CODE` in the loader of a game
  (if the end of the tape is reached, the search continues at the start).
- `LOAD "x"`, when there is no file `x` or `x.tap`, searches the current tape
  for a header named `x`, like a Spectrum winding through a tape.

Like on a Spectrum, LOAD shows every header it finds:

```
Program: chess
Bytes: screen
```

What can be saved and loaded:

| Command | Header type | Data |
|---------|-------------|------|
| `SAVE "n" [LINE l]` | 0 program | program and variables; LOAD starts the program at line `l` |
| `SAVE "n" DATA a()` | 1 number array | the array |
| `SAVE "n" DATA a$()` | 2 character array | the array (not a simple string, see GW CHK_VAR) |
| `SAVE "n" CODE start,length` | 3 bytes | the memory |
| `SAVE "n" SCREEN$` | 3 bytes | `CODE 16384,6912`: the display file and attributes |

`LOAD "n" CODE [start[,length]]` loads to the saved or to the given address,
and a length given in the command must not be smaller than the block.
`LOAD "n" DATA b()` loads the array under the new name. VERIFY compares
instead of loading. MERGE adds the lines and variables of a program to those
in memory, replacing lines with the same number and variables with the same
name.

Errors are those of the Spectrum: "F Invalid file name" when there is no
such file (and no tape that could contain it) or a file cannot be written,
"R Tape loading error" when no suitable block is found or VERIFY finds a
difference.

**SCREEN$ and the display file.** zxgw keeps the screen as character cells,
not as a bitmap. When the display file is saved (SCREEN$ or CODE covering
16384..23295), the top-left 32×24 cells are drawn into the display file with
the character set, block graphics and dots. When a display file is loaded,
every 8×8 cell is compared with the character set (as SCREEN$ does): a match
becomes that character, anything else becomes braille dots. So a game's
loading screen is shown as a rough braille picture. PEEK and POKE of the
display file and attributes work the same way.

## Text listings

A program can also be saved and loaded as a text file, one line per
program line, as LIST shows it:

```
10 PRINT "hello"
20 GO TO 10
```

- `SAVE "prog.bas"` writes the listing (UTF-8). The hidden numbers are not
  written. UDGs are written as `\a`..`\u`, other control characters (colour
  codes in strings) as `\{n}` with the character code, `↑` as `^`.
- `LOAD "prog.bas"` deletes the program and variables, then enters the lines
  as if they were typed: they are tokenized (keywords in any case, GW
  style) and checked for syntax. Lines without a line number are ignored.
  When a line has a syntax error, loading stops and that line stays in the
  editor with the error marker, so it can be corrected.
- `MERGE "prog.bas"` enters the lines without deleting the program.
- `VERIFY "prog.bas"` compares the program with the file.

Text listings can be written with any editor, so this is a convenient way to
write longer programs.

## CAT, ERASE, MOVE and FORMAT

In the 48K ROM these four Microdrive commands only give "O Invalid stream".
In zxgw they work on the file system:

| Command | Action |
|---------|--------|
| `CAT` | list the current directory (sub-directories end with `/`) |
| `CAT "dir"` | list a directory (an addition to the syntax) |
| `ERASE "file"` | delete a file |
| `MOVE "old","new"` | rename or move a file |
| `FORMAT "dir"` | create a directory |

The standard extensions add `CD "dir"` to change the current directory.

## The command line

`zxgw file` types `LOAD "file"` when it starts, `zxgw -r file` also types
`RUN`. A `.tap` saved with `LINE` starts by itself.
