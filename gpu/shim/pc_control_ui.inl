// SPDX-License-Identifier: GPL-3.0-or-later
// Included in pc_game_ui.cpp's private namespace; all UI stays on the render thread.
bool pad_connected(){int count=0;auto* ids=SDL_GetGamepads(&count);SDL_free(ids);return count>0;}
bool playstation(){const auto& settings=BbSettings::Get();if(settings.pad_icons==BbSettings::PadIconsPlayStation)return true;if(settings.pad_icons==BbSettings::PadIconsXbox)return false;const int type=runtime_pad_type();return type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5;}
void choose_controls(){
 choice_can_cancel=true;choice_return_menu=opened;choice_return_selected=selected;
 choosing=true;choice=BbSettings::Get().control_mode;choice_unavailable=false;
 opened=false;menu_nav=0;menu_adjust=0;menu_confirm=false;
 BbEngine::PcHud::Pause(BbEngine::PcHud::InWorld());
}
void cancel_control_choice(){
 choosing=false;choice_unavailable=false;opened=choice_return_menu;selected=choice_return_selected;
 release_until=SDL_GetTicks()+250;menu_nav=0;menu_adjust=0;menu_confirm=false;
 BbEngine::PcHud::Pause(opened && BbEngine::PcHud::InWorld());
}
void control_choice(){
 auto& io=ImGui::GetIO();const float s=std::min(io.DisplaySize.x/1920,io.DisplaySize.y/1080);
 if(choice<0)choice=BbSettings::Get().control_mode;
 choice=(choice+menu_nav.exchange(0)%2+2)%2;
 if(io.MouseWheel!=0)choice=1-choice;
 const bool connected=pad_connected(),enter=menu_confirm.exchange(false);
 const char* override_mode=std::getenv("BB_ALLOW_UNDETECTED_PAD");const bool manual=override_mode&&std::strcmp(override_mode,"1")==0;
 const ImVec2 origin((io.DisplaySize.x-1120*s)/2,(io.DisplaySize.y-650*s)/2);
 ImGui::SetNextWindowPos(origin);ImGui::SetNextWindowSize(ImVec2(1120*s,650*s));
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
 ImGui::PushFont(menu_font,23.f);
 ImGui::Begin("猎人契约##control-choice",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar);
 auto* d=ImGui::GetWindowDrawList();auto pt=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
 auto text=[&](float x,float y,const char* value,ImU32 color,float size=28.f){d->AddText(ImGui::GetFont(),size*s,pt(x+1,y+1),IM_COL32(0,0,0,230),value);d->AddText(ImGui::GetFont(),size*s,pt(x,y),color,value);};
 ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(0,0,0,180));
 d->AddRectFilledMultiColor(origin,pt(1120,650),IM_COL32(20,21,19,252),IM_COL32(20,21,19,252),IM_COL32(5,7,7,252),IM_COL32(5,7,7,252));
 d->AddRect(pt(1,1),pt(1119,649),IM_COL32(153,145,120,150));d->AddRect(pt(8,8),pt(1112,642),IM_COL32(128,120,100,50));
 for(int side=0;side<2;side++)for(int bottom=0;bottom<2;bottom++){float x=side?1106:14,y=bottom?636:14,dx=side?-1:1,dy=bottom?-1:1;d->AddLine(pt(x,y),pt(x+dx*52,y),ink);d->AddLine(pt(x,y),pt(x,y+dy*34),ink);}
 for(unsigned i=0;i<100;i++){float x=20+i*317%1080,y=20+i*193%610;d->AddLine(pt(x,y),pt(std::min(x+float(i%23+5),1100.f),y),IM_COL32(175,164,133,6));}
 text(65,38,"猎人契约",ink,42);text(870,50,"操作方式",ink,26);
 d->AddLine(pt(55,99),pt(1065,99),IM_COL32(166,156,127,110));
 text(75,130,"选择本次游玩的操作方式",ink,30);
 int picked=-1;
 for(int i=0;i<2;i++){
  const float y=202+i*108.f;
  ImGui::SetCursorScreenPos(pt(58,y));ImGui::PushID(i);bool click=ImGui::InvisibleButton("control",ImVec2(1004*s,91*s));
  if(ImGui::IsItemHovered()&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0||ImGui::IsMouseClicked(0)))choice=i;
  if(choice==i){d->AddRectFilledMultiColor(pt(58,y),pt(1062,y+90),IM_COL32(143,121,72,12),IM_COL32(143,121,72,58),IM_COL32(143,121,72,58),IM_COL32(143,121,72,12));d->AddLine(pt(86,y+88),pt(1020,y+88),IM_COL32(192,167,113,100));d->AddTriangleFilled(pt(72,y+31),pt(72,y+39),pt(79,y+35),gold);}
  text(104,y+16,i==0?"键盘与鼠标":"手柄",choice==i?gold:ink,34);
  text(430,y+20,i==0?"鼠标转镜头 · 左键攻击 · Ctrl 格挡":connected?(BbSettings::Get().pad_guard_mode==0?"已连接 · 按住 L1 / LB 格挡":"已连接 · L1 + L2 格挡"):manual?"手动模式 · 可稍后连接手柄":"未连接 · 请连接手柄后选择",ink,25);
  ImGui::PopID();if(click||(enter&&choice==i))picked=i;
 }
 text(77,445,choice_unavailable?"尚未检测到手柄，也可选择键盘与鼠标进入。":choice_can_cancel?"Esc / 右键 / B（○）返回，保留原操作方式。":"进入前请确认选择；游玩中按 F3 更改操作方式。",ink,25);
 if(const char* gpu=std::getenv("BB_GPU_NAME"))text(77,487,gpu,IM_COL32(152,148,134,240),23);
 const auto& settings=BbSettings::Get();char mode[180];
 std::snprintf(mode,sizeof mode,"神经增强：%s　硬件光追：%s　（可在系统 → PC图形中查看）",settings.dlss_nr_enabled?"开启":"关闭",settings.hardware_rt_enabled?"开启":"关闭");
 text(77,522,mode,IM_COL32(152,148,134,240),23);
 d->AddLine(pt(55,565),pt(1065,565),IM_COL32(166,156,127,90));
 text(77,593,"左键 / Enter / 手柄 A（×）确认       滚轮 / 方向键选择",ink,24);
 if(picked>=0){
  if(BbGameUiModel::CanSelect(picked,connected,manual)){
   auto& settings=BbSettings::Get();settings.control_mode=picked;settings.button_prompts=picked?BbSettings::PromptsController:BbSettings::PromptsKeyboard;BbSettings::Save();
   choosing=false;release_until=SDL_GetTicks()+250;menu_confirm=false;menu_nav=0;BbEngine::PcHud::Pause(false);
   std::printf("Controls: startup selection confirmed: %s\n",picked?"controller":"keyboard/mouse");
  }else choice_unavailable=true;
 }
 ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(2);
}
void beginner_hints(){
 const auto& settings=BbSettings::Get();if(!settings.beginner_hints)return;
 const auto world=BbEngine::PcHud::ReadSummary();if(!world.valid||!BbGameUiModel::BeginnerArea(world.block))return;
 const auto& io=ImGui::GetIO();const auto layout=BbGameUiModel::LeftGuide(io.DisplaySize.x,io.DisplaySize.y);
 const float s=layout.scale,x=layout.x,y=layout.y;
 auto* d=ImGui::GetBackgroundDrawList();auto pt=[&](float a,float b){return ImVec2(x+a*s,y+b*s);};
 // Fully transparent HUD: no panel fill, border or mouse-catching window.
 // A narrow left column stays below health/items and clear of the centre/map.
 ImGui::PushFont(menu_font,21.f);
 auto text=[&](float a,float b,const char* value,ImU32 color,float size=20.f){
  d->AddText(ImGui::GetFont(),size*s,pt(a+1,b+1),IM_COL32(0,0,0,210),value);
  d->AddText(ImGui::GetFont(),size*s,pt(a,b),color,value);
 };
 text(0,0,settings.control_mode==1?"猎人指引 · 手柄":"猎人指引 · 键鼠",IM_COL32(174,165,139,200),18);
 const bool pad=settings.control_mode==1,ps=playstation();
 const char* labels[]={"移动 / 镜头","普通攻击","重击 / 蓄力","射击 / 枪反","锁定敌人","闪避 / 奔跑","近战格挡","武器变形","切换右 / 左武器","切换 / 使用道具","使用采血瓶","交互","装备栏","系统菜单"};
 const int bindings[]={-1,kBindAttack,kBindStrong,kBindFirearm,kBindLockOn,kBindRoll,kBindMeleeGuard,kBindTransform,-1,kBindUseItem,kBindBloodVial,kBindInteract,kBindMenu,-1};
 const bool single_guard=settings.pad_guard_mode==0;
 const char* xbox[]={"双摇杆","RB","RT（按住蓄力）","LT","按右摇杆","B / 按住 B 移动",single_guard?"按住 LB":"按住 LB + LT",single_guard?"按左摇杆":"LB","十字右 / 左","十字下 / X","Y","A","View / Back","Menu / Start"};
 const char* sony[]={"双摇杆","R1","R2（按住蓄力）","L2","按右摇杆","○ / 按住○移动",single_guard?"按住 L1":"按住 L1 + L2",single_guard?"按左摇杆":"L1","十字右 / 左","十字下 / □","△","×","触摸板左侧","Options"};
 for(int i=0;i<14;i++){
  char value[128]{};
  if(pad)std::snprintf(value,sizeof value,"%s",ps?sony[i]:xbox[i]);
  else if(i==0){char f[16],b[16],l[16],r[16];host_binding_describe(kBindMoveF,f,sizeof f);host_binding_describe(kBindMoveB,b,sizeof b);host_binding_describe(kBindMoveL,l,sizeof l);host_binding_describe(kBindMoveR,r,sizeof r);std::snprintf(value,sizeof value,"%s%s%s%s / 鼠标",f,b,l,r);}
  else if(i==5){char roll[48],sprint[48];host_binding_describe(kBindRoll,roll,sizeof roll);host_binding_describe(kBindStrongMod,sprint,sizeof sprint);std::snprintf(value,sizeof value,"%s / %s",roll,sprint);}
  else if(i==8){char right[48],left[48];host_binding_describe(kBindSwitchRight,right,sizeof right);host_binding_describe(kBindSwitchLeft,left,sizeof left);std::snprintf(value,sizeof value,"%s / %s",right,left);}
  else if(i==9){char select[48],use[48];host_binding_describe(kBindSwitchItem,select,sizeof select);host_binding_describe(kBindUseItem,use,sizeof use);std::snprintf(value,sizeof value,"%s / %s",select,use);}
  else if(i==13)std::snprintf(value,sizeof value,"Esc");
  else host_binding_describe(bindings[i],value,sizeof value);
  constexpr float key_width=143;
  const float measured=ImGui::GetFont()->CalcTextSizeA(18,10000,0,value).x;
  const float font_size=std::min(18.f,18.f*key_width/std::max(key_width,measured));
  const float row=30+i*25.f;
  const bool badge=std::strlen(value)<=5;
  if(badge){
   const float w=std::max(28.f,measured+14);
   d->AddRect(pt(0,row-2),pt(w,row+21),IM_COL32(175,163,129,150),5*s,0,s);
   text((w-measured)*.5f,row,value,IM_COL32(205,195,166,230),font_size);
  }else text(0,row,value,IM_COL32(205,195,166,230),font_size);
  text(155,row,labels[i],IM_COL32(203,198,180,228),20);
 }
 text(0,389,"格挡需面向敌人，仅限锯齿砍刀",IM_COL32(170,163,140,185),16);
 text(0,412,"F2 隐藏指引   F3 更改操作方式",IM_COL32(174,165,139,195),16);
 ImGui::PopFont();
}
