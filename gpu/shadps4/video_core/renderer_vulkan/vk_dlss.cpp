// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include "bbport_settings.h"

namespace Vulkan {

namespace {

// The NGX interface, as the driver's core exports it (NVIDIA DLSS SDK 310.x, NGX API 1.5).
// Calls are cdecl, which is the platform calling convention on x86-64.
using NgxResult = unsigned int;
constexpr bool Failed(NgxResult r) {
    return (r & 0xFFF00000u) == 0xBAD00000u;
}
constexpr unsigned kNgxApiVersion = 0x15;
constexpr int kFeatureSuperSampling = 1;
constexpr unsigned long long kApplicationId = 231313132; // NVIDIA's id for unregistered apps

enum DlssFlags : int {
    FlagIsHdr = 1 << 0,
    FlagMvLowRes = 1 << 1,
    FlagAutoExposure = 1 << 6,
};
// NVSDK_NGX_PerfQuality_Value
enum PerfQuality : int { MaxPerf = 0, Balanced = 1, MaxQuality = 2, UltraPerformance = 3, Dlaa = 5 };

struct NgxParameter; ///< NVSDK_NGX_Parameter: a C++ interface implemented by the core
struct NgxHandle;

struct PathListInfo {
    const wchar_t* const* path;
    unsigned int length;
};
using LogCallback = void (*)(const char* message, int level, int feature);
struct LoggingInfo {
    LogCallback callback;
    int minimum_level; ///< 0 off, 1 on, 2 verbose
    bool disable_other_sinks;
};
struct FeatureCommonInfo {
    PathListInfo path_list;
    void* internal_data;
    LoggingInfo logging;
};

struct ImageViewInfoVk {
    VkImageView image_view;
    VkImage image;
    VkImageSubresourceRange subresource_range;
    VkFormat format;
    unsigned int width;
    unsigned int height;
};
struct BufferInfoVk {
    VkBuffer buffer;
    unsigned int size_in_bytes;
};
struct ResourceVk {
    union {
        ImageViewInfoVk image_view_info;
        BufferInfoVk buffer_info;
    } resource;
    int type; ///< 0 image view, 1 buffer
    bool read_write;
};

using PFN_InitExt2 = NgxResult (*)(unsigned long long app_id, const wchar_t* data_path,
                                   VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr,
                                   PFN_vkGetDeviceProcAddr, unsigned version,
                                   const FeatureCommonInfo*);
using PFN_GetParameters = NgxResult (*)(NgxParameter**);
using PFN_DestroyParameters = NgxResult (*)(NgxParameter*);
using PFN_CreateFeature1 = NgxResult (*)(VkDevice, VkCommandBuffer, int feature, NgxParameter*,
                                         NgxHandle**);
using PFN_EvaluateFeature = NgxResult (*)(VkCommandBuffer, const NgxHandle*, const NgxParameter*,
                                          void* progress_callback);
using PFN_ReleaseFeature = NgxResult (*)(NgxHandle*);
using PFN_Shutdown1 = NgxResult (*)(VkDevice);

// NVSDK_NGX_Parameter declares Set and Get overloads for unsigned long long, float, double,
// unsigned int, int, two D3D resource pointers and void*, then Reset. Calls go through its
// vtable: MSVC (the Windows core) lays an overload set out in reverse order of declaration,
// the Itanium ABI (Linux core) in declaration order. This build may use either ABI itself, so
// the slots are fixed here rather than taken from a C++ declaration.
#ifdef _WIN32
enum ParamSlot : int { SetVoid = 0, SetInt = 3, SetUint = 4, SetFloat = 6,
                       GetVoid = 8, GetInt = 11, GetUint = 12, GetFloat = 14 };
#else
enum ParamSlot : int { SetFloat = 1, SetUint = 3, SetInt = 4, SetVoid = 7,
                       GetFloat = 9, GetUint = 11, GetInt = 12, GetVoid = 15 };
#endif

template <typename Fn>
Fn Slot(NgxParameter* p, int slot) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(p))[slot]);
}
void SetI(NgxParameter* p, const char* name, int v) {
    Slot<void (*)(NgxParameter*, const char*, int)>(p, SetInt)(p, name, v);
}
void SetUI(NgxParameter* p, const char* name, unsigned v) {
    Slot<void (*)(NgxParameter*, const char*, unsigned)>(p, SetUint)(p, name, v);
}
void SetF(NgxParameter* p, const char* name, float v) {
    Slot<void (*)(NgxParameter*, const char*, float)>(p, SetFloat)(p, name, v);
}
void SetP(NgxParameter* p, const char* name, void* v) {
    Slot<void (*)(NgxParameter*, const char*, void*)>(p, SetVoid)(p, name, v);
}
NgxResult GetI(NgxParameter* p, const char* name, int* v) {
    return Slot<NgxResult (*)(NgxParameter*, const char*, int*)>(p, GetInt)(p, name, v);
}
NgxResult GetUI(NgxParameter* p, const char* name, unsigned* v) {
    return Slot<NgxResult (*)(NgxParameter*, const char*, unsigned*)>(p, GetUint)(p, name, v);
}
NgxResult GetF(NgxParameter* p, const char* name, float* v) {
    return Slot<NgxResult (*)(NgxParameter*, const char*, float*)>(p, GetFloat)(p, name, v);
}

