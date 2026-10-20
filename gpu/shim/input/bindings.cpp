// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from bbhost db5457cbd447f4ef129b58df0a78e0a8819c7033.
#define BBHOST_HAVE_SDL3 1
#include "compat.h"
#include "bindings.h"
#include "pc_chinese.h"


#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#if defined(BBHOST_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace {

// Bloodborne's pad, for the record, since every row below is one of these:
// R1 attacks, R2 is the strong attack, L1 transforms the weapon, L2 is the
// firearm (and the parry), Circle dodges and sprints, Triangle drinks a Blood
// Vial ("KG_R_U: Restore HP (Uses Blood Vial)" in the game's own help text),
// Square uses the quick item ("KG_R_L: Use item"), Cross interacts, the d-pad
// switches weapons and items, R3 locks on, the touchpad's two sides are
// Gestures and Personal Effects. That is the US disc's layout; the Japanese
// region swaps Circle and Cross, in play and in the menus alike, and the
// region comes from param.sfo (hle/system.cpp).
constexpr std::uint32_t kL3 = 0x2, kR3 = 0x4, kOptions = 0x8, kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80,
                        kL2 = 0x100, kR2 = 0x200, kL1 = 0x400, kR1 = 0x800, kTriangle = 0x1000, kCircle = 0x2000,
                        kCross = 0x4000, kSquare = 0x8000, kTouchPad = 0x100000;

