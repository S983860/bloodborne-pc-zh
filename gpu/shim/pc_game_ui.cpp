// SPDX-License-Identifier: GPL-3.0-or-later
#include "pc_game_ui.h"
#include "bbport_settings.h"
#include "engine/pc_hud.h"
#include "input/bindings.h"
#include "input/compat.h"
#include "game_ui_model.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
extern "C" int runtime_pad_type();
namespace BbGameUi {
namespace {
std::atomic<bool> opened{false};bool confirm_title=false;int selected=0;
// The portable entry confirms controls before the guest is loaded. Direct engine
// launches still require the fallback chooser. F2 only toggles hints; F3 reselects controls.
std::atomic<bool> choosing{[] {const char* confirmed=std::getenv("BB_CONTROLS_CONFIRMED");return !confirmed||std::strcmp(confirmed,"1")!=0;}()};std::atomic<std::uint64_t> release_until{0};
int choice=-1;bool choice_unavailable=false;
bool choice_can_cancel=false,choice_return_menu=false;int choice_return_selected=0;
std::atomic<bool> request_choice{false},request_choice_cancel{false};
std::atomic<unsigned> request_hint_toggles{0};
ImFont* menu_font=nullptr;
std::atomic<int> menu_nav{0},menu_adjust{0};std::atomic<bool> menu_confirm{false};
constexpr ImU32 ink=IM_COL32(197,185,155,240),gold=IM_COL32(219,190,124,250);
void open(bool value){opened=value;confirm_title=false;selected=0;menu_nav=0;menu_adjust=0;menu_confirm=false;BbEngine::PcHud::Pause(value);
 if(value){float x,y;SDL_GetMouseState(&x,&y);SDL_Window* w=SDL_GetMouseFocus();float density=w?SDL_GetWindowPixelDensity(w):1;ImGui::GetIO().AddMousePosEvent(x*density,y*density);}}
#include "pc_control_ui.inl"
bool minimap_event(const SDL_Event& e){
 const int key=host_binding_key(kBindMinimap),mouse=host_binding_mouse(kBindMinimap);
 if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && key>0 && e.key.scancode==key)return true;
 if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){int b=e.button.button;b=b==2?3:b==3?2:b;return mouse>0 && mouse==b;}
 if(e.type==SDL_EVENT_MOUSE_WHEEL)return (mouse==6&&e.wheel.y>0)||(mouse==7&&e.wheel.y<0)||(mouse==8&&e.wheel.y!=0);
 return false;
}
void menu(){
 auto& io=ImGui::GetIO();const float s=std::min(io.DisplaySize.x/1920,io.DisplaySize.y/1080);
 const ImVec2 extent(1080*s,870*s);
 ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(0,0,0,95));
 ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
 ImGui::SetNextWindowSize(extent);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
 ImGui::PushFont(menu_font,23.f);
 ImGui::Begin("狩猎手记##pc-game-menu",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
 auto* d=ImGui::GetWindowDrawList();const auto origin=ImGui::GetWindowPos();
 auto pt=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
 const ImU32 parchment=IM_COL32(205,201,184,255),muted=IM_COL32(148,146,133,255),edge=IM_COL32(133,128,110,155);
 d->AddRectFilledMultiColor(origin,pt(1080,870),IM_COL32(17,18,17,247),IM_COL32(17,18,17,247),IM_COL32(6,8,8,248),IM_COL32(6,8,8,248));
 d->AddRect(pt(1,1),pt(1079,869),edge);d->AddRect(pt(7,7),pt(1073,863),IM_COL32(114,112,99,48));
 // Stable, faint grain and engraved corner strokes use only a few simple UI primitives.
 for(unsigned i=0;i<140;++i){float x=14+(i*317%1048),y=15+(i*193%728);d->AddLine(pt(x,y),pt(std::min(x+float(7+i%29),1066.f),y),IM_COL32(182,178,157,5));}
 for(int side=0;side<2;++side)for(int bottom=0;bottom<2;++bottom){float x=side?1067:13,y=bottom?857:13,dx=side?-1:1,dy=bottom?-1:1;
  d->AddLine(pt(x,y),pt(x+dx*46,y),edge,s);d->AddLine(pt(x,y),pt(x,y+dy*35),edge,s);
  d->AddLine(pt(x+dx*5,y+dy*5),pt(x+dx*24,y+dy*5),IM_COL32(175,164,133,90),s);
 }
 auto text=[&](float x,float y,const char* value,ImU32 color,float size=32.f){d->AddText(ImGui::GetFont(),size*s,pt(x+1,y+1),IM_COL32(0,0,0,210),value);d->AddText(ImGui::GetFont(),size*s,pt(x,y),color,value);};
 auto rule=[&](float y){
  d->AddLine(pt(43,y),pt(1037,y),IM_COL32(147,141,119,110),s);
  d->AddLine(pt(57,y+4),pt(1023,y+4),IM_COL32(98,96,84,55),s);
  for(int side=0;side<2;++side){float x=side?1037:43,dir=side?-1:1;
   d->AddQuad(pt(x,y-3),pt(x+dir*6,y),pt(x,y+3),pt(x-dir*6,y),edge,s);
   d->AddBezierCubic(pt(x+dir*8,y),pt(x+dir*20,y-12),pt(x+dir*30,y+7),pt(x+dir*38,y),edge,s,12);
  }
 };
 text(63,33,confirm_title?"返回主界面":"系统",parchment,40);text(891,41,"狩猎手记",muted,24);rule(89);
 const int count=confirm_title?2:8;
 const int navigation=menu_nav.exchange(0)%count;selected=(selected+navigation+count)%count;
 if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)||io.MouseWheel<0)selected=(selected+1)%count;
 if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)||io.MouseWheel>0)selected=(selected+count-1)%count;
 selected=std::clamp(selected,0,count-1);
 const bool enter=menu_confirm.exchange(false);
 const int step=std::clamp(menu_adjust.exchange(0),-10,10);
 const bool mouse_moved=io.MouseDelta.x!=0||io.MouseDelta.y!=0;
 auto highlight=[&](float y,bool active){if(!active)return;
  d->AddRectFilledMultiColor(pt(43,y),pt(540,y+54),IM_COL32(142,121,72,0),IM_COL32(142,121,72,63),IM_COL32(142,121,72,63),IM_COL32(142,121,72,0));
  d->AddRectFilledMultiColor(pt(540,y),pt(1037,y+54),IM_COL32(142,121,72,63),IM_COL32(142,121,72,0),IM_COL32(142,121,72,0),IM_COL32(142,121,72,63));
  d->AddLine(pt(83,y+52),pt(997,y+52),IM_COL32(186,164,110,85),s);
  d->AddTriangleFilled(pt(55,y+23),pt(55,y+31),pt(61,y+27),gold);
 };
 auto row=[&](int index,float y,const char* label){
  ImGui::SetCursorScreenPos(pt(43,y));ImGui::PushID(index);
  bool click=ImGui::InvisibleButton("row",ImVec2(994*s,54*s));
  if(ImGui::IsItemHovered()&&(mouse_moved||ImGui::IsMouseClicked(0)))selected=index;
  ImGui::PopID();highlight(y,selected==index);text(84,y+14,label,selected==index?parchment:muted);
  return click||(selected==index&&enter);
 };
 if(confirm_title){
  text(84,210,"将保存当前进度并返回主界面。",parchment);
  if(row(0,330,"保存并返回")){BbEngine::PcHud::RequestTitle();open(false);}
  if(row(1,402,"取消")||ImGui::IsMouseClicked(ImGuiMouseButton_Right)){confirm_title=false;selected=2;}
  rule(615);text(65,647,"左键 / Enter  确认     右键 / Esc  返回",muted,24);
 }else{
  int action=-1;const char* labels[]={"继续游戏","保存游戏","返回主界面"};
  for(int i=0;i<3;++i)if(row(i,123+i*66.f,labels[i]))action=i;
  rule(335);
  bool changed=false;
  auto setting=[&](int index,float y,const char* label,int value,int low,int high,int stride,bool toggle){
   if(ImGui::IsMouseHoveringRect(pt(43,y),pt(1037,y+54))&&(mouse_moved||ImGui::IsMouseClicked(0)))selected=index;
   highlight(y,selected==index);text(84,y+14,label,selected==index?parchment:muted);
   ImGui::PushID(index);int delta=selected==index?step:0;
   auto arrow=[&](float x,bool right){
    ImGui::SetCursorScreenPos(pt(x-17,y+5));bool click=ImGui::InvisibleButton(right?"next":"previous",ImVec2(34*s,44*s));
    float dir=right?1.f:-1.f;ImVec2 points[]={pt(x-dir*3,y+21),pt(x+dir*3,y+27),pt(x-dir*3,y+33)};
    d->AddPolyline(points,3,ImGui::IsItemHovered()?gold:muted,ImDrawFlags_None,1.3f*s);return click;
   };
   if(arrow(595,false))--delta;if(arrow(1009,true))++delta;
   if(toggle){
    ImGui::SetCursorScreenPos(pt(620,y+5));bool click=ImGui::InvisibleButton("state",ImVec2(365*s,44*s));
    if(click||(selected==index&&enter))delta=1;
    if(delta)value=!value;
    text(773,y+14,value?"开启":"关闭",parchment);
   }else{
    value=std::clamp(value+delta*stride,low,high);
    constexpr float left=630,right=970;const float track_y=y+28;
    ImGui::SetCursorScreenPos(pt(left-8,y+6));ImGui::InvisibleButton("scale",ImVec2((right-left+16)*s,42*s));
    if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){
     float t=std::clamp((io.MousePos.x-origin.x-left*s)/((right-left)*s),0.f,1.f);
     value=std::clamp(int(std::lround((low+t*(high-low))/stride))*stride,low,high);
    }
    const float t=float(value-low)/(high-low),position=left+(right-left)*t;
    d->AddLine(pt(left,track_y),pt(right,track_y),IM_COL32(131,127,110,125),2*s);
    d->AddLine(pt(left,track_y),pt(position,track_y),IM_COL32(184,171,134,215),2*s);
    for(int tick=0;tick<=10;++tick){float x=left+(right-left)*tick/10;d->AddLine(pt(x,track_y-4),pt(x,track_y+4),IM_COL32(141,135,113,120),s);}
    d->AddQuadFilled(pt(position,track_y-7),pt(position+4,track_y),pt(position,track_y+7),pt(position-4,track_y),parchment);
    char number[32];std::snprintf(number,sizeof number,index==5?"%d 米":"%d / 10",value);text(493,y+14,number,parchment,30);
   }
   ImGui::PopID();return value;
  };
  auto& settings=BbSettings::Get();
  int sensitivity=setting(3,361,"鼠标镜头灵敏度",settings.mouse_sensitivity,0,10,1,false);
  int map=setting(4,427,"显示小地图",settings.minimap_enabled?1:0,0,1,1,true);
  int range=setting(5,493,"小地图范围",settings.minimap_range,15,120,5,false);
  int hints=setting(6,559,"亚楠操作指引",settings.beginner_hints?1:0,0,1,1,true);
  if(sensitivity!=settings.mouse_sensitivity){settings.mouse_sensitivity=sensitivity;changed=true;}
  if(bool(map)!=settings.minimap_enabled){settings.minimap_enabled=bool(map);changed=true;}
  if(range!=settings.minimap_range){settings.minimap_range=range;changed=true;}
  if(bool(hints)!=settings.beginner_hints){settings.beginner_hints=bool(hints);changed=true;}
  if(changed)BbSettings::Save();
  const bool change_controls=row(7,625,settings.control_mode==1?"操作方式：手柄（重新选择）":"操作方式：键鼠（重新选择）");
  rule(691);
  const char* help[]={"射击、锁定与武器切换可在“系统 → 按键设置 → 武器与药品”修改。","将当前角色进度写入存档。","保存进度并返回主界面。","调整鼠标转动镜头的速度，修改立即生效。","显示或隐藏附近的道路、墙体与角色标记。","调整小地图显示距离，单位为米。","仅在中央亚楠与教会镇显示；F2可直接开关，不暂停游戏。","重新选择键盘鼠标或手柄；也可按 F3，Esc或手柄B／○可取消。"};
  text(65,723,help[selected],parchment,26);
  text(65,768,settings.control_mode==1?"A（×）确认    B（○）返回    方向键选择及调整    Menu / Options 继续":"左键 / Enter  确认     滚轮  选择     ← / →  调整     Esc  继续游戏",muted,24);
  const auto status=BbEngine::PcHud::SaveStatus();
  text(65,811,status.empty()?"设置即时生效并自动保存":status.c_str(),muted,24);
  if(action==0||ImGui::IsMouseClicked(ImGuiMouseButton_Right))open(false);else if(action==1)BbEngine::PcHud::RequestSave();else if(action==2){confirm_title=true;selected=0;}
  if(change_controls)choose_controls();
 }
 ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(2);
}
void minimap(){
 const auto draw_start=std::chrono::steady_clock::now();
 const auto world=BbSettings::Get().minimap_enabled?BbEngine::PcHud::Read():BbEngine::PcHud::ReadSummary();if(!world.valid)return;
 const auto& io=ImGui::GetIO();const float s=std::min(io.DisplaySize.x/1920,io.DisplaySize.y/1080);
 const auto& settings=BbSettings::Get();
 const int res=std::clamp(BbSettings::ResolutionNeedsRestart()?settings.startup_output_res:settings.output_res.load(),0,BbSettings::OutputCount-1);
 const float ow=BbSettings::OutputWidth(res),oh=BbSettings::OutputHeight(res);
 const float fit=std::min(io.DisplaySize.x/ow,io.DisplaySize.y/oh);
 const float viewport_w=ow*fit,viewport_h=oh*fit;
 const float size=258*s, x=(io.DisplaySize.x+viewport_w)*.5f-30*s-size, y=(io.DisplaySize.y-viewport_h)*.5f+(settings.show_fps?64.f:40.f)*s;
 auto* d=ImGui::GetBackgroundDrawList();
 char key[80];host_binding_describe(kBindMinimap,key,sizeof key);
 char hint[120];std::snprintf(hint,sizeof hint,"%s %s小地图",key,BbSettings::Get().minimap_enabled?"关闭":"打开");
 if(!BbSettings::Get().minimap_enabled){d->AddRectFilled(ImVec2(x,y),ImVec2(x+size,y+30*s),IM_COL32(16,16,15,190));d->AddText(ImVec2(x+10*s,y+5*s),ink,hint);return;}
 d->AddRectFilled(ImVec2(x,y),ImVec2(x+size,y+size+100*s),IM_COL32(14,15,14,215));
 d->AddRect(ImVec2(x,y),ImVec2(x+size,y+size+100*s),ink);
 const ImVec2 center(x+size/2,y+size/2);float r=std::clamp(BbSettings::Get().minimap_range.load(),15,120),unit=(size/2-10*s)/r;
 d->PushClipRect(ImVec2(x+2*s,y+2*s),ImVec2(x+size-2*s,y+size-2*s),true);
 for(int i=-2;i<=2;++i){float t=i*(size-20*s)/4;d->AddLine(ImVec2(center.x+t,y),ImVec2(center.x+t,y+size),IM_COL32(154,147,124,22));d->AddLine(ImVec2(x,center.y+t),ImVec2(x+size,center.y+t),IM_COL32(154,147,124,22));}
 auto project=[&](const BbEngine::PcHud::Point& p){return ImVec2(center.x+(p.x-world.player.x)*unit,center.y-(p.z-world.player.z)*unit);};
 BbMinimap::Request(world.block,world.player,r);const auto terrain=BbMinimap::Ready(world.block);
 std::size_t drawn_rects=0;
 if(terrain && terrain->error.empty() && terrain->resolution>0 && std::abs(terrain->center.y-world.player.y)<2.5f){
  constexpr ImU32 colors[]={0,IM_COL32(50,50,44,110),IM_COL32(92,88,72,205),IM_COL32(138,128,102,205),IM_COL32(208,197,162,245)};
  const float pixel=2*terrain->half_span/terrain->resolution;
  for(auto rect:terrain->rects){
   auto a=project({terrain->center.x-terrain->half_span+rect.x0*pixel,0,terrain->center.z+terrain->half_span-rect.y0*pixel});
   auto b=project({terrain->center.x-terrain->half_span+rect.x1*pixel,0,terrain->center.z+terrain->half_span-rect.y1*pixel});
   a.x=std::max(a.x,x+2*s);a.y=std::max(a.y,y+2*s);b.x=std::min(b.x,x+size-2*s);b.y=std::min(b.y,y+size-2*s);
   if(a.x<b.x&&a.y<b.y){d->AddRectFilled(a,b,colors[std::min<int>(rect.shade,4)]);++drawn_rects;}
  }
 }
 for(std::size_t i=1;i<world.trail.size();++i){auto a=project(world.trail[i-1]),b=project(world.trail[i]);if(std::hypot(a.x-b.x,a.y-b.y)<size)d->AddLine(a,b,IM_COL32(165,159,131,140),1.5f*s);}
 for(auto& m:world.markers){auto p=project(m.p);const auto c=m.kind=='I'?gold:m.kind=='E'?IM_COL32(239,99,79,255):m.kind=='N'?IM_COL32(97,199,230,255):m.kind=='F'?IM_COL32(133,214,144,255):IM_COL32(167,167,163,220);
  if(m.kind=='I'){ImVec2 a(p.x,p.y-4*s),b(p.x+4*s,p.y),c1(p.x,p.y+4*s),e(p.x-4*s,p.y);if(m.elevated)d->AddQuad(a,b,c1,e,c,1.4f*s);else d->AddQuadFilled(a,b,c1,e,c);}
  else if(m.kind=='E'){ImVec2 a(p.x,p.y-4*s),b(p.x-4*s,p.y+3*s),e(p.x+4*s,p.y+3*s);if(m.elevated)d->AddTriangle(a,b,e,c,1.4f*s);else d->AddTriangleFilled(a,b,e,c);}
  else if(m.kind=='N'){if(m.elevated)d->AddCircle(p,4*s,c,12,1.4f*s);else d->AddCircleFilled(p,4*s,c,12);}
  else if(m.kind=='F'){d->AddLine(ImVec2(p.x-4*s,p.y),ImVec2(p.x+4*s,p.y),c,1.8f*s);d->AddLine(ImVec2(p.x,p.y-4*s),ImVec2(p.x,p.y+4*s),c,1.8f*s);}
  else d->AddCircle(p,2.5f*s,c,8,1.2f*s);
 }
 const auto forward=BbGameUiModel::Heading(world.yaw);ImVec2 f(forward.x,forward.y),side(-forward.y,forward.x);
 d->AddCircleFilled(center,12*s,IM_COL32(8,9,8,220),20);
 d->AddTriangleFilled(ImVec2(center.x+f.x*10*s,center.y+f.y*10*s),ImVec2(center.x-f.x*6*s+side.x*5*s,center.y-f.y*6*s+side.y*5*s),ImVec2(center.x-f.x*6*s-side.x*5*s,center.y-f.y*6*s-side.y*5*s),IM_COL32(239,232,211,255));
 d->PopClipRect();d->AddText(ImVec2(x+10*s,y+7*s),ink,"北 N");
 d->AddText(ImVec2(x+10*s,y+size+3*s),ink,hint);
 d->AddText(ImVec2(x+10*s,y+size+26*s),IM_COL32(239,99,79,255),"▲ 敌人");d->AddText(ImVec2(x+94*s,y+size+26*s),IM_COL32(97,199,230,255),"● NPC");d->AddText(ImVec2(x+169*s,y+size+26*s),gold,"◆ 道具");
 d->AddText(ImVec2(x+10*s,y+size+49*s),ink,"△ 自己　＋ 友军　○ 未明");
 const char* note=!terrain?"正在准备道路与墙体……":!terrain->error.empty()?terrain->error.c_str():"空心标记为上下层 · 浅线为墙体";
 d->AddText(ImVec2(x+10*s,y+size+73*s),IM_COL32(160,153,132,240),note);
 static unsigned frames=0;static double total=0,worst=0;double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-draw_start).count();total+=elapsed;worst=std::max(worst,elapsed);
 if(++frames==600){std::printf("Minimap draw: avg=%.1f us worst=%.1f us visible-rects=%zu markers=%zu\n",total/frames,worst,drawn_rects,world.markers.size());frames=0;total=worst=0;}
}
}
bool HandleEvent(const SDL_Event& e){
 if((e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) && e.key.key==SDLK_F2){
  if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && !ChoosingControls() && BbEngine::PcHud::InWorld())request_hint_toggles.fetch_add(1);
  return true;
 }
 if(e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_F3){
  if(!e.key.repeat){
   if(ChoosingControls())request_choice_cancel=true;
   else if(opened || !menu_pointer_in_menu())request_choice=true;
  }
  return true;
 }
 if(ChoosingControls()){
  if(e.type==SDL_EVENT_KEY_DOWN){
   if(!e.key.repeat && e.key.key==SDLK_ESCAPE)request_choice_cancel=true;
   if(!e.key.repeat && (e.key.key==SDLK_RETURN||e.key.key==SDLK_KP_ENTER))menu_confirm=true;
   if(e.key.key==SDLK_UP||e.key.key==SDLK_DOWN||e.key.key==SDLK_LEFT||e.key.key==SDLK_RIGHT)menu_nav.fetch_add(1);
   return true;
  }
  if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
   if(e.gbutton.button==SDL_GAMEPAD_BUTTON_EAST)request_choice_cancel=true;
   if(e.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP||e.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)menu_nav.fetch_add(1);
   if(e.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)menu_confirm=true;
   return true;
  }
  if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_RIGHT){request_choice_cancel=true;return true;}
  return false; // ImGui receives pointer events through the existing overlay adapter.
 }
 if(BbSettings::Get().control_mode==1 && e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button==SDL_GAMEPAD_BUTTON_START && BbEngine::PcHud::InWorld()&&!menu_pointer_in_menu()){
  if(opened)open(false);else open(true);return true;
 }
 if(opened && BbSettings::Get().control_mode==1 && e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
  switch(e.gbutton.button){
  case SDL_GAMEPAD_BUTTON_DPAD_UP:menu_nav.fetch_sub(1);break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:menu_nav.fetch_add(1);break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:menu_adjust.fetch_sub(1);break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:menu_adjust.fetch_add(1);break;
  case SDL_GAMEPAD_BUTTON_SOUTH:menu_confirm=true;break;
  case SDL_GAMEPAD_BUTTON_EAST:if(confirm_title)confirm_title=false;else open(false);break;
  default:break;
  }
  return true;
 }
 if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key==SDLK_ESCAPE){
  if(opened){if(confirm_title)confirm_title=false;else open(false);return true;}
  if(BbEngine::PcHud::InWorld()&&!menu_pointer_in_menu()){open(true);return true;}
 }
 // SDL menu actions are consumed explicitly so ImGui navigation cannot claim arrows first.
 if(opened && e.type==SDL_EVENT_KEY_DOWN){
  switch(e.key.key){
  case SDLK_UP:menu_nav.fetch_sub(1);return true;
  case SDLK_DOWN:menu_nav.fetch_add(1);return true;
  case SDLK_LEFT:menu_adjust.fetch_sub(1);return true;
  case SDLK_RIGHT:menu_adjust.fetch_add(1);return true;
  case SDLK_RETURN:case SDLK_KP_ENTER:if(!e.key.repeat)menu_confirm=true;return true;
  default:break;
  }
 }
 if(!opened && BbEngine::PcHud::InWorld()&&!menu_pointer_in_menu()&&minimap_event(e)){auto& s=BbSettings::Get();s.minimap_enabled=!s.minimap_enabled;BbSettings::Save();return true;}
 return false;
}
bool Open(){return opened||ChoosingControls();}
bool ChoosingControls(){return choosing||request_choice;}
bool CapturesInput(){return Open()||SDL_GetTicks()<release_until;}
void SetMenuFont(ImFont* font){menu_font=font;}
void Close(){if(!choosing)open(false);}
void Draw(bool other_menu){
 // Input callbacks enqueue requests; modal state, pause and ImGui stay on the
 // drawing thread. Toggling the guide never opens a modal or pauses the world.
 if(request_hint_toggles.exchange(0)&1u){auto& s=BbSettings::Get();s.beginner_hints=!s.beginner_hints;BbSettings::Save();}
 if(request_choice.exchange(false) && !choosing)choose_controls();
 if(request_choice_cancel.exchange(false) && choosing && choice_can_cancel)cancel_control_choice();
 if(choosing)control_choice();
 else if(opened){if(!BbEngine::PcHud::InWorld())Close();else menu();}
 else if(!other_menu&&!menu_pointer_in_menu()){minimap();beginner_hints();}
}
}
