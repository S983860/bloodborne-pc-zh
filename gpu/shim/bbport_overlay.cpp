// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

#include <SDL3/SDL.h>
#include "bbport_settings.h"
#include "pc_game_ui.h"
#include "engine/pc_hud.h"
#include "input/bindings.h"
#include "bbport_usage.h"
#include "engine/cheats.h"
#include "engine/graphics.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

// DejaVu Sans (Latin with Latin Extended, Greek, Cyrillic), embedded (third_party/fonts, Bitstream
// Vera license).
#ifdef _WIN32
// PE/COFF assemblers have no .hidden/.previous: the compiler embeds the file (#embed, a GCC
// extension in C++).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
alignas(16) static const unsigned char bb_font_ttf[] = {
#embed BB_FONT_PATH
};
#pragma GCC diagnostic pop
static const unsigned char* const bb_font_ttf_end = bb_font_ttf + sizeof(bb_font_ttf);
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];
#endif

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
std::atomic<bool> open_requested{false};
bool l3_down = false, r3_down = false;
bool dirty = false; // settings changed while open: saved on close
float base_scale = 1.0f;

// The game's text dialog (ImeDialog, the character name), typed on the keyboard: drawn while it
// is open. In fullscreen the window title that showed it is not visible (issues #17, #19).
std::mutex prompt_mutex;
std::atomic<bool> prompt_active{false};
std::string prompt_title, prompt_text;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f;

float PixelDensity(SDL_WindowID id);

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    // The system cursor shows over the menu (window.cpp); ImGui learns where it is now, not at
    // the next motion: mouse motion is not passed on while the menu is closed.
    if (value) {
        if (SDL_Window* window = SDL_GetMouseFocus()) {
            float x = 0.0f, y = 0.0f;
            SDL_GetMouseState(&x, &y);
            const float density = PixelDensity(SDL_GetWindowID(window));
            ImGui::GetIO().AddMousePosEvent(x * density, y * density);
        }
    }
    if (!value && dirty) {
        dirty = false;
        BbSettings::Save();
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them.
template <typename T>
void Store(std::atomic<T>& target, T value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
    }
}

// The widget runs before Store reads v: argument evaluation order is unspecified (clang on
// Windows copied v before the widget changed it, so clicks stored the old value).
void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    const bool changed = ImGui::Checkbox(label, &v);
    Store(value, v, changed);
}

bool Slider(const char* label, std::atomic<float>& value, float lo, float hi,
            const char* format = "%.2f", ImGuiSliderFlags flags = ImGuiSliderFlags_None) {
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, format, flags);
    Store(value, v, changed);
    return changed;
}

// The amounts Custom starts from (the last one chosen in this session).
int custom_vials = 20, custom_bullets = 20;

// A cheat (engine/cheats.cpp): Off / Infinite / Custom, and the amount for Custom; live.
void Cheat(const char* label, std::atomic<int>& value, BbEngine::Cheats::Which which, int& custom) {
    ImGui::PushID(label);
    const char* modes[] = {BbSettings::MenuText("Off", "Выкл."),
                           BbSettings::MenuText("Infinite", "Бесконечно"),
                           BbSettings::MenuText("Custom", "Своё количество")};
    const int current = value;
    int mode = current == BbSettings::CheatOff ? 0 : current == BbSettings::CheatInfinite ? 1 : 2;
    if (current > 0) {
        custom = current;
    }
    if (ImGui::Combo(label, &mode, modes, 3)) {
        Store(value, mode == 0 ? BbSettings::CheatOff : mode == 1 ? BbSettings::CheatInfinite : custom, true);
    }
    if (mode == 2) {
        int amount = std::clamp(custom, 1, BbSettings::CheatMaxAmount);
        const bool changed = ImGui::SliderInt(BbSettings::MenuText("Amount", "Количество"), &amount, 1,
                                              BbSettings::CheatMaxAmount, "%d", ImGuiSliderFlags_AlwaysClamp);
        if (changed) {
            custom = std::clamp(amount, 1, BbSettings::CheatMaxAmount);
            Store(value, custom, true);
        }
    }
    if (const char* why = BbEngine::Cheats::Unavailable(which)) {
        ImGui::TextDisabled("%s: %s", BbSettings::MenuText("Unavailable", "Недоступно"), why);
    }
    ImGui::PopID();
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void CheatUnavailable(BbEngine::Cheats::Which which) {
    if (const char* why = BbEngine::Cheats::Unavailable(which)) {
        ImGui::TextDisabled("%s: %s", BbSettings::MenuText("Unavailable", "Недоступно"), why);
    }
}

// An on/off cheat (engine/cheats.cpp); live.
void CheatToggle(const char* label, std::atomic<bool>& value, BbEngine::Cheats::Which which, const char* hint) {
    ImGui::PushID(label);
    Checkbox(label, value);
    Hint(hint);
    CheatUnavailable(which);
    ImGui::PopID();
}

// A cheat with a few levels, the first the game's own; live.
template <std::size_t N>
void CheatLevels(const char* label, std::atomic<int>& value, const int (&levels)[N], const char* const (&names)[N],
                 BbEngine::Cheats::Which which, const char* hint) {
    ImGui::PushID(label);
    const int current = value;
    int index = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (levels[i] == current) {
            index = int(i);
        }
    }
    if (ImGui::Combo(label, &index, names, int(N))) {
        Store(value, levels[index], true);
    }
    Hint(hint);
    CheatUnavailable(which);
    ImGui::PopID();
}

