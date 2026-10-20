// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/debug.h"
#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "sdl_window.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderdoc.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "bbport_overlay.h"
#include "bbport_settings.h"
#include "bbport_timeline.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/texture_cache/image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <system_error>
#include <vector>
#include <vk_mem_alloc.h>

namespace Vulkan {

bool CanBlitToSwapchain(const vk::PhysicalDevice physical_device, vk::Format format) {
    const vk::FormatProperties props{physical_device.getFormatProperties(format)};
    return static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);
}

[[nodiscard]] vk::ImageSubresourceLayers MakeImageSubresourceLayers() {
    return vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 dst_width,
                                          s32 dst_height, s32 offset_x, s32 offset_y) {
    return vk::ImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            std::array{
                vk::Offset3D{
                    .x = offset_x,
                    .y = offset_y,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = offset_x + dst_width,
                    .y = offset_y + dst_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlitStretch(s32 frame_width, s32 frame_height,
                                                 s32 swapchain_width, s32 swapchain_height) {
    return MakeImageBlit(frame_width, frame_height, swapchain_width, swapchain_height, 0, 0);
}

static vk::Rect2D FitImage(s32 frame_width, s32 frame_height, s32 swapchain_width,
                           s32 swapchain_height) {
    float frame_aspect = static_cast<float>(frame_width) / frame_height;
    float swapchain_aspect = static_cast<float>(swapchain_width) / swapchain_height;

    u32 dst_width = swapchain_width;
    u32 dst_height = swapchain_height;

    if (frame_aspect > swapchain_aspect) {
        dst_height = static_cast<s32>(swapchain_width / frame_aspect);
    } else {
        dst_width = static_cast<s32>(swapchain_height * frame_aspect);
    }

    const s32 offset_x = (swapchain_width - dst_width) / 2;
    const s32 offset_y = (swapchain_height - dst_height) / 2;

    return vk::Rect2D{{offset_x, offset_y}, {dst_width, dst_height}};
}

[[nodiscard]] vk::ImageBlit MakeImageBlitFit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                             s32 swapchain_height) {
    const auto& dst_rect = FitImage(frame_width, frame_height, swapchain_width, swapchain_height);

    return MakeImageBlit(frame_width, frame_height, dst_rect.extent.width, dst_rect.extent.height,
                         dst_rect.offset.x, dst_rect.offset.y);
}

// bbport: screenshot capture and ImGui overlays removed; the port presents directly.

Presenter::Presenter(Frontend::WindowSDL& window_, AmdGpu::Liverpool* liverpool_)
    : window{window_}, liverpool{liverpool_},
      instance{window, EmulatorSettings.GetGpuId(), EmulatorSettings.IsVkValidationEnabled(),
               EmulatorSettings.IsVkCrashDiagnosticEnabled()},
      draw_scheduler{instance, true}, present_scheduler{instance}, flip_scheduler{instance},
      swapchain{instance, window}, runtime{instance, draw_scheduler},
      rasterizer{std::make_unique<Rasterizer>(instance, draw_scheduler, runtime, liverpool)},
      texture_cache{rasterizer->GetTextureCache()} {
    const u32 num_images = swapchain.GetImageCount();
    const vk::Device device = instance.GetDevice();

    // Create presentation frames.
    present_frames.resize(num_images);
    for (u32 i = 0; i < num_images; i++) {
        Frame& frame = present_frames[i];
        frame.id = i;
        auto fence = Check<"create present done fence">(
            device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
        frame.present_done = fence;
        free_queue.push(&frame);
    }

    fsr_settings.enable = EmulatorSettings.IsFsrEnabled();
    fsr_settings.use_rcas = EmulatorSettings.IsRcasEnabled();
    fsr_settings.rcas_attenuation =
        static_cast<float>(EmulatorSettings.GetRcasAttenuation() / 1000.f);

    fsr_pass.Create(device, instance.GetAllocator(), num_images);
    pp_pass.Create(device, swapchain.GetSurfaceFormat().format);
    BbOverlay::Init(instance, swapchain.GetSurfaceFormat().format, num_images);
    // bbport: HDR output (hdr in bbport.ini): frames, passes and the menu stay SDR; the encode
    // pass writes the HDR swapchain format chosen at start.
    if (const vk::Format hdr_format = swapchain.GetHdrFormat().format;
        hdr_format != vk::Format::eUndefined) {
        hdr_pass.Create(device, hdr_format);
    }

}

Presenter::~Presenter() {

    draw_scheduler.Finish();
    present_scheduler.Finish();
    flip_scheduler.Finish();
    Check(draw_scheduler.CommandBuffer().reset());
    Check(present_scheduler.CommandBuffer().reset());
    Check(flip_scheduler.CommandBuffer().reset());

    const vk::Device device = instance.GetDevice();
    for (auto& frame : present_frames) {
        vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
        device.destroyImageView(frame.image_view);
        device.destroyFence(frame.present_done);
    }
    DestroyHdrTarget(hdr_game);
    DestroyHdrTarget(hdr_overlay);
}

void Presenter::CreateHdrTarget(HdrTarget& target, vk::Extent2D extent, const char* name) {
    const vk::Device device = instance.GetDevice();
    // The SDR format: the blit's and the menu pipeline's (BbOverlay::Init); sRGB encoded values,
    // decoded by the encode pass. Format support is checked by Swapchain::FindPresentFormat.
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = swapchain.GetSurfaceFormat().format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eSampled,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    const VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VkImage image{};
    const VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);
    const VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info,
                                           &alloc_info, &image, &target.allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating the HDR output image with error {}",
                     vk::to_string(vk::Result{result}));
        UNREACHABLE();
    }
    target.image = vk::Image{image};
    SetObjectName(device, target.image, "HDR output {}", name);
    target.view = Check<"create HDR output image view">(device.createImageView({
        .image = target.image,
        .viewType = vk::ImageViewType::e2D,
        .format = image_info.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    }));
}

