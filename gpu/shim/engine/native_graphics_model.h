// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../bbport_settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace BbEngine::NativeGraphics {
enum Option { Output, Upscaler, Preset, Sharpness, Reset, NrEnabled, NrIntensity,
    NrTone, NrStructure, SrModel, RtEnabled, RtStrength, RtRadius, RtSamples,
    RtDetail, PictureEnabled, Bloom, Fog, Ambient, Shadow };
struct Row {Option option;const char* label;const char* help;};
inline constexpr int PageCount=4,RowsPerPage=5;
inline constexpr const char* Pages[]={"显示与画质","DLSS 5","硬件光追","画面效果"};
inline constexpr Row Rows[PageCount][RowsPerPage]={
 {{Output,"输出分辨率","自动适配屏幕可保持正确比例并铺满全屏；固定分辨率与屏幕比例不同时会留黑边。重启后生效。"},
  {Upscaler,"超分辨率","选择画面重建方式；DLSS 5 神经增强需要选用 DLSS。"},
  {Preset,"超分质量","更高质量保留更多细节，也会增加显卡负担；重启后生效。"},
  {Sharpness,"画面锐化","调整画面清晰度；降到零可关闭锐化。"},
  {Reset,"重置增强参数","恢复神经增强、光追和锐化默认值；不更改分辨率、超分档位或键位。再次确认执行。"}},
 {{NrEnabled,"DLSS 5 神经增强","开启或关闭神经增强。关闭后保留 DLSS 超分；仅在选用 DLSS 时生效。"},
  {NrIntensity,"效果强度","调整神经增强对画面的影响程度。"},
  {NrTone,"局部光照","调整局部明暗与光照效果的影响程度。"},
  {NrStructure,"结构细节","调整神经增强的结构细节保留程度。"},
  {SrModel,"超分模型","选择自动、流畅、均衡或细节模型；切换时可能短暂停顿。"}},
 {{RtEnabled,"硬件光追","开启或关闭可见场景的环境遮蔽与接触阴影。"},
  {RtStrength,"阴影强度","调整接触阴影深浅。"},
  {RtRadius,"作用范围","调整环境遮蔽的作用范围。"},
  {RtSamples,"采样质量","提高采样可减少噪点，也会增加显卡负担。"},
  {RtDetail,"场景细节","调整参与环境遮蔽的可见场景细节；较高档位更耗性能。"}},
 {{PictureEnabled,"画面调整","开启或关闭泛光、雾气等画面效果；色彩在 PC 色彩中单独设置。"},
  {Bloom,"泛光","调整明亮区域的光晕。"},
  {Fog,"雾气","调整场景雾气浓度。"},
  {Ambient,"环境遮蔽","调整游戏原有环境遮蔽的强度。"},
  {Shadow,"阴影距离","调整游戏原有动态阴影的距离。"}}
};
inline int Cycle(int value,int delta,int count){return (value+delta%count+count)%count;}
inline std::string Number(float value,bool percent=false){
 char text[32];if(percent)std::snprintf(text,sizeof text,"%.0f%%",value*100);
 else std::snprintf(text,sizeof text,"%.2f",value);return text;
}
inline std::string Value(Option option){
 const auto& s=BbSettings::Get();
 const auto on=[](bool value){return value?"开启":"关闭";};
 switch(option){
 case Output:return (s.output_res==BbSettings::OutputAuto?"自动适配 ":"")+std::to_string(BbSettings::OutputWidth(s.output_res))+"×"+std::to_string(BbSettings::OutputHeight(s.output_res));
 case Upscaler:{const char* names[]={"关闭","FSR 3.1","FSR 4","FSR 4.1.1","TAA 抗锯齿","DLSS"};return names[std::clamp(s.upscaler.load(),0,BbSettings::UpscalerCount-1)];}
 case Preset:{const char* names[]={"原生抗锯齿","质量","均衡","性能","极致性能"};return names[std::clamp(s.preset.load(),0,BbSettings::PresetCount-1)];}
 case Sharpness:return s.sharpen?Number(s.sharpness):"关闭";
 case Reset:return "确认";
 case NrEnabled:return s.gpu_vendor!=0x10de?"当前显卡不适用":on(s.dlss_nr_enabled);
 case NrIntensity:return Number(s.dlss_nr_intensity,true);
 case NrTone:return Number(s.dlss_nr_tone,true);
 case NrStructure:return Number(s.dlss_nr_structure,true);
 case SrModel:{const char* names[]={"自动","流畅（K）","均衡（L）","细节（M）"};return names[std::clamp(s.dlss_sr_model.load(),0,3)];}
 case RtEnabled:return s.gpu_vendor!=0x10de?"兼容模式关闭":on(s.hardware_rt_enabled);
 case RtStrength:return Number(s.rt_strength,true);
 case RtRadius:return Number(s.rt_radius);
 case RtSamples:return s.rt_samples<=1?"流畅":s.rt_samples<=2?"标准":"细腻";
 case RtDetail:{const char* names[]={"低","中","高"};return names[std::clamp(s.rt_detail.load(),0,2)];}
 case PictureEnabled:return on(s.graphics_controls);
 case Bloom:return Number(s.graphics_bloom);
 case Fog:return Number(s.graphics_fog);
 case Ambient:return Number(s.graphics_ao_strength);
 case Shadow:return Number(s.graphics_shadow_scale);
 }
 return {};
}
template<class T> inline bool Store(std::atomic<T>& target,T value){if(target.load()==value)return false;target=value;return true;}
inline bool Step(Option option,int delta){
 auto& s=BbSettings::Get();delta=delta<0?-1:1;
 const auto step=[&](std::atomic<float>& v,float amount,float low,float high){return Store(v,std::clamp(v.load()+amount*delta,low,high));};
 switch(option){
 case Output:return Store(s.output_res,Cycle(s.output_res,delta,BbSettings::OutputCount));
 case Upscaler:{int value=s.upscaler;for(int i=0;i<BbSettings::UpscalerCount;++i){value=Cycle(value,delta,BbSettings::UpscalerCount);
   if((value==BbSettings::UpscalerDlss && !s.dlss_supported) || (value==BbSettings::UpscalerFsr4 && !s.fsr4_supported) ||
      (value==BbSettings::UpscalerFsr411 && !s.fsr411_supported)) {
       continue;
   }
   return Store(s.upscaler,value);
  }
  return false;
 }
 case Preset:return Store(s.preset,Cycle(s.preset,delta,BbSettings::PresetCount));
 case Sharpness:{const float value=std::clamp((s.sharpen?s.sharpness.load():0.f)+.1f*delta,0.f,2.f);const bool flag=Store(s.sharpen,value>0);return Store(s.sharpness,value)||flag;}
 case Reset:s.dlss_nr_enabled=false;s.hardware_rt_enabled=false;s.dlss_nr_intensity=s.dlss_nr_tone=s.dlss_nr_structure=1.f;
   s.rt_strength=.28f;s.rt_radius=1.25f;s.rt_samples=2;s.rt_detail=2;s.sharpen=true;s.sharpness=.3f;return true;
 case NrEnabled:return s.gpu_vendor==0x10de && Store(s.dlss_nr_enabled,!s.dlss_nr_enabled);
 case NrIntensity:return step(s.dlss_nr_intensity,.1f,0.f,1.f);
 case NrTone:return step(s.dlss_nr_tone,.1f,0.f,1.f);
 case NrStructure:return step(s.dlss_nr_structure,.1f,0.f,1.f);
 case SrModel:return Store(s.dlss_sr_model,Cycle(s.dlss_sr_model,delta,4));
 case RtEnabled:return s.gpu_vendor==0x10de && s.hardware_rt_supported && Store(s.hardware_rt_enabled,!s.hardware_rt_enabled);
 case RtStrength:return step(s.rt_strength,.05f,0.f,1.f);
 case RtRadius:return step(s.rt_radius,.25f,.1f,3.f);
 case RtSamples:{const int index=s.rt_samples<=1?0:s.rt_samples<=2?1:2;const int levels[]={1,2,4};return Store(s.rt_samples,levels[Cycle(index,delta,3)]);}
 case RtDetail:return Store(s.rt_detail,Cycle(s.rt_detail,delta,3));
 case PictureEnabled:return Store(s.graphics_controls,!s.graphics_controls);
 case Bloom:{const bool changed=step(s.graphics_bloom,.1f,0.f,10.f);if(changed)s.graphics_controls=true;return changed;}
 case Fog:{const bool changed=step(s.graphics_fog,.1f,0.f,5.f);if(changed)s.graphics_controls=true;return changed;}
 case Ambient:{const bool changed=step(s.graphics_ao_strength,.1f,0.f,5.f);if(changed)s.graphics_controls=true;return changed;}
 case Shadow:{const bool changed=step(s.graphics_shadow_scale,.25f,.25f,8.f);if(changed)s.graphics_controls=true;return changed;}
 }
 return false;
}
}
