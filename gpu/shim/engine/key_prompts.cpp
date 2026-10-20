// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/key_prompts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "bbport_settings.h"
#include "engine/keycap_art.h"
#include "input/bindings.h"
#include "input/compat.h"
#include "input/input.h"

namespace KeyPrompts {
namespace {

static_assert(kIconBytes == Keycap::kBytes);

// The game's icons, as on the US disc: FNV-1a 64 of each one's 0x400 bytes. The names follow the
// Japanese convention: KG_OK draws Circle and KG_Cancel Cross.
enum Icon : int {
    kCross, kL1, kL2, kL3, kLeftStick, kDpadDown, kDpadLeftRight, kDpadUp, kDpadUpDown, kDpad,
    kCircle, kR1, kR2, kR3, kRightStick, kSquare, kTriangle, kOptions, kTouchLeft, kTouchRight,
    kIconCount,
};
constexpr std::uint64_t kHashes[kIconCount] = {
    0x2310f7c71b939e0bull,  // KG_Cancel
    0xdf0f0104031e5f67ull,  // KG_L1
    0xfadfdba7de6c76b0ull,  // KG_L2
    0xd210779fe3f51673ull,  // KG_L3
    0xcc75c8c3bfbf5b35ull,  // KG_LS
    0xce0de7ced6567e92ull,  // KG_L_D
    0x294394b7ad99f129ull,  // KG_L_LR
    0xaa338448add2c9f0ull,  // KG_L_U
    0xad221c010b1f4101ull,  // KG_L_UD
    0x57d112cbe3da49b1ull,  // KG_L_UDLR
    0xed95e3b0221d51d1ull,  // KG_OK
    0x665da1b194608b8aull,  // KG_R1
    0x73f2b5572f49f886ull,  // KG_R2
    0xe0848ecdcdd5582dull,  // KG_R3
    0x03b41e07b779abf7ull,  // KG_RS
    0x51f55dd737e72648ull,  // KG_R_L
    0xeb8fef4557aea451ull,  // KG_R_U
    0xadd716f21d77cfdfull,  // KG_Start
    0xce3869fe20faa939ull,  // KG_TP_L
    0x9376eea6c514d1fbull,  // KG_TP_R
};

constexpr std::uint32_t kPadCross = 0x4000, kPadCircle = 0x2000;

std::uint64_t Fnv(const void* data, std::size_t size) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        h = (h ^ static_cast<const std::uint8_t*>(data)[i]) * 0x100000001b3ull;
    }
    return h;
}

struct State {
    bool keys = false;          // keycaps shown
    bool menu = false;          // a menu owns the screen (menu_pointer_in_menu)
    std::uint32_t confirm = 0;  // the pad button that confirms there
    bool mouse_camera = false;
    std::uint64_t serial = 0;   // host_bindings_serial
    bool xbox = false;          // the controller icons as an Xbox controller's buttons
    bool operator==(const State&) const = default;
};

// Chosen, or (auto) while the gamepad in use is not a PlayStation one: a controller in DirectInput
// mode (an 8BitDo in D-input) reports as a generic gamepad, not as an Xbox one, and kept the
// PlayStation icons. No gamepad: the game's own.
bool XboxIcons() {
    switch (BbSettings::Get().pad_icons.load(std::memory_order_relaxed)) {
    case BbSettings::PadIconsXbox: return true;
    case BbSettings::PadIconsPlayStation: return false;
    default: {
        const int type = runtime_pad_type();
        return type != SDL_GAMEPAD_TYPE_UNKNOWN && type != SDL_GAMEPAD_TYPE_PS3 &&
               type != SDL_GAMEPAD_TYPE_PS4 && type != SDL_GAMEPAD_TYPE_PS5;
    }
    }
}