// The defaults are DS3's shape - WASD, Space to roll, E to interact, R for the
// healing item, F for the quick item, Q to lock on, Shift as the strong
// modifier, the mouse's buttons for the hands - put on Bloodborne's buttons.
const BindingInfo kInfo[kBindCount] = {
    {"move_forward", "向前移动", "按W向前移动。", 0, "W", 0},
    {"move_back", "向后移动", "按S向后移动。", 0, "S", 0},
    {"move_left", "向左移动", "按A向左移动。", 0, "A", 0},
    {"move_right", "向右移动", "按D向右移动。", 0, "D", 0},
    // Held with the movement keys, as Dark Souls III's Left Alt: a walk.
    {"walk", "步行", "按住左Alt并使用移动键步行。", 0, "Left Alt", 0},
    {"roll", "闪避", "按空格闪避。", kCircle, "Space", 0},
    {"lock_on", "锁定／取消锁定", "按下鼠标滚轮（中键）或Q锁定前方目标；再按取消，无目标时重置镜头。转动滚轮是切换道具。以右侧绑定为准。", kR3, "Q", 3},

    {"attack", "普通攻击", "鼠标左键攻击。", kR1, nullptr, 1},
    {"strong_attack", "重击", "按住左Shift并点击鼠标左键重击，按住左键可蓄力。", kR2, "Left Shift+Mouse1", 0},
    {"sprint", "奔跑", "按住CapsLock并使用WASD奔跑。", kCircle, "CapsLock", 0},
    {"transform", "武器变形", "切换武器形态。武器占用双手时，变回单手后可再次使用左手枪械。以右侧绑定为准。", kL1, "C", 5},
    {"firearm", "射击／盾牌格挡", "单击使用已装备枪械（需弹药），在敌人攻击时命中可枪反；持盾时按住格挡。占用双手时可能变为特殊攻击，需变回单手再用左手枪械。以右侧绑定为准。", kL2, "X", 2},
    {"blood_vial", "使用采血瓶", "使用采血瓶恢复生命。以右侧当前绑定为准。", kTriangle, "V", 0},
    {"use_item", "使用快捷物品", "使用当前选中的快捷物品。以右侧当前绑定为准。", kSquare, "R", 0},

    {"interact", "交互", "按E交谈、调查、拾取或开门；菜单用Enter确认。", kCross, "E", 0},
    {"switch_item", "切换快捷道具", "循环选择已装备的快捷道具；由使用快捷物品键使用。", kDown, "Down", 8},
    {"switch_right", "切换右手武器", "在已装备的右手武器栏位之间切换。", kRight, "1", 0},
    {"switch_left", "切换左手武器", "在已装备的左手武器栏位之间切换。", kLeft, "2", 0},
    {"dpad_up", "制造血子弹", "按方向上键执行游戏原有的血子弹操作。", kUp, "Up", 0},
    // The touchpad is a button with a side: the game's key guide reads
    // "<KG_TP_L> : Gestures" and "<KG_TP_R> : Personal Effects". A press alone
    // cannot say which, so these place a finger as well (apply below).
    {"gestures", "动作", "按G打开动作菜单。", kTouchPad, "G", 0},
    {"personal_effects", "随身物品", "按T打开随身物品。", kTouchPad, "T", 0},

    {"look_up", "向上看", "鼠标移动或I键调整视角。", 0, "I", 0},
    {"look_down", "向下看", "鼠标移动或K键调整视角。", 0, "K", 0},
    {"look_left", "向左看", "鼠标移动或J键调整视角。", 0, "J", 0},
    {"look_right", "向右看", "鼠标移动或L键调整视角。", 0, "L", 0},
    {"menu", "装备栏", "按Tab打开装备栏；游戏中按Esc打开继续、存档与返回主界面菜单。", kOptions, "Tab", 0},
    // Menus need an OK and a Back that are not the gameplay keys sharing their
    // pad bits: nobody reaches for Space to accept or E to back out.
    // Their buttons are the region's (engine/menu_pointer.h), set in apply.
    {"confirm", "菜单确认", "按Enter确认菜单选项。", 0, "Return", 0},
    {"back", "菜单返回", "按Esc、Backspace或鼠标右键返回。", 0, "Backspace", 0},

    {"l3", "冲刺跳跃", "奔跑时按F跳跃。", kL3, "F", 0},
    // The developers' debug menu: not a pad press at all. The patch opens it
    // on the touchpad's left side, which is also Gestures; this key toggles
    // the menu itself (engine/debug_menu.h), when the Debug Menu plugin is
    // on. The Key Bindings screen lists it only then.
    {"minimap", "小地图开关", "开关右上角小地图，显示自身、道具与附近角色。", 0, "M", 0},
    {"melee_guard", "锯齿砍刀格挡", "按住格挡；正面减伤并消耗精力，耗尽会破防。手柄默认按住L1／LB，按左摇杆变形；控制页可选原L1＋L2组合。", kL3 | kL2, "Left Ctrl", 0},
    {"debug_menu", "调试菜单", "仅在启用调试功能时，按反引号键开关调试菜单。", 0, "`", 0},
};

// Earlier names a bbhost.toml may still use.
const struct {
    const char* old_name;
    int action;
} kAliases[] = {
    {"dpad_down", kBindSwitchItem}, {"dpad_left", kBindSwitchLeft}, {"dpad_right", kBindSwitchRight},
};

std::atomic<int> g_key[kBindCount];
std::atomic<int> g_mouse[kBindCount], g_mouse_modifier[kBindCount];

std::atomic<int> g_capture{-1};
std::atomic<int> g_capture_modifier{0};
// The Debug Menu key: held per device (keyboard, mouse), since each device's
// actions are read on their own, and a press queued for the pad read.
std::atomic<bool> g_debug_held[2], g_debug_toggle{false};

void note_debug_key(int device, bool down) {
    if (!down) {
        g_debug_held[device].store(false, std::memory_order_relaxed);
    } else if (!g_debug_held[device].exchange(true) && debug_menu_active()) {
        g_debug_toggle.store(true);
    }
}
std::atomic<int> g_capture_done{-1};
std::atomic<bool> g_hold{false};  // after a capture: blocked until nothing is held
std::atomic<std::int64_t> g_hold_ms{0};  // when that hold began
std::atomic<std::int64_t> g_capture_ms{0};  // when the capture began

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int scancode_from_name(const std::string& name) {
#if defined(BBHOST_HAVE_SDL3)
    return static_cast<int>(SDL_GetScancodeFromName(name.c_str()));
#else
    (void)name;
    return 0;
#endif
}

