# Extending zxgw

zxgw can learn new BASIC commands and functions written in C++. This guide
shows how to add them, explains the rules they follow, and how to embed the
interpreter in your own program.

## Where new commands fit in

Sinclair BASIC has no implied `LET`: every statement starts with a keyword.
A statement that starts with a plain name, such as

```
SOUND 440, 0.5
```

is therefore always "Nonsense in BASIC". Likewise, a name of two or more
letters followed by `(` is never valid, because only single letter arrays
exist. zxgw uses exactly these two gaps:

- **commands**: a statement that starts with a registered name;
- **functions**: a registered name of two or more letters, optionally ending
  in `$`, followed by arguments in brackets: `UPPER$(a$)`, `MAX(a, b)`,
  `TIME$()`.

Programs that run on a real Spectrum never contain either, so extensions
cannot change the meaning of an existing program.

## A first command

```cpp
#include <zxgw/zxgw.h>
#include <zxgw/terminal_host.h>

int main() {
    zxgw::TerminalHost host;
    zxgw::Spectrum spectrum(host);

    // SQUARES n  - print the squares from 1 to n
    spectrum.addCommand("SQUARES", [](zxgw::Statement& s) {
        long n = s.integer(1, 1000);    // a number, rounded, checked
        s.end();                        // end of the statement
        for (long i = 1; i <= n; ++i) {
            s.print(double(i * i));
            s.print("\r");              // CHR$ 13 is the Spectrum's ENTER
        }
    });

    return spectrum.run();
}
```

```
SQUARES 3
1
4
9
0 OK, 0:1
```

(`examples/extend/extend.cpp` contains all the examples of this guide; it is
built with the project and run as a test.)

### Two passes: checking and running

Every command routine of the ROM runs twice in the life of a line: first
when the line is entered (its syntax is checked, before it is stored or run),
and then each time it is executed. Extension handlers work the same way:

1. **Checking** (`s.checking()` is true): `number()`, `string()`,
   `expression()` and `integer()` only check the syntax of the expression and
   return 0 or "". `end()` checks that the statement ends here and then
   **leaves the handler** (it throws an internal exception, just as the ROM's
   CHECK-END drops the return addresses).
2. **Running**: the same calls evaluate the expressions, `end()` returns, and
   the code after it does the work.

So the rule is simple: **read all parameters, call `end()`, then act.** Code
before `end()` must not have side effects, because it also runs when the line
is typed in.

If a handler returns without calling `end()`, zxgw checks the end of the
statement itself.

### Reading parameters

| Call | Reads |
|------|-------|
| `double number()` | a numeric expression |
| `std::string string()` | a string expression |
| `Value expression()` | an expression of either type |
| `long integer(min, max)` | a number rounded to an integer, "B Integer out of range" outside `min..max` |
| `VariableRef variable()` | a variable to assign to later (`a`, `a$`, `a(3)`, `a$(2 TO 4)`) |
| `uint8_t peek()` | the next character (or keyword token) without taking it |
| `bool accept(c)` | takes `c` if it is next |
| `void expect(c)` | requires `c` ("C Nonsense in BASIC" otherwise) |
| `bool atEnd()` | true at `:` or the end of the line |
| `void end()` | end of the statement (see above) |

Separators are characters such as `','`, `';'` or `'='`. Keywords are
tokens, available as constants in `src/core/sysvars.h` (for example `TO` is
`0xCC`) or simply as numbers.

Optional parameters use `atEnd()` or `accept()`:

```cpp
// WAIT [frames]
spectrum.addCommand("WAIT", [](zxgw::Statement& s) {
    long frames = 50;
    if (!s.atEnd()) frames = s.integer(0, 65535);
    s.end();
    std::this_thread::sleep_for(std::chrono::milliseconds(frames * 20));
});
```

### Assigning to a variable

```cpp
// GETENV name$, v$   - read an environment variable into v$
spectrum.addCommand("GETENV", [](zxgw::Statement& s) {
    std::string name = s.string();
    s.expect(',');
    zxgw::VariableRef var = s.variable();
    s.end();
    if (!var.isString()) s.error(zxgw::Report::NonsenseInBasic);
    const char* v = std::getenv(zxgw::toUtf8(name).c_str());
    s.assign(var, zxgw::Value::str(zxgw::fromUtf8(v ? v : "")));
});
```

`assign()` uses the ROM's LET, so the usual rules apply. New variables are
created, and slices and array elements are assigned with Procrustean padding.

### Output and errors

`print(text)` and `print(number)` print to the upper screen like `PRINT`
(numbers in the Spectrum format). Use `"\r"` for a new line.

