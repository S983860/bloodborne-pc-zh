// SPDX-License-Identifier: GPL-3.0-or-later
// Renderer-independent engine data adapted from bbhost's params, player_data,
// event_flags, world_chr and menu_steps. Initialized only after executable verification.
#pragma once
#include "engine_hooks.h"
#include "bbport_platform.h"
#include <vector>
namespace BbEngine {
constexpr std::uint64_t kPreferredGuestSlide=0x400000;
bool host_read_safe(const void* p,void* out,std::size_t n);
void host_log(const char*,...);
void StateInitialize(std::uint64_t image);
void* params_row(const char* table,std::uint32_t id,std::size_t* size);
// A loaded param table (the repository's ResCap and its file) and its records, for walking every
// row. The file changes if the game reloads the table: params_table_file(cap) reads it again.
struct ParamTable { std::uint64_t cap=0,file=0;std::uint16_t rows=0;std::uint8_t format=0,flags=0; };
bool params_table(const char* table,ParamTable* out);
std::uint64_t params_table_file(std::uint64_t cap);
struct ParamRecord { std::uint32_t id;std::uint64_t data; };
bool params_records(const ParamTable& table,std::vector<ParamRecord>* out);
// GameDataMan's local player record (PlayerGameData), or 0 while there is none.
std::uint64_t player_record();
bool player_stat_get(const char* name,std::int64_t* value);
bool player_stat_set(const char* name,std::int64_t value);
bool player_origin(int* origin);
bool world_player_block(std::uint32_t* block);
bool event_flag_get(std::uint32_t id,bool* value);
bool event_flag_set(std::uint32_t id,bool value);
int menu_steps_watch(std::uint64_t type,const char16_t* name);
void menu_steps_notify(const std::uint64_t* saved);
std::uint64_t menu_steps_take(int watch);
inline std::int32_t* menu_step_count(std::uint64_t step) { return reinterpret_cast<std::int32_t*>(step+8); }
inline void menu_step_hold(std::uint64_t step) { ++*menu_step_count(step); }
void menu_step_release(std::uint64_t step);
template<typename T,typename... A> T hle_call_guest(void* fn,A... a) { return static_cast<T>(Call(reinterpret_cast<std::uint64_t>(fn),a...)); }
}
