// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: BB_SAMPLE_THREADS - where chosen threads spend their time, sampled. See bbport_sampler.cpp.
#pragma once

namespace BbSampler {
/// Starts the sampler thread when BB_SAMPLE_THREADS is set (Windows); nothing otherwise.
void Start();
} // namespace BbSampler
