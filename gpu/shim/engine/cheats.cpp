// SPDX-License-Identifier: GPL-3.0-or-later
// Live cheats: Blood Vials (goods 1000) and Quicksilver Bullets (goods 900), each off, infinite
// or kept at an amount (bbport.ini cheat_vials / cheat_bullets, the overlay's Cheats section).
//
// The inventory helpers are the game's own, the ones its debug menu's "fill to the maximum"
// (sub_18d3dc0) calls, as droogie/bbhost's boss_rush plugin found and used them
// (plugins/boss_rush/boss_rush.cpp refill(), GPL-3.0); the param row layouts are bbhost's
// include/bbhost/params.hpp (EQUIP_PARAM_GOODS_ST, EQUIP_PARAM_WEAPON_ST). Credit: droogie.
// Every address and offset here was read back from our 1.09 image (071df19c...); Install checks
// the code bytes that show them again before anything is enabled.
//
// Save data: a cheat changes param bytes in memory only, from a snapshot of their original values,
// and writes those values back when it is turned off. The counts it raises are inventory, which
// the game saves: cheats affect the save (the counts) only while they are on, and turning one off
// leaves the counts as they are (above the normal carry cap, the game lowers them to the cap the
// next time the count changes).
//
// The other cheats (second half of this file) follow the same rules: each changes only bytes it
// took a snapshot of first (param rows, two static debug flags, one code range) and writes the
// originals back when turned off; or it uses the game's own data and helpers from the frame hook
// (stamina, durability, echoes and insight), which leaves nothing to restore. Echoes, insight and
// durability are save data and stay as they are when a cheat is turned off.
#include "cheats.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "bbport_settings.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <span>
#include <vector>

