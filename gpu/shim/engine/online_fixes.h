// SPDX-License-Identifier: GPL-3.0-or-later
// Game-side fixes for playing online (BB_ONLINE=1), after droogie/bbhost src/engine/chara_id.cpp
// and sos_signs.cpp: a character the server once gave no id gets asked for one again, and a
// summon sign entry times out sooner. See online_fixes.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::OnlineFixes {
/// Loader start, after the executable is verified: nothing unless online. Commits its own hook.
bool Install(std::uint8_t* image, std::size_t size);
} // namespace BbEngine::OnlineFixes