void Log(const char* message, int, int) {
    std::printf("DLSS: NGX %s%s", message,
                *message && message[std::strlen(message) - 1] == '\n' ? "" : "\n");
}

bool IsFloatFormat(vk::Format f) {
    switch (f) {
    case vk::Format::eR16G16B16A16Sfloat:
    case vk::Format::eR32G32B32A32Sfloat:
    case vk::Format::eB10G11R11UfloatPack32:
    case vk::Format::eR16G16B16Sfloat:
        return true;
    default:
        return false;
    }
}

int Quality(int preset) {
    switch (preset) {
    case BbSettings::NativeAA:
        return Dlaa;
    case BbSettings::Quality:
        return MaxQuality;
    case BbSettings::Balanced:
        return Balanced;
    case BbSettings::Performance:
        return MaxPerf;
    default:
        return UltraPerformance;
    }
}

ResourceVk Resource(const Fsr4Upscaler::Image& image, vk::ImageAspectFlags aspect, bool rw) {
    ResourceVk r{};
    r.resource.image_view_info = {
        .image_view = static_cast<VkImageView>(image.view),
        .image = static_cast<VkImage>(image.image),
        .subresource_range = {static_cast<VkImageAspectFlags>(aspect), 0, 1, 0, 1},
        .format = static_cast<VkFormat>(image.format),
        .width = image.width,
        .height = image.height,
    };
    r.type = 0;
    r.read_write = rw;
    return r;
}

} // namespace

struct DlssUpscaler::Impl {
    const Instance& instance;
    Scheduler& scheduler;
    bool available = false;
    bool fatal = false;
    std::string problem;

#ifdef _WIN32
    HMODULE core = nullptr;
#else
    void* core = nullptr;
#endif
    PFN_InitExt2 init = nullptr;
    PFN_GetParameters get_capability_parameters = nullptr;
    PFN_GetParameters allocate_parameters = nullptr;
    PFN_DestroyParameters destroy_parameters = nullptr;
    PFN_CreateFeature1 create_feature = nullptr;
    PFN_EvaluateFeature evaluate_feature = nullptr;
    PFN_ReleaseFeature release_feature = nullptr;
    PFN_Shutdown1 shutdown = nullptr;

    bool initialized = false;
    NgxParameter* capabilities = nullptr;
    NgxParameter* params = nullptr;
    NgxHandle* feature = nullptr;
#ifdef _WIN32
    HMODULE nr_module = nullptr;
    PFN_CreateFeature1 nr_create = nullptr;
    PFN_EvaluateFeature nr_evaluate = nullptr;
    PFN_ReleaseFeature nr_release = nullptr;
    PFN_Shutdown1 nr_shutdown = nullptr;
    void(*nr_unload)()=nullptr;
    NgxParameter* nr_params = nullptr;
    NgxHandle* nr_feature = nullptr;
    VideoCore::UniqueImage nr_image;
    vk::UniqueImageView nr_view;
    u32 nr_width = 0, nr_height = 0;
    u64 nr_frames = 0;
    bool nr_initialized = false, nr_failed = false, nr_attempted=false, nr_reset=true;
    std::array<float,3> nr_tuning{-1,-1,-1};