// Adds Blood Echoes / Insight to the character in the world (applied by the engine's frame hook).
void CheatStats() {
    const bool off = BbEngine::Cheats::Unavailable(BbEngine::Cheats::Echoes) != nullptr;
    ImGui::BeginDisabled(off);
    ImGui::TextUnformatted(BbSettings::MenuText("Blood Echoes", "Отголоски крови"));
    for (int amount : {1000, 10000, 100000}) {
        char label[32];
        std::snprintf(label, sizeof(label), "+%d,%03d", amount / 1000, 0);
        ImGui::SameLine();
        if (ImGui::Button(label)) {
            BbEngine::Cheats::AddEchoes(amount);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(BbSettings::MenuText("+1 Insight", "+1 озарение"))) {
        BbEngine::Cheats::AddInsight(1);
    }
    ImGui::EndDisabled();
    Hint(BbSettings::MenuText(
        "Added to the character in the world, up to the game's limits (999,999,999 Blood Echoes, 99 Insight). "
        "They are saved with your character: turning cheats off does not take them back.",
        "Добавляются персонажу в игре, до пределов игры (999 999 999 отголосков, 99 озарения). Они "
        "сохраняются вместе с персонажем: выключение читов их не отнимает."));
    CheatUnavailable(BbEngine::Cheats::Echoes);
}

// HDR output (bbport.ini hdr*): on/off on restart; paper white, peak and boost live.
void HdrSection() {
    auto& s = BbSettings::Get();
    ImGui::SeparatorText(BbSettings::MenuText("HDR output", "Вывод HDR"));
    Checkbox(BbSettings::MenuText("HDR output (restart required)", "Вывод HDR (после перезапуска)"),
             s.hdr);
    Hint(BbSettings::MenuText(
        "Shows the game's SDR picture on an HDR display without washed-out colors, with optional "
        "highlight expansion. Windows HDR must be on (Settings > Display > Use HDR). Turning it "
        "on or off applies after restarting the game.",
        "Показывает SDR-картинку игры на HDR-дисплее без блёклых цветов, с необязательным "
        "расширением светлых участков. В Windows должен быть включён HDR (Параметры > Дисплей > "
        "Использовать HDR). Включение и выключение применяются после перезапуска игры."));
    const int output = s.hdr_output.load();
    if (output == BbSettings::HdrOutputOff) {
        if (s.startup_hdr) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s",
                               BbSettings::MenuText(
                                   "Not active: the display offers no HDR format (is Windows HDR on?)",
                                   "Не активен: дисплей не предлагает HDR-формат (включён ли HDR "
                                   "в Windows?)"));
            ImGui::PopTextWrapPos();
        }
        return;
    }
    ImGui::TextDisabled("%s", output == BbSettings::HdrOutputScRgb
                                  ? BbSettings::MenuText("Active: scRGB (FP16)", "Активен: scRGB (FP16)")
                                  : BbSettings::MenuText("Active: HDR10 (PQ, BT.2020)",
                                                         "Активен: HDR10 (PQ, BT.2020)"));
    // bbport: hdr_mode, live (docs/hdr.md).
    const char* modes[BbSettings::HdrModeCount] = {
        BbSettings::MenuText("Native (from the game's HDR scene)", "Нативный (из HDR-сцены игры)"),
        BbSettings::MenuText("Expand (from the SDR picture)", "Расширение (из SDR-картинки)"),
    };
    int mode = std::clamp(s.hdr_mode.load(), 0, BbSettings::HdrModeCount - 1);
    const bool mode_changed = ImGui::Combo(BbSettings::MenuText("HDR mode", "Режим HDR"), &mode,
                                           modes, BbSettings::HdrModeCount);
    Store(s.hdr_mode, mode, mode_changed);
    Hint(BbSettings::MenuText(
        "Native: the brightness of lamps, fire, the moon and other highlights comes from the "
        "game's own HDR scene, before its tonemap clipped it to SDR white. Midtones, colours and "
        "the UI stay as in the SDR picture. Menus, loading screens and movies (no scene) use "
        "Expand. Expand: the SDR picture's brightest parts are stretched towards the peak.",
        "Нативный: яркость ламп, огня, луны и других светлых мест берётся из собственной "
        "HDR-сцены игры, до того как тонмаппинг обрезал её до белого SDR. Средние тона, цвета и "
        "интерфейс остаются как в SDR-картинке. В меню, на загрузках и в видеороликах (без сцены) "
        "работает «Расширение». Расширение: самые яркие места SDR-картинки растягиваются к пику."));
    Slider(BbSettings::MenuText("Paper white (nits)", "Белый SDR (нит)"), s.hdr_paper_white,
           BbSettings::HdrPaperWhiteMin, BbSettings::HdrPaperWhiteMax, "%.0f",
           ImGuiSliderFlags_AlwaysClamp);
    Slider(BbSettings::MenuText("Peak brightness (nits)", "Пиковая яркость (нит)"), s.hdr_peak,
           BbSettings::HdrPeakMin, BbSettings::HdrPeakMax, "%.0f",
           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    Slider(BbSettings::MenuText("Highlight boost", "Усиление светлых участков"), s.hdr_boost, 0.0f,
           1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (mode == BbSettings::HdrModeNative) {
        Slider(BbSettings::MenuText("UI brightness", "Яркость интерфейса"), s.hdr_ui_brightness,
               BbSettings::HdrUiBrightnessMin, BbSettings::HdrUiBrightnessMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        Slider(BbSettings::MenuText("UI colour", "Цвета интерфейса"), s.hdr_ui_color, 0.0f, 1.0f, "%.2f",
               ImGuiSliderFlags_AlwaysClamp);
        Hint(BbSettings::MenuText(
            "Native mode only: the game's HUD, menus, subtitles and loading screens. UI brightness: their "
            "white, times paper white. UI colour: 0 shows their exact sRGB colours, 1 the wider colours a "
            "wide-gamut monitor shows SDR with (the usual look; exact sRGB can look washed out next to "
            "it). Live.",
            "Только в нативном режиме: интерфейс игры, меню, субтитры и экраны загрузки. Яркость "
            "интерфейса: его белый, умноженный на белый SDR. Цвета интерфейса: 0 — точные цвета sRGB, "
            "1 — более насыщенные, как на мониторе с широким охватом в SDR (привычный вид; точный sRGB "
            "рядом с ним может казаться блёклым). Меняются сразу."));
    }
    Hint(BbSettings::MenuText(
        "Paper white: how bright the game's white (and this menu) is; about 200 suits a dim room. "
        "Peak: your display's peak brightness (its HDR specs, or the Windows HDR Calibration app). "
        "Highlight boost: 0 shows the SDR picture as it is; higher values let the brightest parts "
        "(lamps, fire, the moon) rise towards the peak. In Native mode it is how much of the "
        "scene's own highlight range is shown: 0.25 about half, 1 all of it (up to the peak). "
        "Changes apply live. Ctrl+click a slider to type an exact value.",
        "Белый SDR: яркость белого в игре (и этого меню); около 200 подходит для неяркой комнаты. "
        "Пиковая яркость: максимум вашего дисплея (в его характеристиках HDR или в приложении "
        "«Калибровка HDR» Windows). Усиление светлых участков: 0 — SDR-картинка как есть; больше — "
        "самые яркие места (лампы, огонь, луна) поднимаются к пику. В нативном режиме — какая "
        "доля собственного диапазона светлых мест сцены показывается: 0.25 — около половины, "
        "1 — весь (до пика). Меняются сразу. Ctrl+клик по ползунку — ввести точное значение."));
}

// The game's own picture for each area (engine/graphics.cpp), live: Picture tuning too.
void PictureSection() {
    auto& s = BbSettings::Get();
    ImGui::SeparatorText(BbSettings::MenuText("Picture", "Изображение"));
    const bool live = BbEngine::Graphics::Enabled();
    if (!live) {
        ImGui::TextDisabled("%s", BbSettings::MenuText("Unavailable: the game's code differs from version 1.09.",
                                                       "Недоступно: код игры отличается от версии 1.09."));
    }
    ImGui::BeginDisabled(!live);
    Checkbox(BbSettings::MenuText("Picture tuning", "Настройка изображения"), s.graphics_controls);
    Checkbox("色彩调整（独立开关）",s.graphics_color_controls);
    // Colour edits enable colour only; effect edits never revive a disabled tint.
    const bool vignette = s.graphics_vignette;
    // The useful range on the slider; Ctrl+click types any value (bbport_settings.cpp keeps it
    // within wide limits).
    bool moved = Slider(BbSettings::MenuText("Brightness", "Яркость"), s.graphics_brightness, 0.0f, 3.0f);
    moved |= Slider(BbSettings::MenuText("Contrast", "Контраст"), s.graphics_contrast, 0.8f, 1.2f, "%.3f");
    moved |= Slider(BbSettings::MenuText("Saturation", "Насыщенность"), s.graphics_saturation, 0.0f, 3.0f);
    moved |= Slider(BbSettings::MenuText("Hue", "Оттенок"), s.graphics_hue, -180.0f, 180.0f, "%.0f");
    if(moved)Store(s.graphics_color_controls,true,true);
    moved = Slider(BbSettings::MenuText("Fog", "Туман"), s.graphics_fog, 0.0f, 3.0f);
    moved |= Slider("Bloom", s.graphics_bloom, 0.0f, 3.0f);
    Checkbox(BbSettings::MenuText("Vignette", "Виньетка"), s.graphics_vignette);
    moved |= vignette != s.graphics_vignette;
    moved |= Slider(BbSettings::MenuText("Ambient occlusion strength", "Сила затенения (AO)"), s.graphics_ao_strength,
                    0.0f, 3.0f);
    moved |= Slider(BbSettings::MenuText("Shadow distance", "Дальность теней"), s.graphics_shadow_scale, 0.5f, 4.0f);
    if (moved && !s.graphics_controls) {
        Store(s.graphics_controls, true, true);
    }
    if (ImGui::Button(BbSettings::MenuText("Game's own picture", "Как в игре"))) {
        Store(s.graphics_color_controls,false,true);
        Store(s.graphics_brightness, 1.0f, true);
        Store(s.graphics_contrast, 1.0f, true);
        Store(s.graphics_saturation, 1.0f, true);
        Store(s.graphics_hue, 0.0f, true);
        Store(s.graphics_fog, 1.0f, true);
        Store(s.graphics_bloom, 1.0f, true);
        Store(s.graphics_vignette, true, true);
        Store(s.graphics_ao_strength, 1.0f, true);
        Store(s.graphics_shadow_scale, 1.0f, true);
    }
    ImGui::EndDisabled();
    Hint(BbSettings::MenuText(
        "Changes apply live, Picture tuning too, to the game's own colour, fog and lighting of each area "
        "(the HUD and menus keep theirs). Moving one turns Picture tuning on; off: the game's own picture. 1.00 and Hue 0 are the game's. Fog 0 clears the haze over distant streets. The "
        "same settings are in the game's System menu (PC Graphics, PC Effects, PC Colour) and the "
        "launcher. Ctrl+click a slider to type any value, beyond its ends too (brightness, saturation and fog "
        "up to 5, contrast 0.5-1.5, hue -180-180, bloom up to 10, shadow distance 0.25-8).",
        "Меняются сразу, как и сама настройка: собственные цвет, туман и освещение каждой области "
        "(интерфейс и меню не меняются). Выкл. — картинка игры. 1.00 и оттенок 0 — как в игре. Туман 0 убирает дымку над дальними улицами. Те же "
        "настройки есть в системном меню игры (PC Graphics, PC Effects, PC Colour) и в лаунчере. "
        "Ctrl+клик по ползунку — ввести любое значение, в том числе за его краями (яркость, "
        "насыщенность и туман до 5, контраст 0.5-1.5, оттенок -180-180, bloom до 10, дальность теней "
        "0.25-8)."));
}

void AdvancedRendering() {
    auto& s=BbSettings::Get();
    if(!ImGui::CollapsingHeader("DLSS 5 与硬件光追",ImGuiTreeNodeFlags_DefaultOpen))return;
    Checkbox("DLSS 5 神经渲染",s.dlss_nr_enabled);
    ImGui::TextDisabled("%s",s.upscaler!=BbSettings::UpscalerDlss?"选用下方 DLSS 超分后生效":
        !s.dlss_nr_enabled?"已关闭神经渲染，保留超分":s.dlss_nr_supported?"神经渲染已就绪":"将在进入场景后检查可用性");
    ImGui::BeginDisabled(!s.dlss_nr_enabled);
    Slider("神经渲染强度",s.dlss_nr_intensity,0.f,1.f);
    Slider("局部光照调整",s.dlss_nr_tone,0.f,1.f);
    Slider("结构细节保留",s.dlss_nr_structure,0.f,1.f);
    ImGui::EndDisabled();
    const char* models[]={"自动","流畅优先（K）","均衡模型（L）","细节优先（M）"};
    int model=s.dlss_sr_model;
    if(ImGui::Combo("DLSS 超分模型",&model,models,4))Store(s.dlss_sr_model,model,true);
    ImGui::TextWrapped("超分质量、锐化和输出分辨率在下方设置。更换模型时可能短暂停顿。");
    ImGui::Separator();
    Checkbox("硬件光追环境遮蔽",s.hardware_rt_enabled);
    if(!s.hardware_rt_supported)ImGui::TextDisabled("当前设备或启动环境未提供硬件光追支持");
    ImGui::BeginDisabled(!s.hardware_rt_enabled || !s.hardware_rt_supported);
    Slider("光追阴影强度",s.rt_strength,0.f,1.f);
    Slider("光追作用范围",s.rt_radius,.1f,3.f);
    const char* rays[]={"流畅（1 次采样）","标准（2 次采样）","细腻（4 次采样）"};
    int quality=s.rt_samples<=1?0:s.rt_samples<=2?1:2;
    if(ImGui::Combo("光追采样质量",&quality,rays,3))Store(s.rt_samples,quality==0?1:quality==1?2:4,true);
    const char* details[]={"低","中","高"};int detail=s.rt_detail;
    if(ImGui::Combo("光追场景细节",&detail,details,3))Store(s.rt_detail,detail,true);
    ImGui::EndDisabled();
    ImGui::TextWrapped("目前覆盖可见场景的环境遮蔽与接触阴影。提高范围、采样和细节会增加显卡负担。");
    if(ImGui::Button("恢复本页渲染默认值")) {
        Store(s.dlss_nr_enabled,true,true);Store(s.hardware_rt_enabled,true,true);
        Store(s.dlss_nr_intensity,1.f,true);Store(s.dlss_nr_tone,1.f,true);Store(s.dlss_nr_structure,1.f,true);
        Store(s.rt_strength,.28f,true);Store(s.rt_radius,1.25f,true);Store(s.rt_samples,2,true);Store(s.rt_detail,2,true);
    }
}

void KeyboardSettings() {
    if(!ImGui::CollapsingHeader("键盘与鼠标设置"))return;
    ImGui::TextWrapped("点击当前键位后按键或鼠标重新绑定；组合重击可按住 Shift 再点击鼠标。单独绑定 Shift、Ctrl 或 Alt 时按下后松开。Esc取消，Delete清除。");
    const int capture=host_bind_capturing();
    for(int action=0;action<kBindDebugMenu;++action) {
        ImGui::PushID(action);
        ImGui::TextUnformatted(host_binding_info(action).label);
        ImGui::SameLine(250.f*base_scale);
        char text[96];host_binding_describe(action,text,sizeof text);
        ImGui::BeginDisabled(capture>=0 && capture!=action);
        if(ImGui::Button(capture==action?"请按键…":text,ImVec2(230.f*base_scale,0)))host_bind_capture_begin(action);
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(capture>=0);
        if(ImGui::SmallButton("清除")){host_binding_clear(action);dirty=true;}
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if(ImGui::Button("恢复默认键位")){host_bindings_load_defaults();dirty=true;}
}

void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Where it was moved last (bbport.ini menu_pos, a fraction of the screen), kept on screen.
    ImVec2 pos(viewport->WorkPos.x + 40.0f * base_scale, viewport->WorkPos.y + 40.0f * base_scale);
    if (s.menu_x >= 0.0f && s.menu_y >= 0.0f) {
        const float margin = 80.0f * base_scale;
        pos.x = viewport->WorkPos.x +
                std::clamp(s.menu_x * viewport->WorkSize.x, 0.0f, std::max(viewport->WorkSize.x - margin, 0.0f));
        pos.y = viewport->WorkPos.y +
                std::clamp(s.menu_y * viewport->WorkSize.y, 0.0f, std::max(viewport->WorkSize.y - margin, 0.0f));
    }
    // The contents are taller than a 1080p screen: the window ends above the screen's bottom edge
    // wherever its top is, so its scroll bar and resize corner stay in reach, and it opens as tall
    // as that allows (the contents' own height when they fit).
    static int last_frame = -2;
    static float window_top = 0.0f;
    const float min_height = 240.0f * base_scale;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - 8.0f * base_scale;
    pos.y = std::min(pos.y, std::max(viewport->WorkPos.y, bottom - min_height));
    const bool appearing = ImGui::GetFrameCount() != last_frame + 1;
    const float top = appearing ? pos.y : window_top;
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f * base_scale, min_height),
                                        ImVec2(FLT_MAX, std::max(min_height, bottom - top)));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(620.0f * base_scale, 0.0f), ImGuiCond_Appearing);
    bool keep_open = true;
    if (!ImGui::Begin(
            BbSettings::MenuText("Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
                                 "Bloodborne — настройки  (Insert / L3+R3)###bbport_settings"),
            &keep_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    last_frame = ImGui::GetFrameCount();
    window_top = ImGui::GetWindowPos().y;
    // Moved: remembered (saved with the settings when the menu closes).
    if (!ImGui::IsWindowAppearing() && viewport->WorkSize.x > 0.0f && viewport->WorkSize.y > 0.0f) {
        const ImVec2 at = ImGui::GetWindowPos();
        const float fx = (at.x - viewport->WorkPos.x) / viewport->WorkSize.x;
        const float fy = (at.y - viewport->WorkPos.y) / viewport->WorkSize.y;
        if (std::abs(at.x - pos.x) >= 1.0f || std::abs(at.y - pos.y) >= 1.0f) {
            s.menu_x = fx;
            s.menu_y = fy;
            dirty = true;
        }
    }
    // Always readable, whatever language the rest of the menu is in: each language by its own
    // name, in MenuLanguage order.
    static const char* languages[] = {"English",  "Русский",  "Español", "Português (Brasil)",
                                      "Français", "Deutsch",  "Italiano", "Polski", "Türkçe"};
    static_assert(std::size(languages) == BbSettings::MenuLanguageCount);
    int language = int(s.menu_language.load());
    const char* pc_chinese=std::getenv("BB_PC_CHINESE");
    if(pc_chinese && pc_chinese[0]=='1')ImGui::TextUnformatted("简体中文");
    else if (ImGui::Combo("Language", &language, languages, BbSettings::MenuLanguageCount)) {
        s.menu_language =
            BbSettings::MenuLanguage(std::clamp(language, 0, BbSettings::MenuLanguageCount - 1));
        BbSettings::Save();
    }
    ImGui::Text(BbSettings::MenuText("%.0f FPS  (%.1f ms)", "%.0f FPS  (%.1f мс)"),
                frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f, frame_ms_avg);

    AdvancedRendering();
    KeyboardSettings();
    {
        const char* modes[]={"L1 / LB 单键格挡（变形：按左摇杆）","L1 + L2 / LB + LT 组合格挡（原方案）"};
        int mode=s.pad_guard_mode;
        if(ImGui::Combo("手柄格挡方式",&mode,modes,2))Store(s.pad_guard_mode,mode,true);
        ImGui::TextWrapped("手柄格挡始终为按住生效、松开退出；L2 / LT 单独使用仍为射击。仅适用于锯齿砍刀。");
    }
    ImGui::SeparatorText(BbSettings::MenuText("Temporal upscaler", "Временной апскейлер"));
    const char* upscalers[] = {
        BbSettings::MenuText("Off", "Выкл"), "FSR 3.1", "FSR 4 (INT8)", "FSR 4.1.1 (INT8 · 实验性)",
        BbSettings::MenuText("TAA (native anti-aliasing)", "TAA (нативное сглаживание)"), "DLSS (NVIDIA)"};
    static const char* later[] = {"XeSS"};
    int upscaler = s.upscaler;
    if (ImGui::BeginCombo(BbSettings::MenuText("Upscaler", "Апскейлер"), upscalers[upscaler])) {
        for (int i = 0; i < BbSettings::UpscalerCount; ++i) {
            const bool supported = i == BbSettings::UpscalerFsr4     ? s.fsr4_supported.load()
                                   : i == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                                                                     : i == BbSettings::UpscalerDlss ? s.dlss_supported.load() : true;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(upscalers[i], i == upscaler)) {
                Store(s.upscaler, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", BbSettings::MenuText("— not supported by this GPU",
                                                         "— не поддерживается видеокартой"));
            }
        }
        for (const char* name : later) {
            ImGui::BeginDisabled();
            ImGui::Selectable(name, false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", BbSettings::MenuText("— in development", "— в работе"));
        }
        ImGui::EndCombo();
    }
    if (const char* problem = s.fsr4_problem.load()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Upscaler unavailable: %s", problem);
        if (!BbSettings::IsFsr4(s.upscaler))
            ImGui::TextUnformatted("The mode selected above is active. You can select it again to retry.");
        ImGui::PopTextWrapPos();
    }
    if (BbSettings::IsFsr4(s.upscaler)) {
        if (s.upscaler == BbSettings::UpscalerFsr411) {
            if (s.gpu_vendor == 0x1002) {
                ImGui::PushTextWrapPos();
                ImGui::TextColored(ImVec4(.82f,.70f,.43f,1.f), "%s",
                    "A卡实景兼容性尚未通过验证。若出现马赛克、闪烁或重影，请选择 FSR 3.1；切换质量档位可能需要重启。此选项为手动试用。");
                ImGui::PopTextWrapPos();
            }
            Hint(BbSettings::MenuText(
                "FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan "
                "(output matches the DLL). One model for Native through Performance and another "
                "for Ultra Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL "
                "and Proton).",
                "FSR 4.1.1 в режиме INT8: модель из DLL AMD 4.1.1, воспроизведённая в Vulkan "
                "(результат совпадает с DLL). Одна модель для Native..Performance и отдельная "
                "для Ultra Performance. Ассеты: tools/fsr4cap/build_assets.sh (нужны DLL и "
                "Proton)."));
        } else {
            Hint(BbSettings::MenuText(
                "FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality "
                "than FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the "
                "model (a brief pause). Assets: tools/fetch_fsr4_assets.sh.",
                "FSR 4 в режиме INT8 (модель v07 из исходников AMD FidelityFX SDK). Качество выше, "
                "чем у FSR 3.1, но проход тяжелее. Смена пресета пересобирает модель (короткая "
                "пауза). Ассеты: tools/fetch_fsr4_assets.sh."));
        }
        Checkbox(BbSettings::MenuText("FSR 4: auto exposure", "FSR 4: авто-экспозиция"),
                 s.fsr4_auto_exposure);
        Checkbox(BbSettings::MenuText("FSR 4: invert jitter sign", "FSR 4: обратный знак jitter"),
                 s.fsr4_invert_jitter);
        Hint(BbSettings::MenuText(
            "For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses "
            "it to decide when to discard previous frames. Changes apply immediately, without a "
            "restart.",
            "Проверка при гостинге: сеть FSR 4 нормирует цвет по экспозиции и по ней решает, "
            "когда отбросить прошлые кадры. Меняются сразу, без перезапуска."));
    }
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    ImGui::BeginDisabled(!upscaler_on);
    ImGui::BeginDisabled(taa);
    int preset = taa ? BbSettings::NativeAA : s.preset.load();
    char preset_label[64];
    std::snprintf(preset_label, sizeof(preset_label), "%s (x%.1f)", BbSettings::PresetName(preset),
                  BbSettings::PresetScale(preset));
    if (ImGui::BeginCombo(BbSettings::MenuText("Preset", "Пресет"), preset_label)) {
        for (int i = 0; i < BbSettings::PresetCount; ++i) {
            char label[64];
            const float scale = BbSettings::PresetScale(i);
            const int output = s.output_res;
            std::snprintf(
                label, sizeof(label),
                BbSettings::MenuText("%s (x%.1f, render %dx%d)", "%s (x%.1f, рендер %dx%d)"),
                BbSettings::PresetName(i), scale,
                int(std::lround(BbSettings::OutputWidth(output) / scale / 2) * 2),
                int(std::lround(BbSettings::OutputHeight(output) / scale / 2) * 2));
            if (ImGui::Selectable(label, i == preset)) {
                Store(s.preset, i, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (taa) {
        ImGui::TextWrapped("TAA anti-aliases the scene at the output resolution, without an FSR "
                           "model or upscaling. The saved preset returns when an upscaler is selected.");
    }
    ImGui::Text("Active scene render: %d x %d", s.active_render_width.load(),
                s.active_render_height.load());
    if (BbSettings::FixedRenderSession()) {
        ImGui::Text("Preset at startup: %s", BbSettings::PresetName(s.startup_preset));
        if (const char* automatic = std::getenv("BB_AUTO_RENDER_RES");
            automatic && automatic[0] == '1') {
            Hint("With an output other than 1080p the whole game renders at the preset resolution "
                 "(patched at startup): fastest on the Steam Deck and weaker GPUs. Preset or output "
                 "changes apply after a restart. \"Live resolution changes\" below allows changing "
                 "without a restart (post-processing then stays at 1080p, slower).");
        } else {
            Hint("BB_RENDER_RES fixes the scene size at startup. Remove this explicit variable "
                 "to change resolution and presets without restarting the game.");
        }
    } else {
        Hint("Native AA: the upscaler works as anti-aliasing. The other presets lower the scene "
             "render resolution relative to the output. The UI is drawn at the output resolution. "
             "The preset applies from the next frame without restarting the game.");
    }
    Checkbox("Sharpening (RCAS)", s.sharpen);
    ImGui::BeginDisabled(!s.sharpen);
    Slider("Sharpness", s.sharpness, 0.0f, 2.0f);
    Hint("Up to 1: the upscaler's own sharpening (RCAS). Above 1 another RCAS pass is added. "
         "DLSS has no sharpening of its own: an RCAS pass does all of it. "
         "Ctrl+click the slider to type an exact value.");
    ImGui::EndDisabled();
    Checkbox("Sub-pixel jitter", s.jitter);
    Hint("Each frame the scene shifts by a fraction of a pixel, and the upscaler gathers more "
         "detail from several frames. Without it you only get history-based anti-aliasing.");

    ImGui::SeparatorText("Reactive mask");
    ImGui::BeginDisabled(taa);
    Checkbox("Enable mask", s.reactive);
    Hint("Marks transparent effects (particles, haze) so the upscaler relies less on past "
         "frames. Fewer trails behind effects, but shimmer returns underneath them.");
    ImGui::BeginDisabled(!s.reactive);
    Slider("Scale", s.reactive_scale, 0.0f, 4.0f);
    Slider("Threshold", s.reactive_threshold, 0.0f, 1.0f);
    Slider("Maximum", s.reactive_max, 0.0f, 1.0f);
    bool show_mask = s.debug_view == BbSettings::DebugReactive;
    if (ImGui::Checkbox("Show mask (debug)", &show_mask)) {
        s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Checkbox(BbSettings::MenuText("Character motion vectors", "Векторы движения персонажей"),
             s.object_motion);
    Hint(BbSettings::MenuText(
        "Accurate vectors for animated objects: clothing and weapons break up less "
        "during movement. The static scene does not get an additional pass. "
        "Changes apply after restarting the game.",
        "Точные векторы для анимированных объектов: одежда и оружие меньше рассыпаются "
        "при движении. Статичная сцена не получает дополнительный проход. "
        "Изменение применяется после перезапуска игры."));
    bool show_motion = s.debug_view == BbSettings::DebugMotion;
    if (ImGui::Checkbox(BbSettings::MenuText("Show motion vectors (debug)",
                                             "Показать векторы движения (отладка)"),
                        &show_motion)) {
        s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
    }
    Hint(BbSettings::MenuText(
        "Red/green: horizontal/vertical motion (8 pixels = full brightness). "
        "Blue: the pixel received an accurate object vector, not just camera motion. "
        "The upscaler treats a moving object with no blue or red/green as "
        "stationary, causing trails.",
        "Красный/зелёный: движение по горизонтали/вертикали (8 пикселей = полная яркость). "
        "Синий: пиксель получил точный вектор объекта, а не только движение камеры. "
        "Движущийся предмет без синего и без красного/зелёного апскейлер считает "
        "неподвижным, отсюда шлейф."));
    ImGui::EndDisabled(); // upscaler off

    ImGui::SeparatorText(BbSettings::MenuText("Output resolution", "Разрешение вывода"));
    static const char* outputs[] = {"1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160",
        "2560 x 1080", "3440 x 1440", "3840 x 1600", "3840 x 1080", "5120 x 1440", "5120 x 2160",
        "7680 x 2160", "Display (auto)", "3200 x 1800"};
    static_assert(std::size(outputs)==BbSettings::OutputCount);
    int output = s.output_res;
    if (ImGui::BeginCombo(BbSettings::MenuText("Output resolution", "Разрешение вывода"),
                          outputs[output])) {
        for (int i = 0; i < BbSettings::OutputCount; ++i) {
            if (ImGui::Selectable(outputs[i], i == output)) {
                Store(s.output_res, i, true);
            }
        }
        ImGui::EndCombo();
    }
    if (BbSettings::FixedRenderSession()) {
        Hint(BbSettings::MenuText(
            "Size of the final frame and UI. The preset sets the scene size relative to "
            "the output: 4K Performance = 1920x1080. Applies after restarting the game.",
            "Размер готового кадра и интерфейса. Пресет задаёт размер сцены относительно "
            "вывода: 4K Performance = 1920x1080. Применяется после перезапуска игры."));
    } else {
        Hint(BbSettings::MenuText(
            "The final frame and UI size changes at the next frame boundary. "
            "The preset sets the scene size relative to the output: 4K Performance = 1920x1080. "
            "Changing the size resets FSR history and may cause a brief pause.",
            "Размер готового кадра и интерфейса меняется на границе следующего кадра. "
            "Пресет задаёт размер сцены относительно вывода: 4K Performance = 1920x1080. "
            "Смена размера сбрасывает историю FSR и может вызвать короткую паузу."));
    }
    const char* live_modes[] = {BbSettings::MenuText("Auto (based on GPU)", "Авто (по видеокарте)"),
                                BbSettings::MenuText("Off (faster)", "Выключена (быстрее)"),
                                BbSettings::MenuText("On", "Включена")};
    int live = s.live_resolution + 1;
    if (ImGui::BeginCombo(
            BbSettings::MenuText("Live resolution changes", "Смена разрешения на лету"),
            live_modes[live])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(live_modes[i], i == live)) {
                Store(s.live_resolution, i - 1, true);
            }
        }
        ImGui::EndCombo();
    }
    Hint(BbSettings::MenuText(
        "On: output resolution and preset change without restarting, but game post-processing "
        "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. "
        "Off: everything renders at the preset resolution, and changes require a restart. Auto "
        "enables this on powerful discrete GPUs. Applies after restarting the game.",
        "Включена: разрешение вывода и пресет меняются без перезапуска, но постобработка игры "
        "остаётся в 1080p — на Steam Deck и старых видеокартах это заметно медленнее. "
        "Выключена: всё рисуется в разрешении пресета, смена — через перезапуск. Авто включает "
        "её на мощных дискретных видеокартах. Применяется после перезапуска игры."));
    HdrSection();
    PictureSection();
    ImGui::SeparatorText(BbSettings::MenuText("Game effects (restart required)",
                                              "Эффекты игры (после перезапуска)"));
    const char* lods[] = {BbSettings::MenuText("Highest (-2)", "Максимальная (-2)"),
                          BbSettings::MenuText("Game default", "Как в игре"),
                          BbSettings::MenuText("Lower (1)", "Ниже (1)"),
                          BbSettings::MenuText("Lowest (2)", "Минимальная (2)")};
    static constexpr int lod_values[] = {-2, 0, 1, 2};
    int lod_index = 1;
    for (int i = 0; i < 4; ++i) {
        if (lod_values[i] == s.model_lod) lod_index = i;
    }
    if (ImGui::BeginCombo(BbSettings::MenuText("Model detail", "Детализация моделей"),
                          lods[lod_index])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(lods[i], i == lod_index)) {
                Store(s.model_lod, lod_values[i], true);
            }
        }
        ImGui::EndCombo();
    }
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        const auto& effect = BbSettings::Effects[e];
        Checkbox(BbSettings::MenuText(effect.label, effect.label_ru), s.effects[e]);
    }
    Hint(BbSettings::MenuText(
        "Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
        "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
        "Эффекты включаются и выключаются патчами игры при запуске (patches/Bloodborne.xml). "
        "Размытие в движении и тени от динамических источников заметно нагружают GPU."));
    Hint(BbSettings::MenuText(
        "Free camera: hold Cross and press L3 (keyboard: Space + Z). "
        "Debug menu: left touchpad / Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf "
        "in dvdroot_ps4/font from Nexus mod #253. Right touchpad: Backspace.",
        "Свободная камера: удерживайте Cross и нажимайте L3 (клавиатура: Space + Z). "
        "Debug menu: левый touchpad / Tab. Нужны DbgFont14h.ccm и DbgFont14h.tpf "
        "в dvdroot_ps4/font из мода Nexus #253. Правый touchpad: Backspace."));

    bool restart =
        s.object_motion != s.startup_object_motion || s.model_lod != s.startup_model_lod ||
        s.live_resolution != s.startup_live_resolution || s.hdr != s.startup_hdr ||
        BbSettings::ResolutionNeedsRestart();
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        restart |= s.effects[e] != s.startup_effects[e];
    }
    if (restart) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                           BbSettings::MenuText("Changes apply after restarting the game",
                                                "Изменения применятся после перезапуска игры"));
        if (ImGui::Button(
                BbSettings::MenuText("Apply and restart game", "Применить и перезапустить игру"))) {
            BbSettings::Save();
            runtime_restart();
        }
    }

    ImGui::SeparatorText("PC Camera");
    ImGui::TextDisabled("Enable PC Camera in the launcher before starting; changes here are live.");
    Slider("Camera FOV scale", s.camera_fov_scale, 1.0f, 1.5f);
    Slider("Camera distance scale", s.camera_distance_scale, 0.5f, 1.5f);
    Slider("Camera height scale", s.camera_height_scale, 0.5f, 1.5f);

    ImGui::SeparatorText(BbSettings::MenuText("Cheats", "Читы"));
    Cheat(BbSettings::MenuText("Blood Vials", "Флаконы крови"), s.cheat_vials, BbEngine::Cheats::Vials,
          custom_vials);
    Cheat(BbSettings::MenuText("Quicksilver Bullets", "Ртутные пули"), s.cheat_bullets,
          BbEngine::Cheats::Bullets, custom_bullets);
    Hint(BbSettings::MenuText(
        "Changes apply live. Infinite: kept full at the normal carry limit, even from none. Custom: the count is "
        "refilled to the amount and never lowered. Off restores the game's item data and keeps the "
        "counts you have. While a cheat is on, the counts it gives are saved with your character. "
        "Do not play online with cheats on.",
        "Применяются сразу. Бесконечно: запас всегда полный, даже если предметов не было. Своё "
        "количество: запас пополняется до него и никогда не уменьшается. Выкл. возвращает данные "
        "предметов игры, а имеющееся количество остаётся. Пока чит включён, полученные предметы "
        "сохраняются вместе с персонажем. Не играйте по сети с включёнными читами."));
    CheatToggle(BbSettings::MenuText("Infinite stamina", "Бесконечная выносливость"), s.cheat_stamina,
                BbEngine::Cheats::Stamina,
                BbSettings::MenuText("Your stamina stays full (enemies are not affected). Off: nothing to restore.",
                                     "Ваша выносливость всегда полная (на врагов не действует). Выкл.: "
                                     "восстанавливать нечего."));
    CheatToggle(BbSettings::MenuText("Stealth", "Скрытность"), s.cheat_stealth, BbEngine::Cheats::Stealth,
                BbSettings::MenuText("Enemies neither see nor hear you: the game's debug flags Player Hide and "
                                     "Player Silence. Off puts the flags back as they were.",
                                     "Враги вас не видят и не слышат: отладочные флаги игры Player Hide и Player "
                                     "Silence. Выкл. возвращает флаги как были."));
    {
        static const char* const ranges[] = {"Off", "150%", "200%", "300%"};
        const char* const names[] = {BbSettings::MenuText(ranges[0], "Выкл."), ranges[1], ranges[2], ranges[3]};
        CheatLevels(BbSettings::MenuText("Firearm range", "Дальность огнестрела"), s.cheat_gun_range,
                    BbSettings::CheatGunRanges, names, BbEngine::Cheats::GunRange,
                    BbSettings::MenuText(
                        "Bullets of the pistols, rifles, blunderbusses, cannons, Gatling Gun and the trick "
                        "weapons' guns fly farther. Not Rosmarinus, the Flamesprayer, arcane or hunter tools. "
                        "Off restores the game's bullet data.",
                        "Пули пистолетов, ружей, мушкетонов, пушек, пулемёта и огнестрела трансформируемого "
                        "оружия летят дальше. Не Розмарин, огнемёт, тайные и охотничьи инструменты. Выкл. "
                        "возвращает данные пуль игры."));
    }
    CheatToggle(BbSettings::MenuText("Keep Blood Echoes on death", "Не терять отголоски при смерти"),
                s.cheat_keep_echoes, BbEngine::Cheats::KeepEchoes,
                BbSettings::MenuText("Dying keeps your Blood Echoes; the bloodstain holds none. Off restores the "
                                     "game's code. Echoes you kept stay in your save.",
                                     "При смерти отголоски остаются у вас, в пятне крови их нет. Выкл. "
                                     "возвращает код игры. Сохранённые отголоски остаются в сохранении."));
    {
        static const char* const multipliers[] = {"x1", "x2", "x5", "x10"};
        const char* const names[] = {BbSettings::MenuText("x1 (off)", "x1 (выкл.)"), multipliers[1], multipliers[2],
                                     multipliers[3]};
        CheatLevels(BbSettings::MenuText("Blood Echo multiplier", "Множитель отголосков"), s.cheat_echo_multiplier,
                    BbSettings::CheatEchoMultipliers, names, BbEngine::Cheats::EchoMultiplier,
                    BbSettings::MenuText(
                        "Blood Echoes from enemies and bosses times this. An enemy already loaded may give the "
                        "normal amount until the area loads again. x1 restores the game's data; echoes gained "
                        "stay in your save.",
                        "Отголоски за врагов и боссов умножаются. Уже загруженный враг может дать обычное "
                        "количество, пока область не загрузится заново. x1 возвращает данные игры; полученные "
                        "отголоски остаются в сохранении."));
    }
    CheatToggle(BbSettings::MenuText("No weapon wear", "Оружие не изнашивается"), s.cheat_no_durability,
                BbEngine::Cheats::NoDurability,
                BbSettings::MenuText("Equipped weapons are kept at full durability. Off stops it; durability is "
                                     "saved as it is.",
                                     "Экипированное оружие всегда с полной прочностью. Выкл. прекращает это; "
                                     "прочность сохраняется как есть."));
    CheatToggle(BbSettings::MenuText("Infinite consumables", "Бесконечные расходники"), s.cheat_consumables,
                BbEngine::Cheats::Consumables,
                BbSettings::MenuText(
                    "Molotovs, knives, pebbles, oil urns, antidotes, sedatives, elixirs, papers, Numbing Mist, "
                    "Shining Coins and similar tools are not used up. Not Blood Vials or Quicksilver Bullets "
                    "(above), key items, materials, or Insight and Blood Echo items. Off restores the item data.",
                    "Коктейли Молотова, ножи, камешки, масло, антидоты, успокоительное, эликсиры, бумага, "
                    "онемляющий туман, блестящие монеты и подобные инструменты не расходуются. Не флаконы крови и "
                    "ртутные пули (выше), не ключевые предметы, материалы, предметы озарения и отголосков. Выкл. "
                    "возвращает данные предметов."));
    CheatStats();

    ImGui::SeparatorText(BbSettings::MenuText("Other", "Прочее"));
    Checkbox(BbSettings::MenuText("FPS counter in corner", "Счётчик FPS в углу"), s.show_fps);
    if (s.show_fps) {
        ImGui::Indent();
        Checkbox(BbSettings::MenuText("Frame rate", "Частота кадров"), s.show_fps_rate);
        Checkbox(BbSettings::MenuText("Frame time", "Время кадра"), s.show_fps_frame_time);
        Checkbox(BbSettings::MenuText("Upscaler", "Апскейлер"), s.show_fps_upscaler);
        Checkbox(BbSettings::MenuText("CPU usage", "Загрузка ЦП"), s.show_fps_cpu);
        Checkbox(BbSettings::MenuText("GPU usage", "Загрузка ГП"), s.show_fps_gpu);
        ImGui::Unindent();
    }
    Checkbox(BbSettings::MenuText("Hide mouse cursor", "Скрывать курсор мыши"), s.hide_cursor);
    Hint(BbSettings::MenuText("Hidden 2 seconds after the mouse stops moving; moving it brings it back.",
                              "Скрывается через 2 секунды после остановки мыши; движение возвращает его."));

    ImGui::Spacing();
    if (ImGui::Button(BbSettings::MenuText("Close", "Закрыть"))) {
        keep_open = false;
    }
    // Quit (issue #21): the same clean exit as closing the window, so a controller player need not
    // force-close the game. Asks once more before quitting; the game's own save is untouched.
    static bool quit_armed = false;
    ImGui::SameLine();
    if (ImGui::Button(quit_armed ? BbSettings::MenuText("Press again to quit", "Нажмите ещё раз")
                                 : BbSettings::MenuText("Quit game", "Выйти из игры"))) {
        if (quit_armed) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        quit_armed = !quit_armed;
    }
    if (quit_armed && !ImGui::IsItemHovered() && !ImGui::IsItemFocused()) {
        quit_armed = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", BbSettings::MenuText("Settings are saved to bbport.ini",
                                             "Настройки сохраняются в bbport.ini"));
    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

