// SPDX-License-Identifier: GPL-3.0-or-later
// The game unlocks a trophy only as it happens: a save imported from a PS4 (or played on another
// port) never reports what it had already done. The game's event scripts award most trophies
// (event/*.emevd, instruction 2003[28] "trophy unlock"), and an event that has run sets the flag
// of its own id, kept in the save: those flags are read once a character is in the world, and each
// one set is reported as earned in this save ("Runtime: trophy N earned in this save"), which the
// launcher records without a notification per trophy. Bosses: their defeat flags, the awarding
// events' ids (as in droogie/bbhost's boss rush table, plugins/boss_rush/boss_rush.cpp). Areas,
// Micolash and the endings: the ids of the events that award them; an ending also sets 6600-6602.
// Trophies the game's code awards (items, weapons, chalices, Queen Yharnam) are not in the
// scripts and are not read here. Trophy ids are the game's (TROP.SFM order, Core/Trophies.cs).
#include "trophy_sync.h"
#include "engine_state.h"
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace BbEngine::TrophySync {
namespace {
struct Earned {
    int trophy;
    std::uint32_t flag;
    std::uint32_t also = 0; ///< another flag that means the same, or 0
};
constexpr Earned kEarned[] = {
    {21, 12411700}, // Cleric Beast
    {14, 12411800}, // Father Gascoigne
    {22, 12301800}, // Blood-starved Beast
    {15, 12401800}, // Vicar Amelia
    {23, 12201800}, // The Witch of Hemwick
    {24, 12301700}, // Darkbeast Paarl
    {16, 12701800}, // Shadow of Yharnam
    {17, 13201800}, // Rom, the Vacuous Spider
    {18, 12801800}, // The One Reborn
    {26, 12501800}, // Martyr Logarius
    {25, 13301800}, // Amygdala
    {27, 12421700}, // Celestial Emissary
    {28, 12421800}, // Ebrietas, Daughter of the Cosmos
    {20, 12601800}, // Mergo's Wet Nurse
    {36, 13401800}, // Ludwig, the Holy Blade
    {38, 13501850}, // Living Failures
    {37, 13501800}, // Lady Maria of the Astral Clocktower
    {39, 13401850}, // Laurence, the First Vicar
    {35, 13601800}, // Orphan of Kos
    {19, 12601850}, // Micolash, Host of the Nightmare
    {10, 12500000}, // Cainhurst (m25_00's entry cutscene)
    {11, 12420990}, // The Choir
    {12, 12110990}, // The Source of the Dream (the abandoned old workshop)
    {13, 13200960}, // Nightmare Lecture Building
    {1, 12100180, 6600}, // Yharnam Sunrise
    {2, 12100000, 6601}, // Honoring Wishes
    {3, 12100002, 6602}, // Childhood's Beginning
};
constexpr int kCount = int(sizeof(kEarned) / sizeof(kEarned[0]));

std::bitset<kCount> g_reported;
std::chrono::steady_clock::time_point g_next{};
std::uint64_t g_read_record = 0; // the character whose save was read (0: read the next one)
} // namespace

void Tick() {
    if (g_reported.all()) return;
    // A few seconds apart: the flags come with the save, before the world appears.
    const auto now = std::chrono::steady_clock::now();
    if (now < g_next) return;
    g_next = now + std::chrono::seconds(3);
    // Once per character in the world: the flags its save brought. Read again after a load
    // (out of the world, or another character), not polled - a boss killed now sets its flag
    // before the game unlocks its trophy, which must keep its own notification.
    const std::uint64_t record = player_record();
    if (!record || !world_player_block(nullptr)) {
        g_read_record = 0;
        return;
    }
    if (record == g_read_record) return;
    g_read_record = record;
    for (int i = 0; i < kCount; ++i) {
        if (g_reported.test(i)) continue;
        bool set = false, also = false;
        if (!(event_flag_get(kEarned[i].flag, &set) && set) &&
            !(kEarned[i].also && event_flag_get(kEarned[i].also, &also) && also)) {
            continue;
        }
        g_reported.set(i);
        std::printf("Runtime: trophy %d earned in this save\n", kEarned[i].trophy);
    }
}
} // namespace BbEngine::TrophySync