    void InitializeNr() {
        if(nr_attempted || !BbSettings::Get().dlss_nr_enabled)return;
        nr_attempted=true;
        // Explicit experimental opt-in. The normal DLSS path remains usable on
        // every initialization/evaluation failure and on unsupported machines.
        const char* opt = std::getenv("BB_NATIVE_NR");
        const char* directory = std::getenv("BB_DLSSNR_DIR");
        if (!opt || std::strcmp(opt, "1") != 0 || !directory) return;
        const auto path = std::filesystem::path(directory) / L"nvngx_dlssnr.dll";
        wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
        const auto forwarder=std::filesystem::path(executable).parent_path()/L"nvngx.dll_bbpc_nr.dll";
        nr_module = LoadLibraryW(forwarder.c_str());
        if (!nr_module) { std::puts("DLSS NR: native forwarding module load failed; SR retained"); return; }
        using NrInit=NgxResult(*)(const wchar_t*,unsigned long long,const wchar_t*,VkInstance,VkPhysicalDevice,VkDevice,
            PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,unsigned,const void*);
        const auto initialize = reinterpret_cast<NrInit>(GetProcAddress(nr_module, "BB_NR_Init"));
        nr_create = reinterpret_cast<PFN_CreateFeature1>(GetProcAddress(nr_module, "BB_NR_Create"));
        nr_evaluate = reinterpret_cast<PFN_EvaluateFeature>(GetProcAddress(nr_module, "BB_NR_Evaluate"));
        nr_release = reinterpret_cast<PFN_ReleaseFeature>(GetProcAddress(nr_module, "BB_NR_Release"));
        nr_shutdown = reinterpret_cast<PFN_Shutdown1>(GetProcAddress(nr_module, "BB_NR_Shutdown"));
        nr_unload = reinterpret_cast<void(*)()>(GetProcAddress(nr_module,"BB_NR_Unload"));
        if (!initialize || !nr_create || !nr_evaluate || !nr_release || !nr_shutdown || !nr_unload) {
            std::puts("DLSS NR: required Vulkan exports absent; SR retained"); return;
        }
        const auto& dispatch = VULKAN_HPP_DEFAULT_DISPATCHER;
        std::error_code ec;
        const auto data = std::filesystem::temp_directory_path(ec) / "bbport-native-nr";
        std::filesystem::create_directories(data, ec);
        const std::wstring data_path = data.wstring();
        const FeatureCommonInfo nr_info{.path_list={nullptr,0},.internal_data=nullptr,.logging={&Log,2,false}};
        const NgxResult result = initialize(path.c_str(),kApplicationId, data_path.c_str(),
            VkInstance(instance.GetInstance()), VkPhysicalDevice(instance.GetPhysicalDevice()),
            VkDevice(instance.GetDevice()), dispatch.vkGetInstanceProcAddr,
            dispatch.vkGetDeviceProcAddr, kNgxApiVersion, &nr_info);
        if (Failed(result)) { std::printf("DLSS NR: native initialization failed 0x%08x; SR retained\n", result); return; }
        nr_initialized = true;
        if (Failed(allocate_parameters(&nr_params)) || !nr_params || !ParameterAbiOk(nr_params)) {
            nr_failed = true;
            std::puts("DLSS NR: parameter ABI unavailable; SR retained"); return;
        }
        std::puts("DLSS NR: signed runtime initialized on the game's own Vulkan device (experimental)");
        BbSettings::Get().dlss_nr_supported=true;
    }