const char* scancode_name(int sc) {
#if defined(BBHOST_HAVE_SDL3)
    const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(sc));
    return n && *n ? n : nullptr;
#else
    (void)sc;
    return nullptr;
#endif
}

// Mouse 1..5 are the buttons; 6 and 7 the wheel turned up and down, which the
// pad read makes a short press of each notch (hle/system.cpp).
constexpr int kMouseInputs = 8; // 8 is either direction of the wheel.
const char* const kMouseNames[kMouseInputs + 1] = {nullptr,  "鼠标左键", "鼠标右键", "鼠标中键",
                                                   "鼠标侧键4", "鼠标侧键5", "滚轮向上", "滚轮向下", "滚轮上下"};
// The same where the name has to fit in a sentence.
const char* const kMouseShort[kMouseInputs + 1] = {nullptr, "鼠标左键", "鼠标右键", "鼠标中键", "鼠标侧键4", "鼠标侧键5", "滚轮向上", "滚轮向下", "滚轮"};
// Their names in [keys]: Mouse1..Mouse5, WheelUp, WheelDown.
std::string mouse_token(int m) { return m == 6 ? "WheelUp" : m == 7 ? "WheelDown" : m == 8 ? "WheelBoth" : "Mouse" + std::to_string(m); }

std::string trim(std::string v) {
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '"')) v.pop_back();
    std::size_t i = 0;
    while (i < v.size() && (v[i] == ' ' || v[i] == '\t' || v[i] == '"')) ++i;
    return v.substr(i);
}

int action_named(const char* name) {
    for (int a = 0; a < kBindCount; ++a) {
        if (std::strcmp(name, kInfo[a].key) == 0) return a;
    }
    for (const auto& al : kAliases) {
        if (std::strcmp(name, al.old_name) == 0) return al.action;
    }
    return -1;
}

// Bumped by every change, for consumers that cache what they built from a
// binding (host_bindings_serial).
std::atomic<std::uint64_t> g_serial{1};

// "E", "Mouse2", "X, Mouse2", "none": the whole binding.
void set_from_text(int a, const std::string& text, const char* where) {
    int key = 0, mouse = 0, modifier = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find(',', pos);
        if (end == std::string::npos) end = text.size();
        const std::string tok = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (tok.empty() || tok == "none") continue;
        // A plus means a chord, unlike comma-separated alternative bindings.
        // Keep Mouse1 available to normal Attack when Shift+Mouse1 is Heavy.
        const auto plus=tok.find('+');
        if(plus!=std::string::npos) {
            const auto mod=trim(tok.substr(0,plus)), button=trim(tok.substr(plus+1));
            const int sc=scancode_from_name(mod);
            if(sc && button.size()==6 && button.rfind("Mouse",0)==0 && button[5]>='1' && button[5]<='5') {
                modifier=sc;mouse=button[5]-'0';continue;
            }
            host_log("keys: invalid mouse chord for %s: %s",kInfo[a].key,tok.c_str());
            continue;
        }
        if (tok.size() == 6 && tok.rfind("Mouse", 0) == 0 && tok[5] >= '1' && tok[5] <= '5') {
            mouse = tok[5] - '0';modifier=0;
            continue;
        }
        if (tok == "WheelUp" || tok == "WheelDown" || tok == "WheelBoth") {
            mouse = tok == "WheelUp" ? 6 : tok == "WheelDown" ? 7 : 8;modifier=0;
            continue;
        }
        const int sc = scancode_from_name(tok);
        if (!sc) {
            host_log("keys: %s: %s = \"%s\" - \"%s\" is not an SDL key name", where, kInfo[a].key, text.c_str(),
                     tok.c_str());
            continue;
        }
        key = sc;
    }
    g_key[a].store(key, std::memory_order_relaxed);
    g_mouse[a].store(mouse, std::memory_order_relaxed);
    g_mouse_modifier[a].store(modifier, std::memory_order_relaxed);
    g_serial.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