void Presenter::DestroyHdrTarget(const HdrTarget& target) {
    if (target.view) {
        instance.GetDevice().destroyImageView(target.view);
    }
    if (target.image) {
        vmaDestroyImage(instance.GetAllocator(), target.image, target.allocation);
    }
}

void Presenter::EnsureHdrTargets(vk::Extent2D extent) {
    if (hdr_game.image && hdr_extent == extent) {
        return;
    }
    if (hdr_game.image) {
        // Presents still in flight may read the old size's images: they go once the GPU has
        // finished this present's submission (later than every earlier one, same queue).
        present_scheduler.DeferOperation(
            [this, game = hdr_game, overlay = hdr_overlay] {
                DestroyHdrTarget(game);
                DestroyHdrTarget(overlay);
            });
    }
    CreateHdrTarget(hdr_game, extent, "composite");
    CreateHdrTarget(hdr_overlay, extent, "menu layer");
    hdr_extent = extent;
}

void Presenter::PresentHdr(vk::CommandBuffer cmdbuf, Frame* frame, vk::Extent2D extent) {
    EnsureHdrTargets(extent);
    const vk::Image swapchain_image = swapchain.Image();
    const bool overlay = BbOverlay::Visible();
    const vk::ImageSubresourceRange color_range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const auto barrier = [&](vk::Image image, vk::ImageLayout old_layout,
                             vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage,
                             vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage,
                             vk::AccessFlags2 dst_access) {
        return vk::ImageMemoryBarrier2{
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = color_range,
        };
    };
    const auto barriers = [&](std::span<const vk::ImageMemoryBarrier2> image_barriers) {
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = static_cast<u32>(image_barriers.size()),
            .pImageMemoryBarriers = image_barriers.data(),
        });
    };
    using Stage = vk::PipelineStageFlagBits2;
    using Access = vk::AccessFlagBits2;
    using Layout = vk::ImageLayout;

    // The composite and the menu layer are rewritten whole: their old contents (read by the
    // previous present's encode, same queue: the all-commands source scope orders it) go.
    // The frame as in the SDR path: General -> transfer source -> General.
    std::array<vk::ImageMemoryBarrier2, 3> pre{
        barrier(hdr_game.image, Layout::eUndefined, Layout::eTransferDstOptimal,
                Stage::eAllCommands, Access::eNone, Stage::eTransfer, Access::eTransferWrite),
        barrier(frame->image, Layout::eGeneral, Layout::eTransferSrcOptimal, Stage::eAllCommands,
                Access::eColorAttachmentWrite, Stage::eTransfer, Access::eTransferRead),
        barrier(hdr_overlay.image, Layout::eUndefined, Layout::eTransferDstOptimal,
                Stage::eAllCommands, Access::eNone, Stage::eTransfer, Access::eTransferWrite),
    };
    barriers(std::span{pre.data(), overlay ? 3u : 2u});
    const vk::ClearColorValue black{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    cmdbuf.clearColorImage(hdr_game.image, Layout::eTransferDstOptimal, black, color_range);
    if (overlay) {
        const vk::ClearColorValue transparent{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
        cmdbuf.clearColorImage(hdr_overlay.image, Layout::eTransferDstOptimal, transparent,
                               color_range);
    }
    const vk::MemoryBarrier2 clear_done{
        .srcStageMask = Stage::eTransfer,
        .srcAccessMask = Access::eTransferWrite,
        .dstStageMask = Stage::eTransfer,
        .dstAccessMask = Access::eTransferWrite,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &clear_done,
    });
    cmdbuf.blitImage(frame->image, Layout::eTransferSrcOptimal, hdr_game.image,
                     Layout::eTransferDstOptimal,
                     MakeImageBlitFit(frame->width, frame->height, extent.width, extent.height),
                     vk::Filter::eLinear);

    std::array<vk::ImageMemoryBarrier2, 4> post{
        barrier(hdr_game.image, Layout::eTransferDstOptimal, Layout::eShaderReadOnlyOptimal,
                Stage::eTransfer, Access::eTransferWrite, Stage::eFragmentShader,
                Access::eShaderSampledRead),
        barrier(frame->image, Layout::eTransferSrcOptimal, Layout::eGeneral, Stage::eTransfer,
                Access::eTransferRead, Stage::eAllCommands, Access::eColorAttachmentWrite),
        // The acquire semaphore is waited on at all commands: this transition comes after it.
        barrier(swapchain_image, Layout::eUndefined, Layout::eColorAttachmentOptimal,
                Stage::eColorAttachmentOutput, Access::eNone, Stage::eColorAttachmentOutput,
                Access::eColorAttachmentWrite),
        barrier(hdr_overlay.image, Layout::eTransferDstOptimal, Layout::eColorAttachmentOptimal,
                Stage::eTransfer, Access::eTransferWrite, Stage::eColorAttachmentOutput,
                Access::eColorAttachmentRead | Access::eColorAttachmentWrite),
    };
    barriers(std::span{post.data(), overlay ? 4u : 3u});

    // bbport: the settings menu / FPS counter, at display resolution, into its own layer: the
    // encode shows it at paper white over the (possibly expanded) frame.
    if (overlay) {
        BbOverlay::Render(cmdbuf, hdr_overlay.view, extent);
        const std::array menu_done{
            barrier(hdr_overlay.image, Layout::eColorAttachmentOptimal,
                    Layout::eShaderReadOnlyOptimal, Stage::eColorAttachmentOutput,
                    Access::eColorAttachmentWrite, Stage::eFragmentShader,
                    Access::eShaderSampledRead),
        };
        barriers(menu_done);
    }

    const auto& settings = BbSettings::Get();
    const bool scrgb =
        swapchain.GetHdrFormat().colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
    // bbport: native mode (hdr_mode, live): the frame's gain map when it has one (made for the
    // frame this display buffer holds, vk_hdr_native.h), else plain SDR at paper white for this
    // frame (title, menus, loading, movies: nothing in them is meant to be brighter).
    const int mode = std::clamp(settings.hdr_mode.load(), 0, BbSettings::HdrModeCount - 1);
    const bool native = mode == BbSettings::HdrModeNative && frame->hdr_native && frame->hdr_gain;
    LogHdrState(scrgb, mode, native);
    // BB_HDR_DEBUG=1: the gain as a heat map over a grey picture (hdr_output.frag).
    static const bool debug = [] {
        const char* env = std::getenv("BB_HDR_DEBUG");
        return env && env[0] == '1';
    }();
    // The gain map covers the frame, which the blit above letterboxed into this rectangle.
    const vk::Rect2D fit = FitImage(s32(frame->width), s32(frame->height), s32(extent.width),
                                    s32(extent.height));
    const HostPasses::HdrOutputPass::Settings hdr{
        .paper_white = std::clamp(settings.hdr_paper_white.load(), BbSettings::HdrPaperWhiteMin,
                                  BbSettings::HdrPaperWhiteMax),
        .peak = std::clamp(settings.hdr_peak.load(), BbSettings::HdrPeakMin,
                           BbSettings::HdrPeakMax),
        .boost = mode == BbSettings::HdrModeNative && !native
                     ? 0.0f
                     : std::clamp(settings.hdr_boost.load(), 0.0f, 1.0f),
        .flags = (scrgb ? HostPasses::HdrOutputPass::FlagScRgb : 0u) |
                 (overlay ? HostPasses::HdrOutputPass::FlagOverlay : 0u) |
                 (native ? HostPasses::HdrOutputPass::FlagNative : 0u) |
                 (debug ? HostPasses::HdrOutputPass::FlagDebug : 0u) |
                 (mode == BbSettings::HdrModeNative && !native ? HostPasses::HdrOutputPass::FlagUiFrame : 0u),
        .gain_offset = {float(fit.offset.x), float(fit.offset.y)},
        .gain_scale = {1.0f / float(std::max(fit.extent.width, 1u)),
                       1.0f / float(std::max(fit.extent.height, 1u))},
        .ui_white = mode == BbSettings::HdrModeNative
                        ? std::clamp(settings.hdr_ui_brightness.load(), BbSettings::HdrUiBrightnessMin,
                                     BbSettings::HdrUiBrightnessMax)
                        : 1.0f,
        .ui_vivid = mode == BbSettings::HdrModeNative ? std::clamp(settings.hdr_ui_color.load(), 0.0f, 1.0f) : 0.0f,
    };
    // bbport: MaxCLL, the brightest the content gets: native mode's highlights reach the peak;
    // expand mode's SDR white goes to boost's share of it. It changes with the settings, not
    // per frame: a display re-mapping its tone curve on every fallback would pump.
    const float expand_white =
        std::max(1.0f + (hdr.peak / hdr.paper_white - 1.0f) * hdr.boost, 1.0f);
    const float content_peak = mode == BbSettings::HdrModeNative
                                   ? hdr.peak
                                   : std::min(hdr.peak, hdr.paper_white * expand_white);
    swapchain.SetHdrMetadata(hdr.peak, content_peak, hdr.paper_white);
    if (native) {
        // Written on the draw scheduler's queue before this submission's wait (Present).
        const std::array gain_ready{
            barrier(vk::Image(frame->hdr_gain->image), Layout::eGeneral, Layout::eGeneral,
                    Stage::eAllCommands, Access::eMemoryWrite, Stage::eFragmentShader,
                    Access::eShaderSampledRead),
        };
        barriers(gain_ready);
    }
    hdr_pass.Render(cmdbuf, hdr_game.view, overlay ? hdr_overlay.view : hdr_game.view,
                    native ? *frame->hdr_gain->view : hdr_game.view,
                    native ? Layout::eGeneral : Layout::eShaderReadOnlyOptimal,
                    swapchain.ImageView(), extent, hdr);

    const std::array to_present{
        barrier(swapchain_image, Layout::eColorAttachmentOptimal, Layout::ePresentSrcKHR,
                Stage::eColorAttachmentOutput, Access::eColorAttachmentWrite,
                Stage::eBottomOfPipe, Access::eNone),
    };
    barriers(to_present);
}