    Fsr4Upscaler::Image RecordNr(const Fsr4Upscaler::Frame& f) {
        const auto& settings=BbSettings::Get();
        if(!settings.dlss_nr_enabled){nr_reset=true;return f.color;}
        if(!nr_initialized)InitializeNr();
        if (!nr_initialized || nr_failed || !nr_params) return f.color;
        const std::array<float,3> tuning{settings.dlss_nr_intensity,settings.dlss_nr_tone,settings.dlss_nr_structure};
        if(tuning!=nr_tuning){nr_reset=true;nr_tuning=tuning;}
        if (!nr_feature || nr_width != f.render_width || nr_height != f.render_height) {
            nr_reset=true;
            if (nr_feature || nr_image) {
                scheduler.DeferOperation([handle=nr_feature,release=nr_release,
                    view=std::move(nr_view),image=std::move(nr_image)]()mutable{
                    if(handle)release(handle);view.reset();image.Destroy();
                });
                nr_feature=nullptr;
            }
            nr_width = f.render_width; nr_height = f.render_height;
            SetUI(nr_params, "CreationNodeMask", 1); SetUI(nr_params, "VisibilityNodeMask", 1);
            SetUI(nr_params, "DLSSNR.Width", nr_width); SetUI(nr_params, "DLSSNR.Height", nr_height);
            SetUI(nr_params, "Width", nr_width); SetUI(nr_params, "Height", nr_height);
            SetUI(nr_params, "OutWidth", nr_width); SetUI(nr_params, "OutHeight", nr_height);
            SetI(nr_params, "DLSSNR.Hint.Render.Preset", 0);
            SetI(nr_params,"DLSSNR.Enabled",1);SetI(nr_params,"DLSSNR.Style",0);
            SetI(nr_params,"DLSSNR.UseAutoMask",0);SetI(nr_params,"DLSSNR.UICorrection",0);
            SetF(nr_params,"DLSSNR.Intensity",tuning[0]);SetF(nr_params,"DLSSNR.LocalToneStrength",tuning[1]);
            SetF(nr_params,"DLSSNR.LocalStructureStrength",tuning[2]);SetF(nr_params,"DLSSNR.SkinStructureStrength",-1.f);
            const NgxResult result = nr_create(VkDevice(instance.GetDevice()), VkCommandBuffer(f.cmdbuf), 18, nr_params, &nr_feature);
            if (Failed(result) || !nr_feature) {
                nr_failed = true; std::printf("DLSS NR: feature 18 creation failed 0x%08x; SR retained\n", result); return f.color;
            }
            nr_image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
            nr_image.Create({.imageType=vk::ImageType::e2D, .format=f.color.format,
                .extent={nr_width,nr_height,1}, .mipLevels=1, .arrayLayers=1,
                .samples=vk::SampleCountFlagBits::e1, .tiling=vk::ImageTiling::eOptimal,
                .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc,
                .sharingMode=vk::SharingMode::eExclusive, .initialLayout=vk::ImageLayout::eUndefined});
            auto [status, view] = instance.GetDevice().createImageViewUnique({
                .image=vk::Image(nr_image), .viewType=vk::ImageViewType::e2D, .format=f.color.format,
                .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
            if (status != vk::Result::eSuccess) {nr_failed=true; return f.color;}
            nr_view = std::move(view);
            std::printf("DLSS NR: native feature 18 created, %ux%u before SR\n", nr_width,nr_height);
        }
        const Fsr4Upscaler::Image enhanced{vk::Image(nr_image), *nr_view, nr_width,nr_height,f.color.format};
        ResourceVk color=Resource(f.color,vk::ImageAspectFlagBits::eColor,false);
        ResourceVk depth=Resource(f.depth,vk::ImageAspectFlagBits::eDepth,false);
        ResourceVk motion=Resource(f.motion,vk::ImageAspectFlagBits::eColor,false);
        ResourceVk output=Resource(enhanced,vk::ImageAspectFlagBits::eColor,true);
        SetP(nr_params,"DLSSNR.Color",&color); SetP(nr_params,"DLSSNR.Depth",&depth);
        SetP(nr_params,"DLSSNR.MVec",&motion); SetP(nr_params,"DLSSNR.Output",&output);
        for (const char* surface : {"Color","Output","Depth","MVec"}) {
            const std::string key=std::string("DLSSNR.")+surface;
            SetUI(nr_params,(key+"SubrectBaseX").c_str(),0); SetUI(nr_params,(key+"SubrectBaseY").c_str(),0);
            SetUI(nr_params,(key+"SubrectWidth").c_str(),nr_width); SetUI(nr_params,(key+"SubrectHeight").c_str(),nr_height);
        }
        SetI(nr_params,"DLSSNR.Enabled",1); SetI(nr_params,"DLSSNR.Reset",f.reset || nr_frames==0 || nr_reset);
        SetI(nr_params,"DLSSNR.Style",0); SetI(nr_params,"DLSSNR.UseAutoMask",0);
        SetI(nr_params,"DLSSNR.DepthInverted",0);
        SetF(nr_params,"DLSSNR.Intensity",tuning[0]); SetF(nr_params,"DLSSNR.LocalToneStrength",tuning[1]);
        SetF(nr_params,"DLSSNR.LocalStructureStrength",tuning[2]); SetF(nr_params,"DLSSNR.SkinStructureStrength",-1.0f);
        SetF(nr_params,"DLSSNR.MVecScaleX",1.0f); SetF(nr_params,"DLSSNR.MVecScaleY",1.0f);
        const vk::ImageMemoryBarrier2 before{
            .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,
            .image=vk::Image(nr_image),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
        f.cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&before});
        const NgxResult result=nr_evaluate(VkCommandBuffer(f.cmdbuf),nr_feature,nr_params,nullptr);
        if (Failed(result)) {nr_failed=true;std::printf("DLSS NR: evaluate failed 0x%08x; SR retained\n",result);return f.color;}
        const vk::MemoryBarrier2 after{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask=vk::AccessFlagBits2::eMemoryRead};
        f.cmdbuf.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&after});
        if (++nr_frames==1 || nr_frames%600==0) std::printf("DLSS NR: native feature 18 evaluation success, count=%llu\n",(unsigned long long)nr_frames);
        nr_reset=false;
        return enhanced;
    }
