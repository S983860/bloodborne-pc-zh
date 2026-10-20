// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Keyboard, mouse and Xbox controller button icons in the style of the game's own
// (menu/common.tpf's KG_*: a dark cap with a light top rim and a shadow under it, tan lettering,
// the face buttons round with a coloured symbol), drawn here, 32x32, and stored the way those
// are: BC7 in the PS4's thin tiling, one tile of 8x8 4x4-blocks in Morton order, 0x400 bytes.
// engine/key_prompts.cpp shows them in place of the game's icons.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Keycap {

constexpr int kSize = 32;
constexpr std::size_t kBytes = 0x400;

/// An Xbox controller's buttons (Spec::Pad). The paddles, the touchpad and the guide button,
/// which a binding can name too, show as their names.
enum PadButton : int {
    PadA, PadB, PadX, PadY, PadLB, PadRB, PadLT, PadRT, PadLS, PadRS, PadView, PadMenu, PadShare,
    PadUp, PadDown, PadLeft, PadRight, PadP1, PadP2, PadP3, PadP4, PadTouchpad, PadGuide, PadPSCross, PadPSCircle,
};

struct Spec {
    enum Kind : std::uint8_t {
        Key,    // one cap: labels[0]
        Keys,   // two caps side by side, or four as an inverted T (up, left, down, right)
        Mouse,  // the mouse, `button` lit
        Pad,    // an Xbox controller's button: `button` a PadButton
    };
    Kind kind = Key;
    /// Cap texts, UTF-8. "Up", "Down", "Left" and "Right" are drawn as arrows.
    std::vector<std::string> labels;
    /// Mouse: 0 none (the mouse itself, for the camera), 1 left, 2 right, 3 middle, 6 and 7 the
    /// wheel up and down. Pad: a PadButton.
    int button = 0;

    /// One string per distinct icon (the caches' key).
    std::string Id() const;
};

/// RGBA8, `size` pixels square, row-major, the colour premultiplied by alpha as the game's icons
/// are. 32 for the KG_* icons; the HUD's buttons are 26.
void Draw(const Spec& spec, std::uint8_t* rgba, int size = kSize);
/// BC7, PS4 thin tiled: kBytes.
void Encode(const std::uint8_t rgba[kSize * kSize * 4], std::uint8_t out[kBytes]);
/// BC7 blocks of a region (`width` and `height` multiples of 4), row by row, 16 bytes each.
void EncodeBlocks(const std::uint8_t* rgba, int width, int height, std::uint8_t* out);

} // namespace Keycap
