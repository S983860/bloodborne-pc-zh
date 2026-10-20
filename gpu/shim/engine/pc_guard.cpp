// SPDX-License-Identifier: GPL-3.0-or-later
// Keep the game's directional hit test, reduction, stamina, break and material
// sound calculation. Select the actual right-hand weapon for the private PC
// guard, rather than letting one-handed styles consult an empty left hand.
#include "pc_guard.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "../bbport_settings.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <SDL3/SDL.h>
extern "C" void runtime_audio_guard_event(int perfect, int volume);
namespace BbEngine::PcGuard {
namespace {
std::uint64_t base = 0;
std::atomic<unsigned> traces{0};
std::atomic<std::uint64_t> pressed_at{0};
std::atomic<bool> input_held{false};
std::mutex pending_lock;
struct Pending { std::uint64_t actor=0,at=0; int stamina=0,cost=0; bool perfect=false; } pending;
std::atomic<unsigned> hit_traces{0};
template<class T> T Read(std::uint64_t at) {
    T value{};
    if (at > 0x10000) host_read_safe(reinterpret_cast<void*>(at), &value, sizeof value);
    return value;
}
struct GuardContext { std::uint64_t actor=0,data=0; int hp=0,stamina=0,maximum=0; bool guarding=false; };
bool Context(std::uint64_t actor, GuardContext& c) {
    if (!base || !BbSettings::Get().pc_controls || !input_held.load(std::memory_order_acquire)) return false;
    const auto world=Read<std::uint64_t>(base+0x553e878);
    if (!actor || actor!=Read<std::uint64_t>(world+0x60)) return false;
    const auto modules=Read<std::uint64_t>(actor+0x3b0);
    const auto data=Read<std::uint64_t>(modules+0x20);
    const auto equipment=Read<std::uint64_t>(actor+0x4b8);
    const int weapon=static_cast<int>(Call(base+0x18fb090,actor,-1));
    const int hp=Read<int>(data+0xf8);
    // Use the host's uninterrupted dedicated guard input. Native held times
    // may clear transiently during locomotion and must not re-arm a perfect block.
    if (!UsesRightHand(true,hp,weapon,Read<int>(equipment+8),1,1)) return false;
    const auto flags=Read<std::uint64_t>(modules+8);
    c={actor,data,hp,Read<int>(data+0x134),Read<int>(data+0x138),
       (Read<unsigned char>(flags+0x1bc)&4)!=0};
    return c.maximum>0;
}
bool Perfect(const GuardContext& c,std::uint64_t now) {
    return c.stamina>=StaminaCost(c.maximum) && PerfectWindow(now,pressed_at.load(std::memory_order_acquire),
        BbSettings::Get().perfect_guard_window_ms.load(),input_held.load(std::memory_order_acquire));
}
void FloatResult(const std::uint64_t* saved,float value) {
    // The checked prologue stub restores xmm0 from this slot before returning.
    std::memcpy(reinterpret_cast<char*>(const_cast<std::uint64_t*>(saved))-136,&value,sizeof value);
}
std::int64_t __attribute__((sysv_abi)) DamageCut(std::uint64_t,const std::uint64_t* saved) {
    GuardContext c;
    // The seventh argument (the caller's stack) is the native guard decision.
    // The four output pointers are remaining physical/magic/fire/lightning
    // damage multipliers: 1 means no absorption, 0 means complete absorption.
    if (!(saved[7]&0xff) || !Context(saved[5],c) || !c.guarding || !Perfect(c,SDL_GetTicks())) return 0;
    for(int i=1;i<=4;++i) if(saved[i]<0x10000) return 0;
    for(int i=1;i<=4;++i) *reinterpret_cast<float*>(saved[i])=0.0f;
    return 1;
}
std::int64_t __attribute__((sysv_abi)) GuardCost(std::uint64_t,const std::uint64_t* saved) {
    GuardContext c;
    if (!Context(saved[5],c) || !c.guarding) return 0;
    const auto now=SDL_GetTicks();
    const int cost=StaminaCost(c.maximum);
    { std::lock_guard lock(pending_lock); pending={c.actor,now,c.stamina,cost,Perfect(c,now)}; }
    FloatResult(saved,static_cast<float>(cost));
    return 1;
}
std::int64_t __attribute__((sysv_abi)) Hand(std::uint64_t, const std::uint64_t* saved) {
    if (!BbSettings::Get().pc_controls) return 0;
    const auto actor = saved[5];
    const auto world = Read<std::uint64_t>(base + 0x553e878);
    if (!actor || actor != Read<std::uint64_t>(world + 0x60)) return 0;
    const auto modules = Read<std::uint64_t>(actor + 0x3b0);
    const auto requests = Read<std::uint64_t>(modules + 0x80);
    const float l2 = Read<float>(requests + 0x50 + 3 * 4);
    const float l3 = Read<float>(requests + 0x50 + 13 * 4);
    if (!(l2 > 0 && l3 > 0)) return 0;
    const auto data = Read<std::uint64_t>(modules + 0x20);
    const auto equipment = Read<std::uint64_t>(actor + 0x4b8);
    const int style = Read<int>(equipment + 8);
    // The byte-verified original weapon resolver accepts -1 for the selected
    // right hand and -2 for the selected left hand. It only reads equipment.
    const int weapon = static_cast<int>(Call(base + 0x18fb090, actor, -1));
    if (!UsesRightHand(true, Read<int>(data + 0xf8), weapon, style, l2, l3)) return 0;
    const_cast<std::uint64_t*>(saved)[-1] = static_cast<std::uint64_t>(-1);
    if (traces.fetch_add(1, std::memory_order_relaxed) < 12)
        host_log("pc-guard: native defensive hand=right weapon=%d style=%d", weapon, style);
    return 1;
}
}
void Input(bool held) {
    const auto now=SDL_GetTicks();
    // Observe the native deduction, rather than sounding a speculative damage
    // query. No HP/stamina memory is written by this feedback path.
    Pending p;
    { std::lock_guard lock(pending_lock);
      if (pending.actor) {
        const auto modules=Read<std::uint64_t>(pending.actor+0x3b0);
        const auto data=Read<std::uint64_t>(modules+0x20);
        const int sp=Read<int>(data+0x134);
        if (now>=pending.at && now-pending.at<=150 && sp<pending.stamina) {p=pending;pending={};}
        else if (now<pending.at || now-pending.at>150) pending={};
      }
    }
    if (p.actor) {
        runtime_audio_guard_event(p.perfect?1:0,BbSettings::Get().guard_sfx_volume.load());
        if (hit_traces.fetch_add(1,std::memory_order_relaxed)<64)
            host_log("pc-guard: committed %s block cost=%d max_stamina_cost=10%%",p.perfect?"perfect":"normal",p.cost);
    }
    const bool was=input_held.exchange(held,std::memory_order_acq_rel);
    if (held && !was) pressed_at.store(now?now:1,std::memory_order_release);
    if (!held) pressed_at.store(0,std::memory_order_release);
}
bool Install(std::uint8_t* image, std::size_t size) {
    // PlayerIns vtable +0x390, RVA 0x18fb170: style==3 ? -1 : -2.
    // The first 16 bytes contain no relative instructions; the displaced
    // comparison restores the flags before the original conditional branch.
    constexpr std::uint8_t expected[] = {0x48,0x8b,0x87,0xb8,0x04,0,0,0x83,0x78,0x08,0x03,0xb8,0xff,0xff,0xff,0xff};
    constexpr std::uint8_t resolver[] = {0x48,0x8b,0x87,0xb8,0x04,0,0,0x8d,0x4e,0x06,0x83,0xf9,0x05};
    if (size < 0x18fb090 + sizeof resolver || std::memcmp(image + 0x18fb090, resolver, sizeof resolver)) return false;
    constexpr std::uint8_t cut[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x68};
    constexpr std::uint8_t cost[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x10};
    Hook hooks[3]{};
    if (!Prepare(hooks[0],image,size,0x18fb170,expected,Hand) ||
        !Prepare(hooks[1],image,size,0x19029e0,cut,DamageCut) ||
        !Prepare(hooks[2],image,size,0x1903300,cost,GuardCost)) {
        for(auto& h:hooks) Discard(h);
        return false;
    }
    base = reinterpret_cast<std::uint64_t>(image);
    Commit(hooks);
    host_log("pc-guard: checked native hand, perfect absorption and 10%% stamina hooks installed; window=%dms",BbSettings::Get().perfect_guard_window_ms.load());
    return true;
}
}
