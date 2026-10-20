// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace BbEngine::PcGuard {
constexpr int StaminaCost(int maximum) { return maximum > 0 ? maximum / 10 + (maximum % 10 != 0) : 0; }
constexpr bool PerfectWindow(std::uint64_t now, std::uint64_t started, int milliseconds, bool held) {
    return held && started > 0 && now >= started && now - started <= static_cast<unsigned>(milliseconds);
}
// The original hand selector uses the offhand in weapon styles 1 and 5.
// The private PC guard is a right-hand Saw Cleaver action in those styles.
constexpr bool UsesRightHand(bool local, int hp, int weapon, int style, float l2, float l3) {
    return local && hp > 0 && weapon >= 7000000 && weapon < 7090000 &&
        (style == 1 || style == 5) && l2 > 0 && l3 > 0;
}
bool Install(std::uint8_t* image, std::size_t size);
// Called after the ordinary PC bindings and action priority have been resolved.
void Input(bool held);
}