void Presenter::LogHdrState(bool scrgb, int mode, bool native_frame) {
    if (mode != hdr_logged_mode) {
        hdr_logged_mode = mode;
        hdr_native_state = hdr_logged_native = -1;
        std::printf("HDR output: %s, mode %s (%s)\n",
                    scrgb ? "scRGB (FP16)" : "HDR10 (PQ, BT.2020)", BbSettings::HdrModeNames[mode],
                    mode == BbSettings::HdrModeNative
                        ? "highlights from the game's HDR scene, plain SDR where a frame has none"
                        : "highlights expanded from the SDR picture");
    }
    if (mode != BbSettings::HdrModeNative) {
        return;
    }
    // Menus, loading screens and movies have no scene: frames come and go without a gain map.
    const int state = native_frame ? 1 : 0;
    if (state != hdr_native_state) {
        hdr_native_state = state;
        ++hdr_native_switches;
    }
    const auto now = std::chrono::steady_clock::now();
    if (hdr_native_state == hdr_logged_native || now < hdr_next_log) {
        return;
    }
    std::printf("HDR native: %s",
                state ? "gain maps from the game's scene"
                      : "no gain map (no scene: title, menu, loading, movie), SDR");
    if (hdr_native_switches > 1) {
        std::printf(" (%u switches since the last line)", hdr_native_switches);
    }
    std::printf("\n");
    hdr_logged_native = state;
    hdr_native_switches = 0;
    hdr_next_log = now + std::chrono::seconds(5);
}

