// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../src/controller_profile.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace BbControllerSetup {
struct Binding {const char* key;const char* name;const char* prompt;bool axis;};
inline constexpr Binding Bindings[]={
 {"a","确认 / 交互 · A / ×","按下位于下方的确认键",false},
 {"b","返回 / 闪避 · B / ○","按下位于右方的返回键",false},
 {"x","使用道具 · X / □","按下位于左方的按键",false},
 {"y","采血瓶 · Y / △","按下位于上方的按键",false},
 {"leftshoulder","左肩键 · LB / L1","按下左肩键 L1 / LB",false},
 {"rightshoulder","普通攻击 · RB / R1","按下右肩键 R1 / RB",false},
 {"lefttrigger","左扳机 · LT / L2","扣下左扳机 L2 / LT",true},
 {"righttrigger","重击 · RT / R2","扣下右扳机 R2 / RT",true},
 {"back","装备栏 · View / 触摸板","按下 View、Back 或触摸板",false},
 {"start","系统菜单 · Menu / Options","按下 Menu、Start 或 Options",false},
 {"leftstick","左摇杆按下 · L3","按下左摇杆",false},
 {"rightstick","锁定敌人 · R3","按下右摇杆",false},
 {"dpup","十字键 · 上","按下十字键上方",false},
 {"dpdown","切换道具 · 十字下","按下十字键下方",false},
 {"dpleft","切换左武器 · 十字左","按下十字键左方",false},
 {"dpright","切换右武器 · 十字右","按下十字键右方",false},
 {"leftx","移动横轴","向右推左摇杆，然后松开",true},
 {"lefty","移动纵轴","向下推左摇杆，然后松开",true},
 {"rightx","镜头横轴","向右推右摇杆，然后松开",true},
 {"righty","镜头纵轴","向下推右摇杆，然后松开",true}
};
inline constexpr int BindingCount=sizeof(Bindings)/sizeof(Bindings[0]);
inline bool AtomicText(const std::filesystem::path& path,const std::string& value){
 std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
 auto temp=path;temp+=".tmp";{std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out||!out.write(value.data(),value.size())||!out.flush())return false;}
#ifdef _WIN32
 return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 std::filesystem::rename(temp,path,ec);return !ec;
