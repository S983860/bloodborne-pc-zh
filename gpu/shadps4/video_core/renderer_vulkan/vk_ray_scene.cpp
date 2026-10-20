// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_ray_scene.h"
#include "bbport_settings.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/host_shaders/ray_scene_expand_comp.h"
#include "video_core/host_shaders/ray_scene_ao_comp.h"
#include <vk_mem_alloc.h>
namespace Vulkan {
namespace {
std::atomic<bool> device_ready{false};
constexpr u32 MaxVertices=3*262144,MaxCameras=8192;
struct Buffer {
 vk::Buffer handle{};VmaAllocation allocation{};void* mapped{};u64 address{};
};
struct Pipeline {
 vk::UniqueDescriptorSetLayout descriptors;
 vk::UniquePipelineLayout layout;
 vk::UniquePipeline pipeline;
};
}
bool RayScene::Wanted(){const char* v=std::getenv("BB_HW_RT");return BbSettings::Get().gpu_vendor==0x10de && v && !std::strcmp(v,"1");}
bool RayScene::Active(){return Wanted()&&device_ready&&BbSettings::Get().hardware_rt_enabled;}
void RayScene::DeviceReady(bool ready){device_ready=ready;BbSettings::Get().hardware_rt_supported=ready;}
struct RayScene::Impl {
 const Instance& instance;Scheduler& scheduler;vk::Buffer positions;
 Pipeline expand,ao;
 struct Slot {
  Buffer refs,cameras,world,blas_storage,tlas_storage,scratch,instances;
  vk::UniqueAccelerationStructureKHR blas,tlas;
  u64 tick{};
 };
 std::array<Slot,3> slots;
 std::vector<std::array<u32,12>> refs;
 u32 ref_count{};
 std::vector<std::array<float,16>> cameras;
 u64 frame{},evaluations{};bool applied=false,failed=false,initialized=false;
 u64 scratch_alignment=256,blas_scratch=0,tlas_scratch=0;
 explicit Impl(const Instance& i,Scheduler& s,vk::Buffer p):instance(i),scheduler(s),positions(p){}
 ~Impl(){
  scheduler.Finish();
  for(auto& s:slots){
   s.blas.reset();s.tlas.reset();
   for(auto* b:{&s.refs,&s.cameras,&s.world,&s.blas_storage,&s.tlas_storage,&s.scratch,&s.instances})
    if(b->handle)vmaDestroyBuffer(instance.GetAllocator(),VkBuffer(b->handle),b->allocation);
  }
 }
 bool MakeBuffer(Buffer& b,u64 bytes,VkBufferUsageFlags usage,bool host=false){
  VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,
   .usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
  VmaAllocationCreateInfo ai{.flags=host?VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT:0u,
   .usage=host?VMA_MEMORY_USAGE_AUTO:VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
   .requiredFlags=host?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0u};
  VkBuffer raw{};VmaAllocationInfo info{};
  if(vmaCreateBuffer(instance.GetAllocator(),&ci,&ai,&raw,&b.allocation,&info)!=VK_SUCCESS)return false;
  b.handle=raw;b.mapped=info.pMappedData;b.address=instance.GetDevice().getBufferAddress({.buffer=b.handle});return b.address!=0;
 }
 bool MakePipeline(Pipeline& p,std::span<const u32> shader,std::span<const vk::DescriptorType> types,u32 push_size){
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  for(u32 b=0;b<types.size();b++)bindings.push_back({.binding=b,.descriptorType=types[b],.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute});
  auto d=instance.GetDevice();
  p.descriptors=Check(d.createDescriptorSetLayoutUnique({.flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,.bindingCount=u32(bindings.size()),.pBindings=bindings.data()}));
  const vk::PushConstantRange range{.stageFlags=vk::ShaderStageFlagBits::eCompute,.size=push_size};
  p.layout=Check(d.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*p.descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&range}));
  const auto module=CompileSPV(shader,d);
  auto result=d.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=*p.layout});
  d.destroyShaderModule(module);if(result.result!=vk::Result::eSuccess)return false;p.pipeline=std::move(result.value);return true;
 }
 bool Initialize(){
  auto props=instance.GetPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2,vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
  scratch_alignment=props.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>().minAccelerationStructureScratchOffsetAlignment;
  const std::array types1{vk::DescriptorType::eStorageBuffer,vk::DescriptorType::eStorageBuffer,vk::DescriptorType::eStorageBuffer};
  const std::array types2{vk::DescriptorType::eAccelerationStructureKHR,vk::DescriptorType::eSampledImage,vk::DescriptorType::eStorageImage};
  if(!MakePipeline(expand,RAY_SCENE_EXPAND_COMP,types1,16)||!MakePipeline(ao,RAY_SCENE_AO_COMP,types2,112))return false;
  std::puts("Hardware RT: experimental triangle capture and ray-query AO pipelines ready");return true;
 }
 vk::AccelerationStructureGeometryKHR Triangles(u64 address)const{
  vk::AccelerationStructureGeometryKHR g{.geometryType=vk::GeometryTypeKHR::eTriangles,.flags=vk::GeometryFlagBitsKHR::eOpaque};
  g.geometry.triangles={.vertexFormat=vk::Format::eR32G32B32Sfloat,.vertexData={.deviceAddress=address},.vertexStride=16,.maxVertex=MaxVertices-1,.indexType=vk::IndexType::eNoneKHR};return g;
 }
 bool InitializeSlot(Slot& s){
  auto d=instance.GetDevice();
  if(!MakeBuffer(s.refs,u64(MaxCameras)*48,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)||
     !MakeBuffer(s.cameras,u64(MaxCameras)*64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)||
     !MakeBuffer(s.world,u64(MaxVertices)*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)||
     !MakeBuffer(s.instances,64,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,true))return false;
  const auto geom=Triangles(s.world.address);const u32 max_prims=MaxVertices/3;
  vk::AccelerationStructureBuildGeometryInfoKHR bi{.type=vk::AccelerationStructureTypeKHR::eBottomLevel,
   .flags=vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,.geometryCount=1,.pGeometries=&geom};
  const auto bs=d.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,bi,max_prims);
  vk::AccelerationStructureGeometryKHR ig{.geometryType=vk::GeometryTypeKHR::eInstances};
  ig.geometry.instances={.arrayOfPointers=false,.data={.deviceAddress=s.instances.address}};
  vk::AccelerationStructureBuildGeometryInfoKHR ti{.type=vk::AccelerationStructureTypeKHR::eTopLevel,
   .flags=vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,.geometryCount=1,.pGeometries=&ig};
  const u32 one=1;const auto ts=d.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,ti,one);
  blas_scratch=bs.buildScratchSize;tlas_scratch=ts.buildScratchSize;
  if(!MakeBuffer(s.blas_storage,bs.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)||
     !MakeBuffer(s.tlas_storage,ts.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)||
     !MakeBuffer(s.scratch,std::max(blas_scratch,tlas_scratch)+scratch_alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))return false;
  s.blas=Check(d.createAccelerationStructureKHRUnique({.buffer=s.blas_storage.handle,.size=bs.accelerationStructureSize,.type=vk::AccelerationStructureTypeKHR::eBottomLevel}));
  s.tlas=Check(d.createAccelerationStructureKHRUnique({.buffer=s.tlas_storage.handle,.size=ts.accelerationStructureSize,.type=vk::AccelerationStructureTypeKHR::eTopLevel}));
  VkAccelerationStructureInstanceKHR inst{};
  inst.transform.matrix[0][0]=inst.transform.matrix[1][1]=inst.transform.matrix[2][2]=1;
  inst.mask=255;inst.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  inst.accelerationStructureReference=d.getAccelerationStructureAddressKHR({.accelerationStructure=*s.blas});
  std::memcpy(s.instances.mapped,&inst,sizeof(inst));return true;
 }
 void Barrier(vk::CommandBuffer cmd,vk::PipelineStageFlags2 dst,vk::AccessFlags2 access){
  const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite|vk::AccessFlagBits2::eMemoryRead,.dstStageMask=dst,.dstAccessMask=access};
  cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b});
 }
 bool Apply(vk::CommandBuffer cmd,vk::ImageView color,vk::ImageView depth,u32 width,u32 height,const RayCamera& camera){
  if(failed||applied||!camera.valid||ref_count<3)return false;
  auto& s=slots[frame%slots.size()];
  if(s.tick)scheduler.Wait(s.tick);
  if(!s.world.handle && !InitializeSlot(s)){failed=true;std::puts("Hardware RT: resource allocation failed; original frame retained");return false;}
  std::memcpy(s.refs.mapped,refs.data(),refs.size()*48);std::memcpy(s.cameras.mapped,cameras.data(),cameras.size()*64);
  const vk::DescriptorBufferInfo infos[]={{s.refs.handle,0,VK_WHOLE_SIZE},{s.cameras.handle,0,VK_WHOLE_SIZE},{s.world.handle,0,VK_WHOLE_SIZE}};
  std::array<vk::WriteDescriptorSet,3> writes;
  for(u32 i=0;i<3;i++)writes[i]={.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};
  Barrier(cmd,vk::PipelineStageFlagBits2::eComputeShader,vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);
  struct {u64 address;u32 count,draws;} params{instance.GetDevice().getBufferAddress({.buffer=positions}),ref_count,u32(refs.size())};
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*expand.pipeline);cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*expand.layout,0,writes);
  cmd.pushConstants(*expand.layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(params),&params);cmd.dispatch((params.count+63)/64,1,1);
  Barrier(cmd,vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,vk::AccessFlagBits2::eAccelerationStructureReadKHR|vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
  const auto geometry=Triangles(s.world.address);
  const u64 scratch=(s.scratch.address+scratch_alignment-1)&~(scratch_alignment-1);
  vk::AccelerationStructureBuildGeometryInfoKHR bi{.type=vk::AccelerationStructureTypeKHR::eBottomLevel,.flags=vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,
   .mode=vk::BuildAccelerationStructureModeKHR::eBuild,.dstAccelerationStructure=*s.blas,.geometryCount=1,.pGeometries=&geometry,.scratchData={.deviceAddress=scratch}};
  const vk::AccelerationStructureBuildRangeInfoKHR br{.primitiveCount=params.count/3};const auto* bp=&br;cmd.buildAccelerationStructuresKHR(bi,bp);
  Barrier(cmd,vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,vk::AccessFlagBits2::eAccelerationStructureReadKHR|vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
  vk::AccelerationStructureGeometryKHR ig{.geometryType=vk::GeometryTypeKHR::eInstances};ig.geometry.instances={.arrayOfPointers=false,.data={.deviceAddress=s.instances.address}};
  vk::AccelerationStructureBuildGeometryInfoKHR ti{.type=vk::AccelerationStructureTypeKHR::eTopLevel,.flags=vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,
   .mode=vk::BuildAccelerationStructureModeKHR::eBuild,.dstAccelerationStructure=*s.tlas,.geometryCount=1,.pGeometries=&ig,.scratchData={.deviceAddress=scratch}};
  const vk::AccelerationStructureBuildRangeInfoKHR tr{.primitiveCount=1};const auto* tp=&tr;cmd.buildAccelerationStructuresKHR(ti,tp);
  Barrier(cmd,vk::PipelineStageFlagBits2::eComputeShader,vk::AccessFlagBits2::eAccelerationStructureReadKHR|vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);
  const vk::WriteDescriptorSetAccelerationStructureKHR as{.accelerationStructureCount=1,.pAccelerationStructures=&*s.tlas};
  const vk::DescriptorImageInfo di{.imageView=depth,.imageLayout=vk::ImageLayout::eGeneral},ci{.imageView=color,.imageLayout=vk::ImageLayout::eGeneral};
  const std::array<vk::WriteDescriptorSet,3> aw{{{.pNext=&as,.dstBinding=0,.descriptorCount=1,.descriptorType=vk::DescriptorType::eAccelerationStructureKHR},
   {.dstBinding=1,.descriptorCount=1,.descriptorType=vk::DescriptorType::eSampledImage,.pImageInfo=&di},
   {.dstBinding=2,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageImage,.pImageInfo=&ci}}};
  const auto& settings=BbSettings::Get();
  struct Push {std::array<float,12> inv;std::array<float,4> proj,viewport;std::array<u32,4> size;std::array<float,4> ao;}
   push{camera.inverse,camera.projection,camera.viewport,{width,height,u32(frame),u32(std::clamp(settings.rt_samples.load(),1,4))},
        {std::clamp(settings.rt_radius.load(),.1f,3.f),std::clamp(settings.rt_strength.load(),0.f,1.f),0,0}};
  static_assert(sizeof(Push)==112);
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*ao.pipeline);cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*ao.layout,0,aw);
  cmd.pushConstants(*ao.layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(push),&push);cmd.dispatch((width+7)/8,(height+7)/8,1);
  Barrier(cmd,vk::PipelineStageFlagBits2::eAllCommands,vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite);
  s.tick=scheduler.CurrentTick();applied=true;
  if(++evaluations==1||evaluations%600==0)std::printf("Hardware RT: BLAS/TLAS and ray-query AO dispatched, triangles=%u draws=%zu frame=%llu\n",params.count/3,cameras.size(),(unsigned long long)evaluations);
  return true;
 }
};
RayScene::RayScene(const Instance& i,Scheduler& s,vk::Buffer positions){
 if(Wanted()&&device_ready)impl=std::make_unique<Impl>(i,s,positions);
}
RayScene::~RayScene()=default;
void RayScene::NextFrame(){if(impl){impl->frame++;impl->refs.clear();impl->ref_count=0;impl->cameras.clear();impl->applied=false;}}
void RayScene::Capture(u32 store,u32 first,u32 vertices,u64 indices,u32 count,
                      u32 index_size,s32 base_vertex,u32 instances,const RayCamera& camera){
 const u64 added=u64(count)*instances;
 const auto& settings=BbSettings::Get();
 const u32 budget=3u*(65536u<<std::clamp(settings.rt_detail.load(),0,2));
 if(!Active() || !impl||impl->failed||!camera.valid||!added||count%3||
    impl->ref_count>=budget || added>budget-impl->ref_count||impl->cameras.size()>=MaxCameras)return;
 if(!impl->initialized){
  if(!impl->Initialize()){impl->failed=true;BbSettings::Get().hardware_rt_enabled=false;BbSettings::Get().hardware_rt_supported=false;std::puts("Hardware RT: initialization failed; original rendering retained");return;}
  impl->initialized=true;
 }
 std::array<float,16> m;std::copy(camera.inverse.begin(),camera.inverse.end(),m.begin());
 std::copy(camera.projection.begin(),camera.projection.end(),m.begin()+12);
 m[12]*=camera.viewport[0];m[13]*=camera.viewport[1];
 impl->cameras.push_back(m);
 impl->refs.push_back({store,first,vertices,instances,count,u32(base_vertex),index_size,0,
                       impl->ref_count,u32(indices),u32(indices>>32),0});
 impl->ref_count+=u32(added);
}
bool RayScene::Apply(vk::CommandBuffer cmd,vk::ImageView color,vk::ImageView depth,u32 w,u32 h,const RayCamera& camera){return Active()&&impl&&impl->initialized&&!impl->failed&&impl->Apply(cmd,color,depth,w,h,camera);}
}
