// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// L1793 CAT-ETC.  In the 48K ROM CAT, ERASE, FORMAT and MOVE only give
// "O Invalid stream" (they were meant for the Microdrive, later handled by
// the Interface 1 ROM).  In the terminal they work on the file system:
//
//   CAT                list the current directory
//   CAT "dir"          list a directory (zxgw extension of the syntax)
//   ERASE "file"       delete a file
//   MOVE "old","new"   rename or move a file
//   FORMAT "dir"       create a directory

#include "machine.h"

#include <algorithm>
#include <filesystem>

#include "tables.h"

namespace zxgw {

using namespace sv;
namespace fs = std::filesystem;

void Machine::catEtc(uint8_t command) {
    if (command == tok::CAT) {
        uint8_t a = getChar();
        bool given = !prStEnd(a);
        if (given) exptExp();
        checkEnd();
        std::string dir = given ? toUtf8(popStr()) : std::string(".");
        std::error_code ec;
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            std::string n = entry.path().filename().string();
            if (!n.empty() && n[0] == '.') continue;
            if (entry.is_directory(ec)) n += "/";
            names.push_back(n);
        }
        if (ec) error(0x0E);                                   // Invalid file name
        std::sort(names.begin(), names.end());
        chanOpen(2);
        temps();
        std::string title = fs::absolute(dir, ec).lexically_normal().string();
        for (uint8_t c : fromUtf8(title)) printA(c);
        printA(0x0D);
        printA(0x0D);
        for (const std::string& n : names) {
            for (uint8_t c : fromUtf8(n)) printA(c);
            printA(0x0D);
        }
        return;
    }
    // FORMAT, ERASE and MOVE: the syntax has been checked already.
    std::error_code ec;
    if (command == tok::MOVE) {
        std::string to = toUtf8(popStr());
        std::string from = toUtf8(popStr());
        fs::rename(from, to, ec);
        if (ec) error(0x0E);
        return;
    }
    std::string name = toUtf8(popStr());
    if (name.empty()) error(0x0E);
    if (command == tok::ERASE) {
        if (!fs::remove(name, ec) || ec) error(0x0E);
    } else {                                                   // FORMAT
        fs::create_directories(name, ec);
        if (ec) error(0x0E);
    }
}

} // namespace zxgw
