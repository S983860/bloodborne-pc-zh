// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Button prompts for the keyboard and mouse. The game draws its button prompts from two places in
// menu/common.tpf: 20 icons of their own (KG_*: 32x32 BC7 textures - the key guides, the
// tutorial notes, the menus) and three 26-pixel buttons on a HUD sheet (MENU_Common_00091:
// Circle, Cross and L3 - the world prompts such as "Examine" and the dialog boxes). While the
// keyboard or mouse is in use the texture cache uploads keycaps over them (engine/keycap_art.h):
// the key, or the mouse button, bound to what that pad button does there - in a menu, Menu
// Confirm and Menu Back for the face buttons that confirm and go back. The pad, or
// button_prompts = controller, brings the controller's icons back: the game's own (PlayStation),
// or with a controller other than a PlayStation one in use (pad_icons = auto) or pad_icons = xbox, the Xbox button bound
// to each PS4 button (src/runtime_pad.c), drawn the same way. Nothing in the game's memory
// changes.

#include <cstddef>
#include <cstdint>

namespace KeyPrompts {

constexpr std::size_t kIconBytes = 0x400, kHudBytes = 0x80000;

/// Which of the game's textures with button icons this is, or -1.
int Identify(const void* data, std::size_t size, std::uint32_t width, std::uint32_t height);

/// Draws the keycaps or Xbox buttons over `data`, a copy of that texture's data about to be
/// uploaded (the game's format and tiling). False, and `data` unchanged, while the game's own
/// icons show.
bool Draw(int texture, std::uint8_t* data, std::size_t size);

/// Once a frame (Rasterizer::OnSubmit): whether what the icons show changed since the last call
/// (the device or gamepad in use, a menu opened or closed, a binding, the settings), so that the
/// texture cache uploads them again.
bool Changed();

} // namespace KeyPrompts