`error(report)` stops with a Spectrum report, for example
`s.error(zxgw::Report::InvalidArgument)` gives `A Invalid argument, 10:1`.
The reports are listed in `zxgw::Report` (`include/zxgw/extension.h`).

## Functions

```cpp
// HYPOT(a, b)
spectrum.addFunction("HYPOT", [](zxgw::FunctionCall& f) {
    f.expectCount(2, 2);
    return zxgw::Value::num(std::hypot(f.number(0), f.number(1)));
});

// REPEAT$(s$, n)
spectrum.addFunction("REPEAT$", [](zxgw::FunctionCall& f) {
    f.expectCount(2, 2);
    std::string r;
    for (int i = 0; i < int(f.number(1)); ++i) r += f.string(0);
    return zxgw::Value::str(r);
});
```

- A name ending in `$` returns a string, otherwise a number. That is how the
  syntax check knows the type of the result, and the handler must return a
  value of that type ("Q Parameter error" otherwise).
- The arguments are evaluated before the handler is called, and the
  handler is only called at runtime. `count()`, `arg(i)`, `number(i)` and
  `string(i)` access them; a missing argument or one of the wrong type gives
  "Q Parameter error"; `expectCount(min, max)` checks the number.
- Functions are always written with brackets, also without arguments:
  `TIME$()`.

## Names and the tokenizer

Names are not case sensitive and consist of a letter followed by letters and
digits (functions may end with `$`). They are not tokenized when they are
typed, provided they don't look like a keyword. The GW tokenizer replaces a
keyword only when it is not preceded by a letter and not followed by a letter
or `$`. So `TOTAL`, `INSTR` or `PRINTER` are safe, but `BEEP2` would be read as
`BEEP 2`, and a name that *is* a keyword (`PLOT`) cannot be used.

## Strings and the character set

Strings in BASIC use the Spectrum character set: ASCII with `£` at 96, `©` at
127, the block graphics at 128..143, UDGs at 144..164 and keyword tokens
above. `zxgw::toUtf8()` and `zxgw::fromUtf8()` convert. Plain ASCII needs no
conversion.

## The standard extensions

`src/ext/standard_extensions.cpp` is a complete example:

| Name | Purpose |
|------|---------|
| `BYE [code]` | leave zxgw with an exit code |
| `CD "dir"` | change the working directory |
| `UPPER$(s$)`, `LOWER$(s$)` | change case |
| `INSTR(s$, t$[, start])` | position of `t$` in `s$`, 0 if not found |
| `MAX(a, ...)`, `MIN(a, ...)` | largest and smallest number |
| `TIME$()`, `DATE$()` | `hh:mm:ss`, `yyyy-mm-dd` |
| `ENV$(name$)` | an environment variable |

`zxgw::addStandardExtensions(spectrum)` registers them.

## Embedding the interpreter

Link the static library:

```cmake
add_subdirectory(zxgw)                 # or: find_package(zxgw)
target_link_libraries(myapp PRIVATE zxgw::zxgw)
```

```cpp
zxgw::Options options;
options.columns = 32;                  // fixed size instead of the terminal's
options.rows = 24;
zxgw::TerminalHost host;
zxgw::Spectrum spectrum(host, options);
spectrum.command("LOAD \"game.tap\"");   // typed as a direct command
int exitCode = spectrum.run();          // until BYE, Ctrl-D or quit()
```

`examples/embed/embed.cpp` runs a BASIC program without a terminal, using
`BatchHost` with string streams.

### Writing a host

To show the Spectrum somewhere else, such as a GUI window or a web page,
implement `zxgw::Host`:

```cpp
class MyHost : public zxgw::Host {
public:
    void screenSize(int& columns, int& rows) override { columns = 64; rows = 32; }

    void present(const zxgw::Screen& screen) override {
        for (int r = 0; r < screen.height(); ++r)
            for (int c = 0; c < screen.width(); ++c) {
                const zxgw::Cell& cell = screen.at(r, c);
                // cell.attr: FLASH, BRIGHT, PAPER, INK; cell.inverse();
                // Screen::glyphUtf8(cell) is the character or braille dots
            }
    }

    bool readKey(zxgw::KeyEvent& ev, int timeoutMs) override {
        // wait up to timeoutMs for a key; fill ev.code / ev.ch
        return false;
    }

    void beep(double hz, double seconds) override { /* play a tone */ }
    void printerLine(const std::string& line) override { /* ZX Printer */ }
};
```

`present()` is called whenever the screen changed and regularly while the
machine waits, so FLASH can be animated (swap ink and paper of cells with
attribute bit 7 every 0.32 s). `readKey()` with a timeout of 0 must not block.