std::uint64_t host_bindings_serial() { return g_serial.load(std::memory_order_relaxed); }

const BindingInfo& host_binding_info(int action) { return kInfo[action < 0 || action >= kBindCount ? 0 : action]; }

void host_bindings_load_defaults() {
    for (int a = 0; a < kBindCount; ++a) {
        std::string text=kInfo[a].def_key?kInfo[a].def_key:"";
        if(kInfo[a].def_mouse){if(!text.empty())text+=", ";text+=mouse_token(kInfo[a].def_mouse);}
        set_from_text(a,text,"defaults");
    }
    g_serial.fetch_add(1,std::memory_order_relaxed);
}

bool host_bindings_parse(const char* name, const char* value) {
    const int a = action_named(name);
    if (a < 0) return false;
    set_from_text(a, value, "options");
    return true;
}

void host_bindings_save(std::FILE* f) {

    for (int a = 0; a < kBindCount; ++a) {
        const auto v=host_binding_ini_value(a);
        std::fprintf(f, "bind.%s=%s\n", kInfo[a].key, v.c_str());
    }
}

std::string host_binding_ini_value(int a) {
    const char* key=scancode_name(g_key[a].load());
    std::string value=key?key:"";
    const int m=g_mouse[a].load();
    if(m) {
        if(!value.empty())value+=", ";
        if(const char* modifier=scancode_name(g_mouse_modifier[a].load())){value+=modifier;value+='+';}
        value+=mouse_token(m);
    }
    return value.empty()?"none":value;
}
int host_binding_key(int action) { return action >= 0 && action < kBindCount ? g_key[action].load() : 0; }
int host_binding_mouse(int action) { return action >= 0 && action < kBindCount ? g_mouse[action].load() : 0; }
int host_binding_mouse_modifier(int action) {return action>=0 && action<kBindCount?g_mouse_modifier[action].load():0;}

void host_binding_describe(int action, char* out, std::size_t n) {
    if (!n) return;
    const char* k = scancode_name(host_binding_key(action));
    k=k?PcChineseText(k):nullptr;
    // A lone ` is a speck in the menu's font.
    if (k && std::strcmp(k, "`") == 0) k = "`（反引号）";
    const int m = host_binding_mouse(action);
    const char* ms = m >= 1 && m <= kMouseInputs ? kMouseNames[m] : nullptr;
    std::string chord;
    if(ms)if(const char* mod=scancode_name(host_binding_mouse_modifier(action))){
        chord=PcChineseText(mod);chord+="＋";chord+=ms;ms=chord.c_str();
    }
    if (k && ms) {
        std::snprintf(out, n, "%s  /  %s", k, ms);
    } else {
        std::snprintf(out, n, "%s", k ? k : ms ? ms : "未绑定");
    }
}

void host_binding_prompt(int action, char* out, std::size_t n) {
    if (!n) return;
    out[0] = '\0';
    const char* k = scancode_name(host_binding_key(action));
    const int m = host_binding_mouse(action);
    if(const char* mod=scancode_name(host_binding_mouse_modifier(action))) {
        if(m>=1 && m<=kMouseInputs){std::snprintf(out,n,"%s＋%s",PcChineseText(mod),kMouseShort[m]);return;}
    }
    // The key first: a player who bound both reads the key in a sentence more
    // easily than "鼠标左键", and the prompt has room for one.
    const char* what = k ? k : m >= 1 && m <= kMouseInputs ? kMouseShort[m] : nullptr;
    if (what) std::snprintf(out, n, "%s", what);
}

