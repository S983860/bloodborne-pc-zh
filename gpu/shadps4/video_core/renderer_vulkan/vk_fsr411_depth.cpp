// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_fsr411_depth.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/host_shaders/fsr411_depth_comp.h"
namespace Vulkan {
Fsr411Depth::Fsr411Depth(const Instance& i, Scheduler& s):instance(i),scheduler(s) {
    const auto device=instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding,2> bindings{{
        {.binding=0,.descriptorType=vk::DescriptorType::eSampledImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute},
        {.binding=1,.descriptorType=vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute}}};
    descriptors=Check(device.createDescriptorSetLayoutUnique({
        .flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount=2,.pBindings=bindings.data()}));
    layout=Check(device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors}));
    const auto shader=CompileSPV(FSR411_DEPTH_COMP,device);
    pipeline=Check(device.createComputePipelineUnique({},vk::ComputePipelineCreateInfo{
        .stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=shader,.pName="main"},.layout=*layout}));
    device.destroyShaderModule(shader);
}
Fsr4Upscaler::Image Fsr411Depth::Record(vk::CommandBuffer cmd,vk::ImageView forward,u32 w,u32 h) {
    bool fresh=false;
    if(w!=width || h!=height || !image) {
        if(image) scheduler.DeferOperation([old_view=std::move(view),old_image=std::move(image)]() mutable {
            old_view.reset();old_image.Destroy();
        });
        width=w;height=h;fresh=true;
        image=VideoCore::UniqueImage(instance.GetDevice(),instance.GetAllocator());
        image.Create({.imageType=vk::ImageType::e2D,.format=vk::Format::eR32Sfloat,
            .extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
            .tiling=vk::ImageTiling::eOptimal,
            .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout=vk::ImageLayout::eUndefined});
        view=Check(instance.GetDevice().createImageViewUnique({.image=image.image,
            .viewType=vk::ImageViewType::e2D,.format=vk::Format::eR32Sfloat,
            .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}));
    }
    const vk::MemoryBarrier2 source{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderSampledRead};
    const vk::ImageMemoryBarrier2 dest{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask=fresh?vk::AccessFlags2{}:vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout=fresh?vk::ImageLayout::eUndefined:vk::ImageLayout::eGeneral,
        .newLayout=vk::ImageLayout::eGeneral,.image=image.image,
        .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
    cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&source,.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&dest});
    const std::array<vk::DescriptorImageInfo,2> infos{{
        {.imageView=forward,.imageLayout=vk::ImageLayout::eGeneral},
        {.imageView=*view,.imageLayout=vk::ImageLayout::eGeneral}}};
    const std::array<vk::WriteDescriptorSet,2> writes{{
        {.dstBinding=0,.descriptorCount=1,.descriptorType=vk::DescriptorType::eSampledImage,.pImageInfo=&infos[0]},
        {.dstBinding=1,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageImage,.pImageInfo=&infos[1]}}};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes);
    cmd.dispatch((w+7)/8,(h+7)/8,1);
    return {image.image,*view,w,h,vk::Format::eR32Sfloat};
}
}
