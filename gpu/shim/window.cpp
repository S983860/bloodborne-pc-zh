// bbport: SDL3 window for the Vulkan swapchain (X11, Wayland or Win32).
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "bbport_overlay.h"
#include "bbport_settings.h"
#include "input/input.h"

namespace Frontend {

#ifdef _WIN32
static HICON LoadBloodborneWindowIcon(bool small) {
    wchar_t exe_path[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return nullptr;
    }
    const std::filesystem::path icon_path =
        std::filesystem::path(exe_path).parent_path() / L"bloodborne.ico";
    const int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON);
    const int cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
    return static_cast<HICON>(LoadImageW(nullptr, icon_path.c_str(), IMAGE_ICON, cx, cy,
                                         LR_LOADFROMFILE | LR_DEFAULTCOLOR));
}

static void ApplyBloodborneWindowIcon(void* hwnd_ptr, void*& big_storage, void*& small_storage) {
    const HWND hwnd = static_cast<HWND>(hwnd_ptr);
    if (!hwnd) {
        return;
    }
    HICON big = LoadBloodborneWindowIcon(false);
    HICON small = LoadBloodborneWindowIcon(true);
    if (!big && !small) {
        return;
    }
    if (big) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(big));
        big_storage = big;
    }
    if (small) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(small));
        small_storage = small;
    }
}
#endif

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    // bbport: SDL makes a focused fullscreen window topmost, which put the launcher's trophy
    // notification (a topmost window of its own) under the game: its sound played, nothing showed.
    SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    // The launcher resolves the saved Windows monitor on every launch. Select it before
    // creating the HWND and Vulkan surface, so fullscreen and HDR start on that display.
    if (const char* selection = std::getenv("BB_MONITOR_BOUNDS"); selection && *selection) {
        int x, y, w, h;
        char trailing;
        if (SDL_sscanf(selection, "%d,%d,%d,%d%c", &x, &y, &w, &h, &trailing) == 4 &&
            x >= -100000 && x <= 100000 && y >= -100000 && y <= 100000 &&
            w > 0 && w <= 100000 && h > 0 && h <= 100000) {
            SDL_Point point{x + w / 2, y + h / 2};
            const SDL_DisplayID selected = SDL_GetDisplayForPoint(&point);
            SDL_Rect bounds{};
            if (selected && SDL_GetDisplayBounds(selected, &bounds) &&
                bounds.x == x && bounds.y == y && bounds.w == w && bounds.h == h) {
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(selected));
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(selected));
                const char* label = std::getenv("BB_MONITOR_LABEL");
                SDL_Log("Monitor selection: %s; SDL display %u (%d,%d %dx%d)",
                        label ? label : "selected monitor", static_cast<unsigned>(selected), x, y, w, h);
            } else {
                SDL_Log("Monitor selection: display disconnected; using primary monitor");
            }
        } else {
            SDL_Log("Monitor selection: invalid bounds; using primary monitor");
        }
    }
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    const char* fullscreen = std::getenv("BB_FULLSCREEN");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
                           fullscreen ? fullscreen[0] == '1' : BbSettings::Get().fullscreen.load());
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
#ifdef _WIN32
    if (driver && !std::strcmp(driver, "windows")) {
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        ApplyBloodborneWindowIcon(window_info.render_surface, win_icon_big, win_icon_small);
    } else
#endif
    if (driver && !std::strcmp(driver, "x11")) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        window_info.render_surface = reinterpret_cast<void*>(SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    } else if (driver && !std::strcmp(driver, "wayland")) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
#ifdef _WIN32
    if (win_icon_big) {
        DestroyIcon(static_cast<HICON>(win_icon_big));
    }
    if (win_icon_small) {
        DestroyIcon(static_cast<HICON>(win_icon_small));
    }
#endif
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
    BbOverlay::SetTextPrompt(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        BbInput::Event(event,text_active || BbOverlay::CapturesInput());
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            last_mouse_motion_ms = SDL_GetTicks();
        }
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        // The controller finishes the text dialog too: Cross (South) accepts, Circle (East)
        // cancels. From Supermedo/bloodborne_pc 1d34202.
        if (text_active && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
             event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
            std::scoped_lock lock{text_mutex};
            text_state = event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ? 1 : 2;
            text_active = false;
            SDL_StopTextInput(window);
            UpdateTextTitle();
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            // F11: borderless fullscreen at the desktop size, or back to the window.
            if (event.key.key == SDLK_F11 && !event.key.repeat) {
                SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
            }
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    BbInput::Pump(window,text_active);
    UpdateCursor();
    return is_open;
}

// Issue #3: the OS cursor over the game. bbport: with hide_cursor (Settings, on by default) it
// goes 2 s after the mouse last moved and comes back when it moves - a controller player never
// sees it, a mouse player in the menus has it while using it - and it is always shown while the
// settings menu is open. The mouse camera (relative mode) hides it itself (input/input.cpp).
void WindowSDL::UpdateCursor() {
    if (SDL_GetWindowRelativeMouseMode(window)) {
        cursor_hidden = false; // input.cpp shows it again when the mouse camera ends
        return;
    }
    const bool hide = BbSettings::Get().hide_cursor && !BbOverlay::MenuOpen() &&
                      SDL_GetTicks() - last_mouse_motion_ms > 2000;
    if (hide != cursor_hidden) {
        cursor_hidden = hide;
        hide ? SDL_HideCursor() : SDL_ShowCursor();
    }
}

} // namespace Frontend
