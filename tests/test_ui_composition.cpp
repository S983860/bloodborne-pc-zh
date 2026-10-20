// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"

int main() {
    using namespace Vulkan::UiComposition;
    // Title/loading menus have no depth/camera. UI must still go to the native target.
    assert(Choose(true, true, false, true) == Background::Copy);
    assert(Choose(true, true, false, false) == Background::Copy);
    // In-game HUD: first reconstruct the scene, then draw the native UI over it.
    assert(Choose(true, true, true, true) == Background::Temporal);
    // Runtime FSR disable/failure must not change UI resolution.
    assert(Choose(true, true, true, false) == Background::Copy);
    assert(Choose(true, false, true, true) == Background::None);
    assert(Choose(false, true, true, true) == Background::None);
    assert(NativeViewport(1920, -1080));
    assert(!NativeViewport(960, -540));
    assert(MovieShader(0x34e8a281) && MovieShader(0x24042a9b));
    assert(!MovieShader(0x0b0acf50)); // fullscreen post/tonemap also has a native viewport
    assert(!MovieShader(0x6c62a79f)); // shadow geometry
    // Captured loading/HUD clip masks: no colour exports, then the item image.
    assert(ObserveDraw(0, 0xa400228b, true));
    assert(ObserveDraw(0, 0x34e8a281, true));
    assert(ObserveDraw(1, 0x44072322, true));
    assert(!ObserveDraw(0, 0x6c62a79f, true)); // scene shadow/depth pass
    assert(!ObserveDraw(0, 0xa400228b, false));
    assert(!ObserveDraw(3, 0xa400228b, true)); // multiple scene targets
    const auto display = Scale(960, 540, 1920, 1080, false);
    assert(display[0] == 2 && display[1] == 2);
    const auto ui = Scale(960, 540, 1920, 1080, true);
    assert(ui[0] == 1 && ui[1] == 1); // don't double an already native UI viewport
    const auto larger = Scale(960, 540, 3840, 2160, true);
    assert(larger[0] == 2 && larger[1] == 2);
    // Native UI must preserve circles/fonts and share the clip-mask transform.
    for(auto size: {std::array<uint32_t,2>{1920,1080},{2560,1440},{3840,2160},{3440,1440},{5120,1440},{2560,1600},{3840,2400}}) {
        auto f=Fit(size[0],size[1]);
        assert(f.scale[0]==f.scale[1]);
        assert(f.offset[0]>=0 && f.offset[1]>=0);
        assert(std::abs(1920*f.scale[0]+2*f.offset[0]-size[0])<.01f);
        assert(std::abs(1080*f.scale[1]+2*f.offset[1]-size[1])<.01f);
    }
    assert(Fit(1920,1080).offset==(std::array<float,2>{0,0}));
    assert(std::abs(Fit(3440,1440).offset[0]-440)<.01f);
    assert(std::abs(Fit(5120,1440).offset[0]-1280)<.01f);
    // Widescreen on a 21:9 or 16:10 screen: the widened UI viewport fills the image unstretched,
    // and the stage's own 1920x1080 inside it lands where the centred fit puts it.
    const auto wide = ParseUiSize("2520x1080");
    assert(wide[0] == 2520 && wide[1] == 1080);
    assert(ParseUiSize(nullptr)[0] == 1920 && ParseUiSize("800x600")[0] == 1920 && ParseUiSize("junk")[1] == 1080);
    assert(NativeViewport(2520, -1080, wide) && NativeViewport(1920, 1080, wide) && !NativeViewport(2560, 1080, wide));
    const auto w = Fit(3360, 1440, wide);
    assert(w.scale[0] == w.scale[1] && std::abs(w.offset[0]) < .01f && std::abs(w.offset[1]) < .01f);
    assert(std::abs((300 * w.scale[0] + w.offset[0]) - Fit(3360, 1440).offset[0]) < .01f);
    const auto t = Fit(1920, 1200, ParseUiSize("1920x1200"));
    assert(t.scale[0] == 1 && t.offset[1] == 0);
    std::puts("UI composition: PASS (menu without camera, HUD, FSR off, viewport scaling)");
}