bool Presenter::IsVideoOutSurface(const AmdGpu::ColorBuffer& color_buffer) const {
    return std::ranges::find(vo_buffers_addr, color_buffer.Address()) != vo_buffers_addr.cend();
}

void Presenter::RecreateFrame(Frame* frame, u32 width, u32 height) {
    const vk::Device device = instance.GetDevice();
    if (frame->image_view) {
        device.destroyImageView(frame->image_view);
    }
    if (frame->image) {
        vmaDestroyImage(instance.GetAllocator(), frame->image, frame->allocation);
    }

    const vk::Format format = swapchain.GetSurfaceFormat().format;
    const vk::ImageCreateInfo image_info = {
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &frame->allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}",
                     vk::to_string(vk::Result{result}));
        UNREACHABLE();
    }
    frame->image = vk::Image{unsafe_image};
    SetObjectName(device, frame->image, "Frame image #{}", frame->id);

    const vk::ImageViewCreateInfo view_info = {
        .image = frame->image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    auto view = Check<"create frame image view">(device.createImageView(view_info));
    frame->image_view = view;
    frame->width = width;
    frame->height = height;

    frame->is_hdr = swapchain.GetHDR();
}

Frame* Presenter::PrepareLastFrame() {
    if (last_submit_frame == nullptr) {
        return nullptr;
    }

    Frame* frame = last_submit_frame;

    while (true) {
        vk::Result result = instance.GetDevice().waitForFences(frame->present_done, false,
                                                               std::numeric_limits<u64>::max());
        if (result == vk::Result::eSuccess) {
            break;
        }
        if (result == vk::Result::eTimeout) {
            continue;
        }
        if (result == vk::Result::eErrorDeviceLost) {
            Breadcrumbs::ReportDeviceLost("waiting for a frame");
        }
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
    }

    auto& scheduler = flip_scheduler;
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto pre_barrier =
        vk::ImageMemoryBarrier2{.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
                                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                .newLayout = vk::ImageLayout::eGeneral,
                                .image = frame->image,
                                .subresourceRange{frame_subresources}};

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

static vk::Format GetFrameViewFormat(const Libraries::VideoOut::PixelFormat format) {
    switch (format) {
    case Libraries::VideoOut::PixelFormat::A8B8G8R8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A8R8G8B8Srgb:
        return vk::Format::eB8G8R8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A2R10G10B10:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Bt2020Pq:
        return vk::Format::eA2R10G10B10UnormPack32;
    default:
        break;
    }
    UNREACHABLE_MSG("Unknown format={}", static_cast<u32>(format));
    return {};
}

Frame* Presenter::PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                               VAddr cpu_address) {
    BbTimeline::Note(BbTimeline::PipeTask, 4, draw_scheduler.CurrentTick());
    // bbport: scaled upscaler presets: the output-size display buffer drawn by the port.
    TemporalUpscaler::Display display{};
    const bool upscaled = rasterizer->GetUpscaler().DisplayOverride(cpu_address, display);
    VideoCore::ImageId image_id{};
    if (!upscaled) {
        auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
        image_id = texture_cache.FindImage(desc);
        texture_cache.UpdateImage(image_id);
    }

    Frame* frame = GetRenderFrame();
    // bbport: native HDR output: the gain map made at this display buffer's display pass (for
    // the frame it holds, a frame earlier in the command stream) goes with the frame to the
    // present thread; the frame's old one, presented (its fence was waited above), goes back.
    frame->hdr_native = rasterizer->GetHdrNative().TakeGainMap(cpu_address, frame->hdr_gain);

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    // bbport: recorded like draws, on the recording threads. CommandBuffer() would first wait
    // for them to record everything handed over before (~90 us a frame of the draw recording
    // thread) and then submit from this thread.
    draw_scheduler.EndRendering();

    VideoCore::ImageViewInfo view_info{};
    view_info.format = GetFrameViewFormat(attribute.attrib.pixel_format);
    // Exclude alpha from output frame to avoid blending with UI.
    view_info.mapping.a = vk::ComponentSwizzle::eOne;

    vk::ImageView image_view{};
    vk::Extent2D image_size{};
    if (upscaled) {
        const auto device = instance.GetDevice();
        image_view = Check(device.createImageView({
            .image = display.image,
            .viewType = vk::ImageViewType::e2D,
            .format = view_info.format,
            .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne},
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        }));
        draw_scheduler.DeferOperation([device, image_view] { device.destroyImageView(image_view); });
        image_size = vk::Extent2D{display.width, display.height};
        draw_scheduler.Record([image = display.image](vk::CommandBuffer cmdbuf) {
            const vk::ImageMemoryBarrier2 to_read{
                .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = image,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_read});
        });
    } else {
        auto& image = texture_cache.GetImage(image_id);
        image_view = *image.FindView(view_info).image_view;
        image_size = vk::Extent2D{image.info.size.width, image.info.size.height};
        // bbport: BB_PRESENT_DUMP_TRIGGER for the game's own display buffer too.
        if (auto& upscaler = rasterizer->GetUpscaler(); upscaler.PresentDumpDue()) {
            runtime.Transit(&image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eTransfer,
                            vk::AccessFlagBits2::eTransferRead);
            runtime.FlushBarriers();
            upscaler.DumpPresented(image.GetImage(), image_size.width, image_size.height,
                                   view_info.format);
        }
        runtime.Transit(&image, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::PipelineStageFlagBits2::eFragmentShader,
                        vk::AccessFlagBits2::eShaderRead);
        runtime.FlushBarriers();
    }
    expected_ratio = static_cast<float>(image_size.width) / static_cast<float>(image_size.height);

    // Vulkan has no sRGB variant of the 10-bit format, so an A2R10G10B10Srgb buffer reaches
    // the post process pass still sRGB encoded and has to be decoded there instead.
    pp_settings.srgb_input =
        attribute.attrib.pixel_format == Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb;

    // Numbered here, in command order; marked where the commands are recorded.
    const u32 stream = draw_scheduler.CrumbStream();
    const bool crumbs = Breadcrumbs::Enabled();
    const u32 fsr_crumb = crumbs ? Breadcrumbs::Note(stream, {.name = "presenter FSR"}) : 0;
    const u32 pp_crumb = crumbs ? Breadcrumbs::Note(stream, {.name = "presenter post process"}) : 0;
    draw_scheduler.Record([this, frame, image_view, image_size, frame_subresources, stream,
                           fsr_crumb, pp_crumb, fsr = fsr_settings,
                           pp = pp_settings](vk::CommandBuffer cmdbuf) {
        // Frames of different submissions may be recorded on two threads at once.
        std::scoped_lock lock{passes_mutex};
        const auto pre_barrier = vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .image = frame->image,
            .subresourceRange{frame_subresources},
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &pre_barrier,
        });
        if (fsr_crumb) {
            Breadcrumbs::Mark(cmdbuf, stream, fsr_crumb, false);
        }
        const vk::ImageView input = fsr_pass.Render(cmdbuf, image_view, image_size,
                                                    {frame->width, frame->height}, fsr,
                                                    frame->is_hdr);
        if (fsr_crumb) {
            Breadcrumbs::Mark(cmdbuf, stream, fsr_crumb, true);
            Breadcrumbs::Mark(cmdbuf, stream, pp_crumb, false);
        }
        pp_pass.Render(cmdbuf, input, image_size, *frame, pp);
        if (pp_crumb) {
            Breadcrumbs::Mark(cmdbuf, stream, pp_crumb, true);
        }
    });

    // Flush frame creation commands.
    BbStats::frame_number.fetch_add(1, std::memory_order_relaxed); // bbport: game frames shown
    frame->ready_semaphore = draw_scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = draw_scheduler.CurrentTick();
    SubmitInfo info{};
    draw_scheduler.Flush(info);
    BbTimeline::Note(BbTimeline::PipeTask, 5, frame->ready_tick);

    // bbport: the GPU command thread runs at most BB_FRAMES_AHEAD (default 1) guest frames
    // ahead of the GPU: it waits here for the frame that many flips back. When the GPU is the
    // bottleneck it finishes frames at an even rate; without this bound the command thread ran
    // ahead and then blocked wherever a resource ran out, so flips (and the guest's frame
    // timing) came in bursts: 12.5/25 ms alternation at 80 FPS. 0 turns it off.
    static const u32 frames_ahead = [] {
        const char* env = std::getenv("BB_FRAMES_AHEAD");
        return env ? u32(std::max(0, std::atoi(env))) : 1u;
    }();
    if (frames_ahead) {
        recent_frame_ticks.push_back(frame->ready_tick);
        while (recent_frame_ticks.size() > frames_ahead) {
            const u64 tick = recent_frame_ticks.front();
            recent_frame_ticks.pop_front();
            if (recent_frame_ticks.size() == frames_ahead) {
                BbStats::WaitTimer timer{BbStats::present_wait_ns};
                draw_scheduler.Wait(tick);
            }
        }
    }
    return frame;
}

