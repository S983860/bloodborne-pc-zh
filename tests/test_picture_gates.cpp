// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real native picture hook against a synthetic game parameter block.
// No game process, graphics device or desktop input is used.
#include "gpu/shim/engine/graphics.cpp"
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
namespace BbEngine {
bool host_read_safe(const void* p,void* out,std::size_t n){if(!p)return false;std::memcpy(out,p,n);return true;}
void host_log(const char*,...){}
}
int main(){
    using namespace BbEngine::Graphics;
    auto& s=BbSettings::Get();
    alignas(16) std::array<std::uint8_t,0x400> block{};
    std::array<std::uint8_t,0x2800> object{};
    std::array<std::uint64_t,6> regs{};regs[5]=reinterpret_cast<std::uint64_t>(object.data());
    g_post_block=block.data();
    const auto put=[&](std::size_t at,float x){std::memcpy(block.data()+at,&x,4);};
    const auto get=[&](std::size_t at){float x;std::memcpy(&x,block.data()+at,4);return x;};
    const auto base=[&]{put(kHue,3.f);put(kSaturation,.8f);put(kValue,1.2f);put(kContrast,1.01f);put(kGlareLuminance,2.f);put(kVignettePower,.5f);};
    s.graphics_hue=16.f;s.graphics_saturation=1.6f;s.graphics_brightness=1.3f;s.graphics_contrast=1.05f;
    s.graphics_bloom=1.5f;s.graphics_vignette=false;
    for(bool effects:{false,true})for(bool color:{false,true}){
        s.graphics_controls=effects;s.graphics_color_controls=color;base();YebisRecord(0,regs.data());
        assert(get(kHue)==(color?19.f:3.f));
        assert(get(kSaturation)==(color?.8f*1.6f:.8f));
        assert(get(kValue)==(color?1.2f*1.3f:1.2f));
        assert(get(kContrast)==(color?1.01f*1.05f:1.01f));
        assert(get(kGlareLuminance)==(effects?3.f:2.f));
        assert(get(kVignettePower)==(effects?0.f:.5f));
    }
    // Simulate the game's fresh blended values each rendered view. Repeated
    // effect edits never touch hue/saturation when colour was turned off.
    s.graphics_color_controls=false;s.graphics_controls=true;
    for(int n=0;n<500;n++){
        s.graphics_bloom=float(n%11)/10.f;base();YebisRecord(0,regs.data());
        assert(get(kHue)==3.f && get(kSaturation)==.8f && get(kValue)==1.2f && get(kContrast)==1.01f);
    }
    std::puts("Picture gates: four independent combinations and 500 effect updates PASS");
}
