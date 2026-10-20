// SPDX-License-Identifier: GPL-3.0-or-later
// Render the actual HUD onto software surfaces. No desktop window or OS input.
#include "pc_game_ui.h"
#include "bbport_settings.h"
#include "engine/pc_hud.h"
#include "input/bindings.h"
#include "game_ui_model.h"
#include "imgui.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"CHECK line %d: %s (%s)\n",__LINE__,#x,SDL_GetError());std::_Exit(3);}}while(false)
namespace {std::uint32_t block=0x18010000;}
namespace BbEngine::PcHud {
Snapshot Read(){Snapshot s;s.valid=true;s.block=block;return s;}
Snapshot ReadSummary(){return Read();}
bool InWorld(){return true;}
void Pause(bool){}
bool CanFreeze(){return true;}
void RequestSave(){}
void RequestTitle(){}
std::string SaveStatus(){return {};}
}
namespace BbMinimap {
void Request(std::uint32_t,Point,float){}
std::shared_ptr<const Raster> Ready(std::uint32_t){return {};}
}
int host_binding_key(int){return SDL_SCANCODE_M;}
int host_binding_mouse(int){return 0;}
extern "C" int runtime_pad_type(){return SDL_GAMEPAD_TYPE_UNKNOWN;}
bool menu_pointer_in_menu(){return false;}
void host_binding_describe(int id,char* out,std::size_t count){
 const char* text="M";
 switch(id){
 case kBindAttack:text="鼠标左键";break;case kBindStrong:text="Shift + 鼠标左键";break;
 case kBindFirearm:text="鼠标右键";break;case kBindLockOn:text="鼠标中键";break;
 case kBindRoll:text="Space";break;case kBindStrongMod:text="CapsLock";break;
 case kBindMeleeGuard:text="Ctrl";break;case kBindTransform:text="F";break;
 case kBindSwitchRight:text="1";break;case kBindSwitchLeft:text="2";break;
 case kBindSwitchItem:text="滚轮";break;case kBindUseItem:text="R";break;
 case kBindBloodVial:text="E";break;case kBindInteract:text="Q";break;case kBindMenu:text="Tab";break;
 case kBindMoveF:text="W";break;case kBindMoveB:text="S";break;case kBindMoveL:text="A";break;case kBindMoveR:text="D";break;
 }
 std::snprintf(out,count,"%s",text);
}
int main(int argc,char** argv){
 CHECK(argc==3);CHECK(!BbGameUi::Open());CHECK(SDL_Init(SDL_INIT_GAMEPAD));
 const auto folder=std::filesystem::path(argv[1]);std::filesystem::create_directories(folder);
 auto& settings=BbSettings::Get();settings.minimap_enabled=false;settings.pad_guard_mode=0;
 for(const auto size:{std::pair{1280,720},std::pair{2560,1600},std::pair{3840,2160},std::pair{5120,1440}}){
  auto* surface=SDL_CreateSurface(size.first,size.second,SDL_PIXELFORMAT_RGBA32);CHECK(surface);
  auto* renderer=SDL_CreateSoftwareRenderer(surface);CHECK(renderer);
  ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
  io.DisplaySize=ImVec2(float(size.first),float(size.second));io.DeltaTime=1.f/60;
  auto* font=io.Fonts->AddFontFromFileTTF(argv[2],23);CHECK(font);BbGameUi::SetMenuFont(font);
  CHECK(ImGui_ImplSDLRenderer3_Init(renderer));
  const auto layout=BbGameUiModel::LeftGuide(io.DisplaySize.x,io.DisplaySize.y);
  CHECK(layout.x>=0 && layout.y>=180*layout.scale && layout.x+layout.width<size.first*.35f && layout.y+layout.height<size.second);
  std::vector<unsigned char> background(std::size_t(surface->pitch)*surface->h);
  auto frame=[&]{
   SDL_SetRenderDrawColor(renderer,45,50,47,255);SDL_RenderClear(renderer);
   const int cell=std::max(8,int(32*layout.scale));SDL_SetRenderDrawColor(renderer,65,68,59,255);
   for(int y=0;y<size.second;y+=cell)for(int x=0;x<size.first;x+=cell)if((x/cell+y/cell)%2){SDL_FRect rect{float(x),float(y),float(cell),float(cell)};SDL_RenderFillRect(renderer,&rect);}
   CHECK(SDL_FlushRenderer(renderer));
   std::memcpy(background.data(),surface->pixels,background.size());
   ImGui::GetStyle().FontScaleMain=layout.scale;
   ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();BbGameUi::Draw(false);ImGui::Render();
   ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
  };
  auto changed=[&]{
   std::size_t count=0,total=0;const auto* pixels=static_cast<const unsigned char*>(surface->pixels);
   for(int y=int(layout.y);y<int(layout.y+layout.height);++y)for(int x=int(layout.x);x<int(layout.x+layout.width);++x){
    const auto at=std::size_t(y)*surface->pitch+x*4;count+=std::memcmp(pixels+at,background.data()+at,4)!=0;++total;
   }
   return double(count)/double(total);
  };
  for(int mode=0;mode<3;mode++){
   settings.control_mode=mode?1:0;settings.pad_icons=mode==2?BbSettings::PadIconsPlayStation:BbSettings::PadIconsXbox;
   settings.beginner_hints=true;block=0x18010000;frame();frame();frame();
   const double ratio=changed();
   const auto filename=(folder/("guide-"+std::to_string(size.first)+"-"+std::to_string(size.second)+"-"+std::to_string(mode)+".bmp")).string();CHECK(SDL_SaveBMP(surface,filename.c_str()));
   std::printf("GUIDE size=%dx%d mode=%d changed=%.5f transparent=1 left=1\n",size.first,size.second,mode,ratio);std::fflush(stdout);
   CHECK(ratio>.005 && ratio<.40);
  }
  settings.beginner_hints=false;frame();CHECK(changed()==0);
  settings.beginner_hints=true;block=0x21000000;frame();CHECK(changed()==0);
  ImGui_ImplSDLRenderer3_Shutdown();BbGameUi::SetMenuFont(nullptr);ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
 }
 SDL_Quit();std::puts("Guide UI PASS: 12 software previews, transparent background, left bounds, keyboard/Xbox/PlayStation, visibility and region gates");
}