#endif
    struct Key {
        u32 render_width, render_height, out_width, out_height;
        int quality;
        bool hdr;
        int model;
        bool operator==(const Key&) const = default;
    } key{};

    Impl(const Instance& instance_, Scheduler& scheduler_)
        : instance{instance_}, scheduler{scheduler_} {}

    void Fail(std::string why, bool permanent) {
        if (problem != why) {
            std::printf("DLSS: %s\n", why.c_str());
        }
        problem = std::move(why);
        fatal = fatal || permanent;
    }

    template <typename Fn>
    bool Load(Fn& fn, const char* name) {
#ifdef _WIN32
        fn = reinterpret_cast<Fn>(GetProcAddress(core, name));
#else
        fn = reinterpret_cast<Fn>(dlsym(core, name));
#endif
        return fn != nullptr;
    }

    bool LoadCore() {
#ifdef _WIN32
        // The driver records its NGX core under this key (nvsdk_ngx_loader.h does the same).
        wchar_t dir[MAX_PATH];
        DWORD size = sizeof(dir);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
                         L"FullPath", RRF_RT_REG_SZ, nullptr, dir, &size) == ERROR_SUCCESS) {
            core = LoadLibraryW((std::wstring(dir) + L"\\_nvngx.dll").c_str());
        }
#else
        core = dlopen("libnvidia-ngx.so.1", RTLD_NOW);
#endif
        if (!core) {
            Fail("NVIDIA NGX core not found (an NVIDIA driver with DLSS support is needed)", true);
            return false;
        }
        if (!Load(init, "NVSDK_NGX_VULKAN_Init_Ext2") ||
            !Load(get_capability_parameters, "NVSDK_NGX_VULKAN_GetCapabilityParameters") ||
            !Load(allocate_parameters, "NVSDK_NGX_VULKAN_AllocateParameters") ||
            !Load(destroy_parameters, "NVSDK_NGX_VULKAN_DestroyParameters") ||
            !Load(create_feature, "NVSDK_NGX_VULKAN_CreateFeature1") ||
            !Load(evaluate_feature, "NVSDK_NGX_VULKAN_EvaluateFeature") ||
            !Load(release_feature, "NVSDK_NGX_VULKAN_ReleaseFeature") ||
            !Load(shutdown, "NVSDK_NGX_VULKAN_Shutdown1")) {
            Fail("the NVIDIA NGX core lacks the Vulkan entry points", true);
            return false;
        }
        return true;
    }

    /// Directories searched for nvngx_dlss: BB_DLSS_DIR, then the executable's directory.
    std::vector<std::wstring> SearchPaths() {
        std::vector<std::wstring> paths;
        if (const char* env = std::getenv("BB_DLSS_DIR"); env && *env) {
            paths.push_back(std::filesystem::path(env).wstring());
        }
#ifdef _WIN32
        wchar_t exe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
            paths.push_back(std::filesystem::path(exe).parent_path().wstring());
        }
#else
        std::error_code ec;
        const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            paths.push_back(exe.parent_path().wstring());
        }
