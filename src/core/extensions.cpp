// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Extension commands and functions, and the public Spectrum facade.

#include "machine.h"

#include <cctype>
#include <cmath>
#include <stdexcept>

#include "fp.h"
#include "tables.h"

namespace zxgw {

using namespace sv;

namespace {

std::string upperName(const std::string& name) {
    std::string n;
    for (char c : name) n += char(std::toupper((unsigned char)c));
    return n;
}

bool validName(const std::string& n, bool allowDollar) {
    if (n.empty() || !std::isalpha((unsigned char)n[0])) return false;
    for (size_t i = 0; i < n.size(); ++i) {
        char c = n[i];
        if (std::isalnum((unsigned char)c)) continue;
        if (allowDollar && c == '$' && i == n.size() - 1) continue;
        return false;
    }
    return true;
}

uint8_t errNr(Report r) { return r == Report::OK ? 0xFF : uint8_t(uint8_t(r) - 1); }

} // namespace

// ---------------------------------------------------------------------------
// Character set conversion.
// ---------------------------------------------------------------------------
std::string toUtf8(const std::string& spectrum) {
    std::string out;
    for (unsigned char c : spectrum) {
        if (c == 0x5E) out += '^';
        else if (c >= 0x20 && c < 0x90) out += Screen::utf8(Screen::codePoint(c));
        else out += spectrumCharToUtf8(c);
    }
    return out;
}

std::string fromUtf8(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = (unsigned char)utf8[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < utf8.size(); ++k) cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3F);
        i += size_t(n);
        if (cp == 0x00A3) out += char(0x60);
        else if (cp == 0x00A9) out += char(0x7F);
        else if (cp == 0x2191) out += char(0x5E);
        else if (cp >= 0x2580 && cp <= 0x259F) {
            for (int k = 0; k < 16; ++k)
                if (Screen::codePoint(uint8_t(0x80 + k)) == cp) { out += char(0x80 + k); break; }
        } else if (cp < 0x80) out += char(cp);
    }
    return out;
}

// The text of a listing: like fromUtf8, plus \a..\u for the UDGs and
// \{n} for any character code (as written by SAVE "x.bas").
std::string fromListing(const std::string& utf8) {
    std::string plain;
    std::string out;
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\\' && i + 1 < utf8.size()) {
            char n = utf8[i + 1];
            if (n == '{') {
                size_t close = utf8.find('}', i);
                if (close != std::string::npos) {
                    out += fromUtf8(plain);
                    plain.clear();
                    out += char(std::atoi(utf8.substr(i + 2, close - i - 2).c_str()) & 0xFF);
                    i = close;
                    continue;
                }
            } else if (n >= 'a' && n <= 'u') {
                out += fromUtf8(plain);
                plain.clear();
                out += char(0x90 + (n - 'a'));
                ++i;
                continue;
            }
        }
        plain += utf8[i];
    }
    out += fromUtf8(plain);
    return out;
}

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------
bool Statement::checking() const { return m_.syntaxZ(); }

double Statement::number() {
    m_.expt1Num();
    if (m_.syntaxZ()) return 0;
    return m_.popNum();
}

std::string Statement::string() {
    m_.exptExp();
    if (m_.syntaxZ()) return std::string();
    return m_.popStr();
}

Value Statement::expression() {
    m_.scanning();
    bool numeric = m_.flag(FLAGS, F_NUMERIC);
    if (m_.syntaxZ()) return numeric ? Value::num(0) : Value::str("");
    if (numeric) return Value::num(m_.popNum());
    return Value::str(m_.popStr());
}

long Statement::integer(long min, long max) {
    double v = number();
    if (m_.syntaxZ()) return min;
    double r = std::floor(v + 0.5);
    if (r < double(min) || r > double(max)) m_.error(0x0A);
    return long(r);
}

VariableRef Statement::variable() {
    m_.class01();
    VariableRef v;
    v.isString_ = !m_.flag(FLAGS, F_NUMERIC);
    v.dest_ = m_.word(DEST);
    v.strlen_ = m_.word(STRLEN);
    v.flagx_ = m_.mem[FLAGX];
    return v;
}

