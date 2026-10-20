// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real menu events through a complete ImGui frame; no running game or OS input.
#include "pc_game_ui.h"
#include "bbport_settings.h"
#include "engine/pc_hud.h"
#include "input/bindings.h"
#include "input/compat.h"
#include "game_ui_model.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>

namespace {
bool paused=false;int saves=0,titles=0,pause_calls=0;
void frame(float w=2560,float h=1600){
 auto& io=ImGui::GetIO();io.DisplaySize={w,h};io.DeltaTime=1.f/60;
 ImGui::GetStyle().FontScaleMain=std::min(w/1920,h/1080);
 ImGui::NewFrame();BbGameUi::Draw(false);ImGui::Render();
}
void key(SDL_Keycode code){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;assert(BbGameUi::HandleEvent(e));frame();}
}
namespace BbEngine::PcHud {
Snapshot Read(){return {};}
Snapshot ReadSummary(){return {};}
bool InWorld(){return true;}
void Pause(bool value){paused=value;++pause_calls;}
bool CanFreeze(){return true;}
void RequestSave(){++saves;}
void RequestTitle(){++titles;}
std::string SaveStatus(){return "保存完成";}
}
namespace BbMinimap {
void Request(std::uint32_t,Point,float){}
std::shared_ptr<const Raster> Ready(std::uint32_t){return {};}
}
int host_binding_key(int){return SDL_SCANCODE_M;}
extern "C" int runtime_pad_type(){return SDL_GAMEPAD_TYPE_UNKNOWN;}
int host_binding_mouse(int){return 0;}
void host_binding_describe(int,char* out,std::size_t n){std::snprintf(out,n,"M");}
bool menu_pointer_in_menu(){return false;}
int main(){
 const auto path=std::filesystem::temp_directory_path()/
  ("bb-game-menu-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
#ifdef _WIN32
 _putenv_s("BB_CONFIG",path.string().c_str());
#else
 setenv("BB_CONFIG",path.string().c_str(),1);
#endif
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 io.Fonts->AddFontDefaultBitmap();unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
 auto& settings=BbSettings::Get();settings.mouse_sensitivity=6;settings.minimap_range=45;settings.minimap_enabled=true;
 assert(BbGameUiModel::BeginnerArea(0x18010000)&&BbGameUiModel::BeginnerArea(0x18000000));
 assert(!BbGameUiModel::BeginnerArea(0x21000000)&&!BbGameUiModel::BeginnerArea(0x18020000));
 auto heading=BbGameUiModel::Heading(0);assert(std::abs(heading.x)<1e-5&&heading.y==1);
 heading=BbGameUiModel::Heading(3.14159265f/2);assert(heading.x<-.999f&&std::abs(heading.y)<1e-5);
 assert(BbGameUiModel::CanSelect(0,false)&&!BbGameUiModel::CanSelect(1,false)&&BbGameUiModel::CanSelect(1,true));
 assert(BbGameUiModel::CanSelect(1,false,true));
 const char* flag=std::getenv("BB_CONTROLS_CONFIRMED");const bool confirmed=flag&&std::strcmp(flag,"1")==0;
 if(confirmed){
  assert(!BbGameUi::ChoosingControls()&&!BbGameUi::Open());
  key(SDLK_F3);assert(BbGameUi::ChoosingControls()&&paused);
  key(SDLK_ESCAPE);assert(!BbGameUi::Open()&&!paused);
  key(SDLK_F3);
 }else{
  assert(BbGameUi::ChoosingControls()&&BbGameUi::CapturesInput());
  key(SDLK_ESCAPE);assert(BbGameUi::ChoosingControls());
  key(SDLK_F2);assert(BbGameUi::ChoosingControls());
  key(SDLK_F3);assert(BbGameUi::ChoosingControls()); // initial selection remains mandatory
 }
 key(SDLK_DOWN);key(SDLK_RETURN);assert(BbGameUi::ChoosingControls());
 key(SDLK_UP);key(SDLK_RETURN);assert(!BbGameUi::ChoosingControls()&&!BbGameUi::Open()&&settings.control_mode==0);
 // F2 only changes hint visibility; no chooser, pause call or held-key repeat.
 const bool hint=settings.beginner_hints;const int calls=pause_calls;
 key(SDLK_F2);assert(settings.beginner_hints!=hint && !BbGameUi::Open() && !paused && pause_calls==calls);
 SDL_Event repeat{};repeat.type=SDL_EVENT_KEY_DOWN;repeat.key.key=SDLK_F2;repeat.key.repeat=true;
 for(int i=0;i<50;i++)assert(BbGameUi::HandleEvent(repeat));frame();
 assert(settings.beginner_hints!=hint && pause_calls==calls);
 key(SDLK_F2);assert(settings.beginner_hints==hint && !BbGameUi::Open() && !paused);
 for(int i=0;i<200;i++){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_F2;assert(BbGameUi::HandleEvent(e));}
 frame();assert(settings.beginner_hints==hint && pause_calls==calls);
 // Reselection can be cancelled, including when the selected pad disconnected.
 key(SDLK_F3);key(SDLK_DOWN);key(SDLK_RETURN);assert(BbGameUi::ChoosingControls());
 key(SDLK_ESCAPE);assert(!BbGameUi::Open()&&!paused && settings.control_mode==0);
 key(SDLK_F3);key(SDLK_F3);assert(!BbGameUi::Open()&&!paused);
 key(SDLK_F3);SDL_Event right{};right.type=SDL_EVENT_MOUSE_BUTTON_DOWN;right.button.button=SDL_BUTTON_RIGHT;
 assert(BbGameUi::HandleEvent(right));frame();assert(!BbGameUi::Open()&&!paused);
 key(SDLK_ESCAPE);assert(BbGameUi::Open()&&paused);
 for(int i=0;i<5;i++)key(SDLK_DOWN);
 key(SDLK_RIGHT);assert(settings.minimap_range==50);
 key(SDLK_LEFT);assert(settings.minimap_range==45);
 key(SDLK_UP);key(SDLK_RETURN);assert(!settings.minimap_enabled);
 key(SDLK_RETURN);assert(settings.minimap_enabled);
 key(SDLK_UP);key(SDLK_RIGHT);assert(settings.mouse_sensitivity==7);
 key(SDLK_LEFT);assert(settings.mouse_sensitivity==6);
 key(SDLK_UP);key(SDLK_RETURN);assert(titles==0&&BbGameUi::Open());
 key(SDLK_ESCAPE);assert(titles==0&&BbGameUi::Open());
 key(SDLK_DOWN);key(SDLK_RETURN);assert(saves==1&&paused);
 for(auto size:{ImVec2(1280,720),ImVec2(1920,1080),ImVec2(2560,1600),ImVec2(3840,2160),ImVec2(5120,1440)})frame(size.x,size.y);
 key(SDLK_DOWN);key(SDLK_KP_ENTER);assert(titles==0);
 key(SDLK_KP_ENTER);assert(titles==1&&!BbGameUi::Open()&&!paused);
 // An application-local SDL virtual device checks controller event routing;
 // this is deliberately separate from real-controller acceptance testing.
 assert(SDL_Init(SDL_INIT_GAMEPAD));SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
 desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
 desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.name="Menu unit test virtual controller";
 auto controller=SDL_AttachVirtualJoystick(&desc);assert(controller);SDL_PumpEvents();
 key(SDLK_F3);key(SDLK_DOWN);key(SDLK_RETURN);assert(!BbGameUi::ChoosingControls()&&settings.control_mode==1&&settings.button_prompts==BbSettings::PromptsController);
 auto pad=[&](SDL_GamepadButton button){SDL_Event e{};e.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;e.gbutton.button=button;assert(BbGameUi::HandleEvent(e));frame();};
 key(SDLK_F3);pad(SDL_GAMEPAD_BUTTON_EAST);assert(!BbGameUi::Open()&&!paused&&settings.control_mode==1);
 pad(SDL_GAMEPAD_BUTTON_START);assert(BbGameUi::Open()&&paused);
 pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN);pad(SDL_GAMEPAD_BUTTON_SOUTH);assert(saves==2);
 pad(SDL_GAMEPAD_BUTTON_EAST);assert(!BbGameUi::Open()&&!paused);
 // Cancelling selection opened from the pause menu returns to that menu.
 pad(SDL_GAMEPAD_BUTTON_START);assert(BbGameUi::Open()&&paused);
 key(SDLK_F3);key(SDLK_ESCAPE);assert(BbGameUi::Open()&&paused&&!BbGameUi::ChoosingControls());
 pad(SDL_GAMEPAD_BUTTON_EAST);assert(!BbGameUi::Open()&&!paused);
 SDL_DetachVirtualJoystick(controller);
 _putenv_s("BB_ALLOW_UNDETECTED_PAD","1");key(SDLK_F3);key(SDLK_RETURN);assert(!BbGameUi::ChoosingControls()&&settings.control_mode==1);
 _putenv_s("BB_ALLOW_UNDETECTED_PAD","");key(SDLK_F3);key(SDLK_RETURN);assert(BbGameUi::ChoosingControls());key(SDLK_UP);key(SDLK_RETURN);assert(!BbGameUi::ChoosingControls()&&settings.control_mode==0);
 SDL_Quit();
 ImGui::DestroyContext();std::filesystem::remove(path);
 std::puts("PC game menu: PASS (mandatory device selection, keyboard and virtual-controller menus, settings, save, confirmation, pause/resume, five layouts, F2 hint-only, F3 cancel/recovery, held-key suppression)");
}
