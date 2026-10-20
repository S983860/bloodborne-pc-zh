// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into bbport.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <string>
#include "gpu/shim/bbport_settings.h"
#include "gpu/shim/engine/native_graphics_model.h"

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    const auto temporary = std::filesystem::temp_directory_path() /
        ("bbport-settings-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string filename = temporary.string();
    const char* path = filename.c_str();
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    { std::ofstream file(path); file << ini; assert(file.good()); }
#ifdef _WIN32
    _putenv_s("BB_CONFIG", path);
    _putenv_s("BB_DISPLAY_SIZE", "2560x1600");
#else
    setenv("BB_CONFIG", path, 1);
    setenv("BB_DISPLAY_SIZE", "2560x1600", 1);
#endif

    BbSettings::Load();
    auto& s = BbSettings::Get();
    assert(s.perfect_guard_window_ms==250 && s.guard_sfx_volume==70);
    s.perfect_guard_window_ms=300;s.guard_sfx_volume=60;
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.show_fps && s.menu_x < 0.0f);
    // A resolution appended after Auto must parse, save, and load unchanged.
    { std::ofstream file(path,std::ios::app); file << "output_res=3200x1800\n"; }
    BbSettings::Load();
    assert(BbSettings::OutputWidth(s.output_res)==3200 && BbSettings::OutputHeight(s.output_res)==1800);
    // Auto must preserve a 16:10 desktop, including after a settings save/reload.
    s.output_res=BbSettings::OutputAuto;
    BbSettings::Save();BbSettings::Load();
    assert(s.output_res==BbSettings::OutputAuto);
    assert(BbSettings::OutputWidth(s.output_res)==2560 && BbSettings::OutputHeight(s.output_res)==1600);
    assert(BbEngine::NativeGraphics::Value(BbEngine::NativeGraphics::Output).find("2560")!=std::string::npos);
    s.output_res=12;
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerFsr411;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    s.fullscreen = true;
    BbSettings::Save();

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=fsr411\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);
    assert(saved.find("fullscreen=1\n") != std::string::npos);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerFsr411);
    // Replacing an existing file works on Windows too, and must not erase key bindings.
    s.upscaler = BbSettings::UpscalerDlss;
    BbSettings::Save();
    assert(Read(path).find("upscaler=dlss\n") != std::string::npos);
    assert(Read(path).find("key.cross=X, Space\n") != std::string::npos);
    BbSettings::ConfigureUpscalerSupport(false, false, false);
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.dlss_supported);
    s.upscaler = BbSettings::UpscalerDlss;
    BbSettings::ConfigureUpscalerSupport(false, false, true);
    assert(s.upscaler == BbSettings::UpscalerDlss && s.dlss_supported);
    // FSR 4.1.1 without its dot products, on a GPU with FSR 4: FSR 4, not 3.1.
    s.upscaler = BbSettings::UpscalerFsr411;
    BbSettings::ConfigureUpscalerSupport(true, false, false);
    assert(s.upscaler == BbSettings::UpscalerFsr4);
    s.upscaler = BbSettings::UpscalerFsr411;
    BbSettings::ConfigureUpscalerSupport(false, false, false);
    assert(s.upscaler == BbSettings::UpscalerFsr3);
    s.camera_controls = true;
    s.camera_fov_scale = 1.25f;
    s.camera_distance_scale = 0.80f;
    s.camera_height_scale = 1.10f;
    BbSettings::Save();
    s.camera_controls = false;
    s.camera_fov_scale = s.camera_distance_scale = s.camera_height_scale = 1.0f;
    BbSettings::Load();
    assert(s.camera_controls && s.camera_fov_scale == 1.25f);
    assert(s.camera_distance_scale == 0.80f && s.camera_height_scale == 1.10f);
    s.change_appearance=true;s.rebirth=true;s.graphics_controls=true;
    s.graphics_bloom=0.7f;s.graphics_shadow_scale=1.4f;
    BbSettings::Save();
    s.rebirth=false;s.graphics_bloom=1.0f;
    BbSettings::Load();
    assert(s.change_appearance && s.rebirth && s.graphics_controls && s.graphics_bloom==0.7f && s.graphics_shadow_scale==1.4f);
    s.dlss_nr_enabled=false;s.hardware_rt_enabled=false;s.dlss_sr_model=1;
    s.dlss_nr_intensity=.6f;s.dlss_nr_tone=.7f;s.dlss_nr_structure=.8f;
    s.rt_radius=2.f;s.rt_strength=.4f;s.rt_samples=4;s.rt_detail=1;
    BbSettings::Save();s.dlss_nr_enabled=true;s.hardware_rt_enabled=true;s.rt_radius=1.f;
    BbSettings::Load();
    assert(!s.dlss_nr_enabled && !s.hardware_rt_enabled && s.dlss_sr_model==1);
    assert(s.dlss_nr_intensity==.6f && s.dlss_nr_tone==.7f && s.dlss_nr_structure==.8f);
    assert(s.rt_radius==2.f && s.rt_strength==.4f && s.rt_samples==4 && s.rt_detail==1);
    {std::ofstream file(path);file<<"rt_radius=nan\nrt_strength=inf\nrt_samples=999\nrt_detail=-5\ndlss_nr_intensity=-1\ndlss_nr_tone=nan\ndlss_nr_structure=inf\n";}
    BbSettings::Load();assert(s.rt_radius==1.25f && s.rt_strength==.28f && s.rt_samples==4 && s.rt_detail==0);
    assert(s.dlss_nr_intensity==0.f && s.dlss_nr_tone==1.f && s.dlss_nr_structure==1.f);
    // The native menu changes the same saved settings, keeps limits and skips
    // unavailable providers; restoring enhancements must preserve resolution.
    using namespace BbEngine::NativeGraphics;
    assert(Cycle(0,-1,4)==3 && Cycle(3,1,4)==0);
    s.sharpness=0.f;s.sharpen=false;assert(Step(Sharpness,1) && s.sharpen);
    assert(Step(Sharpness,-1) && !s.sharpen);
    s.rt_strength=1.f;assert(!Step(RtStrength,1) && s.rt_strength==1.f);
    s.rt_samples=4;assert(Step(RtSamples,1) && s.rt_samples==1);
    s.upscaler=BbSettings::UpscalerTaa;s.dlss_supported=false;
    assert(Step(Upscaler,1) && s.upscaler==BbSettings::UpscalerOff);
    s.graphics_controls=false;s.graphics_bloom=1.f;assert(Step(Bloom,1) && s.graphics_controls);
    // DLL-backed FSR4.1.1 stays selectable without legacy FSR4 files.
    s.upscaler=BbSettings::UpscalerFsr411;
    BbSettings::ConfigureUpscalerSupport(false,true,false);
    assert(s.upscaler==BbSettings::UpscalerFsr411 && s.fsr411_supported && !s.fsr4_supported);
    assert(Step(Upscaler,-1) && s.upscaler==BbSettings::UpscalerFsr3);
    assert(Step(Upscaler,1) && s.upscaler==BbSettings::UpscalerFsr411);
    s.upscaler=BbSettings::UpscalerOff;
    assert(Step(Upscaler,1) && s.upscaler==BbSettings::UpscalerFsr3);
    assert(Step(Upscaler,1) && s.upscaler==BbSettings::UpscalerFsr411);
    // A disabled custom tint stays disabled when editing effects, even with
    // non-neutral saved sliders. All existing NVIDIA colour values are retained.
    s.graphics_controls=false;s.graphics_color_controls=false;
    s.graphics_hue=16.f;s.graphics_saturation=1.6f;s.graphics_brightness=1.3f;
    for(auto option:{Bloom,Fog,Ambient,Shadow}){
        Step(option,1);assert(s.graphics_controls && !s.graphics_color_controls);
        assert(s.graphics_hue==16.f && s.graphics_saturation==1.6f && s.graphics_brightness==1.3f);
    }
    BbSettings::Save();s.graphics_color_controls=true;BbSettings::Load();
    assert(s.graphics_controls && !s.graphics_color_controls && s.graphics_hue==16.f);
    // Old on/off profiles preserve their original appearance at migration.
    {std::ofstream f(path);f<<"graphics_controls=0\ngraphics_hue=16\n";}
    BbSettings::Load();assert(!s.graphics_color_controls);
    {std::ofstream f(path);f<<"graphics_controls=1\ngraphics_hue=16\n";}
    BbSettings::Load();assert(s.graphics_color_controls && s.graphics_hue==16.f);
    BbSettings::ConfigureHardware(0x10de);assert(s.graphics_color_controls && s.graphics_hue==16.f);
    BbSettings::ConfigureHardware(0x1002);assert(!s.graphics_color_controls && s.graphics_hue==16.f);
    s.graphics_color_controls=true;BbSettings::Save();BbSettings::Load();
    BbSettings::ConfigureHardware(0x1002);assert(s.graphics_color_controls && s.graphics_hue==16.f);
    const int saved_resolution=s.output_res,saved_preset=s.preset;
    Step(Reset,1);assert(s.output_res==saved_resolution && s.preset==saved_preset);
    assert(!s.dlss_nr_enabled && !s.hardware_rt_enabled && s.rt_samples==2 && s.rt_radius==1.25f);
    // Edited configs must not feed infinite/NaN camera transforms to the engine.
    { std::ofstream file(path); file << "camera_fov_scale=nan\ncamera_distance_scale=-10\ncamera_height_scale=inf\n"; }
    BbSettings::Load();
    assert(s.camera_fov_scale == 1.0f && s.camera_distance_scale == 0.5f && s.camera_height_scale == 1.0f);
    // Menu languages: the code is read and written back; the added languages translate by the
    // English text and fall back to it; Russian keeps its own text.
    { std::ofstream file(path); file << "menu_language=pt_BR\n"; }
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::PortugueseBrazil);
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Fechar");
    assert(std::string(BbSettings::MenuText("Not in the tables", "x")) == "Not in the tables");
    BbSettings::Save();
    assert(Read(path).find("menu_language=pt_BR\n") != std::string::npos);
    s.menu_language = BbSettings::MenuLanguage::Turkish;
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Kapat");
    s.menu_language = BbSettings::MenuLanguage::Russian;
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Закрыть");
    // Cheats: off, infinite or an amount 1-99, written back as read; anything else is off.
    { std::ofstream file(path); file << "cheat_vials=infinite\ncheat_bullets=50\n"; }
    BbSettings::Load();
    assert(s.cheat_vials == BbSettings::CheatInfinite && s.cheat_bullets == 50);
    BbSettings::Save();
    assert(Read(path).find("cheat_vials=infinite\n") != std::string::npos);
    assert(Read(path).find("cheat_bullets=50\n") != std::string::npos);
    { std::ofstream file(path); file << "cheat_vials=100\ncheat_bullets=lots\n"; }
    BbSettings::Load();
    assert(s.cheat_vials == BbSettings::CheatOff && s.cheat_bullets == BbSettings::CheatOff);
    BbSettings::Save();
    assert(Read(path).find("cheat_vials=off\n") != std::string::npos);
    // The other cheats: 0/1 switches, firearm range off/150/200/300, echo multiplier 1/2/5/10;
    // written back as read, anything else the game's own.
    assert(Read(path).find("cheat_gun_range=off\n") != std::string::npos);
    assert(Read(path).find("cheat_echo_multiplier=1\n") != std::string::npos);
    assert(Read(path).find("cheat_stamina=0\n") != std::string::npos);
    {
        std::ofstream file(path);
        file << "cheat_stamina=1\ncheat_stealth=1\ncheat_gun_range=200\ncheat_keep_echoes=1\n"
                "cheat_echo_multiplier=5\ncheat_no_durability=1\ncheat_consumables=1\n";
    }
    BbSettings::Load();
    assert(s.cheat_stamina && s.cheat_stealth && s.cheat_keep_echoes && s.cheat_no_durability && s.cheat_consumables);
    assert(s.cheat_gun_range == 200 && s.cheat_echo_multiplier == 5);
    BbSettings::Save();
    for (const char* line : {"cheat_stamina=1\n", "cheat_stealth=1\n", "cheat_gun_range=200\n", "cheat_keep_echoes=1\n",
                             "cheat_echo_multiplier=5\n", "cheat_no_durability=1\n", "cheat_consumables=1\n"}) {
        assert(Read(path).find(line) != std::string::npos);
    }
    {
        std::ofstream file(path);
        file << "cheat_stamina=0\ncheat_stealth=0\ncheat_gun_range=250\ncheat_keep_echoes=0\n"
                "cheat_echo_multiplier=3\ncheat_no_durability=0\ncheat_consumables=0\n";
    }
    BbSettings::Load();
    assert(!s.cheat_stamina && !s.cheat_stealth && !s.cheat_keep_echoes && !s.cheat_no_durability && !s.cheat_consumables);
    assert(s.cheat_gun_range == 0 && s.cheat_echo_multiplier == 1);
    { std::ofstream file(path); file << "cheat_gun_range=300\ncheat_echo_multiplier=10\n"; }
    BbSettings::Load();
    assert(s.cheat_gun_range == 300 && s.cheat_echo_multiplier == 10);
    { std::ofstream file(path); file << "cheat_gun_range=off\ncheat_echo_multiplier=x2\n"; }
    BbSettings::Load();
    assert(s.cheat_gun_range == 0 && s.cheat_echo_multiplier == 1);
    BbSettings::Save();
    assert(Read(path).find("cheat_gun_range=off\n") != std::string::npos);
    assert(Read(path).find("cheat_echo_multiplier=1\n") != std::string::npos);
    // HDR output: hdr 0/1 (applied at start), paper white 10-500 and peak 10-4000 nits, boost
    // 0-1; written back as read; anything else clamped or the default.
    assert(Read(path).find("hdr=0\n") != std::string::npos);
    assert(Read(path).find("hdr_paper_white=200\n") != std::string::npos);
    assert(Read(path).find("hdr_peak=1000\n") != std::string::npos);
    assert(Read(path).find("hdr_boost=0.25\n") != std::string::npos);
    // hdr_mode: native (the default: highlights from the game's HDR scene) or expand (from the
    // SDR picture); live, so it is not a startup setting.
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    assert(Read(path).find("hdr_mode=native\n") != std::string::npos);
    { std::ofstream file(path); file << "hdr=1\nhdr_paper_white=250\nhdr_peak=20000\nhdr_boost=0.75\n"
                                        "hdr_mode=expand\n"; }
    BbSettings::Load();
    assert(s.hdr && s.startup_hdr && s.hdr_paper_white == 250.0f && s.hdr_peak == 4000.0f &&
           s.hdr_boost == 0.75f && s.hdr_mode == BbSettings::HdrModeExpand);
    BbSettings::Save();
    for (const char* line : {"hdr=1\n", "hdr_paper_white=250\n", "hdr_peak=4000\n", "hdr_boost=0.75\n",
                             "hdr_mode=expand\n"}) {
        assert(Read(path).find(line) != std::string::npos);
    }
    assert(Read(path).find("hdr_mode=native\n") == std::string::npos);
    { std::ofstream file(path); file << "hdr=0\nhdr_paper_white=10\nhdr_peak=nan\nhdr_boost=-1\n"
                                        "hdr_mode=bright\n"; }
    BbSettings::Load();
    assert(!s.hdr && !s.startup_hdr && s.hdr_paper_white == 10.0f && s.hdr_peak == 1000.0f &&
           s.hdr_boost == 0.0f && s.hdr_mode == BbSettings::HdrModeNative);
    { std::ofstream file(path); file << "hdr_mode=expand\n"; }
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeExpand);
    { std::ofstream file(path); file << "hdr_mode=native\n"; }
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    // BB_HDR* override the file.
#ifdef _WIN32
    _putenv_s("BB_HDR", "1");
    _putenv_s("BB_HDR_PEAK", "600");
    _putenv_s("BB_HDR_MODE", "expand");
