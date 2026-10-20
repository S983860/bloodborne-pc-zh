// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace BbGameUiModel {
inline bool BeginnerArea(std::uint32_t block){return (block>>24)==24 && ((block>>16)&255)<=1;}
struct Direction {float x,y;};
// Character yaw zero faces world -Z. The north-up map projects +Z upwards.
inline Direction Heading(float yaw){return {-std::sin(yaw),std::cos(yaw)};}
inline bool CanSelect(int mode,bool connected,bool manual=false){return mode==0 || (mode==1&&(connected||manual));}
struct GuideLayout {float x,y,scale,width,height;};
inline GuideLayout LeftGuide(float width,float height){
    const float scale=std::min(width/1920.f,height/1080.f);
    constexpr float panel_width=346,panel_height=432;
    const float y=std::max(180.f*scale,std::min((height-panel_height*scale)*.5f,height-24.f*scale-panel_height*scale));
    return {30.f*scale,y,scale,panel_width*scale,panel_height*scale};
}
}
