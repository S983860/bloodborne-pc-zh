// SPDX-License-Identifier: GPL-3.0-or-later
// SDL virtual device -> production runtime_pad sampler -> production PC input.
// No window, OS device driver, game process or guest memory is touched.
#include "input/input.h"
#include "input/compat.h"
#include "bbport_settings.h"
#include "bbport_overlay.h"
#include "engine/option_menu.h"
#include "engine/pc_guard.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
// A failed background check must never open a Windows CRT assertion dialog.
#undef assert
#define assert(expression) do { if(!(expression)){std::fprintf(stderr,"CHECK failed at line %d: %s\n",__LINE__,#expression);std::_Exit(3);} } while(false)
extern "C" {int fixture_pad_open();int fixture_pad_read(BbPcPad*);void fixture_pad_close();int fixture_pad_vibrate();}
namespace {bool capture=false,native_menu=false,guard=false;unsigned region_confirm=0x4000;int rumble_calls=0;}
namespace BbOverlay {bool MenuOpen(){return capture;}bool CapturesInput(){return capture;}}
namespace BbEngine {bool Prepare(Hook&,std::uint8_t*,std::size_t,std::size_t,std::span<const std::uint8_t>,Callback){return false;}void Commit(std::span<Hook>) {}}
namespace BbEngine::Options {bool ClickDecides(std::uint8_t*,int){return false;}void ListUpdate(std::uint8_t*,bool){}}
namespace BbEngine::PcGuard {void Input(bool value){guard=value;}}
extern "C" int bbgpu_overlay_captures_input(){return capture;}
void menu_pointer_install(ElfImage*){} void menu_pointer_click(){} uint32_t menu_pointer_take_press(){return 0;} uint64_t menu_pointer_tick(){return 0;}
bool menu_pointer_in_menu(){return native_menu;}bool menu_pointer_navigating(){return native_menu;}
uint32_t menu_region_confirm_button(){return region_confirm;}uint32_t menu_confirm_button(){return region_confirm;}uint32_t menu_back_button(){return region_confirm==0x4000?0x2000:0x4000;}
void mouse_camera_install(ElfImage*){} bool mouse_camera_installed(){return false;}void mouse_camera_publish(int,bool,bool,bool){}bool mouse_camera_flick(float&,float&){return false;}

