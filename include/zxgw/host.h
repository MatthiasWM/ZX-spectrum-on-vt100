// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The Host interface connects the BASIC machine to the outside world.
//
// The machine itself never touches the terminal.  Everything that the
// original ROM did with hardware (ULA display, keyboard matrix, beeper,
// ZX Printer) goes through this interface.  The library ships with a
// terminal host (ANSI escape sequences, see TerminalHost) and a batch host
// (stdin/stdout, for scripting and tests).  Applications embedding the
// interpreter can provide their own.

#ifndef ZXGW_HOST_H
#define ZXGW_HOST_H

#include <cstdint>
#include <string>

namespace zxgw {

class Screen;

// Keys as delivered by a host.  Printable characters use KeyCode::Char with
// a Unicode code point; everything else has its own code.
enum class KeyCode : uint8_t {
    None,
    Char,       // a printable character in `ch`
    Enter,
    Backspace,  // Spectrum DELETE (CAPS SHIFT + 0)
    Delete,     // delete character right of the cursor (terminal extra)
    Left, Right, Up, Down,
    Home, End,  // start / end of line (terminal extras)
    Edit,       // Spectrum EDIT (CAPS SHIFT + 1)
    CapsLock,   // CAPS SHIFT + 2
    TrueVideo,  // CAPS SHIFT + 3
    InvVideo,   // CAPS SHIFT + 4
    Graphics,   // CAPS SHIFT + 9, toggles G mode
    Extend,     // CAPS SHIFT + SYMBOL SHIFT, E mode for the next key
    Break,      // CAPS SHIFT + SPACE (BREAK)
    Redraw,     // repaint the whole terminal
    Quit        // end of input / quit the application
};

struct KeyEvent {
    KeyCode code = KeyCode::None;
    uint32_t ch = 0;          // code point for KeyCode::Char
    bool symbolShift = false; // pressed with Alt/Option = SYMBOL SHIFT
};

// Where printed text went, for hosts that keep a transcript.
enum class Output : uint8_t { UpperScreen, LowerScreen, Printer };

class Host {
public:
    virtual ~Host() = default;

    // Size of the output device in character cells.  Called at start-up and
    // whenever the machine is idle in the editor to adapt to window changes.
    virtual void screenSize(int& columns, int& rows) = 0;

    // Draw the screen.  Called whenever the content changed and also
    // regularly while waiting for keys, so FLASH attributes can blink.
    virtual void present(const Screen& screen) = 0;

    // Wait up to `timeoutMs` milliseconds for a key (0 = poll, <0 = forever).
    // Returns false if no key arrived.
    virtual bool readKey(KeyEvent& event, int timeoutMs) = 0;

    // Sound a tone.  The default implementation just waits, so BEEP keeps
    // its timing even without sound output.
    virtual void beep(double frequencyHz, double seconds);

    // A completed line of ZX Printer output (LPRINT, LLIST, COPY) as UTF-8.
    virtual void printerLine(const std::string& utf8Line);

    // Every character that the ROM places on the screen through PRINT-OUT,
    // as UTF-8 ("\n" for line ends).  Editor echo is not reported.
    // Used by the batch host to produce a plain text transcript.
    virtual void transcript(Output where, const std::string& utf8) { (void)where; (void)utf8; }

    // True if the host is not interactive (no "scroll?" prompts).
    virtual bool batch() const { return false; }
};

} // namespace zxgw

#endif
