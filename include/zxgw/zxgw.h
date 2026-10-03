// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Public interface of the zxgw library.
//
//   #include <zxgw/zxgw.h>
//   #include <zxgw/terminal_host.h>
//
//   int main() {
//       zxgw::TerminalHost host;
//       zxgw::Spectrum spectrum(host);
//       spectrum.addCommand("HELLO", [](zxgw::Statement& s) {
//           s.end();                       // no parameters
//           s.print("Hello from C++\r");   // \r is the Spectrum ENTER
//       });
//       return spectrum.run();
//   }

#ifndef ZXGW_ZXGW_H
#define ZXGW_ZXGW_H

#include <memory>
#include <string>

#include "zxgw/extension.h"
#include "zxgw/host.h"
#include "zxgw/screen.h"

namespace zxgw {

constexpr const char* kVersion = "1.0.0";

struct Options {
    // Screen size in character cells.  0 follows the size reported by the
    // host (the terminal window) and adapts when it changes.
    int columns = 0;
    int rows = 0;

    // PLOT/DRAW/CIRCLE use the braille dots of the terminal as pixels
    // (2 per column, 4 per row).  With scaleGraphics the classic 256x176
    // coordinate space is scaled onto the available dots instead.
    bool scaleGraphics = false;

    // Start in classic keyword-entry mode (as after typing STOP), where the
    // first letter of a statement produces a keyword like on a real Spectrum.
    bool classicKeywords = false;

    // Characters per line of the emulated ZX Printer.
    int printerWidth = 32;

    // Limit execution speed to this many statements per second (0 = no limit).
    int throttle = 0;
};

class Machine;

class Spectrum {
public:
    explicit Spectrum(Host& host, const Options& options = Options());
    ~Spectrum();
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;

    // Register an extension command, e.g. "SOUND".  Names are not case
    // sensitive and must start with a letter.  See extension.h.
    void addCommand(const std::string& name, CommandHandler handler);

    // Register an extension function.  A name ending in '$' returns a string.
    void addFunction(const std::string& name, FunctionHandler handler);

    // Queue a direct command (UTF-8 text) as if it was typed and entered,
    // e.g. command("LOAD \"game.tap\"") or command("RUN").
    void command(const std::string& line);

    // Run the machine until BYE, the host reports KeyCode::Quit, or quit().
    // Returns the exit code.
    int run();

    // Ask run() to return as soon as possible.
    void quit(int exitCode = 0);

    // Direct memory access (PEEK/POKE).
    uint8_t peek(uint16_t address) const;
    void poke(uint16_t address, uint8_t value);

    // The screen as it is now (character cells).
    const Screen& screen() const;

    // The screen as plain text, one line per row, trailing spaces removed.
    std::string screenText() const;

    Machine& machine() { return *machine_; }

private:
    std::unique_ptr<Machine> machine_;
};

} // namespace zxgw

#endif