namespace {

// Two opposite directions held at once cancel, as Dark Souls III's keyboard
// does (its digital-to-analog converter sums each axis's keys). Pressing A
// while sprinting on W+D so runs straight on until D is let go - two turns of
// 45 degrees. Letting the newer key win instead (#51's first cut) turned the
// stick 90 degrees in one frame, past the character script's 90-degree
// sprint-turn threshold, and the character spun as if told to turn round.
constexpr int kOpposites[4][2] = {
    {kBindMoveF, kBindMoveB}, {kBindMoveL, kBindMoveR}, {kBindLookU, kBindLookD}, {kBindLookL, kBindLookR}};

// Walk held on a mouse button, for the keyboard's movement keys (the mouse is
// read on the pad read, the keyboard on the window's update).
std::atomic<bool> g_walk_on_mouse{false};

// How far the movement keys push the stick while Walk is held, of a full push.
// Measured in the Hunter's Dream: under 35% the character stands, from 40% to
// 80% it walks (1.65 m/s), at a full push it runs (3.7 m/s) - so the middle of
// the walk, with room either side.
constexpr float kWalkPush = 0.6f;

}  // namespace

void host_bindings_keys_held(const bool* keys, int nkeys, bool held[kBindCount]) {
    for (int a = 0; a < kBindCount; ++a) {
        const int sc = g_key[a].load(std::memory_order_relaxed);
        held[a] = sc > 0 && sc < nkeys && keys[sc];
    }
    for (const auto& pair : kOpposites) {
        if (held[pair[0]] && held[pair[1]]) held[pair[0]] = held[pair[1]] = false;
    }
    held[kBindWalk] = held[kBindWalk] || g_walk_on_mouse.load(std::memory_order_relaxed);
    note_debug_key(0, held[kBindDebugMenu]);
}

void host_bindings_mouse_held(std::uint32_t buttons, bool held[kBindCount],const bool* keys,int nkeys) {
    for (int a = 0; a < kBindCount; ++a) {
        const int m = g_mouse[a].load(std::memory_order_relaxed);
        const int physical=m==2?3:m==3?2:m;
        const int modifier=g_mouse_modifier[a].load(std::memory_order_relaxed);
        const uint32_t mask=m==8?(32u|64u):m>=1 && m<=7?1u<<(physical-1):0;
        held[a] = (buttons & mask) &&
                  (!modifier || (keys && modifier<nkeys && keys[modifier]));
    }
    g_walk_on_mouse.store(held[kBindWalk], std::memory_order_relaxed);
    note_debug_key(1, held[kBindDebugMenu]);
}

void host_bindings_apply(const bool held[kBindCount], bool strong, PadState& p) {
    bool any = false;
    for (int a = 0; a < kBindCount; ++a) {
        // The Debug Menu key sets no pad bit: its press was queued when it was
        // read, and the pad read toggles the menu on the game's thread
        // (host_bind_take_debug_toggle).
        if (!held[a] || a == kBindDebugMenu) continue;
        any = true;
        std::uint32_t bit = kInfo[a].button;
        // Asian regions swap both gameplay face buttons as well as menu OK.
        // Keep actions and displayed keycaps consistent with the same region.
        if (a == kBindInteract) bit = menu_region_confirm_button();
        if (a == kBindRoll || a == kBindStrongMod)
            bit = menu_region_confirm_button() == kCircle ? kCross : kCircle;
        // Shift is the strong-attack modifier, as in DS3: one key rather than
        // a second binding for every attack.
        if (a == kBindAttack && strong) bit = kR2;
        if (a == kBindConfirm) bit = menu_confirm_button();
        if (a == kBindBack) bit = menu_back_button();
        p.buttons |= bit;
        if (bit == kR2) p.r2 = 255;  // the triggers are analogue as well as buttons
        if (bit & kL2) p.l2 = 255;
        switch (a) {
            case kBindLookU: p.ry = kStickLow; break;
            case kBindLookD: p.ry = kStickHigh; break;
            case kBindLookL: p.rx = kStickLow; break;
            case kBindLookR: p.rx = kStickHigh; break;
            case kBindGestures:
            case kBindEffects:
                // One quarter in from the edge, half way down, and a stable id
                // for as long as it is held: the game tracks fingers by id, so a
                // new one every frame would read as tapping.
                if (p.touch_count < 2) {
                    PadState::Touch& t = p.touch[p.touch_count++];
                    t.x = static_cast<std::uint16_t>(a == kBindEffects ? kPadTouchW * 3 / 4 : kPadTouchW / 4);
                    t.y = static_cast<std::uint16_t>(kPadTouchH / 2);
                    t.id = 200;
                    t.down = true;
                }
                break;
            default: break;
        }
    }
    // The movement keys: a full push each way, a diagonal the square's corner
    // (as Dark Souls III's keys give) - or, with Walk held, a gentle push the
    // game reads as a walk, a diagonal's two axes shortened so that its reach,
    // and so the walk, is the same. Opposite keys cancel (keys_held above).
    const int x = (held[kBindMoveR] ? 1 : 0) - (held[kBindMoveL] ? 1 : 0);
    const int y = (held[kBindMoveB] ? 1 : 0) - (held[kBindMoveF] ? 1 : 0);
    if (x != 0 || y != 0) {
        int reach = kStickHigh - kStickRest;
        if (held[kBindWalk]) {
            const float r = static_cast<float>(reach) * kWalkPush / (x != 0 && y != 0 ? std::sqrt(2.0f) : 1.0f);
            reach = static_cast<int>(std::lround(r));
        }
        if (x != 0) p.lx = static_cast<std::uint8_t>(kStickRest + x * reach);
        if (y != 0) p.ly = static_cast<std::uint8_t>(kStickRest + y * reach);
    }
    if (any) p.connected = true;
}