namespace BbEngine::Cheats {
namespace {
using Clock = std::chrono::steady_clock;

// Game functions (BN addresses: image base 0x400000).
constexpr std::uint64_t kGoodsRow = 0x231e3d0;      // (&{id, pad, row}, goods id): the goods row
constexpr std::uint64_t kGoodsCap = 0x231e7e0;      // (&{id, pad, row}): most one can carry
constexpr std::uint64_t kInventorySlot = 0x18d9f60; // (EquipInventoryData*, &{0x40000000|id, 0, 0, 0})
constexpr std::uint64_t kAddItem = 0x18cdfd0;       // (EquipGameData*, type, id, count, 1, 1, [stack] 0)
constexpr std::uint32_t kGoods = 0x40000000;        // the item type of goods
// PlayerGameData (GameDataMan 0x593b130 +0x08, engine_state player_record) + 0x1d0 is EquipGameData,
// which holds EquipInventoryData at +0x158: the first *(+0x24) of its 16-byte entries at *(+0x58),
// the rest at *(+0x48), the slot bound *(+0x88); an entry's item at +0, its quantity at +8.
constexpr std::uint64_t kEquipGameData = 0x1d0, kInventory = 0x158;
constexpr std::uint64_t kInvFirst = 0x24, kInvRest = 0x48, kInvHead = 0x58, kInvBound = 0x88;
// EQUIP_PARAM_GOODS_ST: int16 maxNum at 0x36; bitfield bytes 0x42-0x47: isConsume (0x44 bit 7),
// useBulletMaxNum (0x46 bit 6), useHpCureMaxNum (0x46 bit 7). With either use* bit the carry cap is
// the live one (runes), else the row's maxNum (sub_231e7e0).
constexpr std::size_t kGoodsRowSize = 0x6c, kMaxNum = 0x36, kConsumeByte = 0x44, kCapByte = 0x46;
constexpr std::uint8_t kIsConsume = 0x80, kUseBulletMaxNum = 0x40, kUseHpCureMaxNum = 0x80;
// EQUIP_PARAM_WEAPON_ST: int8 bulletConsumeNum at 0x13a, read from the row at each shot. Weapons
// without a firearm hold -1 there (694 of the 1090 rows in 1.09, every melee weapon); 1, 2, 10
// and 12 are the costs of the guns, trick-weapon guns and arcane tools.
constexpr std::size_t kWeaponRowSize = 0x13c, kBulletConsumeNum = 0x13a;
constexpr std::uint32_t kBloodVial = 1000, kQuicksilverBullets = 900;
constexpr auto kDebounce = std::chrono::milliseconds(400); // a slider being dragged applies once
constexpr auto kInterval = std::chrono::milliseconds(250); // refills: at most four times a second
constexpr auto kSettle = std::chrono::milliseconds(1500);  // in the world this long before acting
constexpr const char* kNotVerified = "the engine's executable was not verified";

// The code that shows the functions, their arguments and the layouts above (our image's bytes).
constexpr std::uint8_t kSigRow[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xf4,
    0x49,0x89,0xfe,0x31,0xc9,0x45,0x85,0xe4,0x0f,0x88,0xf2,0x00,0x00,0x00};
constexpr std::uint8_t kSigCap[] = {0x55,0x48,0x89,0xe5,0x53,0x48,0x83,0xec,0x28,0x48,0x8b,0x1d,0xf0,0x53,
    0x4c,0x03,0x48,0x8b,0x03,0x48,0x89,0x45,0xf0,0x48,0x8b,0x4f,0x08,0xb8,0x63,0x00,0x00,0x00,0x48,0x85,
    0xc9,0x0f,0x84,0x8c,0x00,0x00,0x00,0x8b,0x51,0x42,0x0f,0xb7,0x41,0x46};
constexpr std::uint8_t kSigCapMaxNum[] = {0x0f,0xbf,0x49,0x36,0x85,0xc9,0xb8,0x63,0x00,0x00,0x00,0x0f,0x4f,
    0xc1};
constexpr std::uint8_t kSigSlot[] = {0x8b,0x06,0x83,0xf8,0xff,0x0f,0x84,0xa6,0x00,0x00,0x00,0x89,0xc1,0x48,
    0x69,0xc9,0x15,0x3f,0xef,0x03,0x48,0xc1,0xe9,0x20};
constexpr std::uint8_t kSigAdd[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,
    0x81,0xec,0x18,0x01,0x00,0x00,0x41,0x89,0xcd,0x41,0x89,0xd4,0x49,0x89,0xff,0x48,0x8b,0x1d,0xec,0x5b,
    0xf1,0x03,0x48,0x8b,0x03,0x48,0x89,0x45,0xd0,0x41,0xc7,0x87,0x9c,0x02};
// sub_18cdfd0, goods: the row (sub_231e3d0), then the inventory at EquipGameData + 0x158.
constexpr std::uint8_t kSigAddInventory[] = {0xe8,0x22,0x02,0xa5,0x00,0x4d,0x8d,0xb7,0x58,0x01,0x00,0x00,
    0xbe,0x00,0x00,0x00,0x40,0x4c,0x89,0xf7,0x44,0x89,0xe2,0xe8,0xbb,0xbc,0x00,0x00};
// Using goods: the quantity changes only when the row's isConsume bit (0x42 dword, 0x800000) is set.
constexpr std::uint8_t kSigConsume[] = {0xe8,0x0b,0xd6,0xa4,0x00,0x48,0x89,0xde,0x48,0x8b,0x45,0xa0,0x48,
    0x85,0xc0,0x0f,0x84,0xc5,0x02,0x00,0x00,0x8b,0x40,0x42,0xa9,0x00,0x00,0x80,0x00,0x0f,0x84,0xb7,0x02,
    0x00,0x00};
// The fire check reads bulletConsumeNum (movsx r13d, byte [row+0x13a]) from the weapon row.
constexpr std::uint8_t kSigBullets[] = {0xe8,0xcb,0xf8,0x62,0x00,0x48,0x8b,0x45,0x98,0x45,0x31,0xe4,0x41,
    0xbd,0x00,0x00,0x00,0x00,0x48,0x85,0xc0,0x74,0x08,0x44,0x0f,0xbe,0xa8,0x3a,0x01,0x00,0x00};
// A game call of sub_18cdfd0: rdi = *(*0x593b130 + 8) + 0x1d0.
constexpr std::uint8_t kSigEquip[] = {0x48,0x8d,0x05,0x3a,0xab,0x22,0x04,0x48,0x8b,0x00,0x4c,0x8b,0x68,0x08,
    0x49,0x8d,0xbd,0xd0,0x01,0x00,0x00};
// The debug menu's fill: slot (sub_18d9f60), bound +0x88, first +0x24, +0x58 / +0x48, quantity +8.
constexpr std::uint8_t kSigEntry[] = {0xe8,0x9d,0x5a,0x00,0x00,0xc7,0x85,0x34,0xfe,0xff,0xff,0x00,0x00,0x00,
    0x00,0x85,0xc0,0x78,0x4f,0x41,0x8b,0x8c,0x24,0xe0,0x01,0x00,0x00,0xff,0xc1,0x39,0xc1,0x76,0x41,0x89,
    0xc1,0x41,0x2b,0x8c,0x24,0x7c,0x01,0x00,0x00,0x73,0x10,0x89,0xc1,0x48,0xc1,0xe1,0x04,0x49,0x03,0x8c,
    0x24,0xb0,0x01,0x00,0x00,0xeb,0x0c,0x48,0xc1,0xe1,0x04,0x49,0x03,0x8c,0x24,0xa0,0x01,0x00,0x00,0x83,
    0x39,0x00,0x74,0x14,0x48,0x85,0xc9,0x74,0x0f,0x8b,0x41,0x08,0x89,0x85,0x34,0xfe,0xff,0xff};
struct Signature { std::uint64_t bn; std::span<const std::uint8_t> bytes; };
constexpr Signature kSignatures[] = {
    {kGoodsRow, kSigRow}, {kGoodsCap, kSigCap}, {0x231e887, kSigCapMaxNum}, {kInventorySlot, kSigSlot},
    {kAddItem, kSigAdd}, {0x18ce1a9, kSigAddInventory}, {0x18d0dc0, kSigConsume}, {0x1cfa1d0, kSigBullets},
    {0x17105ef, kSigEquip}, {0x18d44be, kSigEntry},
};

std::uint64_t g_slide = 0;
std::uint8_t* g_image = nullptr;
std::size_t g_image_size = 0;
bool g_installed = false;
// The hunter in the world for kSettle: same record and map block since then (checked every kInterval).
std::uint64_t g_record = 0;
std::uint32_t g_block = 0;
Clock::time_point g_since{}, g_check{};
bool g_present = false, g_settled = false;
std::uint64_t Slot(std::uint64_t bn) { return g_slide + (bn - kPreferredGuestSlide); }
template <typename T> bool Read(std::uint64_t at, T* out) { return at && host_read_safe(reinterpret_cast<const void*>(at), out, sizeof(T)); }
// Guest heap and data: above the null page, below 1 TiB (the guest-visible range), readable.
bool Plausible(std::uint64_t p) { std::uint64_t v = 0; return p >= 0x10000 && p < (std::uint64_t(1) << 40) && Read(p, &v); }
// Our image's bytes at a BN address match `bytes` (checked at Install, before guest code runs).
bool Matches(std::uint64_t bn, std::span<const std::uint8_t> bytes) {
    const std::size_t at = bn - kPreferredGuestSlide;
    return g_image && at <= g_image_size && bytes.size() <= g_image_size - at &&
           !std::memcmp(g_image + at, bytes.data(), bytes.size());
}

// A param byte a cheat may change: its original value (the snapshot) and what is there now.
struct Byte { std::uint64_t at; std::uint8_t original, now; };
struct Cheat {
    const char* name;                  // the log's: "vials", "bullets"
    std::uint32_t goods;
    std::uint8_t cap_bit;              // useHpCureMaxNum / useBulletMaxNum
    std::atomic<const char*> unavailable{kNotVerified};
    bool disabled = false;
    int active = BbSettings::CheatOff, pending = BbSettings::CheatOff;
    Clock::time_point pending_since{};
    // The snapshot: taken once from the original values, kept until the game reloads the table.
    bool snapped = false, weapons = false;
    std::uint64_t goods_cap = 0, goods_file = 0, weapon_cap = 0, weapon_file = 0;
    std::vector<Byte> bytes;           // [0..1] maxNum, [2] isConsume byte, [3] cap byte, then weapons
    int base_cap = 0;                  // the carry cap while the row's cap bytes are original
    int floor = -1;                    // infinite: the count it keeps if the param change does not hold
    bool floor_logged = false;
    int failures = 0;
};
Cheat g_cheats[2] = {{"vials", kBloodVial, kUseHpCureMaxNum}, {"bullets", kQuicksilverBullets, kUseBulletMaxNum}};

struct Handle { std::int32_t id, pad; std::uint64_t row; };
std::uint64_t GuestRow(std::uint32_t id) {
    Handle h{-1, 0, 0};
    Call(Slot(kGoodsRow), &h, id);
    return h.row;
}
int GuestCap(std::uint32_t id) {
    Handle h{-1, 0, 0};
    Call(Slot(kGoodsRow), &h, id);
    return h.row ? static_cast<std::int32_t>(Call(Slot(kGoodsCap), &h)) : -1;
}
// The count held (0 when the item is not in the inventory); false if the inventory looks wrong.
bool Count(std::uint64_t equip, std::uint32_t id, int* have) {
    const std::uint64_t inv = equip + kInventory;
    std::uint32_t bound = 0, first = 0;
    std::uint64_t head = 0, rest = 0;
    if (!Read(inv + kInvBound, &bound) || !Read(inv + kInvFirst, &first) || !Read(inv + kInvHead, &head) ||
        !Read(inv + kInvRest, &rest) || bound > 65536 || first > bound + 1 || (!Plausible(head) && !Plausible(rest)))
        return false;
    std::uint32_t item[4] = {kGoods | id, 0, 0, 0};
    const auto slot = static_cast<std::int32_t>(Call(Slot(kInventorySlot), inv, item));
    *have = 0;
    if (slot < 0 || static_cast<std::uint32_t>(slot) >= bound + 1) return true;
    const auto index = static_cast<std::uint32_t>(slot);
    if (!Plausible(index < first ? head : rest)) return false;
    const std::uint64_t entry = index < first ? head + std::uint64_t(index) * 16 : rest + std::uint64_t(index - first) * 16;
    std::uint32_t e[4] = {};
    if (!Read(entry, &e)) return false;
    if (e[0]) *have = static_cast<std::int32_t>(e[2]);
    return *have >= 0 && *have <= 9999;
}

// The game reloaded the table (a new file): the snapshot's addresses are no longer its rows.
bool SameTables(const Cheat& c) {
    return params_table_file(c.goods_cap) == c.goods_file && (!c.weapons || params_table_file(c.weapon_cap) == c.weapon_file);
}

// Writes back each original value that is still the one this cheat wrote; never writes elsewhere.
void RestoreSafe(Cheat& c) {
    if (!c.snapped) return;
    if (!SameTables(c)) { c.snapped = false; c.bytes.clear(); return; }
    for (Byte& b : c.bytes) {
        std::uint8_t current = 0;
        if (b.now != b.original && Read(b.at, &current) && current == b.now) {
            std::memcpy(reinterpret_cast<void*>(b.at), &b.original, 1);
            b.now = b.original;
        }
    }
}

void Disable(Cheat& c, const char* why) {
    if (c.disabled) return;
    c.disabled = true;
    RestoreSafe(c);
    c.unavailable.store(why);
    std::printf("Cheats: %s unavailable: %s\n", c.name, why);
}

// Reads one table's records and the rows' bytes (one read each); checks the rows' size.
bool LoadTable(const char* table, std::size_t row_size, ParamTable* t, std::vector<ParamRecord>* records,
               std::vector<std::uint8_t>* data, std::uint64_t* lo) {
    if (!params_table(table, t) || !params_records(*t, records) || records->size() < 2 ||
        (*records)[1].data - (*records)[0].data != row_size) return false;
    std::uint64_t first = ~std::uint64_t(0), last = 0;
    for (const ParamRecord& r : *records) { first = std::min(first, r.data); last = std::max(last, r.data); }
    if (last - first > 16 * 1024 * 1024) return false;
    data->resize(last - first + row_size);
    *lo = first;
    return host_read_safe(reinterpret_cast<const void*>(first), data->data(), data->size());
}

// The snapshot: the goods row's bytes (vials, bullets) and, for bullets, each weapon row's
// bulletConsumeNum that is not 0. Checked against the game's own row lookup and plausible values.
const char* Snapshot(Cheat& c) {
    ParamTable goods;
    std::vector<ParamRecord> records;
    std::vector<std::uint8_t> data;
    std::uint64_t lo = 0;
    if (!LoadTable("EquipParamGoods", kGoodsRowSize, &goods, &records, &data, &lo))
        return "EquipParamGoods not found or its rows are not 0x6c bytes";
    const auto it = std::find_if(records.begin(), records.end(), [&](const ParamRecord& r) { return r.id == c.goods; });
    if (it == records.end()) return "goods row missing";
    if (GuestRow(c.goods) != it->data) return "goods row differs from the game's own lookup";
    const std::uint8_t* row = data.data() + (it->data - lo);
    std::int16_t max_num = 0;
    std::memcpy(&max_num, row + kMaxNum, 2);
    if (max_num < 1 || max_num > 999) return "goods row maxNum implausible";
    if (c.goods == kBloodVial && !(row[kConsumeByte] & kIsConsume)) return "Blood Vial row is not consumable";
    c.bytes.clear();
    for (std::size_t off : {kMaxNum, kMaxNum + 1, kConsumeByte, kCapByte})
        c.bytes.push_back({it->data + off, row[off], row[off]});
    c.goods_cap = goods.cap;
    c.goods_file = goods.file;
    c.weapons = false;
    if (c.goods == kQuicksilverBullets) {
        ParamTable weapons;
        std::vector<Byte> found;
        const char* problem = nullptr;
        if (!LoadTable("EquipParamWeapon", kWeaponRowSize, &weapons, &records, &data, &lo)) {
            problem = "EquipParamWeapon not found or its rows are not 0x13c bytes";
        } else {
            // -1 marks rows without a cost (left alone); any other negative value, or one above
            // 64, would mean the offset is wrong for this table.
            std::size_t none = 0;
            for (const ParamRecord& r : records) {
                const auto v = static_cast<std::int8_t>(data[r.data - lo + kBulletConsumeNum]);
                if (v > 64 || v < -1) { problem = "a weapon's bulletConsumeNum is out of range"; break; }
                if (v < 0) { ++none; continue; }
                if (v) found.push_back({r.data + kBulletConsumeNum, std::uint8_t(v), std::uint8_t(v)});
            }
            if (!problem && found.empty()) problem = "no weapon uses bullets";
            if (!problem)
                std::printf("Cheats: bullets: %zu weapons cost bullets, %zu without a cost (-1) left alone\n",
                            found.size(), none);
        }
        if (problem) {
            // The weapon rows stay untouched; infinite bullets keep the count by refilling instead.
            std::printf("Cheats: bullets: weapon params not used (%s); infinite bullets refill instead\n", problem);
        } else {
            c.weapons = true;
            c.weapon_cap = weapons.cap;
            c.weapon_file = weapons.file;
            c.bytes.insert(c.bytes.end(), found.begin(), found.end());
        }
    }
    c.snapped = true;
    return nullptr;
}

bool CapBytesOriginal(const Cheat& c) {
    return c.bytes[0].now == c.bytes[0].original && c.bytes[1].now == c.bytes[1].original &&
           c.bytes[3].now == c.bytes[3].original;
}

// The bytes a mode wants: infinite clears isConsume (vials) or every weapon's bulletConsumeNum
// (bullets); an amount above the carry cap raises the row's maxNum to it and makes the cap the
// row's own (clears useHpCureMaxNum / useBulletMaxNum). Everything else: the original values.
std::vector<std::uint8_t> Wanted(const Cheat& c, int mode) {
    std::vector<std::uint8_t> want(c.bytes.size());
    for (std::size_t i = 0; i < c.bytes.size(); ++i) want[i] = c.bytes[i].original;
    if (mode == BbSettings::CheatInfinite) {
        if (c.goods == kBloodVial) want[2] = std::uint8_t(c.bytes[2].original & ~kIsConsume);
        for (std::size_t i = 4; i < c.bytes.size(); ++i) want[i] = 0;
    } else if (mode > 0 && mode > c.base_cap) {
        want[0] = std::uint8_t(mode & 0xff);
        want[1] = std::uint8_t(mode >> 8);
        want[3] = std::uint8_t(c.bytes[3].original & ~c.cap_bit);
    }
    return want;
}

// Brings the bytes to `want`: every byte to change is checked first (still what this cheat left
// there), then written. A byte changed by anything else stops the cheat.
bool Reconcile(Cheat& c, const std::vector<std::uint8_t>& want) {
    for (std::size_t i = 0; i < c.bytes.size(); ++i) {
        std::uint8_t current = 0;
        if (want[i] != c.bytes[i].now && (!Read(c.bytes[i].at, &current) || current != c.bytes[i].now)) {
            Disable(c, "a param byte changed outside the cheat");
            return false;
        }
    }
    for (std::size_t i = 0; i < c.bytes.size(); ++i) {
        if (want[i] == c.bytes[i].now) continue;
        std::memcpy(reinterpret_cast<void*>(c.bytes[i].at), &want[i], 1);
        c.bytes[i].now = want[i];
    }
    return true;
}

// A changed setting, after the debounce.
void Switch(Cheat& c, int mode) {
    c.active = mode;
    c.floor = -1;
    c.failures = 0;
    g_check = Clock::time_point{}; // the new mode applies at the next frame
    if (mode == BbSettings::CheatOff) {
        if (c.snapped && SameTables(c)) Reconcile(c, Wanted(c, mode));
        else { c.snapped = false; c.bytes.clear(); }
        std::printf("Cheats: %s off, params restored\n", c.name);
    } else if (mode == BbSettings::CheatInfinite) {
        std::printf("Cheats: infinite %s on\n", c.name);
    } else {
        std::printf("Cheats: %s kept at %d\n", c.name, mode);
    }
}

// In the world, settled: params to the mode, then the count (at most every kInterval).
void Run(Cheat& c, std::uint64_t equip) {
    if (c.snapped && !SameTables(c)) {
        std::printf("Cheats: %s: the game reloaded its params; applying again\n", c.name);
        c.snapped = false;
        c.bytes.clear();
    }
    if (!c.snapped) {
        if (const char* why = Snapshot(c)) { Disable(c, why); return; }
    }
    if (CapBytesOriginal(c)) {
        c.base_cap = GuestCap(c.goods);
        if (c.base_cap < 1 || c.base_cap > 999) { Disable(c, "carry cap implausible"); return; }
    }
    if (!Reconcile(c, Wanted(c, c.active))) return;
    int have = 0;
    if (!Count(equip, c.goods, &have)) { Disable(c, "inventory not readable"); return; }
    const int cap = GuestCap(c.goods);
    if (cap < 1 || cap > 999) { Disable(c, "carry cap implausible"); return; }
    int target = 0;
    if (c.active > 0) {
        target = std::min(c.active, cap); // never lowers a count above it
    } else {
        // Infinite: kept full (the carry cap), from none too. Where the param change applies,
        // using one does not lower the count at all; otherwise it is refilled.
        if (c.floor == cap && have < cap && !c.floor_logged) {
            c.floor_logged = true;
            std::printf("Cheats: %s dropped while infinite; keeping the count by refilling\n", c.name);
        }
        c.floor = have;
        target = cap;
    }
    if (have >= target) { c.failures = 0; return; }
    Call(Slot(kAddItem), equip, kGoods, c.goods, target - have, 1, 1, 0);
    int after = have;
    if (Count(equip, c.goods, &after) && after > have) c.failures = 0;
    else if (++c.failures >= 3) Disable(c, "refill had no effect");
}

// ---------------------------------------------------------------------------------------------
// The other cheats.
//
// Stamina: the hunter's ChrIns is WorldChrMan (0x593e878) +0x60; its module container (+0x3b0)
// holds the data module at +0x20 (droogie/bbhost world_chr.cpp, rebirth.cpp heal()), where the
// stamina spending code keeps stamina at +0x134 between -50 and the maximum at +0x138
// (kSigStaminaClamp, one of ten such sites; each first tests the debug menu's "All No Stamina
// Consume" flag 0x593e88a, which would spare every character, so the cheat does not use it).
constexpr std::uint64_t kWorldChrMan = 0x593e878;
constexpr std::uint64_t kPlayerIns = 0x60, kModules = 0x3b0, kDataModule = 0x20, kStamina = 0x134, kMaxStamina = 0x138;
constexpr std::uint8_t kSigStaminaLoad[] = {0x49,0x8b,0x8c,0x24,0xb0,0x03,0x00,0x00,0x4c,0x8b,0x79,0x20,0x41,0x8b,
    0x9f,0x34,0x01,0x00,0x00};
constexpr std::uint8_t kSigStaminaClamp[] = {0x48,0x8d,0x05,0xe1,0x83,0xc8,0x03,0x80,0x38,0x00,0x75,0x1f,0xb8,0xce,
    0xff,0xff,0xff,0x83,0xfb,0xce,0x7c,0x0e,0x41,0x8b,0x87,0x38,0x01,0x00,0x00,0x39,0xd8,0x0f,0x4e,0xd8,0x89,
    0xd8,0x41,0x89,0x87,0x34,0x01,0x00,0x00};
// Stealth: the static ChrDbg flag bytes the debug menu's "Player Hide" and "Player Silence" toggle
// (the menu builder passes their addresses with those labels, kSigStealthMenu / kSigSilentMenu),
// read live by the game (kSigStealthUse / kSigSilentUse); Kyo's XML patches write the same bytes.
// 0x593e888 next to them is "Player No Dead" and is never touched.
constexpr std::uint64_t kPlayerHide = 0x593e88e, kPlayerSilence = 0x593e88f;
constexpr std::uint8_t kSigStealthMenu[] = {0x48,0x8d,0x35,0xfc,0x48,0x06,0x03,0x48,0x8d,0x15,0xc9,0xd3,0xc1};
constexpr std::uint8_t kSigSilentMenu[] = {0x48,0x8d,0x35,0xda,0x48,0x06,0x03,0x48,0x8d,0x15,0x88,0xd3,0xc1};
constexpr std::uint8_t kSigStealthUse[] = {0x48,0x8d,0x05,0xac,0x93,0xae,0x03,0x8a,0x00,0x88,0x43,0x64};
constexpr std::uint8_t kSigSilentUse[] = {0x48,0x8d,0x05,0xc5,0x95,0xae,0x03,0x80,0x38,0x00};
// Echoes and insight: PlayerGameData +0x94 (capped at 999,999,999 by the game's own add, kSigEchoes)
// and +0x84 (capped at 99, kSigInsight); engine_state's player_stat "echoes" / "insight".
constexpr std::int64_t kEchoCap = 999999999, kInsightCap = 99;
constexpr std::uint8_t kSigInsight[] = {0x44,0x03,0xb8,0x84,0x00,0x00,0x00,0x78,0x0d,0x41,0x83,0xff,0x63,0xb9,0x63,
    0x00,0x00,0x00,0x41,0x0f,0x4e,0xcf,0x89,0x88,0x84,0x00,0x00,0x00};
constexpr std::uint8_t kSigEchoes[] = {0x48,0x8b,0x93,0xc0,0x03,0x00,0x00,0x8b,0xb2,0x94,0x00,0x00,0x00,0x31,0xc9,
    0x89,0xf7,0x01,0xc7,0x78,0x0e,0x81,0xff,0xff,0xc9,0x9a,0x3b,0xb9,0xff,0xc9,0x9a,0x3b,0x0f,0x4e,0xcf,0x89,
    0x8a,0x94,0x00,0x00,0x00};
// Keeping echoes on death: the blood mark commit (droogie/bbhost death_events.hpp
// BLOOD_MARK_COMMIT_FN 0x17d2910, which the death events call with capture=1) adds the carried
// echoes (+0x94) and a pending amount into r12d, stores r12d as the mark's echoes (+0x38) and
// zeroes the carried echoes. The patch keeps the carried echoes and leaves only the pending part
// in the mark (r12d -= carried), so picking the bloodstain up cannot give them a second time.
constexpr std::uint64_t kBloodMarkSite = 0x17d2bd2;
constexpr std::uint8_t kSigBloodMark[] = {0x45,0x8b,0xa5,0x94,0x00,0x00,0x00,0x48,0x8d,0x05,0x78,0x85,0x16,0x04,
    0x48,0x8b,0x00,0x48,0x85,0xc0,0x75,0x2b,0x48,0x8d,0x3d,0xca,0x87,0x56,0x03,0x48,0x8d,0x15,0x17,0x88,0x56,
    0x03,0x48,0x8d,0x0d,0xfe,0x85,0x56,0x03,0xbe,0xb1,0x00,0x00,0x00,0x31,0xc0,0xe8,0xf7,0x29,0xce,0x00,0x48,
    0x8d,0x05,0x48,0x85,0x16,0x04,0x48,0x8b,0x00,0x48,0x8b,0x40,0x08,0x44,0x03,0x60,0x28,0xc7,0x40,0x28,0x00,
    0x00,0x00,0x00,0x44,0x89,0x63,0x38,0x41,0xc7,0x85,0x94,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
// mov [rbx+0x38], r12d; mov dword [r13+0x94], 0  ->  sub r12d, [r13+0x94]; mov [rbx+0x38], r12d; nop4
constexpr std::uint8_t kBloodMarkOriginal[] = {0x44,0x89,0x63,0x38,0x41,0xc7,0x85,0x94,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00};
constexpr std::uint8_t kBloodMarkKeep[] = {0x45,0x2b,0xa5,0x94,0x00,0x00,0x00,0x44,0x89,0x63,0x38,0x0f,0x1f,0x40,
    0x00};
static_assert(sizeof(kBloodMarkOriginal) == sizeof(kBloodMarkKeep));
// Durability: sub_18d24e0 (EquipGameData*, slot 0-7, change) adds the change to the durability of
// the weapon in an equipment slot, clamped to 0..EquipParamWeapon durabilityMax (+0xbe); the
// game's PlayerIns calls it with rdi = PlayerGameData + 0x1d0 (kSigDurabilityCall). A slot's item
// handle is EquipGameData +0x94 + 4 * slot, its id from sub_1e89070 (&{handle, 0, 0, -1}) at +0x10.
constexpr std::uint64_t kChangeDurability = 0x18d24e0, kGaitemLookup = 0x1e89070, kEquipHandles = 0x94;
constexpr int kDurabilitySlots = 8;
constexpr std::size_t kDurabilityMax = 0xbe;
constexpr std::uint8_t kSigDurability[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,
    0x83,0xec,0x68,0x41,0x89,0xf5,0x48,0x89,0xfb,0x4c,0x8b,0x25,0xe2,0x16,0xf1,0x03,0x49,0x8b,0x04,0x24,0x48,
    0x89,0x45,0xd0,0x41,0x83,0xfd,0x07,0x0f,0x87,0xaf,0x00,0x00,0x00,0x89,0x55,0x8c,0x4d,0x63,0xf5,0x42,0x8b,
    0x84,0xb3,0x94,0x00,0x00,0x00,0x89,0x45,0x90,0x48,0xc7,0x45,0x98,0x00,0x00,0x00,0x00,0xc7,0x45,0xa0,0xff,
    0xff,0xff,0xff,0x48,0x8d,0x7d,0x90,0xe8,0x37,0x6b,0x5b,0x00,0x8b,0x45,0xa0,0x25,0xff,0xff,0xff,0x0f,0x3d,
    0xff,0xff,0xff,0x0f,0x41,0xbf,0xff,0xff,0xff,0xff,0x44,0x0f,0x45,0xf8,0x41,0x83,0xff,0xff};
constexpr std::uint8_t kSigDurabilityClamp[] = {0x44,0x01,0xf3,0x48,0x8b,0x4d,0x98,0x31,0xd2,0xb8,0x00,0x00,0x00,
    0x00,0x48,0x85,0xc9,0x74,0x07,0x0f,0xb7,0x81,0xbe,0x00,0x00,0x00,0x85,0xdb,0x78,0x07,0x39,0xc3,0x0f,0x4f,
    0xd8,0x89,0xda,0x48,0x8b,0x7d,0x80,0x48,0x81,0xc7,0x58,0x01,0x00,0x00,0x8b,0xb5,0x7c,0xff,0xff,0xff,0xe8,
    0x58,0x71,0x00,0x00};
constexpr std::uint8_t kSigDurabilityCall[] = {0x49,0x8b,0xbf,0xc0,0x03,0x00,0x00,0x48,0x81,0xc7,0xd0,0x01,0x00,0x00,
    0x89,0xde,0x44,0x89,0xe2,0xe8,0xfd,0x69,0xbd,0xff};
// Firearm range: the guns' BehaviorParam_PC rows (variationId = the weapons' behaviorVariationId:
// 600 Blunderbuss, 610 Ludwig's Rifle, 1000 Rifle Spear, 1010 Reiterpallasch, 1400 Hunter Pistol
// and Evelyn, 1410 Repeating Pistol, 1500 Cannon, 3300 Gatling Gun, 3500 Church Cannon, 3600
// Piercing Rifle) with refType 1 name a Bullet row; each shot is a chain of Bullet rows (HitBulletID,
// the next segment). The moving segments' life (+0x10) and dist (+0x14, where gravity and
// acceleration change) are scaled. Rows any other player behaviour reaches (spells, hunter tools,
// the shared non-moving 77) are left alone. 1.09: 45 rows, none shared with enemies.
constexpr std::uint32_t kFirearmVariations[] = {600, 610, 1000, 1010, 1400, 1410, 1500, 3300, 3500, 3600};
constexpr std::size_t kBehaviorRowSize = 0x20, kBehaviorVariation = 0x0, kBehaviorRefType = 0x9, kBehaviorRefId = 0xc;
constexpr std::size_t kBulletRowSize = 0xa0, kBulletLife = 0x10, kBulletDist = 0x14, kBulletVelocity = 0x28,
    kBulletNext = 0x68;
// Echo multiplier: NpcParam getSoul (+0x28, an enemy's echoes; 11025 at most in 1.09) and
// GameAreaParam bonusSoul_single / _multi (+0x0 / +0x4, a boss's; 230000 at most).
constexpr std::size_t kNpcRowSize = 0x184, kGetSoul = 0x28, kAreaRowSize = 0x20;
// Consumables: the stackable usable goods whose isConsume bit is cleared (the game then does not
// lower the count, as for the Hunter's Mark). Not the Blood Vials (1000) and Quicksilver Bullets
// (900), which have their own cheats; not keys, materials, Blood Echo and Insight items (Coldblood,
// Madman's Knowledge, Great One's Wisdom, Blood Dreg), the Bold Hunter's Mark, Vermin, one-of
// items (runes, gems, Umbilical Cords) or the unnamed rows.
constexpr std::uint32_t kConsumables[] = {
    1100, 1101, // Antidote, Sedative
    1110, 1120, // Beast Blood Pellet, Blue Elixir
    1200, 1201, 1250, 1251, // Molotov Cocktail, Delayed Molotov, Rope / Delayed Rope Molotov
    1210, 1240, 1230, // Poison Knife, Throwing Knife, Pebble
    1220, 1260, 1270, // Oil Urn, Pungent Blood Cocktail, Numbing Mist
    1300, 1320, 1330, // Fire Paper, Bolt Paper, Bone Marrow Ash
    1410, // Shining Coins
    2030, 2090, // Lead Elixir, Shaman Bone Blade
};
constexpr std::size_t kGoodsType = 0x3a, kRefCategory = 0x3b;

// The bytes one of these cheats changes: each original (the snapshot) and what is there now, and
// the param tables they are in (a reloaded table voids the snapshot).
struct Patch {
    std::vector<Byte> bytes;
    std::vector<ParamTable> tables;
    bool snapped = false, code = false;
};
bool Current(const Patch& p) {
    for (const ParamTable& t : p.tables)
        if (params_table_file(t.cap) != t.file) return false;
    return true;
}
void Forget(Patch& p) { p.snapped = false; p.bytes.clear(); p.tables.clear(); }
std::vector<std::uint8_t> Originals(const Patch& p) {
    std::vector<std::uint8_t> v(p.bytes.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = p.bytes[i].original;
    return v;
}
// Brings the bytes to `want` after checking every byte to change still holds what this cheat
// left there; false (nothing written) if one does not. Code (one contiguous range) goes through
// WriteCode, data and params are written directly.
bool Write(Patch& p, const std::vector<std::uint8_t>& want) {
    // Consecutive bytes are read in one go (the echo multiplier changes about 20000).
    bool change = false;
    std::vector<std::uint8_t> run;
    for (std::size_t i = 0, j = 0; i < p.bytes.size(); i = j) {
        bool differs = want[i] != p.bytes[i].now;
        for (j = i + 1; j < p.bytes.size() && p.bytes[j].at == p.bytes[j - 1].at + 1; ++j)
            differs = differs || want[j] != p.bytes[j].now;
        if (!differs) continue;
        run.resize(j - i);
        if (!host_read_safe(reinterpret_cast<const void*>(p.bytes[i].at), run.data(), run.size())) return false;
        for (std::size_t k = i; k < j; ++k)
            if (want[k] != p.bytes[k].now && run[k - i] != p.bytes[k].now) return false;
        change = true;
    }
    if (!change) return true;
    if (p.code) {
        if (!WriteCode(reinterpret_cast<void*>(p.bytes[0].at), want.data(), want.size())) return false;
        for (std::size_t i = 0; i < p.bytes.size(); ++i) p.bytes[i].now = want[i];
        return true;
    }
    for (std::size_t i = 0; i < p.bytes.size(); ++i) {
        if (want[i] == p.bytes[i].now) continue;
        std::memcpy(reinterpret_cast<void*>(p.bytes[i].at), &want[i], 1);
        p.bytes[i].now = want[i];
    }
    return true;
}
// Writes back the originals where they are still what this cheat wrote.
void RestoreSafe(Patch& p) {
    if (!p.snapped) return;
    if (!Current(p)) { Forget(p); return; }
    if (!Write(p, Originals(p)) && !p.code) {
        // Something else changed a byte: put back only the ones still ours, byte by byte. Code is
        // left as it is (half an instruction sequence would be worse than either).
        for (Byte& b : p.bytes) {
            std::uint8_t current = 0;
            if (b.now == b.original || !Read(b.at, &current) || current != b.now) continue;
            std::memcpy(reinterpret_cast<void*>(b.at), &b.original, 1);
            b.now = b.original;
        }
    }
}

enum Kind { Refill, Flags, Params, Code };
struct Extra {
    Which which;
    Kind kind;
    const char* name;                  // the log's and the reasons': "stamina", "firearm range"
    std::atomic<const char*> unavailable{kNotVerified};
    bool disabled = false;
    int active = 0, pending = 0;
    Clock::time_point pending_since{};
    Patch patch;
    int failures = 0;
};
Extra g_extras[] = {
    {Stamina, Refill, "stamina"}, {Stealth, Flags, "stealth"}, {GunRange, Params, "firearm range"},
    {KeepEchoes, Code, "keep echoes"}, {EchoMultiplier, Params, "echo multiplier"},
    {NoDurability, Refill, "durability"}, {Consumables, Params, "consumables"},
};
Extra& ExtraOf(Which which) {
    for (Extra& e : g_extras)
        if (e.which == which) return e;
    return g_extras[0];
}
std::atomic<const char*> g_echoes_unavailable{kNotVerified};
std::atomic<long long> g_add_echoes{0}, g_add_insight{0};

void Disable(Extra& e, const char* why) {
    if (e.disabled) return;
    e.disabled = true;
    RestoreSafe(e.patch);
    e.unavailable.store(why);
    std::printf("Cheats: %s unavailable: %s\n", e.name, why);
}

// Online (BB_ONLINE=1, scripts/online.py): other players share the world, so no cheat runs. Off
// for the whole session.
bool SharedWorld() {
    static const bool shared = [] {
        const char* online = std::getenv("BB_ONLINE");
        const bool on = online && online[0] == '1';
        if (on) std::printf("Cheats: off while playing online\n");
        return on;
    }();
    return shared;
}

int SettingOf(const Extra& e, const BbSettings::Values& s) {
    switch (e.which) {
    case Stamina: return s.cheat_stamina.load(std::memory_order_relaxed);
    case Stealth: return s.cheat_stealth.load(std::memory_order_relaxed);
    case KeepEchoes: return s.cheat_keep_echoes.load(std::memory_order_relaxed);
    case NoDurability: return s.cheat_no_durability.load(std::memory_order_relaxed);
    case Consumables: return s.cheat_consumables.load(std::memory_order_relaxed);
    case GunRange: {
        const int v = s.cheat_gun_range.load(std::memory_order_relaxed);
        return std::find(std::begin(BbSettings::CheatGunRanges), std::end(BbSettings::CheatGunRanges), v) !=
                   std::end(BbSettings::CheatGunRanges) ? v : 0;
    }
    case EchoMultiplier: {
        // Off is 0 here (x1 changes nothing).
        const int v = s.cheat_echo_multiplier.load(std::memory_order_relaxed);
        return v > 1 && std::find(std::begin(BbSettings::CheatEchoMultipliers),
                                  std::end(BbSettings::CheatEchoMultipliers), v) !=
                            std::end(BbSettings::CheatEchoMultipliers) ? v : 0;
    }
    default: return 0;
    }
}

// Param snapshots (taken in the world, the first time the cheat runs and after a table reload).
const char* SnapshotGunRange(Patch& p) {
    ParamTable behavior, bullets;
    std::vector<ParamRecord> brecords, records;
    std::vector<std::uint8_t> bdata, data;
    std::uint64_t blo = 0, lo = 0;
    if (!LoadTable("BehaviorParam_PC", kBehaviorRowSize, &behavior, &brecords, &bdata, &blo))
        return "BehaviorParam_PC not found or its rows are not 0x20 bytes";
    if (!LoadTable("Bullet", kBulletRowSize, &bullets, &records, &data, &lo))
        return "Bullet not found or its rows are not 0xa0 bytes";
    const auto row = [&](std::int32_t id) -> const ParamRecord* {
        const auto it = std::find_if(records.begin(), records.end(), [&](const ParamRecord& r) { return std::int32_t(r.id) == id; });
        return it == records.end() ? nullptr : &*it;
    };
    // Every Bullet row a behaviour reaches through its chain (at most 8 segments).
    const auto reach = [&](bool firearm, std::vector<std::uint64_t>* out, std::size_t* variations) {
        std::vector<std::uint32_t> seen;
        for (const ParamRecord& b : brecords) {
            const std::uint8_t* r = bdata.data() + (b.data - blo);
            std::uint32_t variation = 0;
            std::int32_t id = 0;
            std::memcpy(&variation, r + kBehaviorVariation, 4);
            std::memcpy(&id, r + kBehaviorRefId, 4);
            const bool gun = std::find(std::begin(kFirearmVariations), std::end(kFirearmVariations), variation) !=
                             std::end(kFirearmVariations);
            if (r[kBehaviorRefType] != 1 || gun != firearm) continue;
            if (gun && variations && std::find(seen.begin(), seen.end(), variation) == seen.end()) {
                seen.push_back(variation);
                ++*variations;
            }
            for (int depth = 0; depth < 8 && id > 0; ++depth) {
                const ParamRecord* bullet = row(id);
                if (!bullet || std::find(out->begin(), out->end(), bullet->data) != out->end()) break;
                out->push_back(bullet->data);
                std::memcpy(&id, data.data() + (bullet->data - lo) + kBulletNext, 4);
            }
        }
    };
    std::vector<std::uint64_t> guns, others;
    std::size_t variations = 0;
    reach(true, &guns, &variations);
    reach(false, &others, nullptr);
    if (variations != std::size(kFirearmVariations)) return "a firearm's behaviour rows are missing";
    p.bytes.clear();
    std::size_t rows = 0;
    for (std::uint64_t at : guns) {
        if (std::find(others.begin(), others.end(), at) != others.end()) continue;
        const std::uint8_t* r = data.data() + (at - lo);
        float life = 0, dist = 0, velocity = 0;
        std::memcpy(&life, r + kBulletLife, 4);
        std::memcpy(&dist, r + kBulletDist, 4);
        std::memcpy(&velocity, r + kBulletVelocity, 4);
        if (!std::isfinite(velocity) || velocity <= 0) continue; // spawners and blasts stay as they are
        if (velocity > 200 || !std::isfinite(life) || life <= 0 || life > 5 || !std::isfinite(dist) || dist < 0 ||
            dist > 50)
            return "a firearm bullet's life, range or speed is implausible";
        for (std::size_t off : {kBulletLife, kBulletDist})
            for (std::size_t k = 0; k < 4; ++k) p.bytes.push_back({at + off + k, r[off + k], r[off + k]});
        ++rows;
    }
    if (rows < 20 || rows > 120) return "unexpected number of firearm bullet rows";
    p.tables = {behavior, bullets};
    std::printf("Cheats: firearm range: %zu bullet rows of %zu firearm behaviours\n", rows, variations);
    return nullptr;
}

const char* SnapshotEchoes(Patch& p) {
    p.bytes.clear();
    p.tables.clear();
    std::size_t rows = 0;
    struct Field { const char* table; std::size_t size; std::size_t offsets[2]; std::size_t count; std::uint32_t most; };
    const Field fields[] = {{"NpcParam", kNpcRowSize, {kGetSoul, 0}, 1, 1000000},
                            {"GameAreaParam", kAreaRowSize, {0x0, 0x4}, 2, 10000000}};
    for (const Field& f : fields) {
        ParamTable t;
        std::vector<ParamRecord> records;
        std::vector<std::uint8_t> data;
        std::uint64_t lo = 0;
        if (!LoadTable(f.table, f.size, &t, &records, &data, &lo))
            return f.size == kNpcRowSize ? "NpcParam not found or its rows are not 0x184 bytes"
                                         : "GameAreaParam not found or its rows are not 0x20 bytes";
        for (const ParamRecord& r : records) {
            for (std::size_t k = 0; k < f.count; ++k) {
                const std::uint8_t* v = data.data() + (r.data - lo) + f.offsets[k];
                std::uint32_t echoes = 0;
                std::memcpy(&echoes, v, 4);
                if (!echoes) continue;
                // x10 must stay far from the int32 the game adds it as.
                if (echoes > f.most) return "an enemy's Blood Echoes are implausible";
                for (std::size_t b = 0; b < 4; ++b) p.bytes.push_back({r.data + f.offsets[k] + b, v[b], v[b]});
                ++rows;
            }
        }
        p.tables.push_back(t);
    }
    if (rows < 100) return "too few enemies give Blood Echoes";
    std::printf("Cheats: echo multiplier: %zu enemy and boss echo values\n", rows);
    return nullptr;
}

const char* SnapshotConsumables(Patch& p) {
    ParamTable goods;
    std::vector<ParamRecord> records;
    std::vector<std::uint8_t> data;
    std::uint64_t lo = 0;
    if (!LoadTable("EquipParamGoods", kGoodsRowSize, &goods, &records, &data, &lo))
        return "EquipParamGoods not found or its rows are not 0x6c bytes";
    p.bytes.clear();
    for (std::uint32_t id : kConsumables) {
        const auto it = std::find_if(records.begin(), records.end(), [&](const ParamRecord& r) { return r.id == id; });
        if (it == records.end()) return "a consumable's goods row is missing";
        const std::uint8_t* row = data.data() + (it->data - lo);
        std::int16_t max_num = 0;
        std::memcpy(&max_num, row + kMaxNum, 2);
        // An ordinary (type 0) stackable item that is used up and uses an effect or an attack.
        if (row[kGoodsType] != 0 || max_num < 2 || max_num > 99 || !(row[kConsumeByte] & kIsConsume) ||
            (row[kRefCategory] != 1 && row[kRefCategory] != 2))
            return "a consumable's goods row is not a stackable consumable";
        p.bytes.push_back({it->data + kConsumeByte, row[kConsumeByte], row[kConsumeByte]});
    }
    p.tables = {goods};
    return nullptr;
}

// The bytes a mode wants; mode 0: the originals.
std::vector<std::uint8_t> WantedOf(const Extra& e, int mode) {
    std::vector<std::uint8_t> want = Originals(e.patch);
    if (!mode) return want;
    switch (e.which) {
    case Stealth: std::fill(want.begin(), want.end(), std::uint8_t(1)); break;
    case KeepEchoes: std::memcpy(want.data(), kBloodMarkKeep, sizeof(kBloodMarkKeep)); break;
    case Consumables: for (std::uint8_t& b : want) b = std::uint8_t(b & ~kIsConsume); break;
    case GunRange:
        for (std::size_t i = 0; i + 4 <= want.size(); i += 4) {
            float v = 0;
            std::memcpy(&v, want.data() + i, 4);
            v = v * float(mode) / 100.0f;
            std::memcpy(want.data() + i, &v, 4);
        }
        break;
    case EchoMultiplier:
        for (std::size_t i = 0; i + 4 <= want.size(); i += 4) {
            std::uint32_t v = 0;
            std::memcpy(&v, want.data() + i, 4);
            v *= std::uint32_t(mode);
            std::memcpy(want.data() + i, &v, 4);
        }
        break;
    default: break;
    }
    return want;
}

void LogSwitch(const Extra& e, int mode) {
    switch (e.which) {
    case Stamina: std::printf("Cheats: infinite stamina %s\n", mode ? "on" : "off"); break;
    case Stealth: std::printf(mode ? "Cheats: stealth on (Player Hide, Player Silence)\n" : "Cheats: stealth off, flags restored\n"); break;
    case GunRange:
        if (mode) std::printf("Cheats: firearm range %d%%\n", mode);
        else std::printf("Cheats: firearm range off, params restored\n");
        break;
    case KeepEchoes: std::printf(mode ? "Cheats: keep Blood Echoes on death on\n" : "Cheats: keep Blood Echoes on death off, code restored\n"); break;
    case EchoMultiplier:
        if (mode) std::printf("Cheats: echo multiplier x%d\n", mode);
        else std::printf("Cheats: echo multiplier off, params restored\n");
        break;
    case NoDurability: std::printf("Cheats: weapon durability kept full %s\n", mode ? "on" : "off"); break;
    case Consumables: std::printf(mode ? "Cheats: infinite consumables on\n" : "Cheats: infinite consumables off, params restored\n"); break;
    default: break;
    }
}

// A changed setting, after the debounce. Flags and code change at once (they need no character);
// params change in the world (Run), and are restored here.
void Switch(Extra& e, int mode) {
    e.active = mode;
    e.failures = 0;
    g_check = Clock::time_point{};
    LogSwitch(e, mode);
    if (e.kind == Flags && mode && !e.patch.snapped) {
        e.patch.bytes.clear();
        for (std::uint64_t bn : {kPlayerHide, kPlayerSilence}) {
            std::uint8_t v = 0;
            if (!Read(Slot(bn), &v) || v > 1) { Disable(e, "a debug flag holds an unexpected value"); return; }
            e.patch.bytes.push_back({Slot(bn), v, v});
        }
        e.patch.snapped = true;
    }
    if (e.kind == Flags || e.kind == Code || !mode) {
        if (!e.patch.snapped) return;
        if (!Current(e.patch)) { Forget(e.patch); return; }
        if (!Write(e.patch, WantedOf(e, mode)))
            Disable(e, e.kind == Code ? "the game code changed outside the cheat" : "a byte changed outside the cheat");
        // Flags are taken again next time: the debug menu or a patch may have set them meanwhile.
        if (e.kind == Flags && !mode) Forget(e.patch);
    }
}

// The hunter's data module, or 0: checked each time (the chain changes on every load).
std::uint64_t DataModule() {
    std::uint64_t man = 0, chr = 0, modules = 0, data = 0;
    if (!Read(Slot(kWorldChrMan), &man) || !Plausible(man) || !Read(man + kPlayerIns, &chr) || !Plausible(chr) ||
        !Read(chr + kModules, &modules) || !Plausible(modules) || !Read(modules + kDataModule, &data) || !Plausible(data))
        return 0;
    return data;
}

// Every frame while settled: stamina back to its maximum (the game spends it within a frame).
void RefillStamina() {
    const std::uint64_t data = DataModule();
    std::int32_t value[2] = {};
    if (!data || !Read(data + kStamina, &value) || value[1] < 1 || value[1] > 2000 || value[0] < -50 ||
        value[0] > value[1] || value[0] == value[1])
        return;
    std::memcpy(reinterpret_cast<void*>(data + kStamina), &value[1], 4);
}

// Each weapon's durabilityMax by id, read once per table file (the slots are checked four times
// a second).
ParamTable g_weapons;
std::vector<std::pair<std::uint32_t, std::uint16_t>> g_durability;
bool LoadDurability() {
    if (!g_durability.empty() && params_table_file(g_weapons.cap) == g_weapons.file) return true;
    g_durability.clear();
    std::vector<ParamRecord> records;
    std::vector<std::uint8_t> data;
    std::uint64_t lo = 0;
    if (!LoadTable("EquipParamWeapon", kWeaponRowSize, &g_weapons, &records, &data, &lo)) return false;
    for (const ParamRecord& r : records) {
        std::uint16_t most = 0;
        std::memcpy(&most, data.data() + (r.data - lo) + kDurabilityMax, 2);
        g_durability.push_back({r.id, most});
    }
    std::sort(g_durability.begin(), g_durability.end());
    return !g_durability.empty();
}

// The equipped weapons' durability raised to their maximum with the game's own change (a large
// increase, clamped by it); slots whose weapon has no durability (fists) are left alone.
void RefillDurability(Extra& e, std::uint64_t equip) {
    if (!LoadDurability()) { Disable(e, "EquipParamWeapon not found or its rows are not 0x13c bytes"); return; }
    int touched = 0;
    for (int slot = 0; slot < kDurabilitySlots; ++slot) {
        std::uint32_t handle = 0;
        if (!Read(equip + kEquipHandles + 4 * std::uint64_t(slot), &handle)) { Disable(e, "equipment not readable"); return; }
        if (!handle || handle == 0xffffffffu) continue;
        struct { std::uint32_t handle, pad; std::uint64_t item; std::uint32_t id, pad2; } gaitem{handle, 0, 0, 0xffffffffu, 0};
        Call(Slot(kGaitemLookup), &gaitem);
        // Weapons are item type 0 (the top four bits); armour (1) and the rest are left alone.
        const std::uint32_t id = gaitem.id;
        if (id == 0xffffffffu || (id & 0xfffffff) == 0xfffffff || id >> 28) continue;
        const auto it = std::lower_bound(g_durability.begin(), g_durability.end(), std::make_pair(id, std::uint16_t(0)));
        if (it == g_durability.end() || it->first != id || !it->second || it->second > 2000) continue;
        Call(Slot(kChangeDurability), equip, slot, int(it->second));
        ++touched;
    }
    if (!e.failures) {
        e.failures = 1; // logged once per switch
        std::printf("Cheats: durability: %d equipped weapons kept at their maximum\n", touched);
    }
}

// In the world, settled.
void Run(Extra& e, std::uint64_t record) {
    if (e.kind == Refill) {
        if (e.which == NoDurability) RefillDurability(e, record + kEquipGameData);
        return;
    }
    if (e.kind != Params) return;
    if (e.patch.snapped && !Current(e.patch)) {
        std::printf("Cheats: %s: the game reloaded its params; applying again\n", e.name);
        Forget(e.patch);
    }
    if (!e.patch.snapped) {
        const char* why = e.which == GunRange ? SnapshotGunRange(e.patch)
                        : e.which == EchoMultiplier ? SnapshotEchoes(e.patch)
                                                    : SnapshotConsumables(e.patch);
        if (why) { Forget(e.patch); Disable(e, why); return; }
        e.patch.snapped = true;
    }
    if (!Write(e.patch, WantedOf(e, e.active))) Disable(e, "a param byte changed outside the cheat");
}

// The overlay's queued echoes / insight, in the world.
void AddStats() {
    const long long echoes = g_add_echoes.exchange(0), insight = g_add_insight.exchange(0);
    std::int64_t v = 0;
    if (echoes > 0) {
        if (!player_stat_get("echoes", &v) || v < 0 || v > kEchoCap) {
            std::printf("Cheats: Blood Echoes not readable; nothing added\n");
        } else {
            const std::int64_t now = std::min<std::int64_t>(kEchoCap, v + echoes);
            player_stat_set("echoes", now);
            std::printf("Cheats: +%lld Blood Echoes (%lld held)\n", static_cast<long long>(now - v), static_cast<long long>(now));
        }
    }
    if (insight > 0) {
        if (!player_stat_get("insight", &v) || v < 0 || v > kInsightCap) {
            std::printf("Cheats: Insight not readable; nothing added\n");
        } else {
            const std::int64_t now = std::min<std::int64_t>(kInsightCap, v + insight);
            player_stat_set("insight", now);
            std::printf("Cheats: +%lld Insight (%lld held)\n", static_cast<long long>(now - v), static_cast<long long>(now));
        }
    }
}

// Install: each cheat's own code checks; one that differs disables only that cheat.
void InstallExtras() {
    const auto check = [](Which which, std::initializer_list<Signature> signatures) {
        for (const Signature& s : signatures) {
            if (!Matches(s.bn, s.bytes)) {
                Extra& e = ExtraOf(which);
                e.unavailable.store("engine code differs from the verified 1.09 image");
                std::printf("Cheats: %s unavailable: engine code differs at 0x%llx\n", e.name,
                            static_cast<unsigned long long>(s.bn));
                return false;
            }
        }
        ExtraOf(which).unavailable.store(nullptr);
        return true;
    };
    check(Stamina, {{0x1cb6451, kSigStaminaLoad}, {0x1cb64a2, kSigStaminaClamp}});
    check(Stealth, {{0x1d214b7, kSigStealthMenu}, {0x1d214f9, kSigSilentMenu}, {0x1e554db, kSigStealthUse},
                    {0x1e552c3, kSigSilentUse}});
    if (check(KeepEchoes, {{0x17d2b82, kSigBloodMark}})) {
        Patch& p = ExtraOf(KeepEchoes).patch;
        p.code = true;
        for (std::size_t i = 0; i < sizeof(kBloodMarkOriginal); ++i)
            p.bytes.push_back({Slot(kBloodMarkSite) + i, kBloodMarkOriginal[i], kBloodMarkOriginal[i]});
        p.snapped = true;
    }
    check(NoDurability, {{kChangeDurability, kSigDurability}, {0x18d265d, kSigDurabilityClamp},
                         {0x1cfbacb, kSigDurabilityCall}});
    // Params only, read and checked in the world; the consumables need the isConsume test.
    check(GunRange, {});
    check(EchoMultiplier, {});
    check(Consumables, {{0x18d0dc0, kSigConsume}});
    const bool stats = Matches(0x1cfc567, kSigInsight) && Matches(0x1cfc6a4, kSigEchoes);
    g_echoes_unavailable.store(stats ? nullptr : "engine code differs from the verified 1.09 image");
    if (!stats) std::printf("Cheats: add echoes / insight unavailable: engine code differs\n");
}
} // namespace

bool Install(std::uint8_t* image, std::size_t size) {
    g_image = image;
    g_image_size = size;
    g_slide = reinterpret_cast<std::uint64_t>(image);
    bool vials = true;
    for (const Signature& s : kSignatures) {
        if (!Matches(s.bn, s.bytes)) {
            for (Cheat& c : g_cheats) {
                c.unavailable.store("engine code differs from the verified 1.09 image");
                std::printf("Cheats: %s unavailable: engine code differs at 0x%llx\n", c.name,
                            static_cast<unsigned long long>(s.bn));
                c.disabled = true;
            }
            vials = false;
            break;
        }
    }
    if (vials)
        for (Cheat& c : g_cheats) c.unavailable.store(nullptr);
    InstallExtras();
    g_installed = true;
    bool any = vials || !g_echoes_unavailable.load();
    for (Extra& e : g_extras) {
        if (e.unavailable.load()) e.disabled = true;
        any = any || !e.disabled;
    }
    g_image = nullptr; // the checks are done; nothing reads the image through it again
    return any;
}

const char* Unavailable(Which which) {
    if (which == Vials || which == Bullets) return g_cheats[which == Bullets ? 1 : 0].unavailable.load();
    if (which == Echoes) return g_echoes_unavailable.load();
    if (which >= WhichCount) return kNotVerified;
    return ExtraOf(which).unavailable.load();
}

void AddEchoes(int amount) {
    if (amount > 0 && g_installed && !g_echoes_unavailable.load()) g_add_echoes.fetch_add(std::min(amount, int(kEchoCap)));
}
void AddInsight(int amount) {
    if (amount > 0 && g_installed && !g_echoes_unavailable.load()) g_add_insight.fetch_add(std::min(amount, int(kInsightCap)));
}

void Tick() {
    if (!g_installed) return;
    const auto& s = BbSettings::Get();
    const bool shared = SharedWorld();
    if (shared) {
        g_add_echoes.store(0, std::memory_order_relaxed);
        g_add_insight.store(0, std::memory_order_relaxed);
    }
    const int settings[2] = {shared ? BbSettings::CheatOff : s.cheat_vials.load(std::memory_order_relaxed),
                             shared ? BbSettings::CheatOff : s.cheat_bullets.load(std::memory_order_relaxed)};
    bool idle = g_add_echoes.load(std::memory_order_relaxed) <= 0 && g_add_insight.load(std::memory_order_relaxed) <= 0;
    for (int i = 0; i < 2; ++i) {
        const Cheat& c = g_cheats[i];
        idle = idle && (c.disabled || (settings[i] == BbSettings::CheatOff && c.pending == BbSettings::CheatOff &&
                                       c.active == BbSettings::CheatOff));
    }
    for (const Extra& e : g_extras) idle = idle && (e.disabled || ((shared || !SettingOf(e, s)) && !e.pending && !e.active));
    if (idle) return; // all off: nothing read, nothing called
    const auto now = Clock::now();
    for (int i = 0; i < 2; ++i) {
        Cheat& c = g_cheats[i];
        if (c.disabled) continue;
        int value = settings[i];
        if (value != BbSettings::CheatInfinite && (value < 0 || value > BbSettings::CheatMaxAmount)) value = BbSettings::CheatOff;
        if (value != c.pending) { c.pending = value; c.pending_since = now; }
        if (c.pending != c.active && now - c.pending_since >= kDebounce) Switch(c, c.pending);
    }
    for (Extra& e : g_extras) {
        if (e.disabled) continue;
        const int value = shared ? 0 : SettingOf(e, s);
        if (value != e.pending) { e.pending = value; e.pending_since = now; }
        if (e.pending != e.active && now - e.pending_since >= kDebounce) Switch(e, e.pending);
    }
    Extra& stamina = ExtraOf(Stamina);
    if (g_settled && !stamina.disabled && stamina.active) RefillStamina();
    if (now < g_check) return;
    g_check = now + kInterval;
    // Acts only with a character loaded and in the world (not at the title, not while loading).
    const std::uint64_t record = player_record();
    std::uint32_t block = 0;
    const bool present = Plausible(record) && world_player_block(&block);
    if (!present || !g_present || record != g_record || block != g_block) {
        g_present = present;
        g_record = record;
        g_block = block;
        g_since = now;
        g_settled = false;
        for (Cheat& c : g_cheats) c.floor = -1;
        if (!present) {
            const bool echoes = g_add_echoes.exchange(0) > 0, insight = g_add_insight.exchange(0) > 0;
            if (echoes || insight) std::printf("Cheats: no character in the world; Blood Echoes / Insight not added\n");
        }
        return;
    }
    if (now - g_since < kSettle) return;
    g_settled = true;
    for (Cheat& c : g_cheats) {
        if (!c.disabled && c.active != BbSettings::CheatOff) Run(c, record + kEquipGameData);
    }
    for (Extra& e : g_extras) {
        if (!e.disabled && e.active) Run(e, record);
    }
    if (!g_echoes_unavailable.load()) AddStats();
}
} // namespace BbEngine::Cheats
