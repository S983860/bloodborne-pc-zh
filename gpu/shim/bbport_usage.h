// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: CPU and GPU usage for the FPS counter (bbport.ini show_fps_cpu / show_fps_gpu).
#pragma once

namespace BbUsage {

/// The PC's CPU and GPU (3D engine) use in percent, as Task Manager shows them, measured over
/// about the last half second; -1 where it is not known (another OS, or no GPU counters).
/// Cheap to call every frame: it samples at most twice a second.
struct Sample {
    float cpu = -1.0f;
    float gpu = -1.0f;
};
Sample Get(bool cpu, bool gpu);

} // namespace BbUsage
