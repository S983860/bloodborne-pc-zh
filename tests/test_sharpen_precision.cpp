// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the old FP32 scratch path with the production FP16 shader on the
// same FP16 scene. GPU timings cover the copy plus RCAS, not guest gameplay.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/host_shaders/taa_sharpen_comp.h"
#include "video_core/host_shaders/taa_sharpen_fp16_comp.h"
#include <vk_mem_alloc.h>
#include <half.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#x);std::_Exit(3);}} while(false)
extern "C" std::uint64_t runtime_savedata_write_generation(){return 0;}
extern "C" void runtime_audio_guard_event(int,int){}

int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
 const unsigned w=argc>1?std::atoi(argv[1]):257,h=argc>2?std::atoi(argv[2]):129;
 CHECK(w>32&&h>32&&w<=3840&&h<=2160);const std::size_t count=std::size_t(w)*h;
 Vulkan::Instance instance(0,std::getenv("BB_TEST_VALIDATION")!=nullptr);
 static vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic d;
 d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));d.init(instance.GetInstance());d.init(instance.GetDevice());
 const auto device=instance.GetDevice();Vulkan::Scheduler scheduler(instance);
 struct Target{VkImage raw{};VmaAllocation allocation{};vk::ImageView view{};};
 auto make=[&](vk::Format format){
  Target t;const VkImageCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VkFormat(format),.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  const VmaAllocationCreateInfo ai{.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
  CHECK(vmaCreateImage(instance.GetAllocator(),&ci,&ai,&t.raw,&t.allocation,nullptr)==VK_SUCCESS);
  const auto v=device.createImageView({.image=t.raw,.viewType=vk::ImageViewType::e2D,.format=format,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,d);CHECK(v.result==vk::Result::eSuccess);t.view=v.value;return t;
 };
 auto original=make(vk::Format::eR16G16B16A16Sfloat),result=make(vk::Format::eR16G16B16A16Sfloat);
 auto scratch32=make(vk::Format::eR32G32B32A32Sfloat),scratch16=make(vk::Format::eR16G16B16A16Sfloat);
 VkBuffer staging{};VmaAllocation allocation{};VmaAllocationInfo info{};
 const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=count*8,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
 const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
 CHECK(vmaCreateBuffer(instance.GetAllocator(),&bi,&ac,&staging,&allocation,&info)==VK_SUCCESS);
 auto* pixels=static_cast<std::uint16_t*>(info.pMappedData);
 auto encoded=[](float v){return std::bit_cast<std::uint16_t>(half_float::half(v));};
 auto decoded=[](std::uint16_t v){return float(std::bit_cast<half_float::half>(v));};
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  // Flat/dark fields, subpixel fabric, fine metal-like ridges and HDR highlights.
  const float base=.25f+.035f*std::sin(x*.31f)+.022f*std::sin(y*.77f)+.014f*std::sin((x+y)*1.8f);
  const float c=y<h/8?.5f:y<h/4?0.f:base;
  const float gain=x>w*3/4?32.f:1.f;
  const auto at=(std::size_t(y)*w+x)*4;
  pixels[at]=encoded(c*gain);pixels[at+1]=encoded(c*gain*.82f);pixels[at+2]=encoded(c*gain*.69f);pixels[at+3]=encoded(1.f);
 }
 const std::array<Target*,4> targets{&original,&result,&scratch32,&scratch16};
 auto cmd=scheduler.CommandBuffer();std::array<vk::ImageMemoryBarrier2,4> initial{};
 for(unsigned i=0;i<targets.size();i++)initial[i]={.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.image=targets[i]->raw,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 cmd.pipelineBarrier2({.imageMemoryBarrierCount=4,.pImageMemoryBarriers=initial.data()},d);
 const vk::BufferImageCopy region{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}};
 cmd.copyBufferToImage(staging,original.raw,vk::ImageLayout::eGeneral,region,d);scheduler.Finish();
 const std::array<vk::DescriptorSetLayoutBinding,2> bindings{{{.binding=0,.descriptorType=vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute},{.binding=1,.descriptorType=vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute}}};
 const auto desc=device.createDescriptorSetLayoutUnique({.flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,.bindingCount=2,.pBindings=bindings.data()},nullptr,d);CHECK(desc.result==vk::Result::eSuccess);
 const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,4};
 const auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*desc.value,.pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d);CHECK(layout.result==vk::Result::eSuccess);
 auto pipeline=[&](const auto& code){
  auto module=device.createShaderModuleUnique({.codeSize=sizeof(code),.pCode=code},nullptr,d);CHECK(module.result==vk::Result::eSuccess);
  auto p=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=*module.value,.pName="main"},.layout=*layout.value},nullptr,d);CHECK(p.result==vk::Result::eSuccess);return std::move(p.value);
 };
 auto old_pipeline=pipeline(TAA_SHARPEN_COMP),new_pipeline=pipeline(TAA_SHARPEN_FP16_COMP);
 auto queries=device.createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=2},nullptr,d);CHECK(queries.result==vk::Result::eSuccess);
 const auto props=instance.GetPhysicalDevice().getProperties(d);CHECK(props.limits.timestampComputeAndGraphics);
 auto barrier=[&](vk::CommandBuffer c){const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite};c.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b},d);};
 const vk::ImageCopy image_copy{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.extent={w,h,1}};
 const vk::ImageBlit blit{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.srcOffsets=std::array{vk::Offset3D{0,0,0},vk::Offset3D{int(w),int(h),1}},.dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.dstOffsets=std::array{vk::Offset3D{0,0,0},vk::Offset3D{int(w),int(h),1}}};
 auto run=[&](bool fp16,float strength,int iterations,bool readback){
  const auto c=scheduler.CommandBuffer();barrier(c);
  c.copyImage(original.raw,vk::ImageLayout::eGeneral,result.raw,vk::ImageLayout::eGeneral,image_copy,d);barrier(c);
  c.resetQueryPool(*queries.value,0,2,d);c.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe,*queries.value,0,d);
  auto& scratch=fp16?scratch16:scratch32;
  const std::array<vk::DescriptorImageInfo,2> si{{{.imageView=scratch.view,.imageLayout=vk::ImageLayout::eGeneral},{.imageView=result.view,.imageLayout=vk::ImageLayout::eGeneral}}};
  std::array<vk::WriteDescriptorSet,2> writes{};
  for(unsigned i=0;i<2;i++)writes[i]={.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageImage,.pImageInfo=&si[i]};
  for(int n=0;n<iterations;n++){
   c.blitImage(result.raw,vk::ImageLayout::eGeneral,scratch.raw,vk::ImageLayout::eGeneral,blit,vk::Filter::eNearest,d);barrier(c);
   c.bindPipeline(vk::PipelineBindPoint::eCompute,fp16?*new_pipeline:*old_pipeline,d);
   c.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout.value,0,writes,d);c.pushConstants(*layout.value,vk::ShaderStageFlagBits::eCompute,0,4,&strength,d);
   c.dispatch((w+7)/8,(h+7)/8,1,d);barrier(c);
  }
  c.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe,*queries.value,1,d);
  if(readback){c.copyImageToBuffer(result.raw,vk::ImageLayout::eGeneral,staging,region,d);const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};c.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b},d);}
  scheduler.Finish();std::array<std::uint64_t,2> times{};
  CHECK(device.getQueryPoolResults(*queries.value,0,2,sizeof(times),times.data(),8,vk::QueryResultFlagBits::e64|vk::QueryResultFlagBits::eWait,d)==vk::Result::eSuccess);
  return double(times[1]-times[0])*props.limits.timestampPeriod/1e6/iterations;
 };
 for(float strength:{0.f,.30f,.45f,1.f,2.f}){
  run(false,strength,1,true);std::vector<std::uint16_t> reference(pixels,pixels+count*4);
  run(true,strength,1,true);std::size_t different=0,invalid=0;
  for(std::size_t i=0;i<count*4;i++){different+=reference[i]!=pixels[i];invalid+=!std::isfinite(decoded(pixels[i]));}
  std::printf("precision %ux%u strength %.2f: different=%zu nonfinite=%zu\n",w,h,strength,different,invalid);CHECK(!different&&!invalid);
 }
 run(false,.3f,4,false);run(true,.45f,4,false);
 std::array<double,7> old_times{},new_times{};
 for(int i=0;i<7;i++){
  if(i%2){new_times[i]=run(true,.45f,12,false);old_times[i]=run(false,.3f,12,false);}
  else{old_times[i]=run(false,.3f,12,false);new_times[i]=run(true,.45f,12,false);}
 }
 std::sort(old_times.begin(),old_times.end());std::sort(new_times.begin(),new_times.end());
 std::printf("BENCH old_ms=%.6f new_ms=%.6f old_p95_ms=%.6f new_p95_ms=%.6f scratch_saved_MiB=%.3f\n",old_times[3],new_times[3],old_times[6],new_times[6],double(count*8)/(1<<20));
 for(auto* t:targets){device.destroyImageView(t->view,nullptr,d);vmaDestroyImage(instance.GetAllocator(),t->raw,t->allocation);}
 vmaDestroyBuffer(instance.GetAllocator(),staging,allocation);
 std::puts("SHARPEN precision and cost: PASS (no game/window; synthetic scene; no gameplay FPS claim)");
}
