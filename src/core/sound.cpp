// zxgw - Gosh Wonderful ZX Spectrum BASIC for the terminal
// Copyright (c) 2026 zxgw contributors. MIT License, see LICENSE.md.
//
// Part 3. LOUDSPEAKER ROUTINES
//
// L03F8 BEEP duration, pitch.  The checks and the frequency calculation are
// those of the ROM; the tone itself is produced by the host (the terminal
// host just waits for the duration).

#include "machine.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace zxgw {

using namespace sv;

void Machine::beepCmd() {
    double pitch = popNum();
    double ip = std::floor(pitch);
    double factor = checked(1 + 0.05762265 * (pitch - ip));
    if (ip < -128 || ip > 127) error(0x0A);                    // REPORT-B
    if (ip < -60) error(0x0A);                                 // BE-I-OK: -60 .. 127
    // L046E semi-tone table: the octave from middle C.
    static const double semitone[12] = {
        261.6255653, 277.1826310, 293.6647679, 311.1269837, 329.6275569, 349.2282314,
        369.9944227, 391.9954360, 415.3046976, 440.0000000, 466.1637615, 493.8833013
    };
    int a = int(ip) + 60;
    int octave = a / 12 - 5;                                   // BE-OCTAVE
    int note = a % 12;
    double freq = checked(semitone[note] * factor * std::ldexp(1.0, octave));
    // FIND-INT1 on the duration: at most 10 seconds.
    stack(calc.empty() ? 0 : calc.back().num);
    uint8_t secondsRounded = findInt1();
    if (secondsRounded > 10) error(0x0A);
    double duration = popNum();
    double cycles = checked(duration * freq);
    double period = checked(437500 / freq - 30.125);
    stack(period);
    findInt2();                                                // REPORT-B if out of range
    stack(cycles);
    findInt2();
    present(true);
    host.beep(freq, duration);
}

// The default beep of a host just waits.
void Host::beep(double frequencyHz, double seconds) {
    (void)frequencyHz;
    if (seconds > 0) std::this_thread::sleep_for(std::chrono::microseconds(long(seconds * 1e6)));
}

void Host::printerLine(const std::string& utf8Line) { (void)utf8Line; }

} // namespace zxgw
