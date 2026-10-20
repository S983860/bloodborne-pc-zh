// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Vulkan::UiComposition {

enum class Background { None, Copy, Temporal };

// UI composition must not require scene depth/camera or an active FSR context.
constexpr Background Choose(bool scaled, bool ui_draw, bool scene_ready, bool fsr_active) {
    if (!scaled || !ui_draw) {
        return Background::None;
    }
    return scene_ready && fsr_active ? Background::Temporal : Background::Copy;
}

// The game's UI movie viewport: 1920x1080, or with Widescreen on a screen of another shape that
// shape around it (2520x1080 at 21:9, 1920x1200 at 16:10; scripts/patches.py ui_size, passed as
// BB_UI_SIZE). Scaleform then shows the 1920x1080 stage whole and centred, as bbhost's window
// does, and what the game places past the stage's edges - the enemy gauges, co-op names and the
// lock-on marker at the screen's sides (engine/widescreen.cpp) - stays inside the viewport
// instead of being clipped at the 16:9 box.
inline std::array<float, 2> ParseUiSize(const char* value) {
    unsigned w = 0, h = 0;
    if (value && std::sscanf(value, "%ux%u", &w, &h) == 2 && w >= 1920 && h >= 1080 &&
        w <= 8192 && h <= 8192) {
        return {float(w), float(h)};
    }
    return {1920.0f, 1080.0f};
}
inline std::array<float, 2> UiSize() {
    static const auto size = ParseUiSize(std::getenv("BB_UI_SIZE"));
    return size;
}

inline bool ViewportIs(float width, float height, std::array<float, 2> size) {
    return std::abs(std::abs(width) - size[0]) < 0.5f && std::abs(std::abs(height) - size[1]) < 0.5f;
}
// A UI movie's viewport: the stage's 1920x1080, or the widened one.
inline bool NativeViewport(float width, float height, std::array<float, 2> ui = UiSize()) {
    return ViewportIs(width, height, {1920.0f, 1080.0f}) || ViewportIs(width, height, ui);
}

// Scaleform draws, including the first stencil/movie pass. A native-size viewport
// alone also matches every fullscreen post pass when guest targets stay at 1080p.
constexpr bool MovieShader(uint64_t hash) {
    return hash == 0x34e8a281 || hash == 0x81d336ce || hash == 0x09957251 ||
           hash == 0x24042a9b || hash == 0xa400228b;
}

// Scaleform clip masks write stencil with no colour exports. Observe them before
// the masked image so a late UI-depth preparation cannot erase the clip mask.
constexpr bool ObserveDraw(uint32_t mrt_mask, uint64_t vs_hash, bool has_stencil) {
    return std::popcount(mrt_mask) == 1 ||
           (mrt_mask == 0 && has_stencil && MovieShader(vs_hash));
}

struct FitTransform {std::array<float,2> scale;std::array<float,2> offset;};
// A UI viewport of `ui` units shown whole, centred and unstretched in a w x h image.
inline FitTransform Fit(uint32_t w,uint32_t h,std::array<float,2> ui={1920.0f,1080.0f}) {
    const float s=std::min(float(w)/ui[0],float(h)/ui[1]);
    return {{s,s},{(float(w)-ui[0]*s)/2,(float(h)-ui[1]*s)/2}};
}
inline std::array<float, 2> Scale(uint32_t guest_width, uint32_t guest_height,
                                uint32_t output_width, uint32_t output_height,
                                bool native_coordinates) {
    return {float(output_width) / float(native_coordinates ? 1920 : guest_width),
            float(output_height) / float(native_coordinates ? 1080 : guest_height)};
}

} // namespace Vulkan::UiComposition
