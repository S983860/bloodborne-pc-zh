// SPDX-License-Identifier: GPL-3.0-or-later
// Game-side fixes for playing online, adapted from droogie/bbhost src/engine/chara_id.cpp and
// src/engine/sos_signs.cpp (566184a); see licenses/BBHOST-PORTS.md. Binary Ninja addresses
// (image base 0x400000), as the other engine hooks.
#include "online_fixes.h"
#include "engine_state.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace BbEngine::OnlineFixes {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))

// The network login's character step (0x2043ee0): when GameDataMan's local PlayerGameData
// (0x593b130 -> +8) holds a character id at +0x690 it hands it to FrpgNetMan and goes on; when it
// holds 0 it asks the server (sync_chara_id), whose first PublishCharacterIdList entry the
// callback stores with no check - 0x8000000000000000 when the list is empty, as The Hunter's Dream
// answered until 2026-10-10. The save keeps that, and the character never asks again: so it is
// put back to 0 before the step runs, and the step asks.
constexpr std::uint64_t kCharaStep = 0x2043ee0;
// push rbp; mov rbp, rsp; push r15; push r14; push r12; push rbx; sub rsp, 0x40
const std::uint8_t kCharaStepPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                           0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x40};
constexpr std::uint64_t kGameDataMan = 0x593b130;
constexpr std::size_t kPlayerGameData = 0x8, kCharaId = 0x690;
constexpr std::uint64_t kUnset = 0x8000000000000000ull;

// A summon sign entry waits this long for its host (.rodata 180.0f, read by the sign code).
constexpr std::uint64_t kSignTimeout = 0x4d27a5c;
constexpr float kGameSignTimeout = 180.0f;

std::uint8_t* g_game_data_man = nullptr;  // the pointer's slot in the image
std::atomic<int> g_cleared{0};

template<typename T> bool Read(std::uint64_t at, T* out) {
    return at && host_read_safe(reinterpret_cast<void*>(at), out, sizeof(T));
}

GUEST_ABI std::int64_t CharaStep(std::uint64_t, const std::uint64_t*) {
    std::uint64_t man = 0, data = 0, id = 0;
    if (!Read(reinterpret_cast<std::uint64_t>(g_game_data_man), &man) || !Read(man + kPlayerGameData, &data) ||
        !Read(data + kCharaId, &id)) return 0;
    if (id == kUnset) {
        const std::uint64_t none = 0;
        std::memcpy(reinterpret_cast<void*>(data + kCharaId), &none, sizeof(none));
        if (g_cleared.fetch_add(1) < 4)
            host_log("online: this character has no id from the server (0x8000000000000000); asking for one");
    } else if (id) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true)) host_log("online: character id %llu", static_cast<unsigned long long>(id));
    }
    return 0;  // the step runs as it would
}
} // namespace

bool Install(std::uint8_t* image, std::size_t size) {
    const char* online = std::getenv("BB_ONLINE");
    if (!online || online[0] != '1') return false;
    bool any = false;
    Hook hook;
    if (Prepare(hook, image, size, kCharaStep - 0x400000, kCharaStepPrologue, CharaStep)) {
        g_game_data_man = image + (kGameDataMan - 0x400000);
        Commit(std::span{&hook, 1});
        any = true;
    } else {
        host_log("online: character id fix off: the login's character step differs from 1.09");
    }
    // BB_SIGN_TIMEOUT seconds (the launcher's; 0 or unset keeps the game's 180).
    const char* timeout = std::getenv("BB_SIGN_TIMEOUT");
    const long seconds = timeout ? std::strtol(timeout, nullptr, 10) : 0;
    const std::size_t at = kSignTimeout - 0x400000;
    float value = 0;
    if (seconds > 0 && seconds != 180 && size >= at + sizeof(value)) {
        std::memcpy(&value, image + at, sizeof(value));
        if (value == kGameSignTimeout) {
            value = static_cast<float>(std::min(seconds, 600L));
            std::memcpy(image + at, &value, sizeof(value));  // the loader still owns the image writable
            host_log("online: a summon sign entry times out after %.0f s (the game's 180)", value);
            any = true;
        } else {
            host_log("online: sign timeout left alone: 0x%llx is not 180.0", static_cast<unsigned long long>(kSignTimeout));
        }
    }
    return any;
}
} // namespace BbEngine::OnlineFixes