Frame* Presenter::PrepareBlankFrame(bool present_thread) {
    // Request a free presentation frame.
    Frame* frame = GetRenderFrame();
    frame->hdr_native = false; // bbport: no game frame, no gain map

    auto& scheduler = present_thread ? present_scheduler : draw_scheduler;
    scheduler.EndRendering();

    const auto cmdbuf = scheduler.CommandBuffer();

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const auto pre_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const auto post_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const vk::RenderingAttachmentInfo attachment = {
        .imageView = frame->image_view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingInfo rendering_info = {
        .renderArea =
            {
                .extent = {frame->width, frame->height},
            },
        .layerCount = 1,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &attachment,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    cmdbuf.beginRendering(rendering_info);
    cmdbuf.endRendering();

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &post_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

void Presenter::Present(Frame* frame, bool is_reusing_frame, bool is_game_frame) {
    // Free the frame for reuse
    const auto free_frame = [&] {
        if (!is_reusing_frame) {
            last_submit_frame = frame;
            std::scoped_lock fl{free_mutex};
            free_queue.push(frame);
            free_cv.notify_one();
        }
    };

    // bbport: nothing to present to while the window is minimised (a 0x0 surface, issue #31).
    if (window.GetWidth() == 0 || window.GetHeight() == 0) {
        free_frame();
        return;
    }
    // Recreate the swapchain if the window was resized (or was minimised at the last try).
    if (window.GetWidth() != swapchain.GetWidth() || window.GetHeight() != swapchain.GetHeight() ||
        !swapchain.IsPresentable()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
        if (!swapchain.IsPresentable()) {
            free_frame();
            return;
        }
    }

    if (!swapchain.AcquireNextImage()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
        if (!swapchain.AcquireNextImage()) {
            // User resizes the window too fast and GPU can't keep up. Skip this frame.
            LOG_WARNING(Render_Vulkan, "Skipping frame!");
            free_frame();
            return;
        }
    }

    // Reset fence for queue submission. Do it here instead of GetRenderFrame() because we may
    // skip frame because of slow swapchain recreation. If a frame skip occurs, we skip signal
    // the frame's present fence and future GetRenderFrame() call will hang waiting for this frame.
    const auto reset_result = instance.GetDevice().resetFences(frame->present_done);
    ASSERT_MSG(reset_result == vk::Result::eSuccess,
               "Unexpected error resetting present done fence: {}", vk::to_string(reset_result));

    // bbport: the game frame is blitted (letterboxed) straight into the swapchain image.
    const vk::Image swapchain_image = swapchain.Image();
    auto& scheduler = present_scheduler;
    const auto cmdbuf = scheduler.CommandBuffer();
    DumpFinalFrameIfDue(instance, scheduler, cmdbuf, frame->image, frame->width, frame->height,
                        swapchain.GetSurfaceFormat().format);

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Present",
        });
    }

    // bbport: HDR output (hdr in bbport.ini) while the swapchain has the HDR format.
    const bool hdr_output = swapchain.IsHdrOutput() && hdr_pass.IsCreated();
    BbSettings::Get().hdr_output.store(
        !hdr_output ? BbSettings::HdrOutputOff
        : swapchain.GetHdrFormat().colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT
            ? BbSettings::HdrOutputScRgb
            : BbSettings::HdrOutputHdr10,
        std::memory_order_relaxed);
    if (hdr_output) {
        const vk::Extent2D extent = swapchain.GetExtent();
        SetExpectedGameSize(s32(extent.width), s32(extent.height));
        PresentHdr(cmdbuf, frame, extent);
    } else {
        const vk::Extent2D extent = swapchain.GetExtent();
        SetExpectedGameSize(s32(extent.width), s32(extent.height));
        const vk::ImageSubresourceRange color_range{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };
        const std::array pre_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = frame->image,
                .subresourceRange = color_range,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                               {}, {}, pre_barriers);
        const vk::ClearColorValue black{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
        cmdbuf.clearColorImage(swapchain_image, vk::ImageLayout::eTransferDstOptimal, black, color_range);
        const vk::MemoryBarrier clear_done{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, clear_done, {}, {});
        cmdbuf.blitImage(frame->image, vk::ImageLayout::eTransferSrcOptimal, swapchain_image,
                         vk::ImageLayout::eTransferDstOptimal,
                         MakeImageBlitFit(frame->width, frame->height, extent.width, extent.height),
                         vk::Filter::eLinear);
        // bbport: the settings menu / FPS counter over the frame, at display resolution.
        const bool overlay = BbOverlay::Visible();
        const std::array post_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = overlay ? vk::AccessFlagBits::eColorAttachmentRead |
                                               vk::AccessFlagBits::eColorAttachmentWrite
                                         : vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = overlay ? vk::ImageLayout::eColorAttachmentOptimal
                                     : vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = frame->image,
                .subresourceRange = color_range,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eAllCommands,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
        if (overlay) {
            BbOverlay::Render(cmdbuf, swapchain.ImageView(), extent);
            const vk::ImageMemoryBarrier to_present{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                   vk::PipelineStageFlagBits::eBottomOfPipe,
                                   vk::DependencyFlagBits::eByRegion, {}, {}, to_present);
        }
    }
    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    // Flush vulkan commands.

    // bbport: the frame's submission goes out from a recording thread (BB_ASYNC_SUBMIT); this
    // one, waiting for it on the same queue, must not get there first.
    if (frame->ready_semaphore == draw_scheduler.GetWorkSemaphore()->Handle()) {
        draw_scheduler.WaitSubmitted(frame->ready_tick);
    }
    SubmitInfo info{};
    info.AddWait(swapchain.GetImageAcquiredSemaphore());
    info.AddWait(frame->ready_semaphore, frame->ready_tick);
    info.AddSignal(swapchain.GetPresentReadySemaphore());
    info.AddSignal(frame->present_done);
    scheduler.Flush(info);

    // Present to swapchain.
    bool presented;
    {
        std::scoped_lock submit_lock{Scheduler::submit_mutex};
        presented = swapchain.Present();
    }
    // bbport: recreated after the lock: its device wait takes it (Swapchain::WaitIdle).
    if (!presented) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }

    free_frame();
    if (!is_reusing_frame && is_game_frame) {
        DebugState.IncFlipFrameNum();
    }
}