int main(){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 const auto config=std::filesystem::temp_directory_path()/("bb-virtual-pad-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
 _putenv_s("BB_CONFIG",config.string().c_str());
 _putenv_s("BB_GAMEPAD","Application-local controller regression");
 assert(SDL_Init(SDL_INIT_GAMEPAD));assert(fixture_pad_open()==1);
 auto& settings=BbSettings::Get();settings.pc_controls=true;settings.control_mode=1;
 assert(BbInput::Install(nullptr,0));
 auto event_focus=[&](bool value){SDL_Event e{};e.type=value?SDL_EVENT_WINDOW_FOCUS_GAINED:SDL_EVENT_WINDOW_FOCUS_LOST;BbInput::Event(e,false);};
 event_focus(true);
 SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
 desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
 desc.name="Application-local controller regression";desc.Rumble=[](void*,Uint16,Uint16){++rumble_calls;return true;};
 auto id=SDL_AttachVirtualJoystick(&desc);assert(id);auto* joystick=SDL_OpenJoystick(id);assert(joystick);
 auto pump=[](){SDL_UpdateJoysticks();SDL_PumpEvents();};
 auto axis=[&](SDL_GamepadAxis a,Sint16 value){assert(SDL_SetJoystickVirtualAxis(joystick,int(a),value));pump();};
 auto button=[&](SDL_GamepadButton b,bool value){assert(SDL_SetJoystickVirtualButton(joystick,int(b),value));pump();};
 auto read=[&](){pump();BbPcPad p{};assert(fixture_pad_read(&p)==0);return p;};
 auto neutral=[&](){for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;b++)assert(SDL_SetJoystickVirtualButton(joystick,b,false));for(int a=0;a<SDL_GAMEPAD_AXIS_COUNT;a++)assert(SDL_SetJoystickVirtualAxis(joystick,a,a>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER?-32768:0));pump();return read();};
 auto p=neutral();assert(p.buttons==0&&p.l2==0&&p.r2==0);
 axis(SDL_GAMEPAD_AXIS_LEFTX,32767);axis(SDL_GAMEPAD_AXIS_LEFTY,-32768);axis(SDL_GAMEPAD_AXIS_RIGHTX,12000);axis(SDL_GAMEPAD_AXIS_RIGHTY,-12000);
 p=read();assert(p.lx==255&&p.ly==0&&p.rx>150&&p.ry<110);neutral();
 struct Map{SDL_GamepadButton button;unsigned bit;};
 for(const auto map:{Map{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0x800},Map{SDL_GAMEPAD_BUTTON_RIGHT_STICK,0x4},Map{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,0x20},Map{SDL_GAMEPAD_BUTTON_DPAD_LEFT,0x80},Map{SDL_GAMEPAD_BUTTON_DPAD_DOWN,0x40},Map{SDL_GAMEPAD_BUTTON_WEST,0x8000},Map{SDL_GAMEPAD_BUTTON_NORTH,0x1000}}){button(map.button,true);assert(read().buttons==map.bit);neutral();}
 for(unsigned region:{0x4000u,0x2000u}){region_confirm=region;button(SDL_GAMEPAD_BUTTON_SOUTH,true);assert(read().buttons==region);neutral();button(SDL_GAMEPAD_BUTTON_EAST,true);assert(read().buttons==(region==0x4000?0x2000u:0x4000u));neutral();}
 region_confirm=0x4000;
 // The new default uses one digital shoulder: immediate first-sample guard.
 assert(settings.pad_guard_mode==0);
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);
 for(int n=0;n<10;n++){p=read();assert(guard&&p.buttons==0x102&&p.l2==255);}
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);p=read();assert(!guard&&p.buttons==0&&p.l2==0);
 button(SDL_GAMEPAD_BUTTON_LEFT_STICK,true);p=read();assert(!guard&&p.buttons==0x400);neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);button(SDL_GAMEPAD_BUTTON_LEFT_STICK,true);assert(read().buttons==0x102&&guard);
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);p=read();assert(!guard&&p.buttons==0);neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);p=read();assert(!guard&&p.buttons==0x800);neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);read();assert(guard);
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);p=read();assert(!guard&&p.l2==0&&p.buttons==0);neutral();
 // Retain all legacy chord/fire/release regression checks below.
 settings.pad_guard_mode=1;
 axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);p=read();assert(p.l2==255&&(p.buttons&0x100));neutral();
 axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);p=read();assert(p.r2==255&&(p.buttons&0x200));neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);p=read();assert(p.buttons==0x400&&!guard);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);
 for(int n=0;n<10;n++){p=read();assert(guard&&p.l2==255&&(p.buttons&0x102)==0x102);}
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);p=read();assert(!guard&&p.l2==0&&!(p.buttons&0x500));neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);read();assert(guard);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);p=read();assert(!guard&&p.l2==0&&!(p.buttons&0x500));neutral();
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);read();capture=true;read();assert(!guard);capture=false;p=read();assert(p.buttons==0&&!guard);neutral();
 // Short Back opens equipment, long Back keeps the native touch/gesture path.
 button(SDL_GAMEPAD_BUTTON_BACK,true);assert(!(read().buttons&0x100000));button(SDL_GAMEPAD_BUTTON_BACK,false);assert(read().buttons&0x8);SDL_Delay(90);neutral();
 button(SDL_GAMEPAD_BUTTON_BACK,true);read();SDL_Delay(370);p=read();assert(p.buttons&0x100000);neutral();SDL_Delay(90);
 button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);capture=true;assert(read().buttons==0&&!guard);capture=false;assert(read().buttons==0);neutral();button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);assert(read().buttons==0x800);neutral();
 button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);event_focus(false);assert(read().buttons==0&&!guard);event_focus(true);neutral();
 settings.control_mode=0;axis(SDL_GAMEPAD_AXIS_LEFTY,-32768);button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);p=read();assert(p.buttons==0&&p.ly==127&&p.l2==0);neutral();settings.control_mode=1;
 settings.control_mode=0;bool keys[SDL_SCANCODE_COUNT]{};keys[SDL_SCANCODE_LCTRL]=true;
 p=BbPcPad{};bbgpu_pc_input_delivery(&p,keys,SDL_SCANCODE_COUNT,0,1);assert(!guard&&(p.buttons&0x102)==0x102);
 keys[SDL_SCANCODE_LCTRL]=false;p=BbPcPad{};bbgpu_pc_input_delivery(&p,keys,SDL_SCANCODE_COUNT,0,0);assert(!guard);
 keys[SDL_SCANCODE_LCTRL]=true;p=BbPcPad{};bbgpu_pc_input_delivery(&p,keys,SDL_SCANCODE_COUNT,0,0);assert(guard);
 keys[SDL_SCANCODE_LCTRL]=false;p=BbPcPad{};bbgpu_pc_input_delivery(&p,keys,SDL_SCANCODE_COUNT,0,0);assert(!guard);settings.control_mode=1;
 const int initial_rumbles=rumble_calls;assert(fixture_pad_vibrate()==0);assert(rumble_calls>initial_rumbles);
 settings.control_mode=0;const int before_keyboard=rumble_calls;assert(fixture_pad_vibrate()==0);assert(rumble_calls==before_keyboard);settings.control_mode=1;
 // Disconnect/reconnect is confined to this process; the live game sees no pad.
 button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);read();assert(guard);
 SDL_CloseJoystick(joystick);assert(SDL_DetachVirtualJoystick(id));p=read();assert(!guard&&p.buttons==0);
 id=SDL_AttachVirtualJoystick(&desc);assert(id);joystick=SDL_OpenJoystick(id);assert(joystick);neutral();button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);assert(read().buttons==0x800);neutral();
 fixture_pad_close();SDL_CloseJoystick(joystick);assert(SDL_DetachVirtualJoystick(id));SDL_Quit();std::filesystem::remove(config);
 std::puts("Virtual controller PASS: actual SDL sampling, sticks/camera, attacks/triggers, lock-on, weapon/item cycling, region faces, guard hold/release, equipment/gestures, overlay release, focus loss, keyboard isolation and hotplug");
}
