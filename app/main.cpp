// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// The command line application.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "zxgw/standard_extensions.h"
#include "zxgw/terminal_host.h"
#include "zxgw/zxgw.h"

namespace {

void usage() {
    std::printf(
        "zxgw %s - the \"Gosh Wonderful\" ZX Spectrum BASIC for the terminal\n"
        "\n"
        "Usage: zxgw [options] [file]\n"
        "\n"
        "  file                 a program to LOAD (.tap or .bas text listing)\n"
        "\n"
        "Options:\n"
        "  -r, --run            RUN the program after loading it\n"
        "  -e, --exec LINE      enter LINE as a direct command (can be repeated)\n"
        "  -b, --batch          read input lines from stdin and write all output as\n"
        "                       plain text to stdout (automatic if stdin or stdout\n"
        "                       is not a terminal)\n"
        "      --no-echo        batch mode: do not copy the input lines to the output\n"
        "  -g, --geometry WxH   screen size in characters (default: the terminal size,\n"
        "                       32x24 in batch mode)\n"
        "  -c, --color MODE     auto, truecolor, 256, 16 or mono\n"
        "  -s, --scale-graphics PLOT/DRAW/CIRCLE use the classic 256x176 pixels,\n"
        "                       scaled onto the braille dots of the terminal\n"
        "  -k, --classic        classic keyword entry ('K' mode), as after STOP\n"
        "  -p, --printer FILE   ZX Printer output file (default: zxprinter.txt;\n"
        "                       in batch mode the printer writes to stdout)\n"
        "  -t, --throttle N     execute at most N statements per second\n"
        "      --bell           BEEP rings the terminal bell\n"
        "      --dump-screen    print the final screen as text when leaving\n"
        "  -h, --help           this help\n"
        "  -v, --version        the version\n"
        "\n"
        "Keys: arrows move the cursor, Tab = EDIT, Esc or Ctrl-C = BREAK,\n"
        "Alt+key = SYMBOL SHIFT, Ctrl-E = extended mode, Ctrl-G = graphics mode,\n"
        "Ctrl-L redraws, Ctrl-D or BYE quits.  See README.md and docs/.\n",
        zxgw::kVersion);
}

std::string quoted(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\"\"";
        else r += c;
    }
    return r + "\"";
}

} // namespace

int main(int argc, char** argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
    // Debug runtime checks print to stderr instead of opening a dialog box.
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    zxgw::Options options;
    zxgw::TerminalHost::Config termConfig;
    zxgw::BatchHost::Config batchConfig;
    bool batch = false, run = false, dumpScreen = false;
    std::string file;
    std::vector<std::string> commands;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "zxgw: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(); return 0; }
        if (a == "-v" || a == "--version") { std::printf("zxgw %s\n", zxgw::kVersion); return 0; }
        if (a == "-r" || a == "--run") { run = true; continue; }
        if (a == "-b" || a == "--batch") { batch = true; continue; }
        if (a == "--no-echo") { batchConfig.echo = false; continue; }
        if (a == "-s" || a == "--scale-graphics") { options.scaleGraphics = true; continue; }
        if (a == "-k" || a == "--classic") { options.classicKeywords = true; continue; }
        if (a == "--bell") { termConfig.bell = true; continue; }
        if (a == "--dump-screen") { dumpScreen = true; continue; }
        if (a == "-e" || a == "--exec") { commands.push_back(value("--exec")); continue; }
        if (a == "-t" || a == "--throttle") { options.throttle = std::atoi(value("--throttle").c_str()); continue; }
        if (a == "-p" || a == "--printer") {
            std::string p = value("--printer");
            termConfig.printerFile = p;
            batchConfig.printerToOutput = p == "-";
            continue;
        }
        if (a == "-g" || a == "--geometry") {
            std::string g = value("--geometry");
            int w = 0, h = 0;
            if (std::sscanf(g.c_str(), "%dx%d", &w, &h) != 2 || w < 16 || h < 4) {
                std::fprintf(stderr, "zxgw: bad geometry '%s' (e.g. 32x24)\n", g.c_str());
                return 2;
            }
            options.columns = w;
            options.rows = h;
            continue;
        }
        if (a == "-c" || a == "--color") {
            std::string m = value("--color");
            if (m == "auto") termConfig.color = zxgw::ColorMode::Auto;
            else if (m == "truecolor" || m == "24bit") termConfig.color = zxgw::ColorMode::TrueColor;
            else if (m == "256") termConfig.color = zxgw::ColorMode::Ansi256;
            else if (m == "16") termConfig.color = zxgw::ColorMode::Ansi16;
            else if (m == "mono" || m == "none") termConfig.color = zxgw::ColorMode::Mono;
            else { std::fprintf(stderr, "zxgw: unknown color mode '%s'\n", m.c_str()); return 2; }
            continue;
        }
        if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "zxgw: unknown option '%s' (see --help)\n", a.c_str());
            return 2;
        }
        file = a;
    }

    if (!batch && !zxgw::TerminalHost::interactive()) batch = true;

    std::unique_ptr<zxgw::Host> host;
    if (batch) {
        if (options.columns) {
            batchConfig.columns = options.columns;
            batchConfig.rows = options.rows;
        }
        host.reset(new zxgw::BatchHost(std::cin, std::cout, batchConfig));
    } else {
        host.reset(new zxgw::TerminalHost(termConfig));
    }

    int code;
    {
        zxgw::Spectrum spectrum(*host, options);
        zxgw::addStandardExtensions(spectrum);
        if (!file.empty()) {
            spectrum.command("LOAD " + quoted(file));
            if (run) spectrum.command("RUN");
        }
        for (const std::string& c : commands) spectrum.command(c);
        code = spectrum.run();
        if (dumpScreen) std::cout << spectrum.screenText();
    }
    host.reset();
    return code;
}
