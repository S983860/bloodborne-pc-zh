// SPDX-License-Identifier: GPL-3.0-or-later
// Trophies a save has already earned (bosses it has defeated) are reported as earned, so an
// imported save, or one from another port, counts what it had. See trophy_sync.cpp.
#pragma once

namespace BbEngine::TrophySync {
/// The main thread's frame hook (change_appearance.cpp FrameHook).
void Tick();
} // namespace BbEngine::TrophySync
