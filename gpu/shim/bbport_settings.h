// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: user settings changed at run time from the in-game menu (bbport_overlay.h) and kept
// in bbport.ini (BB_CONFIG overrides the path). Environment variables override the file at
// start. Readers load the atomics every frame; writers are the menu and Load().

#pragma once

#include <atomic>

namespace BbSettings {

enum Upscaler : int { UpscalerOff = 0, UpscalerFsr3 = 1, UpscalerFsr4 = 2, UpscalerFsr411 = 3,
                      UpscalerTaa = 4, UpscalerDlss = 5, UpscalerCount };
/// FSR 4 v07 or FSR 4.1.1: the same inputs, settings and placement in the frame.
inline bool IsFsr4(int upscaler) {
    return upscaler == UpscalerFsr4 || upscaler == UpscalerFsr411;
}
/// FSR 4, FSR 4.1.1 or DLSS: one frame's inputs to a separate upscaler, which writes the
/// output image (TemporalUpscaler::RecordFsr4); FSR 3.1 and TAA are recorded in place.
inline bool IsFrameUpscaler(int upscaler) {
    return IsFsr4(upscaler) || upscaler == UpscalerDlss;
}
enum Preset : int { NativeAA = 0, Quality, Balanced, Performance, UltraPerformance, PresetCount };
enum DebugView : int { DebugNone = 0, DebugReactive = 1, DebugMotion = 2, DebugViewCount };
/// The menu's language, in the Language selector's order (bbport_overlay.cpp).
enum class MenuLanguage {
    English, Russian, Spanish, PortugueseBrazil, French, German, Italian, Polish, Turkish
};
/// menu_language in bbport.ini, by MenuLanguage; the launcher's BB_MENU_LANGUAGE uses the same codes.
inline constexpr const char* MenuLanguageCodes[] = {"en", "ru", "es", "pt_BR", "fr", "de", "it", "pl", "tr"};
inline constexpr int MenuLanguageCount = int(sizeof(MenuLanguageCodes) / sizeof(MenuLanguageCodes[0]));
static_assert(MenuLanguageCount == int(MenuLanguage::Turkish) + 1);

/// Game effects switched by the community patches at start (patches.py EFFECTS): ini key,
/// menu label, default (the game's own behaviour).
struct Effect {
    const char* key;
    const char* label;
    const char* label_ru;
    bool default_on;
};
inline constexpr Effect Effects[] = {
    // bbport: chromatic aberration, depth of field, motion blur and the game's own anti-aliasing start
    // off - a clearer picture, and the upscalers anti-alias.
    {"effect_chromatic_aberration", "Chromatic aberration", "Хроматическая аберрация", false},
    {"effect_dof", "Depth of field (DoF)", "Глубина резкости (DoF)", false},
    {"effect_motion_blur", "Motion blur", "Размытие в движении", false},
    {"effect_ssao", "Ambient occlusion (SSAO)", "Затенение SSAO", true},
    {"effect_game_aa", "Game's own anti-aliasing", "Собственное сглаживание игры", false},
    {"effect_dynamic_shadows", "Shadows from dynamic lights", "Тени от динамических источников", true},
    {"effect_ssr", "Screen-space reflections (not in original game)", "Отражения SSR (не было в игре)", false},
    {"skip_intro", "Skip startup intros", "Пропуск заставок при запуске", false},
    {"debug_camera", "Free camera (Cross + L3)", "Свободная камера (Cross + L3)", false},
    {"debug_menu", "Debug menu (requires font files)", "Debug menu (нужны файлы шрифтов)", false},
};
inline constexpr int EffectCount = int(sizeof(Effects) / sizeof(Effects[0]));
/// Live output resolutions: the upscaler's output and the UI host targets. The last is the
/// display's own size (output_res = auto): BB_DISPLAY_SIZE, which run_windows.py reads from
/// Windows at start, else 1920x1080. Use OutputWidth/OutputHeight, which know it.
inline constexpr int OutputWidths[] = {1280,1920,2560,3840,2560,3440,3840,3840,5120,5120,7680,0,3200};
inline constexpr int OutputHeights[] = {720,1080,1440,2160,1080,1440,1600,1080,1440,2160,2160,0,1800};
inline constexpr int OutputCount = 13;
inline constexpr int OutputAuto = 11;
inline constexpr int OutputDefault = 1; ///< 1920x1080, the game's own size
static_assert(sizeof(OutputWidths) / sizeof(OutputWidths[0]) == OutputCount &&
              sizeof(OutputHeights) / sizeof(OutputHeights[0]) == OutputCount);
int OutputWidth(int index);
int OutputHeight(int index);
/// Live cheats (engine/cheats.cpp): cheat_vials / cheat_bullets in bbport.ini are "off",
/// "infinite" or an amount kept in the inventory, 1..CheatMaxAmount.
inline constexpr int CheatOff = 0, CheatInfinite = -1, CheatMaxAmount = 99;
/// cheat_gun_range: "off" (0) or the firearm bullets' range in percent.
inline constexpr int CheatGunRanges[] = {0, 150, 200, 300};
/// cheat_echo_multiplier: Blood Echoes from kills times this (1: the game's own).
inline constexpr int CheatEchoMultipliers[] = {1, 2, 5, 10};
/// HDR output (hdr_paper_white / hdr_peak in nits, hdr_boost 0..1): defaults and ranges.
inline constexpr float HdrPaperWhiteDefault = 200.0f, HdrPaperWhiteMin = 10.0f, HdrPaperWhiteMax = 500.0f;
inline constexpr float HdrPeakDefault = 1000.0f, HdrPeakMin = 10.0f, HdrPeakMax = 4000.0f;
inline constexpr float HdrBoostDefault = 0.25f;
/// Native mode: the game's UI at hdr_ui_brightness times paper white, and hdr_ui_color of the way
/// from sRGB's colours (0) to a wide-gamut SDR monitor's (1, DCI-P3).
inline constexpr float HdrUiBrightnessMin = 0.5f, HdrUiBrightnessMax = 2.0f, HdrUiColorDefault = 1.0f;
/// The swapchain's HDR signal (Values::hdr_output).
enum HdrOutput : int { HdrOutputOff = 0, HdrOutputHdr10 = 1, HdrOutputScRgb = 2 };
/// hdr_mode in bbport.ini (docs/hdr.md). Native: the highlights' brightness from the game's own
/// HDR scene (vk_hdr_native.h), expand where a frame has none (menus, loading, movies). Expand:
/// the SDR picture's highlights stretched towards the peak.
enum HdrMode : int { HdrModeNative = 0, HdrModeExpand = 1, HdrModeCount };
inline constexpr const char* HdrModeNames[] = {"native", "expand"};
static_assert(sizeof(HdrModeNames) / sizeof(HdrModeNames[0]) == HdrModeCount);

/// button_prompts in bbport.ini: the game's button icons drawn as the bound keys while the keyboard
/// or mouse is in use (auto), always (keyboard), or never (controller). engine/key_prompts.h.
enum ButtonPrompts : int { PromptsAuto = 0, PromptsKeyboard = 1, PromptsController = 2, PromptsCount };
inline constexpr const char* ButtonPromptsNames[] = {"auto", "keyboard", "controller"};
static_assert(sizeof(ButtonPromptsNames) / sizeof(ButtonPromptsNames[0]) == PromptsCount);

/// pad_icons in bbport.ini: the game's controller icons as they are (PlayStation's) or as an Xbox
/// controller's buttons; auto follows the gamepad in use. engine/key_prompts.h.
enum PadIcons : int { PadIconsAuto = 0, PadIconsPlayStation = 1, PadIconsXbox = 2, PadIconsCount };
inline constexpr const char* PadIconsNames[] = {"auto", "playstation", "xbox"};
static_assert(sizeof(PadIconsNames) / sizeof(PadIconsNames[0]) == PadIconsCount);

struct Values {
    std::atomic<MenuLanguage> menu_language{MenuLanguage::English};
    std::atomic<int> upscaler{UpscalerFsr3};
    std::atomic<int> preset{NativeAA};
    std::atomic<bool> sharpen{true};
    std::atomic<float> sharpness{0.3f};
    std::atomic<bool> dlss_nr_enabled{false}, hardware_rt_enabled{false};
    std::atomic<unsigned> gpu_vendor{0}; // actual Vulkan adapter, never an imported PC profile
    std::atomic<float> dlss_nr_intensity{1.0f}, dlss_nr_tone{1.0f}, dlss_nr_structure{1.0f};
    std::atomic<int> dlss_sr_model{0}; // auto, K, L, M
    std::atomic<float> rt_radius{1.25f}, rt_strength{0.28f};
    std::atomic<int> rt_samples{2}, rt_detail{2};
    std::atomic<bool> dlss_nr_supported{false}, hardware_rt_supported{false};
    std::atomic<bool> jitter{true};
    std::atomic<bool> reactive{false};
    std::atomic<bool> object_motion{true};
    std::atomic<float> reactive_scale{1.0f};
    std::atomic<float> reactive_threshold{0.2f};
    std::atomic<float> reactive_max{0.9f};
    std::atomic<int> debug_view{DebugNone};
    std::atomic<bool> minimap_enabled{false};
    std::atomic<bool> beginner_hints{true};
    std::atomic<int> control_mode{0}; // 0 keyboard/mouse, 1 controller; chosen before title input
    std::atomic<int> minimap_range{45};
    std::atomic<bool> show_fps{false};
    /// What the FPS counter shows: the frame rate, the frame time, the upscaler.
    std::atomic<bool> show_fps_rate{true};
    std::atomic<bool> show_fps_frame_time{true};
    std::atomic<bool> show_fps_upscaler{true};
    std::atomic<bool> show_fps_cpu{false}; ///< bbport: the PC's CPU use (bbport_usage.h)
    std::atomic<bool> show_fps_gpu{false}; ///< bbport: the GPU's 3D engine use
    /// The mouse cursor hidden over the game 2 s after the mouse last moved (window.cpp).
    std::atomic<bool> hide_cursor{true};
    /// The settings menu's position (fraction of the screen), -1 until it is moved.
    std::atomic<float> menu_x{-1.0f}, menu_y{-1.0f};
    // FSR 4 checks (menu): the provider's auto exposure, the jitter sign it is given.
    std::atomic<bool> fsr4_auto_exposure{true};
    std::atomic<bool> fsr4_invert_jitter{false};
    std::atomic<int> active_render_width{1920}, active_render_height{1080};
    /// Applied at start (patches.py); the menu shows when a restart is needed.
    std::atomic<bool> effects[EffectCount]{};
    std::atomic<bool> camera_controls{false}; // startup engine hook, opt-in
    std::atomic<bool> widescreen{true},pc_controls{true},mouse_camera{true},mouse_menu{true};
    std::atomic<bool> mouse_invert_x{false},mouse_invert_y{false},mouse_auto_rotation{false};
    std::atomic<int> mouse_sensitivity{5};
    std::atomic<bool> melee_guard_toggle{false}; // false: hold, true: press to toggle
    std::atomic<int> pad_guard_mode{0}; // 0 L1/LB guard + L3 transform, 1 legacy L1+L2 chord
    std::atomic<int> perfect_guard_window_ms{250};
    std::atomic<int> guard_sfx_volume{70};
    std::atomic<int> button_prompts{PromptsAuto}; ///< ButtonPrompts; live
    std::atomic<int> pad_icons{PadIconsAuto}; ///< PadIcons; live
    std::atomic<bool> change_appearance{false}, rebirth{false}; // next launch, engine features
    std::atomic<bool> graphics_controls{false}, graphics_vignette{true};
    /// Colour has its own gate: changing bloom/fog must not revive a disabled tint.
    std::atomic<bool> graphics_color_controls{false};
    std::atomic<float> graphics_ao_strength{1},graphics_shadow_scale{1},graphics_bloom{1},graphics_saturation{1};
    /// Fog opacity (0..1 of the area's), the tone map's brightness (ColorV) and contrast (Contrust)
    /// scales and a hue offset in degrees (ColorH); 1, 1, 1 and 0 leave the area's own.
    std::atomic<float> graphics_fog{1},graphics_brightness{1},graphics_contrast{1},graphics_hue{0};
    std::atomic<float> camera_fov_scale{1.0f}, camera_distance_scale{1.0f}, camera_height_scale{1.0f};
    std::atomic<int> model_lod{0}; ///< -2 highest .. 2 lowest, 0 the game's
    std::atomic<int> output_res{OutputDefault}; ///< index into OutputWidths
    /// CheatOff, CheatInfinite or the amount to keep (1..CheatMaxAmount); live.
    std::atomic<int> cheat_vials{CheatOff}, cheat_bullets{CheatOff};
    /// The other cheats (0/1 in bbport.ini); live.
    std::atomic<bool> cheat_stamina{false}, cheat_stealth{false}, cheat_keep_echoes{false},
        cheat_no_durability{false}, cheat_consumables{false};
    /// One of CheatGunRanges / CheatEchoMultipliers; live.
    std::atomic<int> cheat_gun_range{0}, cheat_echo_multiplier{1};
    /// Borderless fullscreen window at the desktop size (F11 toggles; BB_FULLSCREEN overrides).
    std::atomic<bool> fullscreen{false};
    /// HDR output (hdr, BB_HDR): the game's SDR picture shown in an HDR swapchain when the
    /// display offers one (Windows HDR on). Applied at start (startup_hdr).
    std::atomic<bool> hdr{false};
    /// Nits of the SDR picture's white, the display's peak, and the highlight expansion (0: SDR
    /// white stays at paper white, 1: up to the peak); live (BB_HDR_PAPER_WHITE, BB_HDR_PEAK,
    /// BB_HDR_BOOST).
    std::atomic<float> hdr_paper_white{HdrPaperWhiteDefault}, hdr_peak{HdrPeakDefault},
        hdr_boost{HdrBoostDefault};
    std::atomic<float> hdr_ui_brightness{1.0f}, hdr_ui_color{HdrUiColorDefault};
    /// HdrMode (hdr_mode, BB_HDR_MODE); live. In native mode boost is the share of the scene's
    /// highlight range shown: sqrt(boost) of it in log2 (0.25: half, 1: all of it).
    std::atomic<int> hdr_mode{HdrModeNative};
    /// Set by the renderer: the HDR output in use (HdrOutput).
    std::atomic<int> hdr_output{HdrOutputOff};
    /// Live resolution and preset changes (run.sh): 0 off by default (startup patch, fastest
    /// on the Steam Deck and older GPUs), -1 auto (strong discrete GPUs), 1 on. On restart.
    std::atomic<int> live_resolution{0};
    /// Why FSR 4 cannot run (assets, device features), or null. Set by the renderer.
    std::atomic<const char*> fsr4_problem{nullptr};
    std::atomic<bool> fsr4_supported{false}, fsr411_supported{false}, dlss_supported{false};

