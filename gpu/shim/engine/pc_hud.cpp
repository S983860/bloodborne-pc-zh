// SPDX-License-Identifier: GPL-3.0-or-later
// Bloodborne 1.09 layouts: bbhost engine/sprj, guarded by executable identity and bytes.
#include "pc_hud.h"
#include "engine_state.h"
#include "../bbport_settings.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" std::uint64_t runtime_savedata_write_generation();
namespace BbEngine::PcHud {
namespace {
using Clock=std::chrono::steady_clock;
std::uint64_t image=0;
std::mutex mutex;
Snapshot state;
std::string status;
std::atomic<bool> in_world{false}, paused{false};
// Read directly by a tiny guest time-step thunk. No thread suspension or host call in the thunk.
std::atomic<std::uint8_t> freeze{0};
std::atomic<int> request{0};
bool freeze_ready=false,save_ready=false,title_ready=false,saving=false;
Clock::time_point next_sample{},save_start{};
std::uint64_t save_generation=0;
std::uint32_t loaded_block=0xffffffff;
unsigned sample_count=0;
struct Entry {char kind;int id;Point p;int lots[3];std::uint32_t flag=0;bool resolved=false;};
std::vector<Entry> entries;
template<class T> T rd(std::uint64_t at){T v{};if(at>0x10000)host_read_safe(reinterpret_cast<void*>(at),&v,sizeof v);return v;}
bool position(std::uint64_t chr,Point& p,float* yaw=nullptr){
 const auto modules=rd<std::uint64_t>(chr+0x3b0), tr=rd<std::uint64_t>(modules+0x68);
 if(!tr || !host_read_safe(reinterpret_cast<void*>(tr+0x1e0),&p,sizeof p))return false;
 if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>100000||std::abs(p.y)>100000||std::abs(p.z)>100000)return false;
 if(yaw){*yaw=rd<float>(tr+0x1d4);if(!std::isfinite(*yaw))*yaw=0;}return true;
}
void load_map(std::uint32_t block){
 entries.clear();loaded_block=block;
 const char* root=std::getenv("BB_MINIMAP_ASSETS");if(!root)return;
 char name[64];std::snprintf(name,sizeof name,"m%02u_%02u_%02u_%02u.tsv",block>>24,(block>>16)&255,(block>>8)&255,block&255);
 std::ifstream f(std::filesystem::u8path(root)/name);std::string line;
 while(std::getline(f,line)&&entries.size()<12000){Entry e{};if(std::sscanf(line.c_str(),"%c %d %f %f %f %d %d %d",&e.kind,&e.id,&e.p.x,&e.p.y,&e.p.z,&e.lots[0],&e.lots[1],&e.lots[2])==8 && (e.kind=='I'||e.kind=='C') && std::isfinite(e.p.x)&&std::isfinite(e.p.y)&&std::isfinite(e.p.z))entries.push_back(e);}
 host_log("pc-minimap: %s loaded, %zu actual map records",name,entries.size());
}
void resolve_flags(Entry& e){
 if(e.resolved)return;
 for(int lot:e.lots)if(lot>=0){std::size_t n=0;void* row=params_row("ItemLotParam",lot,&n);if(!row)return;if(n>=0x84){std::uint32_t flag=0;host_read_safe(static_cast<std::uint8_t*>(row)+0x80,&flag,4);if(flag && !e.flag)e.flag=flag;}}
 e.resolved=true;
}
void collect_actors(std::uint64_t man,std::uint64_t player,Point p,std::vector<Marker>& out){
 std::unordered_set<std::uint64_t> seen;
 struct Slot{std::uint64_t chr;std::uint8_t unknown[0x30];};static_assert(sizeof(Slot)==0x38);
 for(unsigned set=0;set<62;++set){auto s=rd<std::uint64_t>(man+0x850+set*8);auto n=rd<std::uint32_t>(s);auto ptr=rd<std::uint64_t>(s+8);
  if(!s||!ptr||n==0||n>4096)continue;
  std::vector<Slot> slots(n);if(!host_read_safe(reinterpret_cast<void*>(ptr),slots.data(),slots.size()*sizeof(Slot)))continue;
  for(auto slot:slots){auto chr=slot.chr;Point q;if(!chr||chr==player||!seen.insert(chr).second||!position(chr,q))continue;
   if(std::hypot(q.x-p.x,q.z-p.z)>140 || std::abs(q.y-p.y)>30)continue;
   auto data=rd<std::uint64_t>(rd<std::uint64_t>(chr+0x3b0)+0x20);if(data && rd<std::int32_t>(data+0xf8)<=0)continue;
   auto kind=BbMinimap::ClassifyTeam(rd<std::uint32_t>(chr+0x88));
   if(kind && out.size()<512)out.push_back({q,kind,std::abs(q.y-p.y)>4});
  }
 }
}
bool prepare_freeze(std::uint8_t* base,std::size_t size){
#ifdef _WIN32
 constexpr std::size_t off=0x2018e3d;
 const std::uint8_t original[]={0xf3,0x0f,0x10,0x83,0x64,0x02,0,0,0xf3,0x0f,0x5f,0x43,0x18,0xf3,0x0f,0x5d,0x05,0x12,0xd5,0x90,0x02};
 if(size<off+sizeof original || std::memcmp(base+off,original,sizeof original))return false;
 auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!code)return false;
 std::size_t n=0;auto byte=[&](std::uint8_t v){code[n++]=v;};auto address=[&](std::uint64_t v){std::memcpy(code+n,&v,8);n+=8;};
 auto jump=[&](std::uint64_t dest){byte(0xff);byte(0x25);for(int i=0;i<4;++i)byte(0);address(dest);};
 byte(0x9c);byte(0x50);byte(0x48);byte(0xb8);address(reinterpret_cast<std::uint64_t>(&freeze));
 byte(0x80);byte(0x38);byte(0);byte(0x75);auto branch=n++; // jne paused
 byte(0x58);byte(0x9d);
 std::memcpy(code+n,original,13);n+=13;
 byte(0x50);byte(0x48);byte(0xb8);address(reinterpret_cast<std::uint64_t>(base+off+21+0x290d512));
 byte(0xf3);byte(0x0f);byte(0x5d);byte(0);byte(0x58); // minss xmm0,[rax]
 jump(reinterpret_cast<std::uint64_t>(base+off+21));
 code[branch]=static_cast<std::uint8_t>(n-branch-1);
 byte(0x58);byte(0x9d);byte(0x0f);byte(0x57);byte(0xc0); // xorps xmm0,xmm0
 jump(reinterpret_cast<std::uint64_t>(base+off+21));
 DWORD old=0;if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code,n)){VirtualFree(code,0,MEM_RELEASE);return false;}
 Hook hook{base+off,code,sizeof original};Commit(std::span<Hook>(&hook,1));return true;