State Current() {
    State s;
    const auto& settings = BbSettings::Get();
    const int mode = settings.button_prompts.load(std::memory_order_relaxed);
    s.keys = settings.pc_controls && (mode == BbSettings::PromptsKeyboard ||
                                      (mode == BbSettings::PromptsAuto &&
                                       host_input_device() == InputDevice::KeyboardMouse));
    s.confirm=menu_region_confirm_button();
    if (s.keys) {
        s.menu = menu_pointer_in_menu();
        s.confirm = menu_confirm_button();
        s.mouse_camera = settings.mouse_camera;
        s.serial = host_bindings_serial();
    } else {
        s.xbox = XboxIcons();
    }
    return s;
}

// What a cap says for a key: short, since it is at most five letters on a 32-pixel icon.
std::string KeyLabel(int scancode) {
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
    if (!name || !*name) {
        return "?";
    }
    static constexpr std::pair<std::string_view, const char*> kShort[] = {
        {"Left Shift", "Shift"}, {"Right Shift", "Shift"}, {"Left Ctrl", "Ctrl"}, {"Right Ctrl", "Ctrl"},
        {"Left Alt", "Alt"},     {"Right Alt", "Alt"},     {"Left GUI", "Win"},   {"Right GUI", "Win"},
        {"Return", "Enter"},     {"Keypad Enter", "Enter"}, {"Escape", "Esc"},    {"Backspace", "Bksp"},
        {"CapsLock", "Caps"},    {"Delete", "Del"},        {"Insert", "Ins"},     {"PageUp", "PgUp"},
        {"PageDown", "PgDn"},    {"PrintScreen", "PrtSc"}, {"ScrollLock", "ScrLk"}, {"Numlock", "NumLk"},
        {"`", "~"},  // a lone backtick is a speck
    };
    for (const auto& [from, to] : kShort) {
        if (from == name) {
            return to;
        }
    }
    std::string label = name;
    if (label.rfind("Keypad ", 0) == 0) {
        label = "Num" + label.substr(7);
    }
    return label.size() > 5 ? label.substr(0, 5) : label;
}

// One cap of several.
const char* MouseLabel(int action) {
    static constexpr const char* labels[]={"-","LMB","RMB","MMB","M4","M5","WhUp","WhDn","Wheel"};
    const int mouse=host_binding_mouse(action);
    return labels[mouse>=0 && mouse<=8?mouse:0];
}
std::string ActionLabel(int action) {
    if(const int modifier=host_binding_mouse_modifier(action))return KeyLabel(modifier)+"+"+MouseLabel(action);
    if (const int key = host_binding_key(action)) {
        return KeyLabel(key);
    }
    return MouseLabel(action);
}

// The key bound to an action, else its mouse button; nothing when it has neither (the game's
// own icon stays).
std::optional<Keycap::Spec> ActionSpec(int action) {
    using Keycap::Spec;
    if(const int modifier=host_binding_mouse_modifier(action))
        return Spec{Spec::Keys,{KeyLabel(modifier),MouseLabel(action)}};
    if (const int key = host_binding_key(action)) {
        return Spec{Spec::Key, {KeyLabel(key)}};
    }
    const int m = host_binding_mouse(action);
    if (m == 4 || m == 5 || m == 8) {
        return Spec{Spec::Key, {MouseLabel(action)}};
    }
    if (m >= 1 && m <= 7) {
        return Spec{Spec::Mouse, {}, m};
    }
    return std::nullopt;
}

Keycap::Spec Caps(std::initializer_list<int> actions) {
    Keycap::Spec spec{Keycap::Spec::Keys, {}};
    for (const int action : actions) {
        spec.labels.push_back(ActionLabel(action));
    }
    return spec;
}