#endif
        return paths;
    }

    /// Checks the parameter vtable slots: values set through it read back unchanged.
    bool ParameterAbiOk(NgxParameter* p) {
        SetI(p, "bbport.Probe.I", 0x5A5A);
        SetUI(p, "bbport.Probe.UI", 77u);
        SetF(p, "bbport.Probe.F", 1.25f);
        int i = 0;
        unsigned ui = 0;
        float f = 0.0f;
        return !Failed(GetI(p, "bbport.Probe.I", &i)) && i == 0x5A5A &&
               !Failed(GetUI(p, "bbport.Probe.UI", &ui)) && ui == 77u &&
               !Failed(GetF(p, "bbport.Probe.F", &f)) && f == 1.25f;
    }

    void Initialize() {
        if (!instance.IsDlssCapable()) {
            Fail("the Vulkan device lacks VK_NVX_binary_import / VK_NVX_image_view_handle "
                 "(DLSS needs an NVIDIA RTX GPU)",
                 true);
            return;
        }
        if (!LoadCore()) {
            return;
        }
        std::error_code ec;
        const auto data_dir = std::filesystem::temp_directory_path(ec) / "bbport-ngx";
        std::filesystem::create_directories(data_dir, ec);
        const std::wstring data_path = data_dir.wstring();
        const auto paths = SearchPaths();
        std::vector<const wchar_t*> path_ptrs;
        for (const auto& p : paths) {
            path_ptrs.push_back(p.c_str());
        }
        const FeatureCommonInfo info{
            .path_list = {path_ptrs.data(), static_cast<unsigned>(path_ptrs.size())},
            .internal_data = nullptr,
            .logging = {&Log, std::getenv("BB_DLSS_LOG") ? 2 : 0, false},
        };
        const auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;
        const NgxResult r =
            init(kApplicationId, data_path.c_str(), VkInstance(instance.GetInstance()),
                 VkPhysicalDevice(instance.GetPhysicalDevice()), VkDevice(instance.GetDevice()),
                 dispatcher.vkGetInstanceProcAddr, dispatcher.vkGetDeviceProcAddr, kNgxApiVersion,
                 &info);
        if (Failed(r)) {
            Fail(fmt_hex("NGX initialization failed", r), true);
            return;
        }
        initialized = true;
        if (Failed(get_capability_parameters(&capabilities)) || !capabilities) {
            Fail("NGX capability query failed", true);
            return;
        }
        if (!ParameterAbiOk(capabilities)) {
            Fail("NGX parameter interface mismatch (unexpected vtable layout)", true);
            return;
        }
        int dlss_available = 0, needs_driver = 0;
        GetI(capabilities, "SuperSampling.NeedsUpdatedDriver", &needs_driver);
        if (Failed(GetI(capabilities, "SuperSampling.Available", &dlss_available)) ||
            !dlss_available) {
            int init_result = 0;
            GetI(capabilities, "SuperSampling.FeatureInitResult", &init_result);
            Fail(needs_driver ? "DLSS needs a newer NVIDIA driver"
                              : fmt_hex("DLSS unavailable (nvngx_dlss not found next to the "
                                        "executable or in BB_DLSS_DIR; tools/fetch_dlss.sh)",
                                        unsigned(init_result)),
                 true);
            return;
        }
        if (Failed(allocate_parameters(&params)) || !params) {
            Fail("NGX parameter allocation failed", true);
            return;
        }
        available = true;
        std::printf("DLSS: Super Resolution available (NGX core loaded)\n");
#ifdef _WIN32
        InitializeNr();
#endif
    }

    static std::string fmt_hex(const char* what, unsigned value) {
        char text[256];
        std::snprintf(text, sizeof(text), "%s (0x%08x)", what, value);
        return text;
    }

    bool Record(const Fsr4Upscaler::Frame& f) {
        if (!available) {
            return false;
        }
        // bbport: a scene at the output size (a native scene) is DLAA, whatever the preset.
        const bool native = f.render_width == f.output.width && f.render_height == f.output.height;
        const Key wanted{f.render_width, f.render_height, f.output.width, f.output.height,
                         native ? int(Dlaa) : Quality(f.preset), IsFloatFormat(f.color.format),BbSettings::Get().dlss_sr_model};
        if (!feature || !(wanted == key)) {
            if (feature) {
                scheduler.DeferOperation([old=feature,release=release_feature]{release(old);});
                feature = nullptr;
            }
            SetUI(params, "CreationNodeMask", 1);
            SetUI(params, "VisibilityNodeMask", 1);
            SetUI(params, "Width", wanted.render_width);
            SetUI(params, "Height", wanted.render_height);
            SetUI(params, "OutWidth", wanted.out_width);
            SetUI(params, "OutHeight", wanted.out_height);
            SetI(params, "PerfQualityValue", wanted.quality);
            // Official NGX SR model hints; NR feature 18 remains unchanged.
            // Explicit opt-in makes the performance/quality tradeoff measurable.
            {
                const u32 model=wanted.model>=1 && wanted.model<=3?u32(10+wanted.model):0;
                {
                    for(const char* mode:{"DLAA","Quality","Balanced","Performance","UltraPerformance"}) {
                        const std::string name=std::string("DLSS.Hint.Render.Preset.")+mode;
                        SetUI(params,name.c_str(),model);
                    }
                    if(model)std::printf("DLSS: requested SR model %c (NGX preset %u)\n",char('K'+wanted.model-1),model);
                }
            }
            // Motion vectors at render resolution, without jitter; standard depth.
            int flags = FlagMvLowRes;
            if (wanted.hdr) {
                flags |= FlagIsHdr | FlagAutoExposure;
            }
            SetI(params, "DLSS.Feature.Create.Flags", flags);
            SetI(params, "DLSS.Enable.Output.Subrects", 0);
            const NgxResult r = create_feature(VkDevice(instance.GetDevice()),
                                               VkCommandBuffer(f.cmdbuf), kFeatureSuperSampling,
                                               params, &feature);
            if (Failed(r) || !feature) {
                feature = nullptr;
                Fail(fmt_hex("DLSS feature creation failed", r), false);
                return false;
            }
            key = wanted;
            std::printf("DLSS: %ux%u -> %ux%u, quality mode %d, %s input\n", key.render_width,
                        key.render_height, key.out_width, key.out_height, key.quality,
                        key.hdr ? "HDR" : "LDR");
        }
        auto input_color=f.color;
#ifdef _WIN32
        input_color=RecordNr(f);
#endif
        ResourceVk color = Resource(input_color, vk::ImageAspectFlagBits::eColor, false);
        ResourceVk depth = Resource(f.depth, vk::ImageAspectFlagBits::eDepth, false);
        ResourceVk motion = Resource(f.motion, vk::ImageAspectFlagBits::eColor, false);
        ResourceVk output = Resource(f.output, vk::ImageAspectFlagBits::eColor, true);
        SetP(params, "Color", &color);
        SetP(params, "Depth", &depth);
        SetP(params, "MotionVectors", &motion);
        SetP(params, "Output", &output);
        SetF(params, "Jitter.Offset.X", f.jitter[0]);
        SetF(params, "Jitter.Offset.Y", f.jitter[1]);
        SetF(params, "Sharpness", 0.0f);
        SetI(params, "Reset", f.reset ? 1 : 0);
        SetF(params, "MV.Scale.X", 1.0f);
        SetF(params, "MV.Scale.Y", 1.0f);
        SetUI(params, "DLSS.Render.Subrect.Dimensions.Width", f.render_width);
        SetUI(params, "DLSS.Render.Subrect.Dimensions.Height", f.render_height);
        SetF(params, "FrameTimeDeltaInMsec", f.frame_ms);
        const NgxResult r =
            evaluate_feature(VkCommandBuffer(f.cmdbuf), feature, params, nullptr);
        if (Failed(r)) {
            Fail(fmt_hex("DLSS evaluation failed", r), false);
            return false;
        }
        problem.clear();
        return true;
    }

    ~Impl() {
        if (!initialized) {
            return;
        }
        scheduler.Finish();
#ifdef _WIN32
        if(nr_feature)nr_release(nr_feature);
        nr_view.reset(); nr_image=VideoCore::UniqueImage{};
        if(nr_params)destroy_parameters(nr_params);
        if(nr_initialized)nr_shutdown(VkDevice(instance.GetDevice()));
        if(nr_unload)nr_unload();
        if(nr_module)FreeLibrary(nr_module);
#endif
        if (feature) {
            release_feature(feature);
        }
        if (params) {
            destroy_parameters(params);
        }
        if (capabilities) {
            destroy_parameters(capabilities);
        }
        shutdown(VkDevice(instance.GetDevice()));
    }
};

DlssUpscaler::DlssUpscaler(const Instance& instance, Scheduler& scheduler)
    : impl{std::make_unique<Impl>(instance, scheduler)} {
    impl->Initialize();
}

DlssUpscaler::~DlssUpscaler() = default;

bool DlssUpscaler::Available() const noexcept {
    return impl->available;
}

bool DlssUpscaler::Record(const Fsr4Upscaler::Frame& frame) {
    return impl->Record(frame);
}

const char* DlssUpscaler::Problem() const noexcept {
    return impl->problem.empty() ? nullptr : impl->problem.c_str();
}

bool DlssUpscaler::Fatal() const noexcept {
    return impl->fatal;
}

} // namespace Vulkan
