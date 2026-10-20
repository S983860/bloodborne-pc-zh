// SPDX-License-Identifier: GPL-3.0-or-later
// Live cheats switched from bbport.ini (cheat_*) and the in-game overlay: Blood Vials and
// Quicksilver Bullets (infinite or kept at an amount), stamina, stealth, firearm range, keeping
// Blood Echoes on death, an echo multiplier, weapon durability, consumables; and the overlay's
// add Blood Echoes / Insight actions. See cheats.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::Cheats {
enum Which : int {
    Vials = 0, Bullets, Stamina, Stealth, GunRange, KeepEchoes, EchoMultiplier, NoDurability, Consumables,
    Echoes, ///< the overlay's add Blood Echoes / Insight actions
    WhichCount
};
/// Checks the game code the cheats rely on. Loader start, after the executable is verified.
bool Install(std::uint8_t* image, std::size_t size);
/// The main thread's frame hook (change_appearance.cpp FrameHook): the only place cheats act.
void Tick();
/// Why a cheat cannot run, for the overlay (static text), or nullptr while it can.
const char* Unavailable(Which which);
/// Queues Blood Echoes / Insight to add; applied by Tick with a character in the world, clamped to
/// the game's limits (999,999,999 echoes, 99 insight). Any thread.
void AddEchoes(int amount);
void AddInsight(int amount);
} // namespace BbEngine::Cheats
