// SPDX-License-Identifier: GPL-3.0-or-later
// Unmapped SDL joystick -> manual editor -> persisted mapping -> actual pad sampler.
#include "../tools/controller_setup.h"
#include "input/input.h"
#include "input/compat.h"
#include "bbport_settings.h"
#include "bbport_overlay.h"
#include "engine/option_menu.h"
#include "engine/pc_guard.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"CHECK line %d: %s (%s)\n",__LINE__,#x,SDL_GetError());std::_Exit(3);}} while(false)
extern "C" {int fixture_pad_open();int fixture_pad_read(BbPcPad*);void fixture_pad_close();}
namespace {bool guard=false;}
namespace BbOverlay {bool MenuOpen(){return false;}bool CapturesInput(){return false;}}
namespace BbEngine {bool Prepare(Hook&,std::uint8_t*,std::size_t,std::size_t,std::span<const std::uint8_t>,Callback){return false;}void Commit(std::span<Hook>) {}}
namespace BbEngine::Options {bool ClickDecides(std::uint8_t*,int){return false;}void ListUpdate(std::uint8_t*,bool){}}
namespace BbEngine::PcGuard {void Input(bool value){guard=value;}}
extern "C" int bbgpu_overlay_captures_input(){return 0;}
void menu_pointer_install(ElfImage*){}void menu_pointer_click(){}uint32_t menu_pointer_take_press(){return 0;}uint64_t menu_pointer_tick(){return 0;}
bool menu_pointer_in_menu(){return false;}bool menu_pointer_navigating(){return false;}
uint32_t menu_region_confirm_button(){return 0x4000;}uint32_t menu_confirm_button(){return 0x4000;}uint32_t menu_back_button(){return 0x2000;}
void mouse_camera_install(ElfImage*){}bool mouse_camera_installed(){return false;}void mouse_camera_publish(int,bool,bool,bool){}bool mouse_camera_flick(float&,float&){return false;}
int main(int argc,char** argv){
 CHECK(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);const std::filesystem::path folder=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(folder);
 _putenv_s("SDL_GAMECONTROLLERCONFIG_FILE","");_putenv_s("SDL_GAMECONTROLLERCONFIG","");
 CHECK(SDL_Init(SDL_INIT_GAMEPAD));
 SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_UNKNOWN;desc.vendor_id=0x1bad;desc.product_id=0xf00d;desc.naxes=6;desc.nbuttons=12;desc.nhats=1;desc.name="BB unrecognised USB manual fixture";
 auto id=SDL_AttachVirtualJoystick(&desc);CHECK(id);auto* raw=SDL_OpenJoystick(id);CHECK(raw);CHECK(!SDL_IsGamepad(id));
 auto pump=[](){SDL_UpdateJoysticks();SDL_PumpEvents();};
 auto axis=[&](int a,int v){CHECK(SDL_SetJoystickVirtualAxis(raw,a,Sint16(v)));pump();};
 auto button=[&](int b,bool v){CHECK(SDL_SetJoystickVirtualButton(raw,b,v));pump();};
 auto hat=[&](int value){CHECK(SDL_SetJoystickVirtualHat(raw,0,Uint8(value)));pump();};
 axis(4,-32768);axis(5,-32768);
 auto clear_events=[](){SDL_Event e;while(SDL_PollEvent(&e)){};};clear_events();
 if(std::string(argv[1])=="--configure"){
  BbControllerSetup::Editor editor(folder);CHECK(editor.select(id));CHECK(!editor.ready());CHECK(BbControllerSetup::Devices().size()==1);
  for(int field=0;field<BbControllerSetup::BindingCount;field++){
   clear_events();CHECK(editor.arm(field));
   if(field<6)button(field,true);
   else if(field==6||field==7)axis(field-2,32767);
   else if(field<12)button(field-2,true);
   else if(field<16){const int values[]={1,4,8,2};hat(values[field-12]);}
   else axis(field-16,field==18?-32768:32767);
   SDL_Event e;while(SDL_PollEvent(&e))editor.capture(e);CHECK(editor.armed==-1);CHECK(!editor.values[field].empty());
   if(field<6)button(field,false);
   else if(field==6||field==7)axis(field-2,-32768);
   else if(field<12)button(field-2,false);
   else if(field<16)hat(0);
   else axis(field-16,0);
  }
  CHECK(editor.ready()&&editor.assigned()==20&&editor.values[18]=="a2~");
  const auto conflict=editor.guid+",Conflicting external layout,a:b11,leftx:a5,platform:Windows,";SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLERCONFIG,conflict.c_str(),SDL_HINT_OVERRIDE);
  CHECK(editor.preview());CHECK(SDL_IsGamepad(id));
  editor.discard();CHECK(!SDL_IsGamepad(id));CHECK(editor.preview());
  axis(0,1200);axis(1,-800);axis(2,700);axis(3,500);CHECK(editor.calibrate());
  std::printf("calibrated centres: %d %d %d %d\n",editor.profile.center[0],editor.profile.center[1],editor.profile.center[2],editor.profile.center[3]);
  CHECK(editor.profile.center[0]==1200&&editor.profile.center[1]==-800&&std::abs(editor.profile.center[2]+700)<=1&&editor.profile.center[3]==500);
  editor.profile.deadzone=12;std::strcpy(editor.profile.icons,"playstation");const bool saved_ok=editor.save();std::printf("save status: %s\n",editor.status.c_str());CHECK(saved_ok);
  const auto profile_path=(folder/"controller.ini").u8string();const auto profile=bb_controller_profile_load(reinterpret_cast<const char*>(profile_path.c_str()));
  CHECK(profile.manual&&profile.deadzone==12&&std::string(profile.icons)=="playstation"&&std::string(profile.guid)==editor.guid);
  CHECK(editor.save());std::ifstream saved(folder/"gamecontrollerdb.txt");std::string line;int matches=0;while(std::getline(saved,line))matches+=line.rfind(editor.guid+",",0)==0;CHECK(matches==1);
  CHECK(editor.arm(0));SDL_Event cancel{};cancel.type=SDL_EVENT_KEY_DOWN;cancel.key.key=SDLK_ESCAPE;CHECK(editor.capture(cancel)&&editor.armed==-1);
  // File contents are portable across Chinese paths and a renamed directory.
  const auto moved=folder/std::filesystem::u8path("中文 换目录");std::filesystem::create_directories(moved);std::filesystem::copy_file(folder/"controller.ini",moved/"controller.ini",std::filesystem::copy_options::overwrite_existing);
  const auto moved_path=(moved/"controller.ini").u8string();CHECK(bb_controller_profile_load(reinterpret_cast<const char*>(moved_path.c_str())).center[2]==editor.profile.center[2]);
  BbControllerSetup::Editor waiting(folder/"waiting");CHECK(waiting.save(true)&&waiting.profile.manual&&!*waiting.profile.guid);CHECK(waiting.automatic()&&!waiting.profile.manual);
  CHECK(bb_controller_calibrated_axis(1200,1200,12)==0&&bb_controller_calibrated_axis(32767,1200,12)==32767&&bb_controller_calibrated_axis(-32768,1200,12)==-32768);
  editor.shutdown();std::puts("MANUAL configure PASS: unmapped USB joystick, 20 captures, hat/buttons/triggers, inverted axis, centre/deadzone, atomic persistence, waiting/automatic, Unicode relocation");
 }else{
  CHECK(std::string(argv[1])=="--runtime");
  const auto profile_path=(folder/"controller.ini").string(),mapping_path=(folder/"gamecontrollerdb.txt").string();
  const auto profile=bb_controller_profile_load(profile_path.c_str());CHECK(profile.manual&&*profile.guid);
  _putenv_s("BB_CONFIG",(folder/"runtime.ini").string().c_str());_putenv_s("BB_GAMEPAD_PROFILE",profile_path.c_str());_putenv_s("BB_GAMEPAD_MAPPINGS",mapping_path.c_str());_putenv_s("BB_GAMEPAD",profile.guid);
  CHECK(fixture_pad_open()==1);auto& settings=BbSettings::Get();settings.pc_controls=true;settings.control_mode=1;CHECK(BbInput::Install(nullptr,0));SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;BbInput::Event(focus,false);
  auto read=[&](){pump();BbPcPad p{};CHECK(fixture_pad_read(&p)==0);return p;};
  auto p=read();CHECK(SDL_IsGamepad(id));axis(0,1200);axis(1,-800);axis(2,700);axis(3,500);p=read();std::printf("neutral sticks: %u %u %u %u\n",p.lx,p.ly,p.rx,p.ry);CHECK(p.lx==128&&p.ly==128&&p.rx==128&&p.ry==128);
  axis(0,32767);axis(1,-32768);axis(2,-32768);p=read();CHECK(p.lx==255&&p.ly==0&&p.rx==255);
  button(5,true);CHECK(read().buttons&0x800);button(5,false);button(9,true);CHECK(read().buttons&0x4);button(9,false);
  hat(2);CHECK(read().buttons&0x20);hat(8);CHECK(read().buttons&0x80);hat(4);CHECK(read().buttons&0x40);hat(0);
  axis(4,32767);p=read();CHECK(p.l2==255&&(p.buttons&0x100));axis(4,-32768);axis(5,32767);CHECK(read().r2==255);axis(5,-32768);
  button(4,true);axis(4,32767);p=read();CHECK(guard&&(p.buttons&0x102)==0x102);button(4,false);read();CHECK(!guard);axis(4,-32768);
  settings.control_mode=0;button(5,true);p=read();CHECK(!p.buttons&&p.lx==127&&p.ly==127);settings.control_mode=1;button(5,false);
  SDL_CloseJoystick(raw);CHECK(SDL_DetachVirtualJoystick(id));CHECK(read().buttons==0&&!guard);
  id=SDL_AttachVirtualJoystick(&desc);CHECK(id);raw=SDL_OpenJoystick(id);CHECK(raw);axis(4,-32768);axis(5,-32768);axis(0,1200);p=read();CHECK(SDL_IsGamepad(id)&&p.lx==128);button(5,true);CHECK(read().buttons&0x800);button(5,false);
  fixture_pad_close();std::puts("MANUAL runtime PASS: separate process reload, preferred device, calibration, sticks, attack/fire/lock, weapon/item hat, guard release and keyboard isolation");
 }
 SDL_CloseJoystick(raw);CHECK(SDL_DetachVirtualJoystick(id));SDL_Quit();
}