// The gamepad button bound to the icon's PS4 button (runtime_pad.c), as an Xbox controller's.
// The d-pad and the sticks keep the game's icons, which fit any controller.
std::optional<Keycap::Spec> PadSpec(int icon) {
    using namespace Keycap;
    static constexpr struct {
        Icon icon;
        std::uint32_t bit;  // scePad's
    } kButtons[] = {
        {kCross, kPadCross}, {kCircle, kPadCircle}, {kSquare, 0x8000}, {kTriangle, 0x1000},
        {kL1, 0x400},        {kR1, 0x800},          {kL2, 0x100},      {kR2, 0x200},
        {kL3, 0x2},          {kR3, 0x4},            {kOptions, 0x8},   {kTouchLeft, 0x100000},
        {kTouchRight, 0x100000},
    };
    const auto it = std::find_if(std::begin(kButtons), std::end(kButtons), [&](const auto& b) { return b.icon == icon; });
    if (it == std::end(kButtons)) {
        return std::nullopt;
    }
    int button;
    switch (runtime_pad_button(it->bit, icon == kTouchRight)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: button = PadA; break;
    case SDL_GAMEPAD_BUTTON_EAST: button = PadB; break;
    case SDL_GAMEPAD_BUTTON_WEST: button = PadX; break;
    case SDL_GAMEPAD_BUTTON_NORTH: button = PadY; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: button = PadLB; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: button = PadRB; break;
    case SDL_GAMEPAD_BUTTON_COUNT: button = PadLT; break;
    case SDL_GAMEPAD_BUTTON_COUNT + 1: button = PadRT; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: button = PadLS; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: button = PadRS; break;
    case SDL_GAMEPAD_BUTTON_BACK: button = PadView; break;
    case SDL_GAMEPAD_BUTTON_START: button = PadMenu; break;
    case SDL_GAMEPAD_BUTTON_MISC1: button = PadShare; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: button = PadUp; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: button = PadDown; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: button = PadLeft; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: button = PadRight; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: button = PadP1; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: button = PadP2; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: button = PadP3; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: button = PadP4; break;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: button = PadTouchpad; break;
    case SDL_GAMEPAD_BUTTON_GUIDE: button = PadGuide; break;
    default: return std::nullopt;  // unbound
    }
    return Spec{Spec::Pad, {}, button};
}

std::optional<Keycap::Spec> IconSpec(int icon, const State& s) {
    using Keycap::Spec;
    if (!s.keys) {
        if(BbSettings::Get().control_mode==1){
            if(icon==kOptions)return Spec{Spec::Pad,{},s.xbox?Keycap::PadView:Keycap::PadTouchpad};
            if(s.confirm==kPadCircle && (icon==kCross||icon==kCircle)){
                icon=icon==kCross?kCircle:kCross;
                if(!s.xbox)return Spec{Spec::Pad,{},icon==kCross?Keycap::PadPSCross:Keycap::PadPSCircle};
            }
        }
        return s.xbox ? PadSpec(icon) : std::nullopt;
    }
    // In a menu the face buttons that confirm and go back are Menu Confirm's and Menu Back's
    // keys, and the d-pad is the arrow keys (input.cpp).
    if (s.menu) {
        switch (icon) {
        case kCross: return ActionSpec(s.confirm == kPadCross ? kBindConfirm : kBindBack);
        case kCircle: return ActionSpec(s.confirm == kPadCircle ? kBindConfirm : kBindBack);
        case kDpadUp: return Spec{Spec::Key, {"Up"}};
        case kDpadDown: return Spec{Spec::Key, {"Down"}};
        case kDpadLeftRight: return Spec{Spec::Keys, {"Left", "Right"}};
        case kDpadUpDown: return Spec{Spec::Keys, {"Up", "Down"}};
        case kDpad: return Spec{Spec::Keys, {"Up", "Left", "Down", "Right"}};
        default: break;
        }
    }
    switch (icon) {
    case kCross: return ActionSpec(s.confirm == kPadCross ? kBindInteract : kBindRoll);
    case kCircle: return ActionSpec(s.confirm == kPadCircle ? kBindInteract : kBindRoll);
    case kTriangle: return ActionSpec(kBindBloodVial);
    case kSquare: return ActionSpec(kBindUseItem);
    case kR1: return ActionSpec(kBindAttack);
    case kR2: return ActionSpec(kBindStrong);
    case kL1: return ActionSpec(kBindTransform);
    case kL2: return ActionSpec(kBindFirearm);
    case kR3: return ActionSpec(kBindLockOn);
    case kL3: return ActionSpec(kBindL3);
    case kOptions: return ActionSpec(kBindMenu);
    case kTouchLeft: return ActionSpec(kBindGestures);
    case kTouchRight: return ActionSpec(kBindEffects);
    case kDpadUp: return ActionSpec(kBindDpadUp);
    case kDpadDown: return ActionSpec(kBindSwitchItem);
    case kDpadLeftRight: return Caps({kBindSwitchLeft, kBindSwitchRight});
    case kDpadUpDown: return Caps({kBindDpadUp, kBindSwitchItem});
    case kDpad: return Caps({kBindDpadUp, kBindSwitchLeft, kBindSwitchItem, kBindSwitchRight});
    case kLeftStick: return Caps({kBindMoveF, kBindMoveL, kBindMoveB, kBindMoveR});
    case kRightStick:
        if (s.mouse_camera) {
            return Spec{Spec::Mouse, {}, 0};
        }
        return Caps({kBindLookU, kBindLookL, kBindLookD, kBindLookR});
    default: return std::nullopt;
    }
}

