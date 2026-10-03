// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The standard extensions.  Every command follows the pattern of the ROM's
// command routines: read the parameters, call end() (CHECK-END, which ends
// the syntax check), then act.

#include "zxgw/standard_extensions.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace zxgw {

void addStandardExtensions(Spectrum& spectrum) {
    Spectrum* sp = &spectrum;

    // BYE [code]
    spectrum.addCommand("BYE", [sp](Statement& s) {
        long code = 0;
        if (!s.atEnd()) code = s.integer(0, 255);
        s.end();
        sp->quit(int(code));
    });

    // CD "directory"
    spectrum.addCommand("CD", [](Statement& s) {
        std::string dir = s.string();
        s.end();
        std::error_code ec;
        std::filesystem::current_path(toUtf8(dir), ec);
        if (ec) s.error(Report::InvalidFileName);
    });

    spectrum.addFunction("UPPER$", [](FunctionCall& f) {
        f.expectCount(1, 1);
        std::string s = f.string(0);
        for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 32);
        return Value::str(s);
    });

    spectrum.addFunction("LOWER$", [](FunctionCall& f) {
        f.expectCount(1, 1);
        std::string s = f.string(0);
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        return Value::str(s);
    });

    spectrum.addFunction("INSTR", [](FunctionCall& f) {
        f.expectCount(2, 3);
        const std::string& a = f.string(0);
        const std::string& b = f.string(1);
        double start = f.count() == 3 ? f.number(2) : 1;
        if (start < 1) f.error(Report::InvalidArgument);
        size_t from = size_t(start) - 1;
        if (from > a.size()) return Value::num(0);
        size_t p = a.find(b, from);
        return Value::num(p == std::string::npos ? 0 : double(p + 1));
    });

    spectrum.addFunction("MAX", [](FunctionCall& f) {
        if (f.count() == 0) f.error(Report::ParameterError);
        double m = f.number(0);
        for (size_t i = 1; i < f.count(); ++i) m = std::max(m, f.number(i));
        return Value::num(m);
    });

    spectrum.addFunction("MIN", [](FunctionCall& f) {
        if (f.count() == 0) f.error(Report::ParameterError);
        double m = f.number(0);
        for (size_t i = 1; i < f.count(); ++i) m = std::min(m, f.number(i));
        return Value::num(m);
    });

    spectrum.addFunction("TIME$", [](FunctionCall& f) {
        f.expectCount(0, 0);
        std::time_t t = std::time(nullptr);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
        return Value::str(buf);
    });

    spectrum.addFunction("DATE$", [](FunctionCall& f) {
        f.expectCount(0, 0);
        std::time_t t = std::time(nullptr);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
        return Value::str(buf);
    });

    spectrum.addFunction("ENV$", [](FunctionCall& f) {
        f.expectCount(1, 1);
        const char* v = std::getenv(toUtf8(f.string(0)).c_str());
        return Value::str(v ? fromUtf8(v) : std::string());
    });
}

} // namespace zxgw
