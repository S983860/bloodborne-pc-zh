// SPDX-License-Identifier: GPL-3.0-or-later
// Engine-side live settings adapted from bbhost graphics_patch.cpp at 7c790536.
// Retains bloodborne_pc's shaders, upscalers, resource lifetime and composition.
#include "graphics.h"
#include "engine_state.h"
#include "bbport_settings.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
namespace BbEngine::Graphics {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))
bool enabled;
constexpr std::uint64_t kRenderView = 0x269e990;
// push rbp; mov rbp, rsp; push r15..r12; push rbx; sub rsp, 0xea8
const std::uint8_t kRenderViewPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                            0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x0e, 0x00, 0x00};
constexpr std::size_t kCapabilities = 0x48, kSsaoFlag = 0x2968, kAaFlag = 0x296a;
constexpr std::uint64_t kSsaoCapability = 0x8, kAaCapability = 0x10;

constexpr std::uint64_t kYebisRecord = 0x25d4860;
// push rbp; mov rbp, rsp; push r15..r12; push rbx; sub rsp, 0x28; mov r13, r9
const std::uint8_t kYebisRecordPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                             0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x28, 0x4d, 0x89, 0xcd};
constexpr std::uint64_t kPostBlock = 0x59406e0;
constexpr std::size_t kSaturation = 0xc8, kVignettePower = 0xd0, kGlareLuminance = 0xdc;
// bbport: the rest of the tone map's colour correction. sub_269e990 copies the blended tone map
// (a stack block in the area GPARAM's group order: ToneMapExposure, Gamma, ToneType,
// ToneMappingFactor, FilmicContrast, ColorH, ColorS, ColorV, Contrust, ...) field by field into
// this block at 0x269fa5c-0x269fb8c: +0x98 exposure, +0x9c gamma, +0xa8 filmic contrast,
// +0xc4 hue, +0xc8 saturation, +0xcc value, +0x248 contrast. Across the 86 areas hue is 0-8.7
// (degrees), value 0.2-4 and contrast 0.983-1.069 - steep, so its setting stays within 5%.
constexpr std::size_t kHue = 0xc4, kValue = 0xcc, kContrast = 0x248;
constexpr std::size_t kYebisHeld = 0x2710;  // on the YEBIS object: the block was not refreshed
std::uint8_t* g_post_block = nullptr;

// Ambient occlusion strength. The SSAO bank (+0x5968) is blended the same way
// (sub_1285500, into a 0x6c-byte block) and copied into the SSAO object the
// renderer keeps at +0x10d0 - to its +0x14c, unless its +0x148 byte holds it -
// right before sub_12669d0 draws the pass with it. Every area uses the second
// version of the pass (UseNewVersion 1), whose output is the visibility times
// "Ver2 AO Scale" (+0x44, 1.0 everywhere) plus "Ver2 AO Offset" (+0x48, 0):
// scale 0 left the clinic black (mean 15.4 -> 3.1) and 2 brightened it. So
// strength k makes the output 1 - k(1 - out) - the scale times k, the offset
// times k plus 1 - k: 0 is no occlusion, 1 the area's, 2 twice as dark.
constexpr std::uint64_t kSsaoDraw = 0x12669d0;
// push rbp; mov rbp, rsp; push r15..r12; push rbx; sub rsp, 0x2a8
const std::uint8_t kSsaoDrawPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                          0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x02, 0x00, 0x00};
constexpr std::size_t kSsaoHeld = 0x148, kSsaoBlock = 0x14c;  // on the SSAO object
constexpr std::size_t kSsaoScale = 0x44, kSsaoOffset = 0x48;  // in its block

constexpr std::uint64_t kShadowSetup = 0x12ac210;
// push rbp; mov rbp, rsp; push r15; push r14; push rbx; sub rsp, 0x18;
// mov r14, rsi; mov rbx, rdi
const std::uint8_t kShadowSetupPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48,
                                             0x83, 0xec, 0x18, 0x49, 0x89, 0xf6, 0x48, 0x89, 0xfb};
constexpr std::size_t kShadowBlock = 0x80, kShadowHeld = 0x297c;  // on the renderer
constexpr std::size_t kShadowScaled[] = {0x38, 0x3c, 0x44, 0x48};