void Statement::assign(const VariableRef& var, const Value& value) {
    if (m_.syntaxZ()) return;
    if (value.isString != var.isString_) m_.error(0x0B);
    m_.setWord(DEST, var.dest_);
    m_.setWord(STRLEN, var.strlen_);
    m_.mem[FLAGX] = var.flagx_;
    m_.setFlag(FLAGS, F_NUMERIC, !value.isString);
    if (value.isString) m_.stackStr(value.string);
    else m_.stack(m_.checked(value.number));
    m_.letCmd();
}

uint8_t Statement::peek() const { return m_.getChar(); }

bool Statement::accept(uint8_t c) {
    if (m_.getChar() != c) return false;
    m_.nextChar();
    return true;
}

void Statement::expect(uint8_t c) {
    if (!accept(c)) m_.error(0x0B);
}

bool Statement::atEnd() const {
    uint8_t a = m_.getChar();
    return a == ':' || a == 0x0D;
}

void Statement::end() { m_.checkEnd(); }

void Statement::print(const std::string& spectrumText) {
    if (m_.syntaxZ()) return;
    m_.chanOpen(2);
    m_.temps();
    for (unsigned char c : spectrumText) m_.printA(c);
}

void Statement::print(double number) {
    if (m_.syntaxZ()) return;
    m_.chanOpen(2);
    m_.temps();
    m_.stack(number);
    m_.printFp();
}

void Statement::error(Report report) { m_.error(errNr(report)); }

// ---------------------------------------------------------------------------
// FunctionCall
// ---------------------------------------------------------------------------
double FunctionCall::number(size_t i) const {
    if (i >= args_.size() || args_[i].isString) error(Report::ParameterError);
    return args_[i].number;
}

const std::string& FunctionCall::string(size_t i) const {
    if (i >= args_.size() || !args_[i].isString) error(Report::ParameterError);
    return args_[i].string;
}

void FunctionCall::expectCount(size_t min, size_t max) const {
    if (args_.size() < min || args_.size() > max) error(Report::ParameterError);
}

void FunctionCall::error(Report report) const { m_.error(errNr(report)); }

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void Machine::addCommand(const std::string& name, CommandHandler handler) {
    std::string n = upperName(name);
    if (!validName(n, false)) throw std::invalid_argument("zxgw: invalid command name: " + name);
    commands[n] = std::move(handler);
}

void Machine::addFunction(const std::string& name, FunctionHandler handler) {
    std::string n = upperName(name);
    if (!validName(n, true) || n.size() < 2 || (n.back() == '$' && n.size() < 3))
        throw std::invalid_argument("zxgw: invalid function name (two letters or more): " + name);
    functions[n] = std::move(handler);
}

// ---------------------------------------------------------------------------
// Spectrum
// ---------------------------------------------------------------------------
Spectrum::Spectrum(Host& host, const Options& options)
    : machine_(new Machine(host, options)) {}

Spectrum::~Spectrum() = default;

void Spectrum::addCommand(const std::string& name, CommandHandler handler) {
    machine_->addCommand(name, std::move(handler));
}

void Spectrum::addFunction(const std::string& name, FunctionHandler handler) {
    machine_->addFunction(name, std::move(handler));
}

void Spectrum::command(const std::string& line) { machine_->queueCommand(line); }

int Spectrum::run() { return machine_->run(); }

void Spectrum::quit(int exitCode) { machine_->requestQuit(exitCode); }

uint8_t Spectrum::peek(uint16_t address) const { return machine_->mem[address]; }

const Screen& Spectrum::screen() const { return machine_->screen; }

std::string Spectrum::screenText() const {
    const Screen& s = machine_->screen;
    std::string text;
    for (int r = 0; r < s.height(); ++r) {
        std::string line;
        for (int c = 0; c < s.width(); ++c) line += Screen::glyphUtf8(s.at(r, c));
        while (!line.empty() && line.back() == ' ') line.pop_back();
        text += line + "\n";
    }
    return text;
}

void Spectrum::poke(uint16_t address, uint8_t value) { machine_->pokeUser(address, value); }

} // namespace zxgw
