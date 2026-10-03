// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Embedding the interpreter in another program.
//
// This example runs a BASIC program without a terminal: the BatchHost reads
// "keyboard" lines from a string stream and writes everything printed to
// std::cout.  It adds one command and one function in C++:
//
//   LOG "text"              writes a line to std::cerr from BASIC
//   SQUARE(n)               returns n*n
//
// Build with the library:  target_link_libraries(myapp PRIVATE zxgw::zxgw)

#include <iostream>
#include <sstream>

#include "zxgw/terminal_host.h"
#include "zxgw/zxgw.h"

int main() {
    std::istringstream keyboard(
        "10 FOR i=1 TO 5\n"
        "20 PRINT i, SQUARE(i)\n"
        "30 NEXT i\n"
        "40 LOG \"done\"\n"
        "RUN\n");

    zxgw::BatchHost::Config config;
    config.echo = false;
    zxgw::BatchHost host(keyboard, std::cout, config);
    zxgw::Spectrum spectrum(host);

    spectrum.addCommand("LOG", [](zxgw::Statement& s) {
        std::string text = s.string();      // the parameter (syntax checked)
        s.end();                            // the syntax check ends here
        std::cerr << "BASIC says: " << zxgw::toUtf8(text) << "\n";
    });

    spectrum.addFunction("SQUARE", [](zxgw::FunctionCall& f) {
        f.expectCount(1, 1);
        double n = f.number(0);
        return zxgw::Value::num(n * n);
    });

    return spectrum.run();
}
