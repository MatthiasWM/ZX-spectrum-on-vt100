// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The examples of docs/extending.md, compiled and run as a test.
// Without arguments it runs a few BASIC lines through BatchHost; with
// --terminal it starts an interactive session with the new commands.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#include "zxgw/terminal_host.h"
#include "zxgw/zxgw.h"

static void addExamples(zxgw::Spectrum& spectrum) {
    // SQUARES n  - print the squares from 1 to n
    spectrum.addCommand("SQUARES", [](zxgw::Statement& s) {
        long n = s.integer(1, 1000);    // a number, rounded, checked
        s.end();                        // end of the statement
        for (long i = 1; i <= n; ++i) {
            s.print(double(i * i));
            s.print("\r");              // CHR$ 13 is the Spectrum's ENTER
        }
    });

    // WAIT [frames]
    spectrum.addCommand("WAIT", [](zxgw::Statement& s) {
        long frames = 50;
        if (!s.atEnd()) frames = s.integer(0, 65535);
        s.end();
        std::this_thread::sleep_for(std::chrono::milliseconds(frames * 20));
    });

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
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--terminal") == 0) {
        zxgw::TerminalHost host;
        zxgw::Spectrum spectrum(host);
        addExamples(spectrum);
        return spectrum.run();
    }
    std::istringstream keyboard(
        "SQUARES 3\n"
        "SQUARES 0\n"
        "WAIT 1: PRINT \"waited\"\n"
        "GETENV \"ZXGW_EXAMPLE\", v$: PRINT v$\n"
        "GETENV \"ZXGW_EXAMPLE\", v\n"
        "PRINT HYPOT(3,4), REPEAT$(\"ab\",3)\n"
        "10 SQUARES 2: PRINT REPEAT$(\"-\", HYPOT(6,8))\n"
        "RUN\n"
        "LIST\n");
    zxgw::BatchHost host(keyboard, std::cout);
    zxgw::Spectrum spectrum(host);
    addExamples(spectrum);
    return spectrum.run();
}