    /// Startup settings for the explicit BB_RENDER_RES compatibility patch only.
    int startup_preset = NativeAA;
    int startup_upscaler = UpscalerFsr3;
    bool startup_object_motion = true;
    bool startup_effects[EffectCount]{};
    int startup_model_lod = 0;
    int startup_output_res = OutputDefault;
    int startup_live_resolution = 0;
    bool startup_hdr = false;
};

Values& Get();
/// Localized overlay text; the Language selector itself stays in English. The other languages
/// look the English text up (bbport_menu_i18n.cpp) and show it when they have no translation.
const char* MenuText(const char* english, const char* russian);

/// Reads the file, then the environment overrides. Called once at start.
void Load();
/// Checks the loaded choice before the first frame; unsupported FSR 4 or DLSS uses FSR 3.1.
void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool dlss = false);
/// A failed old frame must not overwrite a more recent menu selection.
bool FallbackUpscaler(int failed_provider, int fallback_provider);
void ConfigureHardware(unsigned vendor);
/// Startup-patched scene dimensions cannot change until run.sh prepares a new image.
bool FixedRenderSession();
int RenderPreset();
bool ResolutionNeedsRestart();
/// Writes the file (menu changes).
void Save();

/// Render resolution divisor of a preset (1.0 native, 1.5 quality, ...).
float PresetScale(int preset);
const char* PresetName(int preset);
const char* UpscalerName(int upscaler);

} // namespace BbSettings
