// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deque>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "core/libraries/videoout/buffer.h"
#include "video_core/renderer_vulkan/host_passes/fsr_pass.h"
#include "video_core/renderer_vulkan/host_passes/hdr_pass.h"
#include "video_core/renderer_vulkan/host_passes/pp_pass.h"
#include "video_core/renderer_vulkan/vk_hdr_native.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Frontend {
class WindowSDL;
}

namespace AmdGpu {
struct Liverpool;
}

namespace Vulkan {

struct Frame {
    u32 width;
    u32 height;
    VmaAllocation allocation;
    vk::Image image;
    vk::ImageView image_view;
    vk::Fence present_done;
    vk::Semaphore ready_semaphore;
    u64 ready_tick;
    bool is_hdr{false};
    u8 id{};
    /// bbport: native HDR output (vk_hdr_native.h): a gain map, exchanged in PrepareFrame for
    /// the one made at the display buffer's display pass; `hdr_native` while it belongs to the
    /// frame shown (else the HDR encode falls back to expand).
    std::unique_ptr<HdrGainMap> hdr_gain;
    bool hdr_native{false};
};

enum SchedulerType {
    Draw,
    Present,
    CpuFlip,
};

class Rasterizer;

class Presenter {
public:
    Presenter(Frontend::WindowSDL& window, AmdGpu::Liverpool* liverpool);
    ~Presenter();

    HostPasses::PostProcessingPass::Settings& GetPPSettingsRef() {
        return pp_settings;
    }

    HostPasses::FsrPass::Settings& GetFsrSettingsRef() {
        return fsr_settings;
    }

    Frontend::WindowSDL& GetWindow() const {
        return window;
    }

    Rasterizer& GetRasterizer() const {
        return *rasterizer.get();
    }

    bool IsHDRSupported() const {
        return swapchain.HasHDR();
    }

    /// The game's own HDR request (VideoOut, every flip). bbport: off (Swapchain::HasHDR is
    /// false), so it leaves the HDR output (hdr in bbport.ini, PresentHdr) and pp_settings alone.
    void SetHDR(bool enable) {
        if (!IsHDRSupported()) {
            return;
        }
        swapchain.SetHDR(enable);
        pp_settings.hdr = enable ? 1 : 0;
    }

    VideoCore::Image& RegisterVideoOutSurface(
        const Libraries::VideoOut::BufferAttributeGroup& attribute, VAddr cpu_address) {
        vo_buffers_addr.emplace_back(cpu_address);
        auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
        const auto image_id = texture_cache.FindImage(desc);
        auto& image = texture_cache.GetImage(image_id);
        image.usage.vo_surface = 1u;
        return image;
    }

    bool IsVideoOutSurface(const AmdGpu::ColorBuffer& color_buffer) const;

    Frame* PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                        VAddr cpu_address);

    Frame* PrepareBlankFrame(bool present_thread);

    void Present(Frame* frame, bool is_reusing_frame = false, bool is_game_frame = true);
    Frame* PrepareLastFrame();

private:
    Frame* GetRenderFrame();

    void RecreateFrame(Frame* frame, u32 width, u32 height);

    void SetExpectedGameSize(s32 width, s32 height);

    /// bbport: HDR output. A swapchain-sized image of the SDR format (the frame letterboxed into
    /// it, or the menu layer drawn into it), sampled by the HDR encode pass.
    struct HdrTarget {
        vk::Image image{};
        VmaAllocation allocation{};
        vk::ImageView view{};
    };
    void CreateHdrTarget(HdrTarget& target, vk::Extent2D extent, const char* name);
    void DestroyHdrTarget(const HdrTarget& target);
    /// The targets at the swapchain's size; the old ones go once the GPU is past them.
    void EnsureHdrTargets(vk::Extent2D extent);
    /// Records the frame (letterboxed), the menu and the HDR encode into the swapchain image.
    void PresentHdr(vk::CommandBuffer cmdbuf, Frame* frame, vk::Extent2D extent);
    /// One line when the HDR output's mode starts or changes; in native mode one when frames
    /// stop or start having a gain map (at most every 5 s, with the switches in between).
    void LogHdrState(bool scrgb, int mode, bool native_frame);

private:
    float expected_ratio{1920.0 / 1080.0f};
    u32 expected_frame_width{1920};
    u32 expected_frame_height{1080};

    Frontend::WindowSDL& window;
    Instance instance;
    HostPasses::FsrPass fsr_pass;
    HostPasses::FsrPass::Settings fsr_settings{};
    HostPasses::PostProcessingPass::Settings pp_settings{};
    HostPasses::PostProcessingPass pp_pass;
    HostPasses::HdrOutputPass hdr_pass; ///< bbport: HDR output encode (present thread)
    HdrTarget hdr_game{}, hdr_overlay{}; ///< bbport: HDR output's SDR composite and menu layer
    vk::Extent2D hdr_extent{};
    int hdr_logged_mode = -1;   ///< bbport: LogHdrState (present thread)
    int hdr_native_state = -1;  ///< native mode: the last frame had a gain map (1) or not (0)
    int hdr_logged_native = -1; ///< ... as last logged
    u32 hdr_native_switches = 0;
    std::chrono::steady_clock::time_point hdr_next_log{};
    std::mutex passes_mutex; ///< bbport: the passes are recorded on the recording threads
    AmdGpu::Liverpool* liverpool;
    Scheduler draw_scheduler;
    std::deque<u64> recent_frame_ticks; ///< bbport: BB_FRAMES_AHEAD bound (PrepareFrame)
    Scheduler present_scheduler;
    Scheduler flip_scheduler;
    Swapchain swapchain;
    Runtime runtime;
    std::unique_ptr<Rasterizer> rasterizer;
    VideoCore::TextureCache& texture_cache;
    vk::UniqueCommandPool command_pool;
    std::vector<Frame> present_frames;
    std::queue<Frame*> free_queue;
    Frame* last_submit_frame;
    std::mutex free_mutex;
    std::condition_variable free_cv;
    std::condition_variable_any frame_cv;
    std::vector<VAddr> vo_buffers_addr;
};

} // namespace Vulkan