std::mutex g_mutex;
State g_state;
bool g_have_state = false;
std::atomic<bool> g_logged{false};

// With g_mutex held.
void SetState(const State& now) {
    static int shown = -1;  // the controller icons last logged: 0 the game's, 1 Xbox
    if (!now.keys && shown != int(now.xbox)) {
        shown = int(now.xbox);
        host_log("button prompts: %s controller icons", now.xbox ? "Xbox" : "the game's (PlayStation)");
    }
    g_state = now;
    g_have_state = true;
}
// Drawn keycaps by what they are for and Spec::Id(), kept for the session (a few dozen at most).
// Never destroyed, so no late upload outlives it.
auto& g_drawn = *new std::unordered_map<std::string, std::vector<std::uint8_t>>;

// The HUD sheet (MENU_Common_00091, 1024x512 BC7, one mip, PS4 thin tiled): its Circle, Cross
// and L3, 26 pixels each with transparent blocks between them. Each is redrawn over its own
// 7x8 blocks; the sheet is recognised by those blocks alone (FNV-1a 64 of them in this order),
// so a mod's changes elsewhere on it stay.
constexpr int kHud = kIconCount;
constexpr std::uint64_t kHudHash = 0x08d886c6323fa447ull;
constexpr std::uint32_t kHudWidth = 1024, kHudHeight = 512;
constexpr int kHudBlockY = 30, kHudBlocksX = 7, kHudBlocksY = 8;  // y 120..151
constexpr struct {
    Icon icon;
    int block_x;  // the first block column
    int left;     // the button's left edge in it
} kHudButtons[] = {{kCircle, 165, 1}, {kCross, 173, 2}, {kL3, 181, 2}};
constexpr int kHudTop = 3;  // the buttons' top edge in the first block row

// Where block (bx, by) of the sheet is: tiles of 8x8 blocks, a row of them after another, each
// in Morton order (x in the even bits).
std::size_t HudBlock(int bx, int by) {
    const int tile = (by >> 3) * int(kHudWidth / 32) + (bx >> 3);
    int morton = 0;
    for (int k = 0; k < 3; ++k) {
        morton |= ((bx >> k) & 1) << (2 * k) | ((by >> k) & 1) << (2 * k + 1);
    }
    return std::size_t(tile * 64 + morton) * 16;
}

std::uint64_t HudHash(const std::uint8_t* data) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto& button : kHudButtons) {
        for (int by = kHudBlockY; by < kHudBlockY + kHudBlocksY; ++by) {
            for (int bx = button.block_x; bx < button.block_x + kHudBlocksX; ++bx) {
                const std::uint8_t* b = data + HudBlock(bx, by);
                for (int i = 0; i < 16; ++i) {
                    h = (h ^ b[i]) * 0x100000001b3ull;
                }
            }
        }
    }
    return h;
}