void host_bind_capture_begin(int action) {
    if (action < 0 || action >= kBindCount) return;
    g_capture_modifier=0;
    g_capture_ms.store(now_ms(), std::memory_order_relaxed);
    g_capture.store(action, std::memory_order_relaxed);
    host_log("keys: rebinding %s - press a key or a mouse button, or turn the wheel (Escape cancels, Delete unbinds)",
             kInfo[action].key);
}

int host_bind_capturing() { return g_capture.load(std::memory_order_relaxed); }

namespace {
void finish(int a, const char* what) {
    g_serial.fetch_add(1, std::memory_order_relaxed);
    g_capture.store(-1, std::memory_order_relaxed);
    g_capture_modifier=0;
    g_hold_ms.store(now_ms(), std::memory_order_relaxed);
    g_hold.store(true, std::memory_order_relaxed);
    g_capture_done.store(a, std::memory_order_relaxed);
    char d[96];
    host_binding_describe(a, d, sizeof(d));
    host_log("keys: %s %s, now %s", kInfo[a].key, what, d);
}
}  // namespace

bool host_bind_capture_key(int scancode) {
    const int a = g_capture.load(std::memory_order_relaxed);
    if (a < 0) return false;
#if defined(BBHOST_HAVE_SDL3)
    // System focus shortcuts must not silently become a gameplay binding.
    if(scancode==SDL_SCANCODE_LGUI || scancode==SDL_SCANCODE_RGUI)return true;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        finish(a, "unchanged");
        return true;
    }
    if (scancode == SDL_SCANCODE_DELETE) {
        g_key[a].store(0, std::memory_order_relaxed);
        g_mouse[a].store(0, std::memory_order_relaxed);
        g_mouse_modifier[a].store(0,std::memory_order_relaxed);
        finish(a, "unbound");
        return true;
    }
