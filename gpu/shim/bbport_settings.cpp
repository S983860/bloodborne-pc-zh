#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"
#include "input/pc_chinese.h"
#include "bbport_menu_i18n.h"
#include "input/ini_bindings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BbSettings {

namespace {

bool legacy_color_profile=false; // initialized before the actual GPU is selected

const char* Path() {
    const char* env = std::getenv("BB_CONFIG");
    return env && env[0] ? env : "bbport.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

// cheat_vials / cheat_bullets: off, infinite or an amount 1..CheatMaxAmount; anything else is off.
int CheatValue(const std::string& key, const std::string& value) {
    if (value == "off" || value == "0") {
        return CheatOff;
    }
    if (value == "infinite") {
        return CheatInfinite;
    }
    char* end = nullptr;
    const long amount = std::strtol(value.c_str(), &end, 10);
    if (!value.empty() && end && *end == '\0' && amount >= 1 && amount <= CheatMaxAmount) {
        return int(amount);
    }
    std::printf("Settings: unknown %s '%s' (expected off, infinite or 1-%d); off\n", key.c_str(),
                value.c_str(), CheatMaxAmount);
    return CheatOff;
}

std::string CheatText(int value) {
    return value == CheatInfinite ? "infinite"
           : value >= 1 && value <= CheatMaxAmount ? std::to_string(value)
                                                   : "off";
}

// cheat_gun_range: off or one of CheatGunRanges (percent); cheat_echo_multiplier: one of
// CheatEchoMultipliers. Anything else is the game's own (off / 1).
template <std::size_t N>
int CheatChoice(const std::string& key, const std::string& value, const int (&choices)[N], const char* expected) {
    if (value == "off") {
        return choices[0];
    }
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);
    if (!value.empty() && end && *end == '\0' && std::find(choices, choices + N, int(v)) != choices + N) {
        return int(v);
    }
    std::printf("Settings: unknown %s '%s' (expected %s); %d\n", key.c_str(), value.c_str(), expected, choices[0]);
    return choices[0];
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "menu_language") {
        bool known = false;
        for (int l = 0; l < MenuLanguageCount; ++l) {
            if (value == MenuLanguageCodes[l]) {
                v.menu_language = MenuLanguage(l);
                known = true;
            }
        }
        if (!known) {
            std::printf("Settings: unknown menu_language '%s' (expected en, ru, es, pt_BR, fr, de, it, pl "
                        "or tr)\n",
                        value.c_str());
        }
    } else if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "dlss_nr_enabled") {
        v.dlss_nr_enabled=i!=0;
    } else if (key == "hardware_rt_enabled") {
        v.hardware_rt_enabled=i!=0;
    } else if (key == "dlss_nr_intensity") {
        v.dlss_nr_intensity=Clamp(std::isfinite(f)?f:1.0f,0.0f,1.0f);
    } else if (key == "dlss_nr_tone") {
        v.dlss_nr_tone=Clamp(std::isfinite(f)?f:1.0f,0.0f,1.0f);
    } else if (key == "dlss_nr_structure") {
        v.dlss_nr_structure=Clamp(std::isfinite(f)?f:1.0f,0.0f,1.0f);
    } else if (key == "dlss_sr_model") {
        v.dlss_sr_model=std::clamp(i,0,3);
    } else if (key == "rt_radius") {
        v.rt_radius=Clamp(std::isfinite(f)?f:1.25f,0.1f,3.0f);
    } else if (key == "rt_strength") {
        v.rt_strength=Clamp(std::isfinite(f)?f:0.28f,0.0f,1.0f);
    } else if (key == "rt_samples") {
        v.rt_samples=std::clamp(i,1,4);
    } else if (key == "rt_detail") {
        v.rt_detail=std::clamp(i,0,2);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "minimap_enabled") {
        v.minimap_enabled=i!=0;
    } else if (key == "beginner_hints") {
        v.beginner_hints=i!=0;
    } else if (key == "control_mode") {
        v.control_mode=std::clamp(i,0,1);
    } else if (key == "minimap_range") {
        v.minimap_range=std::clamp(i,15,120);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "show_fps_rate") {
        v.show_fps_rate = i != 0;
    } else if (key == "show_fps_frame_time") {
        v.show_fps_frame_time = i != 0;
    } else if (key == "show_fps_upscaler") {
        v.show_fps_upscaler = i != 0;
    } else if (key == "show_fps_cpu") {
        v.show_fps_cpu = i != 0;
    } else if (key == "show_fps_gpu") {
        v.show_fps_gpu = i != 0;
    } else if (key == "hide_cursor") {
        v.hide_cursor = i != 0;
    } else if (key == "menu_pos") {
        float x = -1.0f, y = -1.0f;
        if (std::sscanf(value.c_str(), "%f,%f", &x, &y) == 2 && x >= 0.0f && x <= 1.0f && y >= 0.0f &&
            y <= 1.0f) {
            v.menu_x = x;
            v.menu_y = y;
        }
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "widescreen") {
        v.widescreen=i!=0;
    } else if (key == "pc_controls") {
        v.pc_controls=i!=0;
    } else if (key == "mouse_camera") {
        v.mouse_camera=i!=0;
    } else if (key == "mouse_menu") {
        v.mouse_menu=i!=0;
    } else if (key == "mouse_invert_x") {
        v.mouse_invert_x=i!=0;
    } else if (key == "mouse_invert_y") {
        v.mouse_invert_y=i!=0;
    } else if (key == "mouse_auto_rotation") {
        v.mouse_auto_rotation=i!=0;
    } else if (key == "melee_guard_toggle") {
        v.melee_guard_toggle=i!=0;
    } else if (key == "pad_guard_mode") {
        v.pad_guard_mode=std::clamp(i,0,1);
    } else if (key == "perfect_guard_window_ms") {
        v.perfect_guard_window_ms=std::clamp(i,50,2000);
    } else if (key == "guard_sfx_volume") {
        v.guard_sfx_volume=std::clamp(i,0,100);
    } else if (key == "button_prompts") {
        int mode = -1;
        for (int m = 0; m < PromptsCount; ++m) {
            if (value == ButtonPromptsNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown button_prompts '%s' (expected auto, keyboard or controller); auto\n",
                        value.c_str());
        }
        v.button_prompts = mode < 0 ? PromptsAuto : mode;
    } else if (key == "pad_icons") {
        int mode = -1;
        for (int m = 0; m < PadIconsCount; ++m) {
            if (value == PadIconsNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown pad_icons '%s' (expected auto, playstation or xbox); auto\n",
                        value.c_str());
        }
        v.pad_icons = mode < 0 ? PadIconsAuto : mode;
    } else if (key == "mouse_sensitivity") {
        v.mouse_sensitivity=std::clamp(i,0,10);
    } else if (key == "camera_controls") {
        v.camera_controls = i != 0;
    } else if (key == "change_appearance") {
        v.change_appearance = i != 0;
    } else if (key == "rebirth") {
        v.rebirth = i != 0;
    } else if (key == "camera_fov_scale") {
        v.camera_fov_scale = Clamp(std::isfinite(f) ? f : 1.0f, 1.0f, 1.5f);
    } else if (key == "camera_distance_scale") {
        v.camera_distance_scale = Clamp(std::isfinite(f) ? f : 1.0f, 0.5f, 1.5f);
    } else if (key == "camera_height_scale") {
        v.camera_height_scale = Clamp(std::isfinite(f) ? f : 1.0f, 0.5f, 1.5f);
    } else if (key == "graphics_controls") {
        v.graphics_controls=i!=0;
    } else if (key == "graphics_color_controls") {
        v.graphics_color_controls=i!=0;
    } else if (key == "graphics_vignette") {
        v.graphics_vignette=i!=0;
    // Wide limits: the menu's sliders show the useful range, Ctrl+click types any value up to these.
    } else if (key == "graphics_ao_strength") {
        v.graphics_ao_strength=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_shadow_scale") {
        v.graphics_shadow_scale=Clamp(std::isfinite(f)?f:1.0f,0.25f,8.0f);
    } else if (key == "graphics_bloom") {
        v.graphics_bloom=Clamp(std::isfinite(f)?f:1.0f,0.0f,10.0f);
    } else if (key == "graphics_saturation") {
        v.graphics_saturation=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_fog") {
        v.graphics_fog=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_brightness") {
        v.graphics_brightness=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_contrast") {
        v.graphics_contrast=Clamp(std::isfinite(f)?f:1.0f,0.5f,1.5f);
    } else if (key == "graphics_hue") {
        v.graphics_hue=Clamp(std::isfinite(f)?f:0.0f,-180.0f,180.0f);
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "fullscreen") {
        v.fullscreen = i != 0;
    } else if (key == "hdr") {
        v.hdr = i != 0;
    } else if (key == "hdr_paper_white") {
        v.hdr_paper_white = Clamp(std::isfinite(f) ? f : HdrPaperWhiteDefault, HdrPaperWhiteMin,
                                  HdrPaperWhiteMax);
    } else if (key == "hdr_peak") {
        v.hdr_peak = Clamp(std::isfinite(f) ? f : HdrPeakDefault, HdrPeakMin, HdrPeakMax);
    } else if (key == "hdr_boost") {
        v.hdr_boost = Clamp(std::isfinite(f) ? f : HdrBoostDefault, 0.0f, 1.0f);
    } else if (key == "hdr_ui_brightness") {
        v.hdr_ui_brightness = Clamp(std::isfinite(f) ? f : 1.0f, HdrUiBrightnessMin, HdrUiBrightnessMax);
    } else if (key == "hdr_ui_color") {
        v.hdr_ui_color = Clamp(std::isfinite(f) ? f : HdrUiColorDefault, 0.0f, 1.0f);
    } else if (key == "hdr_mode") {
        int mode = -1;
        for (int m = 0; m < HdrModeCount; ++m) {
            if (value == HdrModeNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown hdr_mode '%s' (expected native or expand); native\n",
                        value.c_str());
        }
        v.hdr_mode = mode < 0 ? HdrModeNative : mode;
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "cheat_vials") {
        v.cheat_vials = CheatValue(key, value);
    } else if (key == "cheat_bullets") {
        v.cheat_bullets = CheatValue(key, value);
    } else if (key == "cheat_stamina") {
        v.cheat_stamina = i != 0;
    } else if (key == "cheat_stealth") {
        v.cheat_stealth = i != 0;
    } else if (key == "cheat_keep_echoes") {
        v.cheat_keep_echoes = i != 0;
    } else if (key == "cheat_no_durability") {
        v.cheat_no_durability = i != 0;
    } else if (key == "cheat_consumables") {
        v.cheat_consumables = i != 0;
    } else if (key == "cheat_gun_range") {
        v.cheat_gun_range = CheatChoice(key, value, CheatGunRanges, "off, 150, 200 or 300");
    } else if (key == "cheat_echo_multiplier") {
        v.cheat_echo_multiplier = CheatChoice(key, value, CheatEchoMultipliers, "1, 2, 5 or 10");
    } else if (key == "output_res") {
        if (value == "auto") {
            v.output_res = OutputAuto;
        }
        for (int r = 0; r < OutputCount; ++r) {
            if (r == OutputAuto) continue;
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

// The menu's language for the launcher's code (BB_MENU_LANGUAGE: en, ru, pt_BR...) or a locale
// (ru_RU.UTF-8, de_DE...): its first two letters; Portuguese only as pt_BR. Else English.
MenuLanguage LanguageOfLocale(const char* lang) {
    if (!lang || !lang[0] || !lang[1]) {
        return MenuLanguage::English;
    }
    const auto lower = [lang](int i) { return char(std::tolower(static_cast<unsigned char>(lang[i]))); };
    if (lower(0) == 'p' && lower(1) == 't') {
        return (lang[2] == '_' || lang[2] == '-') && lower(3) == 'b' && lower(4) == 'r'
                   ? MenuLanguage::PortugueseBrazil
                   : MenuLanguage::English;
    }
    for (int l = 0; l < MenuLanguageCount; ++l) {
        const char* code = MenuLanguageCodes[l];
        if (code[2] == '\0' && lower(0) == code[0] && lower(1) == code[1]) {
            return MenuLanguage(l);
        }
    }
    return MenuLanguage::English;
}

} // namespace

// The display's size for output_res = auto: BB_DISPLAY_SIZE ("WxH", from run_windows.py), even
// (the game has half-resolution buffers); 1920x1080 without it.
static std::pair<int, int> DisplaySize() {
    static const std::pair<int, int> size = [] {
        int w = 0, h = 0;
        const char* env = std::getenv("BB_DISPLAY_SIZE");
        if (env && std::sscanf(env, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360 && w <= 16384 &&
            h <= 8640) {
            return std::pair<int, int>{w & ~1, h & ~1};
        }
        return std::pair<int, int>{1920, 1080};
    }();
    return size;
}

int OutputWidth(int index) {
    index = std::clamp(index, 0, OutputCount - 1);
    return index == OutputAuto ? DisplaySize().first : OutputWidths[index];
}

int OutputHeight(int index) {
    index = std::clamp(index, 0, OutputCount - 1);
    return index == OutputAuto ? DisplaySize().second : OutputHeights[index];
}

Values& Get() {
    static Values values;
    return values;
}

const char* MenuText(const char* english, const char* russian) {
    static const bool pc_chinese=[] {const char* mode=std::getenv("BB_PC_CHINESE");return mode && mode[0]=='1';}();
    if(pc_chinese)return PcChineseText(english);
    switch (const MenuLanguage language = Get().menu_language) {
    case MenuLanguage::English: return english;
    case MenuLanguage::Russian: return russian;
    default: return MenuTranslation(language, english);
    }
}

void Load() {
    auto& v = Get();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    // bbport: the menu's language until one is chosen in it (menu_language in bbport.ini): the
    // launcher's (BB_MENU_LANGUAGE), else the system's, as the launcher picks its own.
    {
        const char* lang = std::getenv("BB_MENU_LANGUAGE");
        if (!lang || !*lang) {
            for (const char* key : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
                if (const char* value = std::getenv(key); value && *value) {
                    lang = value;
                    break;
                }
            }
        }
        v.menu_language = LanguageOfLocale(lang);
    }
    bool color_gate_present=false;
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            if(text.substr(0,eq)=="graphics_color_controls")color_gate_present=true;
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Preserve the appearance of existing profiles. Once saved, colour and effects
    // remain independently enabled, including after switching between native pages.
    legacy_color_profile=!color_gate_present;
    if(legacy_color_profile)v.graphics_color_controls=v.graphics_controls.load();
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("BB_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_HDR", "hdr"},                          {"BB_HDR_PAPER_WHITE", "hdr_paper_white"},
        {"BB_HDR_PEAK", "hdr_peak"},                {"BB_HDR_BOOST", "hdr_boost"},
        {"BB_HDR_MODE", "hdr_mode"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    if(const char* model=std::getenv("BB_DLSS_SR_MODEL"))
        v.dlss_sr_model=!std::strcmp(model,"K")?1:!std::strcmp(model,"L")?2:!std::strcmp(model,"M")?3:0;
    v.startup_preset = v.preset;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
    v.startup_hdr = v.hdr;
}

void ConfigureHardware(unsigned vendor) {
    auto& v=Get();v.gpu_vendor=vendor;
    if(vendor==0x1002 && legacy_color_profile){
        // Earlier shipped profiles coupled effect edits to strong custom tint.
        // First AMD migration restores native colour without erasing its sliders.
        // NVIDIA's existing appearance and explicit new colour choices are kept.
        v.graphics_color_controls=false;
        std::printf("GPU policy: old AMD colour profile isolated; native colour active, custom sliders retained\n");
    }
    if(vendor!=0x10de)v.dlss_nr_enabled=false;
    // The experimental capture/ray-query path is validated only on NVIDIA here.
    if(vendor!=0x10de)v.hardware_rt_enabled=false;
    if(vendor!=0x10de){v.object_motion=false;v.startup_object_motion=false;}
    std::printf("GPU policy: vendor=%04x neural-enhancement=%d hardware-RT=%d\n",vendor,
                int(v.dlss_nr_enabled.load()),int(v.hardware_rt_enabled.load()));
}
bool FallbackUpscaler(int failed_provider,int fallback_provider) {
    return Get().upscaler.compare_exchange_strong(failed_provider,fallback_provider);
}
void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool dlss) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    // The DLL-backed 4.1.1 path does not require the legacy FSR4 asset set.
    v.fsr411_supported = fsr411;
    v.dlss_supported = dlss;
    const int requested = v.upscaler;
    if (requested == UpscalerDlss && !dlss) {
        v.fsr4_problem = "DLSS needs an NVIDIA RTX GPU, its driver's NGX and nvngx_dlss; using FSR 3.1";
        std::printf("Upscaler: dlss unavailable; falling back to FSR 3.1 before the first frame\n");
        v.upscaler = UpscalerFsr3;
    } else if (requested == UpscalerFsr411 && !v.fsr411_supported && v.fsr4_supported) {
        // FSR 4.1.1 needs mixed-precision dot products (VK_VALVE_shader_mixed_float_dot_product)
        // that FSR 4 does not, and its fsr4_411 files: the next best is FSR 4, not 3.1.
        v.fsr4_problem = "FSR 4.1.1 needs its fsr4_411 files and a GPU that runs its shaders; using FSR 4";
        std::printf("Upscaler: fsr411 unavailable (GPU or fsr4_411 files); falling back to FSR 4 before the first frame\n");
        v.upscaler = UpscalerFsr4;
    } else if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("BB_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    // The keys the menu writes, in this order; the rest of the file stays as it is: the
    // launcher's controls (key.* / pad.*) and its other keys, comments.
    std::vector<std::pair<std::string, std::string>> keys;
    const auto put = [&](const char* key, std::string value) { keys.emplace_back(key, std::move(value)); };
    const auto fixed = [](float value, int digits) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", digits, value);
        return std::string(text);
    };
    const auto flag = [](bool value) { return std::string(value ? "1" : "0"); };
    put("menu_language", MenuLanguageCodes[int(v.menu_language.load())]);
    put("upscaler", UpscalerName(v.upscaler));
    put("preset", std::to_string(v.preset.load()));
    put("sharpen", flag(v.sharpen));
    put("sharpness", fixed(v.sharpness, 2));
    put("dlss_nr_enabled",flag(v.dlss_nr_enabled));
    put("hardware_rt_enabled",flag(v.hardware_rt_enabled));
    put("dlss_nr_intensity",fixed(v.dlss_nr_intensity,2));
    put("dlss_nr_tone",fixed(v.dlss_nr_tone,2));
    put("dlss_nr_structure",fixed(v.dlss_nr_structure,2));
    put("dlss_sr_model",std::to_string(v.dlss_sr_model.load()));
    put("rt_radius",fixed(v.rt_radius,2));
    put("rt_strength",fixed(v.rt_strength,2));
    put("rt_samples",std::to_string(v.rt_samples.load()));
    put("rt_detail",std::to_string(v.rt_detail.load()));
    put("jitter", flag(v.jitter));
    put("reactive", flag(v.reactive));
    put("object_motion", flag(v.object_motion));
    put("reactive_scale", fixed(v.reactive_scale, 2));
    put("reactive_threshold", fixed(v.reactive_threshold, 2));
    put("reactive_max", fixed(v.reactive_max, 2));
    put("debug_view", std::to_string(v.debug_view.load()));
    put("minimap_enabled",flag(v.minimap_enabled));
    put("beginner_hints",flag(v.beginner_hints));
    put("control_mode",std::to_string(v.control_mode.load()));
    put("minimap_range",std::to_string(v.minimap_range.load()));
    put("show_fps", flag(v.show_fps));
    put("show_fps_rate", flag(v.show_fps_rate));
    put("show_fps_frame_time", flag(v.show_fps_frame_time));
    put("show_fps_upscaler", flag(v.show_fps_upscaler));
    put("show_fps_cpu", flag(v.show_fps_cpu));
    put("show_fps_gpu", flag(v.show_fps_gpu));
    put("hide_cursor", flag(v.hide_cursor));
    put("fsr4_auto_exposure", flag(v.fsr4_auto_exposure));
    put("fsr4_invert_jitter", flag(v.fsr4_invert_jitter));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        put(Effects[e].key, flag(v.effects[e]));
    }
    put("fullscreen", flag(v.fullscreen));
    put("hdr", flag(v.hdr));
    put("hdr_paper_white", fixed(v.hdr_paper_white, 0));
    put("hdr_peak", fixed(v.hdr_peak, 0));
    put("hdr_boost", fixed(v.hdr_boost, 2));
    put("hdr_ui_brightness", fixed(v.hdr_ui_brightness, 2));
    put("hdr_ui_color", fixed(v.hdr_ui_color, 2));
    put("hdr_mode", HdrModeNames[std::clamp(v.hdr_mode.load(), 0, HdrModeCount - 1)]);
    put("graphics_controls",flag(v.graphics_controls));
    put("graphics_vignette",flag(v.graphics_vignette));
    put("graphics_ao_strength",fixed(v.graphics_ao_strength,2));
    put("graphics_shadow_scale",fixed(v.graphics_shadow_scale,2));
    put("graphics_bloom",fixed(v.graphics_bloom,2));
    put("graphics_color_controls",flag(v.graphics_color_controls));
    put("graphics_saturation",fixed(v.graphics_saturation,2));
    put("graphics_fog",fixed(v.graphics_fog,2));
    put("graphics_brightness",fixed(v.graphics_brightness,2));
    put("graphics_contrast",fixed(v.graphics_contrast,2));
    put("graphics_hue",fixed(v.graphics_hue,0));
    put("camera_controls", flag(v.camera_controls));
    put("widescreen",flag(v.widescreen));
    put("pc_controls",flag(v.pc_controls));
    put("mouse_camera",flag(v.mouse_camera));
    put("mouse_menu",flag(v.mouse_menu));
    put("mouse_invert_x",flag(v.mouse_invert_x));
    put("mouse_invert_y",flag(v.mouse_invert_y));
    put("mouse_auto_rotation",flag(v.mouse_auto_rotation));
    put("melee_guard_toggle",flag(v.melee_guard_toggle));
    put("pad_guard_mode",std::to_string(v.pad_guard_mode.load()));
    put("perfect_guard_window_ms",std::to_string(v.perfect_guard_window_ms.load()));
    put("guard_sfx_volume",std::to_string(v.guard_sfx_volume.load()));
    put("mouse_sensitivity",std::to_string(v.mouse_sensitivity.load()));
    put("button_prompts", ButtonPromptsNames[std::clamp(v.button_prompts.load(), 0, PromptsCount - 1)]);
    put("pad_icons", PadIconsNames[std::clamp(v.pad_icons.load(), 0, PadIconsCount - 1)]);
    put("change_appearance", flag(v.change_appearance));
    put("rebirth", flag(v.rebirth));
    put("camera_fov_scale", fixed(v.camera_fov_scale, 2));
    put("camera_distance_scale", fixed(v.camera_distance_scale, 2));
    put("camera_height_scale", fixed(v.camera_height_scale, 2));
    put("model_lod", std::to_string(v.model_lod.load()));
    put("cheat_vials", CheatText(v.cheat_vials));
    put("cheat_bullets", CheatText(v.cheat_bullets));
    put("cheat_stamina", flag(v.cheat_stamina));
    put("cheat_stealth", flag(v.cheat_stealth));
    put("cheat_gun_range", v.cheat_gun_range == 0 ? "off" : std::to_string(v.cheat_gun_range.load()));
    put("cheat_keep_echoes", flag(v.cheat_keep_echoes));
    put("cheat_echo_multiplier", std::to_string(v.cheat_echo_multiplier.load()));
    put("cheat_no_durability", flag(v.cheat_no_durability));
    put("cheat_consumables", flag(v.cheat_consumables));
    put("output_res", v.output_res == OutputAuto ? std::string("auto")
                                                  : std::to_string(OutputWidths[v.output_res]) + "x" +
                                                        std::to_string(OutputHeights[v.output_res]));
    // Read by run.sh at start.
    put("live_resolution", v.live_resolution < 0 ? "auto" : flag(v.live_resolution != 0));
    if (v.menu_x >= 0.0f && v.menu_y >= 0.0f) {
        put("menu_pos", fixed(v.menu_x, 4) + "," + fixed(v.menu_y, 4));
    }

    BbInputConfig::Append(keys);
    std::string out;
    bool had_lines = false;
    std::vector<bool> written(keys.size());
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), file)) {
            had_lines = true;
            std::string text{line};
            text.erase(text.find_last_not_of("\r\n") + 1);
            const auto eq = text.find('=');
            if (!text.empty() && text[0] != '#' && eq != std::string::npos) {
                std::string key = text.substr(0, eq);
                key.erase(key.find_last_not_of(" \t") + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                const auto it = std::find_if(keys.begin(), keys.end(),
                                             [&](const auto& k) { return k.first == key; });
                if (it != keys.end()) {
                    out += it->first + "=" + it->second + "\n";
                    written[size_t(it - keys.begin())] = true;
                    continue;
                }
            }
            out += text + "\n";
        }
        std::fclose(file);
    }
    if (!had_lines) {
        out = "# bbport settings (in-game menu: Insert / L3+R3)\n";
    }
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!written[k]) {
            out += keys[k].first + "=" + keys[k].second + "\n";
        }
    }
    // Replaced whole: a crash while writing leaves the old file.
    const std::string temporary = std::string(Path()) + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "w");
    bool ok = file && std::fwrite(out.data(), 1, out.size(), file) == out.size();
    ok = file && std::fclose(file) == 0 && ok;
#ifdef _WIN32
    const bool replaced = ok && MoveFileExA(temporary.c_str(), Path(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool replaced = ok && std::rename(temporary.c_str(), Path()) == 0;
#endif
    if (!replaced) {
        std::printf("Settings: cannot write %s\n", Path());
    }
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa",
                                                                  "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

} // namespace BbSettings