Frame* Presenter::GetRenderFrame() {
    // Wait for free presentation frames
    Frame* frame;
    {
        std::unique_lock lock{free_mutex};
        free_cv.wait(lock, [this] { return !free_queue.empty(); });
        LOG_DEBUG(Render_Vulkan, "Got render frame, remaining {}", free_queue.size() - 1);

        // Take the frame from the queue
        frame = free_queue.front();
        free_queue.pop();
    }

    const vk::Device device = instance.GetDevice();
    vk::Result result{};

    const auto wait = [&]() {
        result = device.waitForFences(frame->present_done, false, std::numeric_limits<u64>::max());
        return result;
    };

    // Wait for the presentation to be finished so all frame resources are free
    while (wait() != vk::Result::eSuccess) {
        if (result == vk::Result::eErrorDeviceLost) {
            Breadcrumbs::ReportDeviceLost("waiting for a frame");
        }
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
        // Retry if the waiting times out
        if (result == vk::Result::eTimeout) {
            continue;
        }
    }

    if (frame->width != expected_frame_width || frame->height != expected_frame_height ||
        frame->is_hdr != swapchain.GetHDR()) {
        RecreateFrame(frame, expected_frame_width, expected_frame_height);
    }

    return frame;
}

void Presenter::SetExpectedGameSize(s32 width, s32 height) {
    if (width <= 0 || height <= 0) {
        return; // minimised: keep the last size (a 0x0 frame image cannot be made, issue #31)
    }
    const float ratio = (float)width / (float)height;

    expected_frame_height = height;
    expected_frame_width = width;
    if (ratio > expected_ratio) {
        expected_frame_width = static_cast<s32>(height * expected_ratio);
    } else {
        expected_frame_height = static_cast<s32>(width / expected_ratio);
    }
}

} // namespace Vulkan