// With g_mutex held. A KG_* icon (kIconBytes, tiled) or a HUD button's blocks (row by row).
const std::vector<std::uint8_t>& Drawn(const Keycap::Spec& spec, int hud_left) {
    const std::string id = (hud_left < 0 ? std::string("kg|") : "hud" + std::to_string(hud_left) + '|') + spec.Id();
    auto it = g_drawn.find(id);
    if (it != g_drawn.end()) {
        return it->second;
    }
    std::vector<std::uint8_t> out;
    if (hud_left < 0) {
        std::uint8_t rgba[Keycap::kSize * Keycap::kSize * 4];
        Keycap::Draw(spec, rgba);
        out.resize(kIconBytes);
        Keycap::Encode(rgba, out.data());
    } else {
        constexpr int size = 26, w = kHudBlocksX * 4, h = kHudBlocksY * 4;
        std::uint8_t cap[size * size * 4];
        Keycap::Draw(spec, cap, size);
        std::vector<std::uint8_t> region(std::size_t(w) * h * 4, 0);
        for (int y = 0; y < size; ++y) {
            std::memcpy(&region[(std::size_t(y + kHudTop) * w + hud_left) * 4], &cap[y * size * 4], size * 4);
        }
        out.resize(std::size_t(kHudBlocksX) * kHudBlocksY * 16);
        Keycap::EncodeBlocks(region.data(), w, h, out.data());
    }
    return g_drawn.emplace(id, std::move(out)).first->second;
}

void LogFound() {
    if (!g_logged.exchange(true)) {
        const auto& settings = BbSettings::Get();
        host_log("button prompts: the game's button icons found (button_prompts = %s, pad_icons = %s)",
                 BbSettings::ButtonPromptsNames[std::clamp(settings.button_prompts.load(), 0, BbSettings::PromptsCount - 1)],
                 BbSettings::PadIconsNames[std::clamp(settings.pad_icons.load(), 0, BbSettings::PadIconsCount - 1)]);
    }
}

} // namespace

int Identify(const void* data, std::size_t size, std::uint32_t width, std::uint32_t height) {
    if (!data) {
        return -1;
    }
    if (size == kIconBytes && width == 32 && height == 32) {
        const std::uint64_t hash = Fnv(data, size);
        for (int icon = 0; icon < kIconCount; ++icon) {
            if (kHashes[icon] == hash) {
                LogFound();
                return icon;
            }
        }
    } else if (size == kHudBytes && width == kHudWidth && height == kHudHeight &&
               HudHash(static_cast<const std::uint8_t*>(data)) == kHudHash) {
        LogFound();
        return kHud;
    }
    return -1;
}

bool Draw(int texture, std::uint8_t* data, std::size_t size) {
    if (texture < 0 || texture > kHud || size != (texture == kHud ? kHudBytes : kIconBytes)) {
        return false;
    }
    std::scoped_lock lock{g_mutex};
    if (!g_have_state) {
        SetState(Current());
    }
    if (!g_state.keys && !g_state.xbox) {
        return false;
    }
    if (texture != kHud) {
        const auto spec = IconSpec(texture, g_state);
        if (!spec) {
            return false;
        }
        std::memcpy(data, Drawn(*spec, -1).data(), kIconBytes);
        return true;
    }
    bool drawn = false;
    for (const auto& button : kHudButtons) {
        const auto spec = IconSpec(button.icon, g_state);
        if (!spec) {
            continue;
        }
        const std::uint8_t* blocks = Drawn(*spec, button.left).data();
        for (int by = 0; by < kHudBlocksY; ++by) {
            for (int bx = 0; bx < kHudBlocksX; ++bx) {
                std::memcpy(data + HudBlock(button.block_x + bx, kHudBlockY + by), blocks, 16);
                blocks += 16;
            }
        }
        drawn = true;
    }
    return drawn;
}

bool Changed() {
    const State now = Current();
    std::scoped_lock lock{g_mutex};
    if (!g_have_state) {
        SetState(now);
        return false;
    }
    if (now == g_state) {
        return false;
    }
    SetState(now);
    return true;
}

} // namespace KeyPrompts
