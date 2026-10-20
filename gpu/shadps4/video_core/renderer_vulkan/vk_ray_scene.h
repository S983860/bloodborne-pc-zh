// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <memory>
#include <span>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
namespace Vulkan {
class Instance;
class Scheduler;
struct RayCamera {
    std::array<float,12> inverse{};
    std::array<float,4> projection{};
    std::array<float,4> viewport{1,1,0,0}; // signs, then jitter in pixels
    bool valid=false;
};
// Experimental hardware ray-query AO from the actual instrumented G-buffer
// triangles. Coverage is the rasterised scene, not an off-screen world cache.
class RayScene {
public:
    static bool Wanted();
    static bool Active();
    static void DeviceReady(bool ready);
    RayScene(const Instance&,Scheduler&,vk::Buffer positions);
    ~RayScene();
    void NextFrame();
    void Capture(u32 store,u32 first,u32 vertices,u64 indices,u32 count,
                 u32 index_size,s32 base_vertex,u32 instances,const RayCamera&);
    bool Apply(vk::CommandBuffer,vk::ImageView color,vk::ImageView depth,
               u32 width,u32 height,const RayCamera&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