void FpsCounter() {
    // What it shows (the menu's checkboxes under the counter); nothing chosen: no window.
    const auto& s = BbSettings::Get();
    char text[128];
    int n = 0;
    if (s.show_fps_rate) {
        n += std::snprintf(text + n, sizeof(text) - n, "%.0f FPS",
                           frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f);
    }
    if (s.show_fps_frame_time) {
        n += std::snprintf(text + n, sizeof(text) - n, "%s%.1f ms", n ? "  " : "", frame_ms_avg);
    }
    if (s.show_fps_upscaler) {
        n += std::snprintf(text + n, sizeof(text) - n, "%s%s", n ? "  " : "",
                           BbSettings::UpscalerName(s.upscaler));
    }
    if (s.show_fps_cpu || s.show_fps_gpu) {
        const BbUsage::Sample use = BbUsage::Get(s.show_fps_cpu, s.show_fps_gpu);
        if (s.show_fps_cpu && use.cpu >= 0.0f) {
            n += std::snprintf(text + n, sizeof(text) - n, "%sCPU %.0f%%", n ? "  " : "", use.cpu);
        }
        if (s.show_fps_gpu && use.gpu >= 0.0f) {
            n += std::snprintf(text + n, sizeof(text) - n, "%sGPU %.0f%%", n ? "  " : "", use.gpu);
        }
    }
    if (n == 0) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad, viewport->WorkPos.y + pad),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(text);
    ImGui::End();
}