#else
 return false;
#endif
}
void set_status(const char* s){std::lock_guard lock(mutex);status=s;}
}
bool Install(std::uint8_t* base,std::size_t size){
 image=reinterpret_cast<std::uint64_t>(base);
 const std::uint8_t save[]={0x48,0x8b,0x05,0x51,0x7e,0xfe,0x03,0x80,0xb8,0x78,0x15,0,0,0};
 const std::uint8_t title[]={0x48,0x8d,0x05,0xd9,0xe0,0x92,0x03,0x48,0x8b,0,0x48,0x83,0x78,0x08,0};
 save_ready=size>0x156e820+sizeof save && !std::memcmp(base+0x156e820,save,sizeof save);
 title_ready=size>0x1c0d050+sizeof title && !std::memcmp(base+0x1c0d050,title,sizeof title);
 freeze_ready=prepare_freeze(base,size);
 host_log("pc-game-menu: save=%d title=%d pause-delta=%d; minimap ready",save_ready,title_ready,freeze_ready);return true;
}
void Tick(){
 if(!image)return;
 std::uint32_t block=0;bool valid=world_player_block(&block);
 auto man=rd<std::uint64_t>(image+0x553e878);auto player=rd<std::uint64_t>(man+0x60);
 const auto helper=rd<std::uint64_t>(image+0x553e8b8);
 const bool loading=rd<std::uint8_t>(image+0x556286b)!=0 && !(helper && (rd<std::uint8_t>(helper+0x50)||rd<std::uint8_t>(helper+0x51)));
 Point p{};float yaw=0;valid=valid&&!loading&&position(player,p,&yaw);in_world=valid;
 if(!valid){paused=false;freeze=0;std::lock_guard lock(mutex);state.valid=false;}
 const auto game=rd<std::uint64_t>(image+0x5556678);
 const auto now=Clock::now();const int action=request.exchange(0);
 if(action && valid && game){
  if(action==2){
   if(title_ready){Pause(false);Call(image+0x1c0d050);set_status("正在保存并返回主界面……");host_log("pc-game-menu: native save and return-to-title requested");}
   else set_status("当前版本无法安全返回主界面");
  }else if(save_ready && rd<std::uint8_t>(game+0x1578) && rd<std::int32_t>(game+0x1490)>=0 && rd<std::int32_t>(game+0x1490)<10 && !saving){
   save_generation=runtime_savedata_write_generation();Call(image+0x156e820,0);saving=true;save_start=now;set_status("正在保存，请稍候……");host_log("pc-game-menu: native character and system save queued");
  }else set_status(saving?"正在保存，请稍候……":"当前场景暂时无法手动保存");
 }
 if(saving && game){
  if(runtime_savedata_write_generation()>save_generation && !rd<std::uint8_t>(game+0x1547) && !rd<std::uint8_t>(game+0x1548) && !rd<std::int32_t>(game+0x1554)){
   saving=false;set_status("保存完成");host_log("pc-game-menu: save completed (native queue clear, writable save unmounted)");
  }else if(now-save_start>std::chrono::seconds(20)){saving=false;set_status("尚未确认写盘完成，请稍后重试");host_log("pc-game-menu: save completion not confirmed before timeout");}
 }
 freeze=(paused && valid && freeze_ready && !saving)?1:0;
 if(!valid)return;
 // The hunter/camera heading follows every frame; nearby actors are sampled separately.
 {std::lock_guard lock(mutex);state.valid=true;state.player=p;state.yaw=yaw;
  if(state.block!=block){state.block=block;state.markers.clear();state.trail.clear();}}
 if(!BbSettings::Get().minimap_enabled)return;
 if(now<next_sample)return;next_sample=now+std::chrono::milliseconds(100);
 if(loaded_block!=block)load_map(block);
 std::vector<Marker> markers;
 for(auto& e:entries){
  Point q=e.p;
  if(e.kind=='I'){if(std::hypot(q.x-p.x,q.z-p.z)>140 || std::abs(q.y-p.y)>30)continue;resolve_flags(e);bool got=false;if(e.flag && event_flag_get(e.flag,&got) && got)continue;}
  else continue; // Spawn records are never substituted for live actor positions.
  if(std::hypot(q.x-p.x,q.z-p.z)<=140 && std::abs(q.y-p.y)<=30 && markers.size()<512)markers.push_back({q,e.kind,std::abs(q.y-p.y)>4});
 }
 collect_actors(man,player,p,markers);
 if(sample_count++%300==20)host_log("pc-minimap: block=%08x player=(%.2f,%.2f,%.2f) items=%zu enemies=%zu NPCs=%zu allies=%zu unknown=%zu pause=%d",block,p.x,p.y,p.z,std::count_if(markers.begin(),markers.end(),[](auto& m){return m.kind=='I';}),std::count_if(markers.begin(),markers.end(),[](auto& m){return m.kind=='E';}),std::count_if(markers.begin(),markers.end(),[](auto& m){return m.kind=='N';}),std::count_if(markers.begin(),markers.end(),[](auto& m){return m.kind=='F';}),std::count_if(markers.begin(),markers.end(),[](auto& m){return m.kind=='U';}),int(freeze.load()));
 std::lock_guard lock(mutex);
 if(state.block!=block)state.trail.clear();
 if(state.trail.empty() || std::hypot(p.x-state.trail.back().x,p.z-state.trail.back().z)>.6f){
  if(state.trail.size()==2048)state.trail.erase(state.trail.begin(),state.trail.begin()+512);state.trail.push_back(p);
 }
 state.valid=true;state.block=block;state.player=p;state.yaw=yaw;state.markers=std::move(markers);
}
Snapshot Read(){std::lock_guard lock(mutex);return state;}
Snapshot ReadSummary(){std::lock_guard lock(mutex);Snapshot s;s.valid=state.valid;s.block=state.block;s.player=state.player;s.yaw=state.yaw;return s;}
bool InWorld(){return in_world;}
bool Paused(){return paused;}
void Pause(bool v){paused=v;if(!v)freeze=0;}
bool CanFreeze(){return freeze_ready;}
void RequestSave(){request=1;}
void RequestTitle(){request=2;}
std::string SaveStatus(){std::lock_guard lock(mutex);return status;}
}
