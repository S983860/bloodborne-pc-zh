// SPDX-License-Identifier: GPL-3.0-or-later
// Native NGX call forwarding. Caller-module compatibility requirement documented
// by Dagherbou/OptiScaler_DLSSNR, OptiScaler/dlssnr/forwarder/README.md.
// NVIDIA's signed runtime is loaded unchanged; this module owns its reference.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
namespace {
using Result=unsigned;
using Init=Result(*)(unsigned long long,const wchar_t*,VkInstance,VkPhysicalDevice,VkDevice,
                    PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,unsigned,const void*);
using Create=Result(*)(VkDevice,VkCommandBuffer,int,void*,void**);
using Evaluate=Result(*)(VkCommandBuffer,const void*,const void*,void*);
using Release=Result(*)(void*);
using Shutdown=Result(*)(VkDevice);
HMODULE runtime{};Init initialize{};Create create{};Evaluate evaluate{};Release release{};Shutdown shutdown{};
constexpr Result Failure=0xbad00001;
}
extern "C" {
__declspec(dllexport) Result BB_NR_Init(const wchar_t* path,unsigned long long app,const wchar_t* data,
    VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr gipa,
    PFN_vkGetDeviceProcAddr gdpa,unsigned version,const void* info){
 if(runtime)return Failure;
 runtime=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!runtime)return Failure;
 initialize=reinterpret_cast<Init>(GetProcAddress(runtime,"NVSDK_NGX_VULKAN_Init_Ext2"));
 create=reinterpret_cast<Create>(GetProcAddress(runtime,"NVSDK_NGX_VULKAN_CreateFeature1"));
 evaluate=reinterpret_cast<Evaluate>(GetProcAddress(runtime,"NVSDK_NGX_VULKAN_EvaluateFeature"));
 release=reinterpret_cast<Release>(GetProcAddress(runtime,"NVSDK_NGX_VULKAN_ReleaseFeature"));
 shutdown=reinterpret_cast<Shutdown>(GetProcAddress(runtime,"NVSDK_NGX_VULKAN_Shutdown1"));
 if(!initialize||!create||!evaluate||!release||!shutdown)return Failure;
 volatile Result result=initialize(app,data,instance,physical,device,gipa,gdpa,version,info);
 return result;
}
__declspec(dllexport) Result BB_NR_Create(VkDevice device,VkCommandBuffer cmd,int feature,void* params,void** handle){
 if(!create)return Failure;volatile Result result=create(device,cmd,feature,params,handle);return result;
}
__declspec(dllexport) Result BB_NR_Evaluate(VkCommandBuffer cmd,const void* handle,const void* params,void* callback){
 if(!evaluate)return Failure;volatile Result result=evaluate(cmd,handle,params,callback);return result;
}
__declspec(dllexport) Result BB_NR_Release(void* handle){
 if(!release)return Failure;volatile Result result=release(handle);return result;
}
__declspec(dllexport) Result BB_NR_Shutdown(VkDevice device){
 if(!shutdown)return Failure;volatile Result result=shutdown(device);return result;
}
__declspec(dllexport) void BB_NR_Unload(){
 if(runtime)FreeLibrary(runtime);runtime=nullptr;initialize=nullptr;create=nullptr;evaluate=nullptr;release=nullptr;shutdown=nullptr;
}
}