#else
    setenv("BB_HDR", "1", 1);
    setenv("BB_HDR_PEAK", "600", 1);
    setenv("BB_HDR_MODE", "expand", 1);
#endif
    BbSettings::Load();
    assert(s.hdr && s.startup_hdr && s.hdr_peak == 600.0f && s.hdr_paper_white == 10.0f &&
           s.hdr_mode == BbSettings::HdrModeExpand);
#ifdef _WIN32
    _putenv_s("BB_HDR", "");
    _putenv_s("BB_HDR_PEAK", "");
    _putenv_s("BB_HDR_MODE", "");
#else
    unsetenv("BB_HDR");
    unsetenv("BB_HDR_PEAK");
    unsetenv("BB_HDR_MODE");
#endif
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    { std::ofstream file(path);file << "minimap_enabled=0\nminimap_range=999\nmouse_sensitivity=6\n"; }
    BbSettings::Load();assert(!s.minimap_enabled && s.minimap_range==120 && s.mouse_sensitivity==6);
    s.minimap_range=35;BbSettings::Save();BbSettings::Load();assert(s.minimap_range==35 && !s.minimap_enabled && s.mouse_sensitivity==6);
    s.melee_guard_toggle=true;BbSettings::Save();s.melee_guard_toggle=false;
    BbSettings::Load();assert(s.melee_guard_toggle);
    s.melee_guard_toggle=false;BbSettings::Save();s.melee_guard_toggle=true;
    BbSettings::Load();assert(!s.melee_guard_toggle);
    s.pad_guard_mode=1;BbSettings::Save();s.pad_guard_mode=0;BbSettings::Load();assert(s.pad_guard_mode==1);
    s.pad_guard_mode=0;BbSettings::Save();s.pad_guard_mode=1;BbSettings::Load();assert(s.pad_guard_mode==0);
    s.perfect_guard_window_ms=400;s.guard_sfx_volume=35;BbSettings::Save();
    s.perfect_guard_window_ms=250;s.guard_sfx_volume=70;BbSettings::Load();
    assert(s.perfect_guard_window_ms==400 && s.guard_sfx_volume==35);
    s.dlss_nr_enabled=true;s.hardware_rt_enabled=true;s.object_motion=true;BbSettings::ConfigureHardware(0x1002);
    assert(!s.dlss_nr_enabled&&!s.hardware_rt_enabled&&!s.object_motion&&!s.startup_object_motion);
    s.dlss_nr_enabled=true;s.hardware_rt_enabled=true;BbSettings::ConfigureHardware(0x8086);
    assert(!s.dlss_nr_enabled&&!s.hardware_rt_enabled);
    BbSettings::ConfigureHardware(0x10de);assert(!s.dlss_nr_enabled&&!s.hardware_rt_enabled);
    s.beginner_hints=false;s.control_mode=1;BbSettings::Save();s.beginner_hints=true;s.control_mode=0;
    BbSettings::Load();assert(!s.beginner_hints&&s.control_mode==1);
    std::filesystem::remove(temporary);
    std::puts("PASS: settings save keeps other keys, menu position");
}