#endif
    if(scancode==SDL_SCANCODE_LSHIFT || scancode==SDL_SCANCODE_RSHIFT ||
       scancode==SDL_SCANCODE_LCTRL || scancode==SDL_SCANCODE_RCTRL ||
       scancode==SDL_SCANCODE_LALT || scancode==SDL_SCANCODE_RALT) {
        g_capture_modifier=scancode;return true;
    }
    // One key, one action: whatever had it gives it up, which is what a
    // player rebinding a key means.
    for (int b = 0; b < kBindCount; ++b) {
        if (b != a && g_key[b].load(std::memory_order_relaxed) == scancode) {
            g_key[b].store(0, std::memory_order_relaxed);
            host_log("keys: %s gives up that key", kInfo[b].key);
        }
    }
    // Rebinding a chord to a single key replaces that chord completely.
    if(g_mouse_modifier[a].exchange(0))g_mouse[a].store(0,std::memory_order_relaxed);
    g_key[a].store(scancode, std::memory_order_relaxed);
    finish(a, "rebound");
    return true;
}

bool host_bind_capture_mouse(int button) {
    const int a = g_capture.load(std::memory_order_relaxed);
    if (a < 0 || button < 1 || button > kMouseInputs) return false;
    // A click that starts a capture is often a double click, and its second
    // press is not a choice: it bound the left button to whatever row was
    // clicked, taking it from Attack. For the first 300 ms a button press is
    // swallowed; a deliberate one comes after the row reads "Press a key".
    if (now_ms() - g_capture_ms.load(std::memory_order_relaxed) < 300) {
        host_log("keys: a mouse press %d ms into the capture taken as part of the click that started it",
                 static_cast<int>(now_ms() - g_capture_ms.load(std::memory_order_relaxed)));
        return true;
    }
    const int modifier=g_capture_modifier.load();
    for (int b = 0; b < kBindCount; ++b) {
        const int previous=g_mouse[b].load(std::memory_order_relaxed);
        const bool overlap=previous==button || (previous==8 && button>=6) || (button==8 && previous>=6);
        if (b != a && g_mouse_modifier[b].load()==modifier && overlap) {
            g_mouse[b].store(0, std::memory_order_relaxed);
            host_log("keys: %s gives up that button", kInfo[b].key);
        }
    }
    g_mouse[a].store(button, std::memory_order_relaxed);
    g_mouse_modifier[a].store(modifier,std::memory_order_relaxed);
    finish(a, "rebound");
    return true;
}

bool host_bind_capture_key_up(int scancode) {
    const int action=g_capture.load();
    if(action<0 || g_capture_modifier.load()!=scancode)return false;
    for(int other=0;other<kBindCount;++other)
        if(other!=action && g_key[other].load()==scancode)g_key[other]=0;
    g_capture_modifier=0;g_mouse_modifier[action]=0;g_mouse[action]=0;g_key[action]=scancode;
    finish(action,"rebound");return true;
}

int host_bind_capture_take_done() { return g_capture_done.exchange(-1); }

bool host_bind_take_debug_toggle() { return g_debug_toggle.exchange(false); }

bool host_bind_capture_blocking() {
    return g_capture.load(std::memory_order_relaxed) >= 0 || g_hold.load(std::memory_order_relaxed);
}

namespace {
std::atomic<std::int64_t> g_clear_ms{0};
}  // namespace

void host_bind_note_clear_key() { g_clear_ms.store(now_ms(), std::memory_order_relaxed); }

bool host_bind_take_clear_key() {
    const std::int64_t at = g_clear_ms.exchange(0);
    return at && now_ms() - at < 250;
}

void host_binding_clear(int action) {
    if (action < 0 || action >= kBindCount) return;
    g_key[action].store(0, std::memory_order_relaxed);
    g_mouse[action].store(0, std::memory_order_relaxed);
    g_mouse_modifier[action].store(0,std::memory_order_relaxed);
    g_serial.fetch_add(1, std::memory_order_relaxed);
    host_log("keys: %s cleared", kInfo[action].key);
}

void host_bind_capture_note_held(bool any_down) {
    // Released, or a second and a half gone: a key the window system reports
    // held with no end must not keep the pad away from the game.
    if (g_hold.load(std::memory_order_relaxed) &&
        (!any_down || now_ms() - g_hold_ms.load(std::memory_order_relaxed) > 1500)) {
        g_hold.store(false, std::memory_order_relaxed);
    }
}
