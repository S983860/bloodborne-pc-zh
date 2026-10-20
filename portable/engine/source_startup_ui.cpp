// SPDX-License-Identifier: GPL-3.0-or-later
// This process owns only the pre-game contract UI. It never loads guest code,
// game saves, neural SDKs or the scene renderer. Exit: 10 keyboard, 11 pad, 20 cancel.
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include "controller_setup_ui.h"

namespace {
constexpr ImU32 ink=IM_COL32(203,193,165,255),gold=IM_COL32(223,194,131,255),muted=IM_COL32(151,149,134,255);
bool connected(){int count=0;auto* ids=SDL_GetGamepads(&count);SDL_free(ids);return count>0;}
bool released(){
 const bool* keys=SDL_GetKeyboardState(nullptr);
 if(keys[SDL_SCANCODE_RETURN]||keys[SDL_SCANCODE_KP_ENTER]||(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK))return false;
 int count=0;auto* ids=SDL_GetGamepads(&count);bool up=true;
 for(int i=0;ids&&i<count;i++)if(auto* p=SDL_GetGamepadFromID(ids[i]);p&&SDL_GetGamepadButton(p,SDL_GAMEPAD_BUTTON_SOUTH))up=false;
 SDL_free(ids);return up;
}
int failure(const char* what){std::fprintf(stderr,"Startup UI: %s: %s\n",what,SDL_GetError());SDL_Quit();return 1;}
}
int main(int argc,char** argv){
 std::map<std::string,std::string> args;
 for(int i=1;i+1<argc;i+=2)args[argv[i]]=argv[i+1];
 if(!args.contains("--gpu")||!args.contains("--vendor")){std::fprintf(stderr,"Startup UI requires a verified hardware profile.\n");return 1;}
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"0");
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return failure("SDL initialization");
 SDL_Rect bounds{0,0,1280,800};SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),&bounds);
 int w=std::min(1040,int(bounds.w*.86f)),h=std::min(752,int(bounds.h*.86f));
 auto* window=SDL_CreateWindow("血源诅咒 · 启动前确认",w,h,SDL_WINDOW_HIDDEN|SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_RESIZABLE);
 if(!window)return failure("window creation");
 SDL_SetWindowMinimumSize(window,std::min(680,w),std::min(492,h));
 SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
 // No game graphics or vendor SDK is needed to show this screen. Software is
 // a fallback for machines whose desktop renderer cannot create a D3D device.
 auto* renderer=SDL_CreateRenderer(window,"direct3d11");
 if(!renderer)renderer=SDL_CreateRenderer(window,"software");
 if(!renderer)return failure("desktop renderer creation");
 SDL_SetRenderVSync(renderer,1);
 IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
 const char* base=SDL_GetBasePath();if(!base)return failure("portable path");
 std::string fontpath=std::string(base)+"assets/fonts/NotoSerifSC.otf";
 ImFont* font=io.Fonts->AddFontFromFileTTF(fontpath.c_str(),26);
 if(!font){std::fprintf(stderr,"Startup UI: missing bundled Chinese font\n");return failure("font");}
 const auto settings_path=(std::filesystem::u8path(base)/".."/"settings").lexically_normal();
 BbControllerSetup::Editor editor(settings_path);
 bool manual_open=false;std::string manual_message;
 std::map<SDL_JoystickID,SDL_Gamepad*> opened_pads;
 io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer3_Init(renderer);
 ImGui::StyleColorsDark();SDL_ShowWindow(window);
 int choice=args["--initial"]=="1"?1:0,result=20,accepted=-1;
 bool done=false,unavailable=false;Uint64 released_since=0,last_axis=0;int axis_direction=0;
 const int vendor=std::atoi(args["--vendor"].c_str());
 const std::string vendor_name=vendor==0x1002?"AMD · A卡":vendor==0x10de?"NVIDIA · N卡":vendor==0x8086?"Intel 显卡":"其他显卡";
 const std::string upscaler=args["--upscaler"]=="fsr411"?"FSR 4.1.1（实验性）":args["--upscaler"]=="dlss"?"DLSS 超分辨率":args["--upscaler"]=="fsr3"?(vendor==0x1002?"FSR 3.1（A卡默认兼容方案）":"FSR 3.1"):args["--upscaler"];
 while(!done){
  const Uint64 frame_start=SDL_GetTicks();bool confirm=false;
  SDL_Event e;
  while(SDL_PollEvent(&e)){
   ImGui_ImplSDL3_ProcessEvent(&e);
   if(e.type==SDL_EVENT_QUIT||e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED){done=true;result=20;}
   if(accepted>=0)continue;
   if(manual_open){
    const bool was_capturing=editor.armed>=0;
    if(editor.capture(e))continue;
    if(!was_capturing&&e.type==SDL_EVENT_KEY_DOWN&&e.key.key==SDLK_ESCAPE){editor.discard();manual_open=false;manual_message=editor.status;}
    continue;
   }
   if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat){
    if(e.key.key==SDLK_UP||e.key.key==SDLK_DOWN||e.key.key==SDLK_LEFT||e.key.key==SDLK_RIGHT)choice=1-choice;
    if(e.key.key==SDLK_RETURN||e.key.key==SDLK_KP_ENTER)confirm=true;
    if(e.key.key==SDLK_ESCAPE){done=true;result=20;}
   }
   if(e.type==SDL_EVENT_MOUSE_WHEEL&&e.wheel.y!=0)choice=1-choice;
   if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP||e.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)choice=1-choice;
    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)confirm=true;
    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_EAST){done=true;result=20;}
   }
   if(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&e.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY){
    int dir=e.gaxis.value>18000?1:e.gaxis.value<-18000?-1:0;
    if(dir&&(dir!=axis_direction||SDL_GetTicks()-last_axis>350)){choice=1-choice;last_axis=SDL_GetTicks();}axis_direction=dir;
   }
  }
  if(done)break;
  if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED){SDL_Delay(30);continue;}
  ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
  const bool pad=connected();
  int pad_count=0;auto* pad_ids=SDL_GetGamepads(&pad_count);
  if(manual_open){for(const auto& item:opened_pads)SDL_CloseGamepad(item.second);opened_pads.clear();}
  else for(int i=0;pad_ids&&i<pad_count;i++)if(!opened_pads.contains(pad_ids[i]))if(auto* p=SDL_OpenGamepad(pad_ids[i]))opened_pads.emplace(pad_ids[i],p);
  SDL_free(pad_ids);
  for(auto it=opened_pads.begin();it!=opened_pads.end();)if(!SDL_GamepadConnected(it->second)){SDL_CloseGamepad(it->second);it=opened_pads.erase(it);}else ++it;
  const float s=std::min(io.DisplaySize.x/1040,io.DisplaySize.y/752);
  const ImVec2 origin((io.DisplaySize.x-1040*s)*.5f,(io.DisplaySize.y-752*s)*.5f);
  auto pt=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
  ImGui::SetNextWindowPos(origin);ImGui::SetNextWindowSize(ImVec2(1040*s,752*s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::PushFont(font,26);
  ImGui::Begin("startup-contract",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|(manual_open?0:ImGuiWindowFlags_NoNavInputs)|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar);
  auto* d=ImGui::GetWindowDrawList();
  auto text=[&](float x,float y,const std::string& value,ImU32 color,float size=25.f,float maxwidth=920.f){
   float measured=font->CalcTextSizeA(size,100000,0,value.c_str()).x;
   float actual=size*std::min(1.f,maxwidth/std::max(1.f,measured));
   d->AddText(font,actual*s,pt(x+1,y+1),IM_COL32(0,0,0,240),value.c_str());d->AddText(font,actual*s,pt(x,y),color,value.c_str());
  };
  d->AddRectFilledMultiColor(pt(0,0),pt(1040,752),IM_COL32(23,24,22,255),IM_COL32(19,20,19,255),IM_COL32(6,8,8,255),IM_COL32(6,8,8,255));
  d->AddRect(pt(10,10),pt(1030,742),IM_COL32(162,151,121,150));d->AddRect(pt(17,17),pt(1023,735),IM_COL32(120,114,97,55));
  for(int side=0;side<2;side++)for(int bottom=0;bottom<2;bottom++){
   float x=side?1017:23,y=bottom?729:23,dx=side?-1:1,dy=bottom?-1:1;
   d->AddLine(pt(x,y),pt(x+dx*45,y),ink);d->AddLine(pt(x,y),pt(x,y+dy*30),ink);
  }
  for(unsigned i=0;i<160;i++){float x=24+i*317%990,y=24+i*193%704;d->AddLine(pt(x,y),pt(std::min(x+float(i%25+4),1014.f),y),IM_COL32(175,164,133,7));}
  auto rule=[&](float y){d->AddLine(pt(55,y),pt(985,y),IM_COL32(164,153,126,110));d->AddLine(pt(67,y+3),pt(973,y+3),IM_COL32(111,106,89,50));};
  text(64,41,manual_open?"手柄手动配置":"猎人契约",ink,39);text(818,52,"启程之前",muted,25);rule(102);
  if(manual_open){
   if(BbControllerSetup::Draw(editor,origin,s,font,ink,gold,muted)){manual_open=false;manual_message=editor.status;if(editor.profile.manual)choice=1;}
  }else{
  text(70,123,"硬件检查已完成",gold,27);text(750,125,vendor_name,ink,23,230);
  text(70,166,args["--gpu"],ink,25,900);
  text(70,206,args["--cpu"]+"  ·  内存 "+std::to_string(std::atoi(args["--ram"].c_str())/1024)+" GiB  ·  显存 "+std::to_string(std::atoi(args["--vram"].c_str())/1024)+" GiB",muted,20,900);
  text(70,243,"当前渲染："+upscaler,ink,22);
  text(70,277,std::string("神经增强：")+(args["--neural"]=="1"?"开启":"关闭")+"     硬件光追："+(args["--rt"]=="1"?"开启":"关闭")+"     可在游戏内系统设置调整",muted,20,900);
  if(vendor==0x1002)text(70,298,args["--upscaler"]=="fsr411"?"实验性 FSR 4.1.1：若有马赛克、重影或闪烁，请改用 FSR 3.1。":"A卡默认使用兼容方案；实验性 FSR 4.1.1 可在设置中手动试用。",gold,17,900);
  rule(317);
  text(70,339,"选择本次游玩的操作方式",ink,28);
  for(int i=0;i<2;i++){
   float y=392+i*83.f;ImGui::SetCursorScreenPos(pt(58,y));ImGui::PushID(i);
   bool click=ImGui::InvisibleButton("choice",ImVec2((i?730:924)*s,70*s));
   if(accepted<0&&(click||(ImGui::IsItemHovered()&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0))))choice=i;
   if(choice==i){d->AddRectFilledMultiColor(pt(58,y),pt(982,y+70),IM_COL32(143,121,72,12),IM_COL32(143,121,72,70),IM_COL32(143,121,72,70),IM_COL32(143,121,72,12));d->AddLine(pt(84,y+69),pt(954,y+69),IM_COL32(192,167,113,120));d->AddTriangleFilled(pt(76,y+27),pt(76,y+35),pt(83,y+31),gold);}
   text(106,y+16,i==0?"键盘与鼠标":"手柄",choice==i?gold:ink,29);
   text(388,y+21,i==0?"鼠标镜头 · 左键攻击 · Ctrl 格挡":pad?(args["--guard-mode"]=="1"?"已连接 · L1＋L2 格挡":"已连接 · 按住 L1／LB 格挡"):editor.profile.manual?"手动模式 · 可稍后连接":"未连接 · 可点击右侧手动配置",muted,21,i?385:570);ImGui::PopID();
  }
  ImGui::SetCursorScreenPos(pt(800,491));const bool manual_click=ImGui::InvisibleButton("manual-controller",ImVec2(171*s,39*s));
  d->AddRectFilled(pt(800,491),pt(971,530),IM_COL32(92,79,53,150));d->AddRect(pt(800,491),pt(971,530),IM_COL32(164,150,118,130));text(816,499,"手动配置手柄",gold,22,145);
  if(manual_click&&accepted<0){manual_open=true;unavailable=false;}
  text(70,568,accepted>=0?"选择已确认，请松开按键，正在准备进入……":unavailable?"尚未识别到手柄；可手动配置，或选择稍后连接进入。":manual_message.empty()?"游戏尚未加载；确认操作方式后才会进入主界面。":manual_message,unavailable?gold:muted,21,910);
  auto button=[&](float x,float width,const char* id,const char* label,bool primary){
   ImGui::SetCursorScreenPos(pt(x,612));bool click=ImGui::InvisibleButton(id,ImVec2(width*s,51*s));
   bool hover=ImGui::IsItemHovered();d->AddRectFilled(pt(x,612),pt(x+width,663),primary?IM_COL32(99,86,58,hover?175:100):IM_COL32(55,55,48,hover?180:80));
   d->AddRect(pt(x,612),pt(x+width,663),IM_COL32(161,147,115,120));text(x+23,622,label,primary?gold:ink,25,width-35);return click;
  };
  bool go=button(600,370,"enter","确认并进入游戏",true),cancel=button(70,235,"cancel","取消启动",false);
  text(70,693,"左键 / Enter / A（×）确认    F2 开关指引    F3 更改操作方式",muted,19,910);
  if(cancel){done=true;result=20;}
  if(accepted<0&&(confirm||go)&&!done&&!manual_open){if(choice==1&&!pad&&!editor.profile.manual)unavailable=true;else{accepted=choice;unavailable=false;std::printf("Startup UI: confirmed %s; waiting for input release\n",choice?"controller":"keyboard/mouse");std::fflush(stdout);}}
  if(accepted==1&&!pad&&!editor.profile.manual){accepted=-1;unavailable=true;released_since=0;}
  if(accepted>=0){if(released()){if(!released_since)released_since=SDL_GetTicks();if(SDL_GetTicks()-released_since>=250){done=true;result=10+accepted;}}else released_since=0;}
  }
  ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(2);ImGui::Render();
  SDL_SetRenderScale(renderer,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);SDL_SetRenderDrawColor(renderer,5,7,7,255);SDL_RenderClear(renderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
  // Bound an idle confirmation screen's power usage even on a high-refresh panel.
  Uint64 elapsed=SDL_GetTicks()-frame_start;if(elapsed<32)SDL_Delay(Uint32(32-elapsed));
 }
 for(const auto& item:opened_pads)SDL_CloseGamepad(item.second);
 ImGui_ImplSDLRenderer3_Shutdown();ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
 editor.shutdown();SDL_Quit();
 std::printf("Startup UI: finished with code %d\n",result);return result;
}
