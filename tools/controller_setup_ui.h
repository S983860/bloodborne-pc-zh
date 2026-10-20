// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "controller_setup.h"
#include "imgui.h"
namespace BbControllerSetup {
inline bool Draw(Editor& editor,ImVec2 origin,float scale,ImFont* font,ImU32 ink,ImU32 gold,ImU32 muted){
 auto* d=ImGui::GetWindowDrawList();auto pt=[&](float x,float y){return ImVec2(origin.x+x*scale,origin.y+y*scale);};
 auto text=[&](float x,float y,const std::string& value,ImU32 color,float size=22.f,float width=900.f){
  const float measured=font->CalcTextSizeA(size,100000,0,value.c_str()).x,actual=size*std::min(1.f,width/std::max(1.f,measured));
  d->AddText(font,actual*scale,pt(x+1,y+1),IM_COL32(0,0,0,230),value.c_str());d->AddText(font,actual*scale,pt(x,y),color,value.c_str());
 };
 auto button=[&](float x,float y,float width,const char* id,const char* label,bool primary=false){
  ImGui::SetCursorScreenPos(pt(x,y));const bool clicked=ImGui::InvisibleButton(id,ImVec2(width*scale,38*scale));const bool hover=ImGui::IsItemHovered();
  d->AddRectFilled(pt(x,y),pt(x+width,y+38),primary?IM_COL32(108,91,58,hover?185:110):IM_COL32(59,57,47,hover?170:85));
  d->AddRect(pt(x,y),pt(x+width,y+38),IM_COL32(161,147,115,120));text(x+14,y+7,label,primary?gold:ink,21,width-26);return clicked;
 };
 ImGui::PushFont(font,21*scale);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(9*scale,6*scale));
 ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(44,43,35,230));ImGui::PushStyleColor(ImGuiCol_PopupBg,IM_COL32(18,20,18,252));
 ImGui::PushStyleColor(ImGuiCol_Header,IM_COL32(105,90,58,120));ImGui::PushStyleColor(ImGuiCol_HeaderHovered,IM_COL32(120,102,64,170));
 ImGui::PushStyleColor(ImGuiCol_Text,ink);ImGui::PushStyleColor(ImGuiCol_SliderGrab,gold);
 ImGui::PushStyleColor(ImGuiCol_Button,IM_COL32(87,77,53,170));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(126,108,70,195));
 ImGui::PushStyleColor(ImGuiCol_ButtonActive,IM_COL32(140,117,72,220));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,IM_COL32(75,68,49,235));
 ImGui::PushStyleColor(ImGuiCol_FrameBgActive,IM_COL32(96,83,54,245));ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,gold);
 auto devices=Devices();
 if(!editor.selected&&!devices.empty()){
  auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& item){return item.guid==editor.profile.guid;});editor.select(found==devices.end()?devices.front().id:found->id);
 }
 text(70,121,"选择设备",ink,23);ImGui::SetCursorScreenPos(pt(181,116));ImGui::SetNextItemWidth(578*scale);
 const std::string selected=editor.attached()?editor.name:"未连接 · 可稍后连接进入";
 if(ImGui::BeginCombo("##manual-device",selected.c_str())){
  for(const auto& item:devices){ImGui::PushID(int(item.id));if(ImGui::Selectable((item.name+(item.mapped?" · 已识别":" · 需要手动绑定")).c_str(),item.id==editor.selected))editor.select(item.id);ImGui::PopID();}ImGui::EndCombo();
 }
 if(button(790,116,180,"scan-devices","重新扫描")){SDL_UpdateJoysticks();editor.status="设备列表已刷新；新连接的设备会自动出现。";}
 text(70,164,editor.attached()?"逐项点击动作后按键；摇杆按提示方向推动。右键可清除绑定。":"系统未列出手柄：请检查USB / 蓝牙连接，或先选择稍后连接。",muted,20,700);
 bool close=false;
 if(button(790,163,180,"auto-device","恢复自动选择")&&editor.automatic())close=true;
 text(70,202,editor.armed>=0?std::string("正在绑定：")+Bindings[editor.armed].prompt:editor.status.empty()?"可保留当前布局，也可手动更改；设置保存在游戏文件夹内。":editor.status,editor.armed>=0?gold:muted,21,910);
 for(int i=0;i<BindingCount;i++){
  const float x=i<10?70:550,y=239+(i%10)*29.f;ImGui::SetCursorScreenPos(pt(x,y));ImGui::PushID(i);
  const bool clicked=ImGui::InvisibleButton("binding",ImVec2(420*scale,27*scale)),hovered=ImGui::IsItemHovered();
  if(hovered||editor.armed==i)d->AddRectFilled(pt(x,y),pt(x+420,y+27),IM_COL32(112,96,63,editor.armed==i?125:60));
  d->AddLine(pt(x,y+27),pt(x+420,y+27),IM_COL32(133,123,100,40));
  text(x+7,y+3,Bindings[i].name,ink,19,312);
  text(x+326,y+3,editor.values[i].empty()?"未绑定":editor.values[i],editor.armed==i?gold:muted,18,87);
  if(clicked)editor.arm(i);
  if(hovered&&ImGui::IsMouseClicked(1)){editor.values[i].clear();editor.armed=-1;editor.status="已清除此项，请重新绑定。";}
  ImGui::PopID();
 }
 text(70,544,"按键图标",ink,21);ImGui::SetCursorScreenPos(pt(181,539));ImGui::SetNextItemWidth(230*scale);
 const char* icon_keys[]={"auto","playstation","xbox"};const char* icon_names[]={"自动","PlayStation","Xbox"};int icons=std::strcmp(editor.profile.icons,"playstation")==0?1:std::strcmp(editor.profile.icons,"xbox")==0?2:0;
 if(ImGui::BeginCombo("##pad-icons",icon_names[icons])){for(int i=0;i<3;i++)if(ImGui::Selectable(icon_names[i],icons==i))std::strcpy(editor.profile.icons,icon_keys[i]);ImGui::EndCombo();}
 text(470,544,"摇杆死区",ink,21);ImGui::SetCursorScreenPos(pt(583,539));ImGui::SetNextItemWidth(387*scale);ImGui::SliderInt("##deadzone",&editor.profile.deadzone,0,30,"%d%%");
 if(button(70,590,200,"calibrate-center","校准中立位置"))editor.calibrate();
 if(button(290,590,200,"preview-layout","应用并测试"))editor.preview();
 const auto axes=editor.axes();char live[150];std::snprintf(live,sizeof live,"左摇杆 %d / %d    右摇杆 %d / %d",axes[0],axes[1],axes[2],axes[3]);text(520,596,live,muted,19,450);
 if(button(70,652,150,"back-manual","返回")){editor.discard();close=true;}
 if(button(240,652,360,"wait-pad","稍后连接，以手柄模式进入")&&editor.save(true))close=true;
 if(button(620,652,350,"save-manual","保存并返回",true)&&editor.save())close=true;
 text(70,710,"默认按住L1／LB格挡，按左摇杆变形；游戏控制页可选原组合。摇杆方向错误可重新绑定。",muted,18,910);
 ImGui::PopStyleColor(12);ImGui::PopStyleVar();ImGui::PopFont();return close;
}
}