// bbport: fog, after bbhost graphics_patch.cpp (droogie, later than 7c790536). The fog manager
// (graphics manager +0x5888) keeps one 0x60-byte block per fog the area defines, in the form the
// shaders read; floats 0, 8 and 12 are its opacities (the fog colour's alpha times the area's
// fogAlpha, and the height fog's). sub_26da690 rebuilds the table from the area's parameters
// every time and ends with a tail call - `jmp sub_122e6b0` at 0x26daadf, the table in rdx - that
// copies it to the GPU, so the scaling goes between the two. Another exit path follows that jump
// at once, so the jump is pointed at the 13 bytes of padding after the function, which jump on
// to the stub; sub_122e6b0 is shared with two other managers' uploads, which is why the jump.
constexpr std::uint64_t kGraphicsManager = 0x5940dd8, kFogUploadJump = 0x26daadf, kFogPadding = 0x26daaf3,
    kTableUpload = 0x122e6b0;
constexpr std::size_t kFogManager = 0x5888, kFogMap = 0x248, kFogTable = 0x250, kFogCount = 0x258, kFogBlock = 0x60;
constexpr int kFogOpacity[] = {0, 8, 12};
const std::uint8_t kFogPaddingBytes[] = {0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0, 0, 0, 0, 0};
std::uint8_t* g_graphics_manager = nullptr;  // the pointer's slot in the image