#endif
}
inline std::string Serialize(const BbControllerProfile& p){
 std::ostringstream out;out<<"# Bloodborne PC controller profile\nmanual_mode="<<p.manual<<"\npreferred_guid="<<p.guid<<"\npad_icons="<<p.icons<<"\ndeadzone="<<p.deadzone<<'\n';
 const char* keys[]={"center_lx","center_ly","center_rx","center_ry"};for(int a=0;a<4;a++)out<<keys[a]<<'='<<p.center[a]<<'\n';return out.str();
}
struct Device{SDL_JoystickID id;std::string name,guid;bool mapped;};
inline std::vector<Device> Devices(){
 std::vector<Device> list;int n=0;auto* ids=SDL_GetJoysticks(&n);
 for(int i=0;ids&&i<n;i++){char guid[33];SDL_GUIDToString(SDL_GetJoystickGUIDForID(ids[i]),guid,sizeof guid);const auto* name=SDL_GetJoystickNameForID(ids[i]);list.push_back({ids[i],name?name:"未命名设备",guid,SDL_IsGamepad(ids[i])});}SDL_free(ids);return list;
}
class Editor {
 SDL_Joystick* joystick=nullptr;SDL_Gamepad* pad=nullptr;
 std::string original_mapping,original_hint;bool preview_changed=false;
 std::array<Sint16,64> baseline{};Uint64 armed_at=0;
 void close(){if(pad){SDL_CloseGamepad(pad);pad=nullptr;}if(joystick){SDL_CloseJoystick(joystick);joystick=nullptr;}}
public:
 BbControllerProfile profile{};
 std::filesystem::path profile_path,mapping_path;
 std::array<std::string,BindingCount> values{};
 SDL_JoystickID selected=0;std::string name,guid,status;int armed=-1;
 explicit Editor(std::filesystem::path settings):profile_path(settings/"controller.ini"),mapping_path(settings/"gamecontrollerdb.txt"){
  if(const auto* hint=SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG))original_hint=hint;
  const auto p=profile_path.u8string();profile=bb_controller_profile_load(reinterpret_cast<const char*>(p.c_str()));
  const auto m=mapping_path.u8string();bb_controller_load_mappings(reinterpret_cast<const char*>(m.c_str()));
 }
 ~Editor(){discard();close();}Editor(const Editor&)=delete;Editor& operator=(const Editor&)=delete;
 void shutdown(){close();}
 bool attached() const{return joystick&&SDL_JoystickConnected(joystick);}
 void discard(){
  if(preview_changed&&attached()){
   if(pad){SDL_CloseGamepad(pad);pad=nullptr;}
   SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLERCONFIG,original_hint.c_str(),SDL_HINT_OVERRIDE);
   SDL_SetGamepadMapping(selected,nullptr);
   SDL_ReloadGamepadMappings();
   const auto file=mapping_path.u8string();bb_controller_load_mappings(reinterpret_cast<const char*>(file.c_str()));
   if(!original_mapping.empty())SDL_SetGamepadMapping(selected,original_mapping.c_str());
  }
  preview_changed=false;armed=-1;
 }
 bool select(SDL_JoystickID id){
  discard();close();selected=id;armed=-1;values={};status.clear();guid.clear();name.clear();original_mapping.clear();
  joystick=SDL_OpenJoystick(id);if(!joystick){status="无法打开此设备，请检查连接后重试。";return false;}
  char text[33];SDL_GUIDToString(SDL_GetJoystickGUID(joystick),text,sizeof text);guid=text;const auto* raw=SDL_GetJoystickName(joystick);name=raw?raw:"手动手柄";
  if(!bb_controller_profile_matches(&profile,id)){profile.deadzone=12;std::fill(std::begin(profile.center),std::end(profile.center),0);}
  char* current=SDL_GetGamepadMappingForID(id);
  if(current){original_mapping=current;std::istringstream fields(current);std::string token;while(std::getline(fields,token,',')){const auto sep=token.find(':');if(sep==std::string::npos)continue;for(int b=0;b<BindingCount;b++)if(token.substr(0,sep)==Bindings[b].key)values[b]=token.substr(sep+1);}SDL_free(current);}
  if(SDL_IsGamepad(id))pad=SDL_OpenGamepad(id);
  return true;
 }
 bool arm(int index){
  if(!attached()||index<0||index>=BindingCount)return false;
  armed=index;armed_at=SDL_GetTicks();for(int a=0;a<64;a++)baseline[a]=a<SDL_GetNumJoystickAxes(joystick)?SDL_GetJoystickAxis(joystick,a):0;
  status=Bindings[index].prompt;return true;
 }
 bool capture(const SDL_Event& e){
  if(armed<0)return false;
  if(e.type==SDL_EVENT_KEY_DOWN&&e.key.key==SDLK_ESCAPE){armed=-1;status="已取消本次绑定。";return true;}
  std::string value;
  if(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN&&e.jbutton.which==selected&&armed<16)value="b"+std::to_string(e.jbutton.button);
  if(e.type==SDL_EVENT_JOYSTICK_HAT_MOTION&&e.jhat.which==selected&&armed<16&&e.jhat.value&&(e.jhat.value&(e.jhat.value-1))==0)value="h"+std::to_string(e.jhat.hat)+"."+std::to_string(e.jhat.value);
  if(e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&e.jaxis.which==selected&&e.jaxis.axis<64){
   const int from=baseline[e.jaxis.axis],change=int(e.jaxis.value)-from;
   if(std::abs(change)>14000){
    const bool trigger=armed==6||armed==7;
    if(armed>=16)value="a"+std::to_string(e.jaxis.axis)+(change<0?"~":"");
    else if(trigger)value=(std::abs(from)<16000?(change>0?"+":"-"):"")+std::string("a")+std::to_string(e.jaxis.axis)+(std::abs(from)>=16000&&change<0?"~":"");
    else value=std::string(e.jaxis.value<0?"-":"+")+"a"+std::to_string(e.jaxis.axis);
   }
  }
  if(value.empty())return false;
  // One raw control cannot accidentally overwrite two unrelated actions. A
  // shared signed trigger axis remains valid because +aN and -aN differ.
  for(int i=0;i<BindingCount;i++)if(i!=armed&&values[i]==value){status="此输入已用于其他动作，请选择另一个按键或先清除旧绑定。";return true;}
  values[armed]=value;if(armed>=16)profile.center[armed-16]=0;armed=-1;status="绑定已记录；保存前可继续调整或校准。";return true;
 }
 int assigned() const{return int(std::count_if(values.begin(),values.end(),[](const auto& s){return !s.empty();}));}
 bool ready() const{
  for(int b:{0,1,2,3,4,5,6,7,8,9,11,13,14,15,16,17,18,19})if(values[b].empty())return false;return attached();
 }
 std::string mapping() const{
  std::string clean=name;for(char& c:clean)if(static_cast<unsigned char>(c)<32||c==',')c='_';
  if(clean.size()>96){clean.resize(96);while(!clean.empty()&&(static_cast<unsigned char>(clean.back())&0xc0)==0x80)clean.pop_back();if(!clean.empty()&&(static_cast<unsigned char>(clean.back())&0x80))clean.pop_back();}
  std::string line=guid+","+clean+",";for(int b=0;b<BindingCount;b++)if(!values[b].empty())line+=std::string(Bindings[b].key)+":"+values[b]+",";
  return line+"platform:Windows,";
 }
 bool preview(){
  if(!ready()){status="请补齐基础操作、锁定、切换武器/道具和四个摇杆方向；L3、十字上可选。";return false;}
  const std::string line=mapping();
  // Leave an unchanged live mapping in place while reading its neutral axes.
  char* active=SDL_GetGamepadMappingForID(selected);
  const bool unchanged=active&&line==active;SDL_free(active);
  if(!unchanged){
   std::istringstream prior(original_hint);std::string row,hint;
   while(std::getline(prior,row))if(row.rfind(guid+",",0)!=0)hint+=row+"\n";
   hint+=line;SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLERCONFIG,hint.c_str(),SDL_HINT_OVERRIDE);
   if(!SDL_SetGamepadMapping(selected,line.c_str())){status="应用失败："+std::string(SDL_GetError());return false;}preview_changed=true;
  }
  if(!pad)pad=SDL_OpenGamepad(selected);
  if(!pad){status="布局已应用，但设备当前不可用，请检查连接。";return false;}
  status="已应用：现在可检查摇杆和按键，再保存。";return true;
 }
 bool calibrate(){
  if(!preview())return false;
  for(int a=0;a<4;a++){const int value=SDL_GetGamepadAxis(pad,static_cast<SDL_GamepadAxis>(a));if(std::abs(value)>8000){status="请松开双摇杆并回到中央，再点击校准。";return false;}}
  for(int a=0;a<4;a++)profile.center[a]=SDL_GetGamepadAxis(pad,static_cast<SDL_GamepadAxis>(a));
  status="中立位置已校准；死区可抑制轻微漂移。";return true;
 }
 std::array<int,4> axes() const{std::array<int,4> out{};if(pad)for(int a=0;a<4;a++)out[a]=bb_controller_calibrated_axis(SDL_GetGamepadAxis(pad,static_cast<SDL_GamepadAxis>(a)),profile.center[a],profile.deadzone);return out;}
 bool save(bool waiting=false){
  if(!waiting){
   if(!preview())return false;
   std::string output;std::ifstream in(mapping_path);std::string line;
   while(std::getline(in,line))if(line.rfind(guid+",",0)!=0)output+=line+"\n";
   in.close(); // Windows cannot replace a file while our reader keeps it open.
   output+=mapping()+"\n";
   if(!AtomicText(mapping_path,output)){status="无法保存手柄布局，请检查文件夹写入权限。";return false;}
   std::snprintf(profile.guid,sizeof profile.guid,"%s",guid.c_str());
  }else{discard();profile.guid[0]=0;std::fill(std::begin(profile.center),std::end(profile.center),0);}
  profile.manual=1;
  if(!AtomicText(profile_path,Serialize(profile))){status="无法保存手柄配置，请检查文件夹写入权限。";return false;}
  if(!waiting){original_mapping=mapping();preview_changed=false;}
  status=waiting?"已选择手柄模式，设备可稍后连接。":"手动配置已保存，重新启动和复制整个游戏文件夹后仍可使用。";return true;
 }
 bool automatic(){
  BbControllerProfile reset{};std::strcpy(reset.icons,"auto");
  if(!AtomicText(profile_path,Serialize(reset))){status="无法保存自动选择设置。";return false;}
  discard();
  profile=reset;status="已恢复自动选择；已保存的设备布局仍可使用。";return true;
 }
};
}
