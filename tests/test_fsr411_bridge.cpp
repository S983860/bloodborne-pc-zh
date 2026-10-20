// SPDX-License-Identifier: GPL-3.0-or-later
// Headless test of the game's actual bridge and scheduler; no guest or OS input.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_fsr4vk.h"
#include "video_core/renderer_vulkan/vk_ray_scene.h"
#include "video_core/renderer_vulkan/vk_fsr411_depth.h"
#include "ffx_vk_portable.h"
#include "bbport_settings.h"
#include <vk_mem_alloc.h>
// Vulkan-Hpp's DispatchLoaderBase layout depends on NDEBUG. Match the production
// library while including its headers, then enable assertions for test checks.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <bit>
#include <cmath>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#undef assert
#define assert(expression) do { if(!(expression)){std::fprintf(stderr,"CHECK failed at line %d: %s\n",__LINE__,#expression);std::_Exit(3);} } while(false)
// No guest save or combat audio exists in this GPU-only test process.
extern "C" std::uint64_t runtime_savedata_write_generation(){return 0;}
extern "C" void runtime_audio_guard_event(int,int){}

int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
 if(argc!=5)return 2;
 using namespace Vulkan;
 const unsigned rw=std::atoi(argv[1]),rh=std::atoi(argv[2]),ow=std::atoi(argv[3]),oh=std::atoi(argv[4]);
 assert(rw>=64&&rh>=64&&ow>=rw&&oh>=rh&&ow<=3840&&oh<=2160);
 std::puts("FSR411 bridge stage: creating headless game device");
 Instance instance(0,std::getenv("BB_TEST_VALIDATION")!=nullptr);
 std::puts("FSR411 bridge stage: device created");
 if(std::getenv("BB_TEST_SIMULATED_AMD")){
  // Inject only the application's vendor policy; Vulkan still uses the real GPU
  // and its real driver. Never report this as AMD silicon/driver emulation.
  auto& s=BbSettings::Get();s.dlss_nr_enabled=true;s.hardware_rt_enabled=true;s.object_motion=true;
  BbSettings::ConfigureHardware(0x1002);
  assert(!s.dlss_nr_enabled&&!s.hardware_rt_enabled&&!s.object_motion&&!s.startup_object_motion);
  assert(!RayScene::Wanted()&&!RayScene::Active());
  std::puts("FSR411 simulation: AMD application policy only; real Vulkan GPU remains NVIDIA; neural=0 RT=0 vertex-motion=0");
 }
 static vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic dispatch;
 dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));dispatch.init(instance.GetInstance());dispatch.init(instance.GetDevice());
 const auto device=instance.GetDevice();Scheduler scheduler(instance);
 struct Target{VkImage raw{};VmaAllocation allocation{};vk::ImageView view;vk::ImageAspectFlags aspects;Fsr4Upscaler::Image frame;};
 auto target=[&](unsigned w,unsigned h,vk::Format format,vk::ImageAspectFlags aspects){
  Target t{};t.aspects=aspects;
  const VkImageCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VkFormat(format),.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
   .usage=VkImageUsageFlags(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|((aspects&vk::ImageAspectFlagBits::eDepth)?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_STORAGE_BIT)),.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
  const VmaAllocationCreateInfo allocation{.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
  assert(vmaCreateImage(instance.GetAllocator(),&ci,&allocation,&t.raw,&t.allocation,nullptr)==VK_SUCCESS);
  auto created=device.createImageView({.image=t.raw,.viewType=vk::ImageViewType::e2D,.format=format,.subresourceRange={aspects&vk::ImageAspectFlagBits::eDepth?vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth):aspects,0,1,0,1}},nullptr,dispatch);
  assert(created.result==vk::Result::eSuccess);t.view=created.value;
  t.frame={t.raw,t.view,w,h,format};return t;
 };
 auto color=target(rw,rh,vk::Format::eR16G16B16A16Sfloat,vk::ImageAspectFlagBits::eColor);
 auto depth=target(rw,rh,vk::Format::eD32SfloatS8Uint,vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil);
 auto motion=target(rw,rh,vk::Format::eR16G16Sfloat,vk::ImageAspectFlagBits::eColor);
 auto output=target(ow,oh,vk::Format::eR16G16B16A16Sfloat,vk::ImageAspectFlagBits::eColor);
 const std::array<Target*,4> targets{&color,&depth,&motion,&output};
 auto cmd=scheduler.CommandBuffer();std::array<vk::ImageMemoryBarrier2,4> transitions{};
 for(unsigned i=0;i<targets.size();i++)transitions[i]={.srcStageMask=vk::PipelineStageFlagBits2::eTopOfPipe,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.image=targets[i]->raw,.subresourceRange={targets[i]->aspects,0,1,0,1}};
 cmd.pipelineBarrier2({.imageMemoryBarrierCount=4,.pImageMemoryBarriers=transitions.data()},dispatch);
 const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
 cmd.clearColorImage(color.raw,vk::ImageLayout::eGeneral,vk::ClearColorValue(std::array{.18f,.30f,.50f,1.f}),range,dispatch);
 cmd.clearColorImage(motion.raw,vk::ImageLayout::eGeneral,vk::ClearColorValue(std::array{0.f,0.f,0.f,0.f}),range,dispatch);
 cmd.clearDepthStencilImage(depth.raw,vk::ImageLayout::eGeneral,vk::ClearDepthStencilValue{.5f,0},vk::ImageSubresourceRange{depth.aspects,0,1,0,1},dispatch);
 const vk::MemoryBarrier2 ready{.srcStageMask=vk::PipelineStageFlagBits2::eAllTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite};
 cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&ready},dispatch);
 bool ok=true;
 const bool textured=std::getenv("BB_TEST_TEXTURED_SCENE")!=nullptr;
 const bool test_fsr3=std::getenv("BB_TEST_FSR3")!=nullptr;
 const int frames=textured?64:12;
 const int test_preset=std::getenv("BB_TEST_PRESET")?std::clamp(std::atoi(std::getenv("BB_TEST_PRESET")),0,4):2;
 const bool capture_sequence=std::getenv("BB_TEST_SEQUENCE_HASH")!=nullptr;
 vk::QueryPool timer{};std::vector<double> timings;
 VkBuffer capture{};VmaAllocation capture_allocation{};VmaAllocationInfo capture_info{};
 if(capture_sequence){
  auto pool=device.createQueryPool({.queryType=vk::QueryType::eTimestamp,.queryCount=2},nullptr,dispatch);assert(pool.result==vk::Result::eSuccess);timer=pool.value;
  const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=VkDeviceSize(ow)*oh*8,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
  assert(vmaCreateBuffer(instance.GetAllocator(),&ci,&ac,&capture,&capture_allocation,&capture_info)==VK_SUCCESS);
 }
 VkBuffer staging{};VmaAllocation staging_allocation{};VmaAllocationInfo staging_info{};
 const VkDeviceSize pixels=VkDeviceSize(rw)*rh;
 if(textured){
  const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=pixels*16,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
  const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
  assert(vmaCreateBuffer(instance.GetAllocator(),&ci,&ac,&staging,&staging_allocation,&staging_info)==VK_SUCCESS);
 }
 {
  Fsr4Vk fsr(instance,scheduler);
  const bool switch_cycle=std::getenv("BB_TEST_SWITCH_CYCLE");
  std::unique_ptr<Fsr4Upscaler> switching;int transitions=0;
  bool previous_fsr3=false;
  if(switch_cycle)assert(!Fsr4Upscaler::AssetsAvailable(test_preset,ow,oh));
  FfxVkPortableUpscaleContext* fsr3{};
  if(test_fsr3 || switch_cycle){
   FfxVkPortableDeviceInfo di{};di.structSize=sizeof(di);di.instance=instance.GetInstance();di.physicalDevice=instance.GetPhysicalDevice();di.device=device;
   di.getDeviceProcAddr=dispatch.vkGetDeviceProcAddr;di.queue=instance.GetGraphicsQueue();di.queueFamilyIndex=instance.GetGraphicsQueueFamilyIndex();
   di.shaderFloat16Enabled=instance.IsShaderFloat16Enabled();di.subgroupSizeControlEnabled=instance.IsSubgroupSizeControlEnabled();di.synchronization2Enabled=VK_TRUE;di.shaderStorageImageWriteWithoutFormatEnabled=VK_TRUE;
   FfxVkPortableUpscaleCreateInfo ci{};ci.structSize=sizeof(ci);ci.maxRenderSize={rw,rh};ci.maxOutputSize={ow,oh};
   assert(ffxVkPortableUpscaleContextCreate(&di,&ci,&fsr3)==FFX_VK_PORTABLE_OK);
  }
  for(int n=0;n<frames;n++){
   const std::array<float,2> jitter=textured?std::array<float,2>{float((n*5)%16)/16.f-.5f,float((n*7)%17)/17.f-.5f}:std::array<float,2>{0.f,0.f};
   if(textured){
    auto* colors=static_cast<std::uint16_t*>(staging_info.pMappedData);
    auto* depths=reinterpret_cast<float*>(colors+pixels*4);
    auto* motions=reinterpret_cast<std::uint16_t*>(depths+pixels);
    const auto half=[](float v){return std::bit_cast<std::uint16_t>(_Float16(v));};
    for(unsigned y=0;y<rh;y++)for(unsigned x=0;x<rw;x++){
     const auto p=std::size_t(y)*rw+x;float sx=x+jitter[0]+n*2,sy=y+jitter[1]+n;
     const bool front=x>rw/3+n*2 && x<rw/2+n*2 && y>rh/4+n && y<rh*3/4+n;
     const bool line=std::fmod(sx,23.f)<1.8f || std::fmod(sy,37.f)<1.8f;
     colors[p*4]=half(front?.7f:line?.08f:.2f+.12f*std::sin(sx*.031f));
     colors[p*4+1]=half(front?.16f:line?.09f:.3f+.1f*std::cos(sy*.027f));
     colors[p*4+2]=half(front?.1f:.4f);colors[p*4+3]=half(1.f);
     depths[p]=y<4?std::array{0.f,.125f,.5f,.875f,1.f}[x%5]:front?.35f:.98f;
     motions[p*2]=half(n?(front?-2.f:2.f):0.f);motions[p*2+1]=half(n?(front?-1.f:1.f):0.f);
    }
    cmd=scheduler.CommandBuffer();
    const vk::MemoryBarrier2 upload_ready{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands|vk::PipelineStageFlagBits2::eHost,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite|vk::AccessFlagBits2::eHostWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead|vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&upload_ready},dispatch);
    for(auto pair:std::array<std::pair<Target*,VkDeviceSize>,3>{{{&color,0},{&depth,pixels*8},{&motion,pixels*12}}})
     cmd.copyBufferToImage(staging,pair.first->raw,vk::ImageLayout::eGeneral,vk::BufferImageCopy{.bufferOffset=pair.second,.imageSubresource={pair.first==&depth?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={rw,rh,1}},dispatch);
    cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&ready},dispatch);
   }
   const bool use_fsr3=test_fsr3 || (switch_cycle && (n/8)%2==0);
   if(switch_cycle && (n==0 || use_fsr3!=previous_fsr3)){
    scheduler.Finish();switching.reset();++transitions;previous_fsr3=use_fsr3;
    if(!use_fsr3)switching=std::make_unique<Fsr4Upscaler>(instance,scheduler);
   }
   const Fsr4Upscaler::Frame frame{.cmdbuf=scheduler.CommandBuffer(),.color=color.frame,.depth=depth.frame,.motion=motion.frame,.output=output.frame,.render_width=rw,.render_height=rh,.preset=test_preset,.jitter=jitter,.frame_ms=1000.f/60,.near_plane=.05f,.far_plane=3000.f,.vertical_fov=1.04f,.sharpness=.3f,.sharpen=false,.reset=n==0||n==32||(switch_cycle && n%8==0),.auto_exposure=true};
   if(capture_sequence){cmd=scheduler.CommandBuffer();cmd.resetQueryPool(timer,0,2,dispatch);cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,timer,0,dispatch);}
   if(switch_cycle && n==8){
    // Fail an actual legacy model build while a new 4.1.1 choice is pending.
    Fsr4Upscaler missing(instance,scheduler);
    BbSettings::Get().upscaler=BbSettings::UpscalerFsr411;
    assert(!missing.Record(frame,BbSettings::UpscalerFsr4));
    for(int wait=0;missing.Preparing() && wait<5000;++wait)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!missing.Preparing());
    assert(!missing.Record(frame,BbSettings::UpscalerFsr4) && missing.Fatal());
    assert(!BbSettings::FallbackUpscaler(BbSettings::UpscalerFsr4,BbSettings::UpscalerFsr3));
    assert(BbSettings::Get().upscaler==BbSettings::UpscalerFsr411);
    std::puts("SWITCH old missing FSR4 failure preserved newer FSR4.1.1 selection");
   }
   if(use_fsr3){
    auto describe=[](const Target& t,bool write){FfxVkPortableImage i{};i.structSize=sizeof(i);i.image=t.raw;i.format=VkFormat(t.frame.format);i.extent={t.frame.width,t.frame.height};i.mipCount=i.arrayLayers=1;
     i.aspect=VkImageAspectFlags(t.aspects&vk::ImageAspectFlagBits::eDepth?vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth):t.aspects);
     i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|((t.aspects&vk::ImageAspectFlagBits::eDepth)?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_STORAGE_BIT);
     i.state=write?FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS:FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ;return i;};
    FfxVkPortableUpscaleDispatchInfo d{};d.structSize=sizeof(d);d.commandBuffer=frame.cmdbuf;d.color=describe(color,false);d.depth=describe(depth,false);d.motionVectors=describe(motion,false);d.output=describe(output,true);
    d.exposure.structSize=d.reactiveMask.structSize=d.transparencyAndCompositionMask.structSize=sizeof(FfxVkPortableImage);
    d.jitterOffset={jitter[0],jitter[1]};d.motionVectorScale={1.f,1.f};d.renderSize={rw,rh};d.outputSize={ow,oh};d.frameTimeMilliseconds=frame.frame_ms;d.preExposure=1.f;
    d.cameraNear=frame.near_plane;d.cameraFar=frame.far_plane;d.cameraVerticalFovRadians=frame.vertical_fov;d.viewSpaceToMeters=1.f;d.reset=frame.reset;d.frameId=n;
    assert(ffxVkPortableUpscaleContextRecordDispatch(fsr3,&d)==FFX_VK_PORTABLE_OK);
   }else if(switch_cycle){
    // Deliberately change the live UI atomic after this frame's selection.
    // Recording must still use the pinned 4.1.1 provider, never legacy FSR4.
    BbSettings::Get().upscaler=BbSettings::UpscalerFsr4;
    assert(switching && switching->Record(frame,BbSettings::UpscalerFsr411));
    BbSettings::Get().upscaler=BbSettings::UpscalerFsr411;
   }else if(!fsr.Record(frame)){std::fprintf(stderr,"FSR411 bridge failed: %s\n",fsr.Problem());ok=false;break;}
   if(capture_sequence)scheduler.CommandBuffer().writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,timer,1,dispatch);
   scheduler.Finish();
   if(capture_sequence){
    std::array<std::uint64_t,2> stamps{};
    assert(dispatch.vkGetQueryPoolResults(VkDevice(device),VkQueryPool(timer),0,2,sizeof(stamps),stamps.data(),sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT)==VK_SUCCESS);
    if(n>=8)timings.push_back(double(stamps[1]-stamps[0])*instance.GetPhysicalDevice().getProperties(dispatch).limits.timestampPeriod/1e6);
    if(n==8||n==16||n==31||n==32||n==33||n==47||n==63){
     cmd=scheduler.CommandBuffer();
     const vk::MemoryBarrier2 read{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead};
     cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&read},dispatch);
     cmd.copyImageToBuffer(output.raw,vk::ImageLayout::eGeneral,capture,vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={ow,oh,1}},dispatch);
     const vk::MemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};
     cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&host},dispatch);scheduler.Finish();
     uint64_t hash=14695981039346656037ull;auto* p=static_cast<const unsigned char*>(capture_info.pMappedData);
     for(size_t i=0;i<size_t(ow)*oh*8;i++){hash^=p[i];hash*=1099511628211ull;}
     std::printf("SEQUENCE frame=%d fnv64=%016llx\n",n,(unsigned long long)hash);
    }
   }
  }
  if(switch_cycle)std::printf("SWITCH cycle PASS transitions=%d frames=%d final=FSR4.1.1\n",transitions,frames);
  std::printf("%s bridge: %s; frames=%d; %ux%u -> %ux%u\n",test_fsr3?"FSR3":"FSR411",test_fsr3?"FSR 3.1 portable Vulkan":fsr.Describe().c_str(),ok?frames:0,rw,rh,ow,oh);
  if(ok){
   VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo info{};const VkDeviceSize bytes=VkDeviceSize(ow)*oh*8;
   const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
   assert(vmaCreateBuffer(instance.GetAllocator(),&ci,&ac,&buffer,&allocation,&info)==VK_SUCCESS);
   cmd=scheduler.CommandBuffer();
   const vk::MemoryBarrier2 copy{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderStorageWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead};
   cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&copy},dispatch);
   cmd.copyImageToBuffer(output.raw,vk::ImageLayout::eGeneral,buffer,vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={ow,oh,1}},dispatch);
   const vk::MemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};
   cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&host},dispatch);scheduler.Finish();
   const auto* output_pixels=static_cast<const std::uint16_t*>(info.pMappedData);std::size_t nonfinite=0,blank=0;
   for(std::size_t i=0;i<std::size_t(ow)*oh;i++){
    for(int c=0;c<4;c++)if((output_pixels[i*4+c]&0x7c00)==0x7c00)nonfinite++;
    if(!(output_pixels[i*4]&0x7fff)&&!(output_pixels[i*4+1]&0x7fff)&&!(output_pixels[i*4+2]&0x7fff))blank++;
   }
   std::printf("FSR411 bridge pixels: nonfinite=%zu blank=%zu total=%u\n",nonfinite,blank,ow*oh);ok=!nonfinite&&!blank;
   if(textured){
    if(const char* dump=std::getenv("BB_TEST_OUTPUT")){auto* file=std::fopen(dump,"wb");assert(file);assert(std::fwrite(info.pMappedData,1,bytes,file)==bytes);std::fclose(file);}
    Fsr411Depth conversion(instance,scheduler);
    for(unsigned trim: {0u,3u}){
     const unsigned dw=rw-trim,dh=rh-trim;
     auto converted=conversion.Record(scheduler.CommandBuffer(),depth.view,dw,dh);
     cmd=scheduler.CommandBuffer();
     const vk::MemoryBarrier2 converted_ready{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead};
     cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&converted_ready},dispatch);
     cmd.copyImageToBuffer(converted.image,vk::ImageLayout::eGeneral,buffer,vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={dw,dh,1}},dispatch);
     cmd.copyImageToBuffer(depth.raw,vk::ImageLayout::eGeneral,buffer,vk::BufferImageCopy{.bufferOffset=pixels*4,.imageSubresource={vk::ImageAspectFlagBits::eDepth,0,0,1},.imageExtent={rw,rh,1}},dispatch);
     cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&host},dispatch);scheduler.Finish();
     const auto* reverse=static_cast<const float*>(info.pMappedData);const auto* original=reverse+pixels;
     const auto* expected=reinterpret_cast<float*>(static_cast<std::uint16_t*>(staging_info.pMappedData)+pixels*4);
     for(std::size_t p=0;p<pixels;p++)assert(original[p]==expected[p]);
     for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++)assert(reverse[std::size_t(y)*dw+x]==1.f-expected[std::size_t(y)*rw+x]);
     std::printf("FSR411 depth precision: exact forward-to-reverse; input unchanged; %ux%u PASS\n",dw,dh);
    }
   }
   vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
  }
  scheduler.Finish();
  if(fsr3)assert(ffxVkPortableUpscaleContextDestroy(fsr3)==FFX_VK_PORTABLE_OK);
 }
 if(staging)vmaDestroyBuffer(instance.GetAllocator(),staging,staging_allocation);
 if(capture)vmaDestroyBuffer(instance.GetAllocator(),capture,capture_allocation);
 if(timer)device.destroyQueryPool(timer,nullptr,dispatch);
 if(!timings.empty()){std::sort(timings.begin(),timings.end());std::printf("GPU_EVAL median_ms=%.6f p95_ms=%.6f samples=%zu\n",timings[timings.size()/2],timings[timings.size()*95/100],timings.size());}
 for(auto* t:targets){device.destroyImageView(t->view,nullptr,dispatch);vmaDestroyImage(instance.GetAllocator(),t->raw,t->allocation);}
 std::printf("FSR411 production bridge: %s\n",ok?"PASS":"FAIL");return ok?0:1;
}
