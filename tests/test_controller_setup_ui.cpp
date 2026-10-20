// SPDX-License-Identifier: GPL-3.0-or-later
// Render the actual manual panel to a software surface, never a desktop window.
#include "../tools/controller_setup_ui.h"
#include "imgui_impl_sdlrenderer3.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"CHECK line %d: %s (%s)\n",__LINE__,#x,SDL_GetError());std::_Exit(3);}} while(false)
int main(int argc,char** argv){
#ifdef _WIN32
 _set_error_mode(_OUT_TO_STDERR);
#endif
 CHECK(argc==3);const auto folder=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(folder);
 CHECK(SDL_Init(SDL_INIT_GAMEPAD));SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_UNKNOWN;desc.vendor_id=0x1bad;desc.product_id=0xf00e;desc.naxes=6;desc.nbuttons=12;desc.nhats=1;desc.name="Generic USB Controller";
 const auto id=SDL_AttachVirtualJoystick(&desc);CHECK(id);
 BbControllerSetup::Editor editor(folder);CHECK(editor.select(id));
 for(const auto size:{std::pair{1040,752},std::pair{680,492},std::pair{1600,1000}}){
  auto* surface=SDL_CreateSurface(size.first,size.second,SDL_PIXELFORMAT_RGBA32);CHECK(surface);auto* renderer=SDL_CreateSoftwareRenderer(surface);CHECK(renderer);
  ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize=ImVec2(float(size.first),float(size.second));io.DeltaTime=1.f/60;
  auto* font=io.Fonts->AddFontFromFileTTF(argv[2],26);CHECK(font);CHECK(ImGui_ImplSDLRenderer3_Init(renderer));
  const float scale=std::min(io.DisplaySize.x/1040,io.DisplaySize.y/752);const ImVec2 origin((io.DisplaySize.x-1040*scale)*.5f,(io.DisplaySize.y-752*scale)*.5f);
  auto frame=[&](){
   ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos(origin);ImGui::SetNextWindowSize(ImVec2(1040*scale,752*scale));ImGui::PushFont(font,26*scale);
   ImGui::Begin("test-manual-ui",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
   auto* d=ImGui::GetWindowDrawList();d->AddRectFilled(origin,ImVec2(origin.x+1040*scale,origin.y+752*scale),IM_COL32(15,18,16,255));d->AddRect(ImVec2(origin.x+10*scale,origin.y+10*scale),ImVec2(origin.x+1030*scale,origin.y+742*scale),IM_COL32(162,151,121,150));d->AddText(font,39*scale,ImVec2(origin.x+64*scale,origin.y+41*scale),IM_COL32(203,193,165,255),"手柄手动配置");d->AddLine(ImVec2(origin.x+55*scale,origin.y+102*scale),ImVec2(origin.x+985*scale,origin.y+102*scale),IM_COL32(164,153,126,110));
   bool close=BbControllerSetup::Draw(editor,origin,scale,font,IM_COL32(203,193,165,255),IM_COL32(223,194,131,255),IM_COL32(151,149,134,255));
   ImGui::End();ImGui::PopFont();ImGui::Render();SDL_SetRenderDrawColor(renderer,5,7,7,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);return close;
  };
  CHECK(!frame());CHECK(!frame());const auto file=(folder/("manual-"+std::to_string(size.first)+".bmp")).u8string();CHECK(SDL_SaveBMP(surface,reinterpret_cast<const char*>(file.c_str())));
  // A process-local ImGui click checks the actual "connect later" button; no OS input.
  io.AddMousePosEvent(origin.x+420*scale,origin.y+671*scale);io.AddMouseButtonEvent(0,true);CHECK(!frame());io.AddMouseButtonEvent(0,false);CHECK(frame());CHECK(editor.profile.manual);
  ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
 }
 editor.shutdown();CHECK(SDL_DetachVirtualJoystick(id));SDL_Quit();std::puts("MANUAL UI PASS: actual panel, three layouts, Chinese font, connect-later persistence; software surfaces only");
}
