// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_fsr4.h"
#include "video_core/texture_cache/image.h"
namespace Vulkan {
class Fsr411Depth {
public:
    Fsr411Depth(const Instance& instance, Scheduler& scheduler);
    Fsr4Upscaler::Image Record(vk::CommandBuffer cmd, vk::ImageView forward_depth,
                              u32 width, u32 height);
private:
    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
    u32 width{}, height{};
};
}