void TextPrompt() {
    std::string title, text;
    {
        std::scoped_lock lock{prompt_mutex};
        title = prompt_title;
        text = prompt_text;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("##textprompt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();
    ImGui::Text("%s_", text.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Keyboard: type, Backspace = delete, Enter = OK, Esc = cancel");
    ImGui::End();
}

} // namespace

void SetTextPrompt(bool active, const std::string& prompt, const std::string& text) {
    {
        std::scoped_lock lock{prompt_mutex};
        prompt_title = prompt;
        prompt_text = text;
    }
    prompt_active = active;
}

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    // No GlyphRanges: the Vulkan backend has ImGuiBackendFlags_RendererHasTextures (ImGui 1.92), so
    // the atlas loads each glyph of the font when first drawn (Cyrillic, ñ ç ü ß ą ł ş ğ ı, —).
    // Ranges would only limit that.
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    ImFont* pc_font=nullptr;
    if(const char* root=std::getenv("BB_DATA_DIR")){
        const auto font=(std::filesystem::u8path(root)/"engine/assets/fonts/NotoSansSC.ttf").u8string();
        pc_font=io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(font.c_str()),20.0f);
    }
#ifdef _WIN32
    // Full local system font also covers arbitrary Chinese character names.
    if(!pc_font)pc_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",18.0f);
#endif
    if(!pc_font)io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);
    BbGameUi::SetMenuFont(nullptr);
    if(const char* root=std::getenv("BB_DATA_DIR")){
        const auto path=std::filesystem::path(reinterpret_cast<const char8_t*>(root))/"engine/assets/fonts/NotoSerifSC.otf";
        if(std::filesystem::is_regular_file(path)){
            const auto font=path.u8string();
            BbGameUi::SetMenuFont(io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(font.c_str()),23.0f));
        }
    }

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert or L3+R3)\n");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        if(initialized && open_requested.exchange(false))SetOpen(true);
        if(initialized && menu_open){
            int count=0;const bool* keys=SDL_GetKeyboardState(&count);
            const bool any=SDL_GetMouseState(nullptr,nullptr)!=0 || (keys && std::any_of(keys,keys+count,[](bool down){return down;}));
            host_bind_capture_note_held(any);
        }
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    // Fullscreen is a window action and must remain available inside either menu.
    if((event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) && event.key.key==SDLK_F11)return false;
    if(!menu_open && !prompt_active && host_bind_capturing()<0 && BbGameUi::HandleEvent(event))return true;
    const bool is_open = menu_open || BbGameUi::Open();
    if(is_open && host_bind_capturing()>=0) {
        bool taken=false;
        if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat)taken=host_bind_capture_key(event.key.scancode);
        if(event.type==SDL_EVENT_KEY_UP)taken=host_bind_capture_key_up(event.key.scancode);
        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){const int button=event.button.button;taken=host_bind_capture_mouse(button==2?3:button==3?2:button);}
        if(event.type==SDL_EVENT_MOUSE_WHEEL && event.wheel.y!=0)taken=host_bind_capture_mouse(event.wheel.y>0?6:7);
        if(taken){dirty=true;return true;}
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat &&
            (event.key.key == SDLK_INSERT || (is_open && event.key.key == SDLK_ESCAPE))) {
            if(BbGameUi::Open())BbGameUi::Close();
            SetOpen(event.key.key == SDLK_INSERT ? !menu_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        return false;
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

bool Visible() {
    return initialized && (menu_open || BbGameUi::Open() || prompt_active || BbSettings::Get().show_fps || BbEngine::PcHud::InWorld());
}

bool MenuOpen() {
    return menu_open || BbGameUi::Open();
}
void RequestOpen(){open_requested=true;}

bool CapturesInput() {
    // The text dialog too: keys typed into it (Backspace is the touchpad) stay out of the game.
    return menu_open || BbGameUi::CapturesInput() || prompt_active;
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    // Present interval for the FPS readout (measured also while nothing is drawn).
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // Keep text, hit areas and HUD geometry at the same uniform scale on 16:10 and ultrawide displays.
    const float scale = std::max(std::min(float(extent.width) / 1920.0f, float(extent.height) / 1080.0f), 0.5f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (menu_open) {
        Menu();
    }
    BbGameUi::Draw(menu_open || prompt_active);
    if (BbSettings::Get().show_fps && !menu_open && !BbGameUi::Open()) {
        FpsCounter();
    }
    if (prompt_active && !menu_open) {
        TextPrompt();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay
