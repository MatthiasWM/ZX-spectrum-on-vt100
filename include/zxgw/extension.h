// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Extending the BASIC with new commands and functions.
//
// Sinclair BASIC never allows a statement to start with a plain name (there
// is no implied LET), and a name of two or more letters followed by "(" is
// never valid either.  zxgw uses exactly these two gaps:
//
//   SOUND 440, 0.5           <- a statement starting with a registered name
//   PRINT UPPER$("hello")    <- a registered function, always with brackets
//
// Like every command routine in the ROM, a command handler runs twice:
// once while the line is checked for syntax (when it is typed in or loaded)
// and once when it is executed.  While checking, number() and string() only
// check the syntax and return 0 or "".  The handler must call end() when all
// parameters have been read: during the syntax check end() leaves the
// handler (it is the ROM's CHECK-END), so everything after end() only runs
// when the program is executed.  See docs/extending.md.

#ifndef ZXGW_EXTENSION_H
#define ZXGW_EXTENSION_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zxgw {

class Machine;

// The Spectrum report codes.  The numeric value is the code shown on screen
// (10 = 'A', 11 = 'B', ...).
enum class Report : uint8_t {
    OK = 0,
    NextWithoutFor = 1,
    VariableNotFound = 2,
    SubscriptWrong = 3,
    OutOfMemory = 4,
    OutOfScreen = 5,
    NumberTooBig = 6,
    ReturnWithoutGosub = 7,
    EndOfFile = 8,
    StopStatement = 9,
    InvalidArgument = 10,    // A
    IntegerOutOfRange = 11,  // B
    NonsenseInBasic = 12,    // C
    BreakContRepeats = 13,   // D
    OutOfData = 14,          // E
    InvalidFileName = 15,    // F
    NoRoomForLine = 16,      // G
    StopInInput = 17,        // H
    ForWithoutNext = 18,     // I
    InvalidIODevice = 19,    // J
    InvalidColour = 20,      // K
    BreakIntoProgram = 21,   // L
    RamtopNoGood = 22,       // M
    StatementLost = 23,      // N
    InvalidStream = 24,      // O
    FnWithoutDef = 25,       // P
    ParameterError = 26,     // Q
    TapeLoadingError = 27    // R
};

// A BASIC value as seen by extensions.  Strings use the Spectrum character
// set (one byte per character, see toUtf8()/fromUtf8()).
struct Value {
    bool isString = false;
    double number = 0;
    std::string string;

    static Value num(double n) { Value v; v.number = n; return v; }
    static Value str(std::string s) { Value v; v.isString = true; v.string = std::move(s); return v; }
};

// Convert between Spectrum strings and UTF-8.
std::string toUtf8(const std::string& spectrum);
std::string fromUtf8(const std::string& utf8);

// A variable named in a statement, e.g. the "k$" in  GETKEY k$
class VariableRef {
public:
    bool isString() const { return isString_; }
private:
    friend class Statement;
    bool isString_ = false;
    uint16_t dest_ = 0, strlen_ = 0;
    uint8_t flagx_ = 0;
};

// Passed to command handlers.  Reads the parameters that follow the command
// name in the BASIC line.
class Statement {
public:
    explicit Statement(Machine& m) : m_(m) {}

    // True while the line is being checked for syntax.
    bool checking() const;

    // Evaluate expressions.  While checking, only the syntax is checked and
    // 0 / "" is returned.  A value of the wrong type gives "C Nonsense in BASIC".
    double number();
    std::string string();
    Value expression();

    // A number rounded to an integer in [min, max], else "B Integer out of range".
    long integer(long min, long max);

    // Parse a variable name that can later be assigned with assign().
    VariableRef variable();

    // The next significant character (a Spectrum character or token code).
    uint8_t peek() const;
    // Skip `c` if it is next and return true.
    bool accept(uint8_t c);
    // Require `c` next, else "C Nonsense in BASIC".
    void expect(uint8_t c);
    // True at ':' or the end of the line.
    bool atEnd() const;

    // End of the statement.  Raises "C Nonsense in BASIC" if more characters
    // follow.  While checking syntax this does not return.
    void end();

    // Assign a value to a variable parsed with variable() (runtime only).
    void assign(const VariableRef& var, const Value& value);

    // Print a Spectrum string or a number to the current channel, like PRINT.
    void print(const std::string& spectrumText);
    void print(double number);

    // Raise a Spectrum report, e.g. error(Report::InvalidArgument).
    [[noreturn]] void error(Report report);

    Machine& machine() { return m_; }

private:
    Machine& m_;
};

// Passed to function handlers.  All arguments are already evaluated.
class FunctionCall {
public:
    FunctionCall(Machine& m, std::vector<Value> args) : m_(m), args_(std::move(args)) {}

    size_t count() const { return args_.size(); }
    const Value& arg(size_t i) const { return args_.at(i); }
    // The i'th argument as a number / string; "Q Parameter error" if missing
    // or of the wrong type.
    double number(size_t i) const;
    const std::string& string(size_t i) const;
    // Check the number of arguments; "Q Parameter error" otherwise.
    void expectCount(size_t min, size_t max) const;

    [[noreturn]] void error(Report report) const;
    Machine& machine() { return m_; }

private:
    Machine& m_;
    std::vector<Value> args_;
};

using CommandHandler = std::function<void(Statement&)>;
using FunctionHandler = std::function<Value(FunctionCall&)>;

} // namespace zxgw

#endif