template<typename T> bool Read(std::uint64_t at,T* out) { return at && host_read_safe(reinterpret_cast<void*>(at),out,sizeof(T)); }
template<typename T> void Write(std::uint64_t at,const T& value) { std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(T)); }
// bbport: the hooks are installed whatever Picture tuning is (graphics_controls), which they
// read every call: off, each returns at once and the game's own values stand - so the switch
// and every setting are live.
bool On() { return BbSettings::Get().graphics_controls.load(std::memory_order_relaxed); }
// SSAO and AA are effects with their own settings (Picture tuning doesn't gate them): written
// every view, so switching Picture tuning off leaves nothing forced behind.
GUEST_ABI std::int64_t RenderView(std::uint64_t,const std::uint64_t* saved) {
    const auto r=saved[0];std::uint64_t caps=0;
    if (!r || !Read(r+kCapabilities,&caps)) return 0;
    const std::uint8_t ssao=BbSettings::Get().effects[3].load() && (caps&kSsaoCapability);
    const std::uint8_t aa=BbSettings::Get().effects[4].load() && (caps&kAaCapability);
    Write(r+kSsaoFlag,ssao);Write(r+kAaFlag,aa);
    return 0;
}
GUEST_ABI std::int64_t AoDraw(std::uint64_t,const std::uint64_t* saved) {
    if (!On()) return 0;
    const auto object=saved[5];const float k=BbSettings::Get().graphics_ao_strength;
    std::uint8_t held=1;float v=0,o=0;
    if (!object || k==1 || !Read(object+kSsaoHeld,&held) || held ||
        !Read(object+kSsaoBlock+kSsaoScale,&v) || !Read(object+kSsaoBlock+kSsaoOffset,&o) || !std::isfinite(v) || !std::isfinite(o)) return 0;
    Write(object+kSsaoBlock+kSsaoScale,v*k);Write(object+kSsaoBlock+kSsaoOffset,o*k+(1-k));
    return 0;
}
GUEST_ABI std::int64_t ShadowSetup(std::uint64_t,const std::uint64_t* saved) {
    if (!On()) return 0;
    const auto block=saved[4];const float k=BbSettings::Get().graphics_shadow_scale;
    std::uint8_t held=1;
    if (!block || k==1 || !Read(block+kShadowHeld-kShadowBlock,&held) || held) return 0;
    for (const auto offset:kShadowScaled) { float v=0;if (Read(block+offset,&v) && std::isfinite(v)) Write(block+offset,v*k); }
    return 0;
}
GUEST_ABI std::int64_t YebisRecord(std::uint64_t,const std::uint64_t* saved) {
    if (!On() && !BbSettings::Get().graphics_color_controls) return 0;
    const auto object=saved[5];std::uint8_t held=1;
    if (!object || !g_post_block || !Read(object+kYebisHeld,&held) || held) return 0;
    const auto& s=BbSettings::Get();
    const auto block=reinterpret_cast<std::uint64_t>(g_post_block);
    float glare=0,saturation=0,power=0,hue=0,value=0,contrast=0;
    // Typed values (Ctrl+click) are kept within bbport_settings.cpp's limits here too.
    const bool color=s.graphics_color_controls;
    const float bloom=On()?std::clamp(s.graphics_bloom.load(),0.0f,10.0f):1.f;
    const float sat=color?std::clamp(s.graphics_saturation.load(),0.0f,5.0f):1.f;
    const float hue_offset=color?std::clamp(s.graphics_hue.load(),-180.0f,180.0f):0.f;
    const float bright=color?std::clamp(s.graphics_brightness.load(),0.0f,5.0f):1.f;
    const float contrast_scale=color?std::clamp(s.graphics_contrast.load(),0.5f,1.5f):1.f;
    if (bloom!=1 && Read(block+kGlareLuminance,&glare) && std::isfinite(glare)) Write(block+kGlareLuminance,glare*bloom);
    if (sat!=1 && Read(block+kSaturation,&saturation) && std::isfinite(saturation)) Write(block+kSaturation,saturation*sat);
    if (On() && !s.graphics_vignette && Read(block+kVignettePower,&power)) Write(block+kVignettePower,0.0f);
    if (hue_offset!=0 && Read(block+kHue,&hue) && std::isfinite(hue)) Write(block+kHue,hue+hue_offset);
    if (bright!=1 && Read(block+kValue,&value) && std::isfinite(value)) Write(block+kValue,value*bright);
    if (contrast_scale!=1 && Read(block+kContrast,&contrast) && std::isfinite(contrast)) Write(block+kContrast,contrast*contrast_scale);
    return 0;
}
// At the fog table's upload: rdi the id map, rdx the table, both checked against the fog
// manager before anything is written.
GUEST_ABI std::int64_t FogUpload(std::uint64_t,const std::uint64_t* saved) {
    const float k=std::clamp(BbSettings::Get().graphics_fog.load(),0.0f,5.0f);
    std::uint64_t manager=0,fog=0,map=0,table=0;std::uint32_t count=0;
    if (!On() || k==1 || !std::isfinite(k) || !Read(reinterpret_cast<std::uint64_t>(g_graphics_manager),&manager) ||
        !Read(manager+kFogManager,&fog) || !Read(fog+kFogMap,&map) || !Read(fog+kFogTable,&table) ||
        !Read(fog+kFogCount,&count) || map!=saved[5] || table!=saved[3] || !table || !count || count>4096) return 0;
    auto* blocks=reinterpret_cast<float*>(table);
    for (std::uint32_t i=0;i<count;++i) for (const int o:kFogOpacity) {
        float& opacity=blocks[i*(kFogBlock/4)+o];
        opacity=k>1 ? std::min(opacity*k,std::max(opacity,1.0f)) : opacity*k;  // more fog: up to opaque
    }
    return 0;
}
}
bool Enabled() { return enabled; }
bool Install(std::uint8_t* image,std::size_t size) {
    Hook hooks[4];
    if (size<0x55406e0+0x370 || !Prepare(hooks[0],image,size,kRenderView-0x400000,kRenderViewPrologue,RenderView) ||
        !Prepare(hooks[1],image,size,kYebisRecord-0x400000,kYebisRecordPrologue,YebisRecord) ||
        !Prepare(hooks[2],image,size,kSsaoDraw-0x400000,kSsaoDrawPrologue,AoDraw) ||
        !Prepare(hooks[3],image,size,kShadowSetup-0x400000,kShadowSetupPrologue,ShadowSetup)) {
        for (auto& h:hooks) Discard(h);
        host_log("live graphics disabled: engine bytes differ; all original paths retained");return false;
    }
    g_post_block=image+0x55406e0;
    // Fog: the jump and the padding checked, the stub made, before anything is written.
    const std::size_t jump=kFogUploadJump-0x400000,padding=kFogPadding-0x400000,upload=kTableUpload-0x400000;
    std::uint8_t* fog=nullptr;std::int32_t rel=0;
    if (size>padding+sizeof(kFogPaddingBytes)) std::memcpy(&rel,image+jump+1,4);
    if (size>padding+sizeof(kFogPaddingBytes) && image[jump]==0xe9 && static_cast<std::int64_t>(jump+5)+rel==static_cast<std::int64_t>(upload) &&
        !std::memcmp(image+padding,kFogPaddingBytes,sizeof(kFogPaddingBytes)))
        fog=TailStub(FogUpload,reinterpret_cast<std::uint64_t>(image+upload));
    Commit(hooks);enabled=true;
    if (fog) {
        g_graphics_manager=image+(kGraphicsManager-0x400000);
        std::uint8_t* p=image+padding;*p++=0x49;*p++=0xbb;  // movabs r11, stub; jmp r11
        const auto target=reinterpret_cast<std::uint64_t>(fog);std::memcpy(p,&target,8);p+=8;*p++=0x41;*p++=0xff;*p=0xe3;
        const std::int32_t to_padding=static_cast<std::int32_t>(padding-(jump+5));std::memcpy(image+jump+1,&to_padding,4);
    }
    host_log("bbhost live graphics enabled (Picture tuning %s, live): AA, SSAO, AO strength, shadow distance, bloom, "
             "saturation, vignette, brightness, contrast, hue%s",On() ? "on" : "off",
             fog ? " and fog" : " (fog: engine bytes differ, left alone)");
    return true;
}
}
