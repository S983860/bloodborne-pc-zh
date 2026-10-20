// SPDX-License-Identifier: GPL-3.0-or-later
// SDL/Pad bridge for bbhost's native engine mouse camera and menu hit testing.
#include "input.h"
#include "compat.h"
#include "bindings.h"
#include "ini_bindings.h"
#include "mouse_camera_step.h"
#include "pc_action_filter.h"
#include "pc_short_press.h"
#include "controller_guard.h"
#include "../bbport_settings.h"
#include "../bbport_overlay.h"
#include "../engine/option_menu.h"
#include "../engine/pc_guard.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <mutex>

namespace {
std::atomic<bool> active{false},relative{false},wanted_relative{false},text_open{false},focused{true};
std::atomic<InputDevice> device{InputDevice::KeyboardMouse};
std::mutex state_lock;
PadState snapshot;
float mouse_x=0,mouse_y=0;
int window_w=1920,window_h=1080;
uint32_t mouse_buttons=0,mouse_edges=0;
std::array<bool, SDL_SCANCODE_COUNT> key_edges{};
float wheel=0;
std::atomic<bool> in_window{true};
ElfImage image{};
uint64_t saved_serial=0;
PcActionFilter pc_actions;
PcShortPress short_press;
ControllerGuard controller_guard;
ControllerEquipment controller_equipment;
}
void host_log(const char* fmt,...) {
    std::fputs("Input: ",stdout);va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::putchar('\n');
}
bool input_prologue_hook(ElfImage* i,uint64_t at,const uint8_t* bytes,size_t n,BbEngine::Callback fn) {
    BbEngine::Hook hook;
    if(at<i->mem.slide || !BbEngine::Prepare(hook,reinterpret_cast<uint8_t*>(i->mem.slide),
        i->mem.size,at-i->mem.slide,std::span<const uint8_t>(bytes,n),fn))return false;
    BbEngine::Commit(std::span<BbEngine::Hook>(&hook,1));return true;
}
HostSettings host_settings() {return {BbSettings::Get().mouse_menu && BbSettings::Get().control_mode==0};}
std::uint32_t input_delivered_buttons() {return host_pad_state().buttons;}
InputDevice host_input_device() {return device.load();}
PadState host_pad_state() {std::lock_guard lock(state_lock);return snapshot;}
bool host_options_open() {return BbOverlay::MenuOpen();}
bool ingame_menu_open() {return false;} // bbhost's separate host menu is not installed here.
bool host_text_entry_open() {return text_open.load();}
bool debug_menu_open() {return false;}
bool debug_menu_active() {return false;} // No dedicated native debug-menu controller in this port.
bool host_mouse_relative() {return relative.load();}
bool host_mouse_stage_position(float& x,float& y) {
    std::lock_guard lock(state_lock);
    if(!in_window || !focused || relative || window_w<=0 || window_h<=0)return false;
    // Presenter fits the output; Scaleform fits its native 1920x1080 stage.
    const auto& s=BbSettings::Get();
    const int idx=std::clamp(BbSettings::ResolutionNeedsRestart()?s.startup_output_res:s.output_res.load(),0,BbSettings::OutputCount-1);
    const float ow=float(BbSettings::OutputWidth(idx)),oh=float(BbSettings::OutputHeight(idx));
    const float present=std::min(window_w/ow,window_h/oh);
    if(!s.widescreen){
        x=(mouse_x-(window_w-ow*present)/2)/present*1920/ow;
        y=(mouse_y-(window_h-oh*present)/2)/present*1080/oh;return true;
    }
    const float ui=std::min(ow/1920,oh/1080);
    const float sx=(window_w-ow*present)/2+(ow-1920*ui)*present/2;
    const float sy=(window_h-oh*present)/2+(oh-1080*ui)*present/2;
    x=(mouse_x-sx)/(present*ui);y=(mouse_y-sy)/(present*ui);return true;
}
void host_mouse_describe(char* out,size_t n) {
    std::lock_guard lock(state_lock);std::snprintf(out,n,"pointer %.0f,%.0f window %dx%d",mouse_x,mouse_y,window_w,window_h);
}
bool option_menu_click_decides(uint8_t* c,int row) {return BbEngine::Options::ClickDecides(c,row);}
void option_menu_list_update(uint8_t* c,bool focus) {BbEngine::Options::ListUpdate(c,focus);}

namespace BbInput {
bool Enabled(){return active && BbSettings::Get().pc_controls;}
bool Install(uint8_t* base,size_t size) {
    if(!BbSettings::Get().pc_controls)return false;
    image={{reinterpret_cast<uint64_t>(base),size},kEboot109Sha256};
    host_bindings_load_defaults();
    if(FILE* f=std::fopen((std::getenv("BB_CONFIG")?std::getenv("BB_CONFIG"):"bbport.ini"),"r")) {
        char line[512];
        while(std::fgets(line,sizeof line,f)) {
            char* eq=std::strchr(line,'=');if(!eq)continue;*eq++=0;
            char* end=line+std::strlen(line);while(end>line && (end[-1]==' '||end[-1]=='\t'))*--end=0;
            if(!std::strncmp(line,"bind.",5)){eq[std::strcspn(eq,"\r\n")]=0;host_bindings_parse(line+5,eq);}
        }
        std::fclose(f);
    }
    BbInputConfig::append_bindings=[](BbInputConfig::Items& items) {for(int a=0;a<kBindDebugMenu;a++)items.emplace_back(std::string("bind.")+host_binding_info(a).key,host_binding_ini_value(a));};
    saved_serial=host_bindings_serial();
    mouse_camera_install(&image);menu_pointer_install(&image);
    active=true;
    host_log("bbhost keyboard/mouse controls enabled, camera hook %s",mouse_camera_installed()?"ready":"unavailable");
    return true;
}
void Tick() {
    if(!active)return;
    if(host_bindings_serial()!=saved_serial){BbSettings::Save();saved_serial=host_bindings_serial();}
}
void Event(const SDL_Event& e,bool blocked) {
    if(!active)return;
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {focused=false;wanted_relative=false;mouse_camera::g_counts=0;
        std::lock_guard lock(state_lock);mouse_buttons=mouse_edges=0;wheel=0;key_edges.fill(false);return;}
    if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;
    if(e.type==SDL_EVENT_WINDOW_MOUSE_LEAVE)in_window=false;
    if(e.type==SDL_EVENT_WINDOW_MOUSE_ENTER)in_window=true;
    if(BbSettings::Get().control_mode==1 && (e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
       (e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(int(e.gaxis.value))>12000)))device=InputDevice::Pad;
    if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type==SDL_EVENT_GAMEPAD_BUTTON_UP || e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION)return;
    if(blocked){
        std::lock_guard lock(state_lock);mouse_buttons=mouse_edges=0;wheel=0;key_edges.fill(false);
        return;
    }
    if(e.type==SDL_EVENT_KEY_UP)host_bind_capture_key_up(e.key.scancode);
    if(e.type==SDL_EVENT_KEY_DOWN){device=InputDevice::KeyboardMouse;
        const bool captured=host_bind_capture_key(e.key.scancode);
        if(!captured && !e.key.repeat && focused && !text_open && e.key.scancode>0 && e.key.scancode<SDL_SCANCODE_COUNT){
            std::lock_guard lock(state_lock);key_edges[e.key.scancode]=true;
        }
        if(e.key.scancode==SDL_SCANCODE_DELETE)host_bind_note_clear_key();}
    if(e.type==SDL_EVENT_MOUSE_MOTION){
        device=InputDevice::KeyboardMouse;std::lock_guard lock(state_lock);mouse_x=e.motion.x;mouse_y=e.motion.y;
        if(relative && focused)mouse_camera::add(mouse_camera::g_counts,e.motion.xrel,e.motion.yrel);
    }
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP){
        device=InputDevice::KeyboardMouse;
        const int b=e.button.button;const uint32_t bit=b>0&&b<=5?1u<<(b-1):0;
        // Configs/keycaps use PC numbering: 1 left, 2 right, 3 middle.
        // SDL numbers middle=2 and right=3; preserve SDL bits for menu input.
        const int token=b==2?3:b==3?2:b;
        const bool capture=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && host_bind_capture_mouse(token);
        std::lock_guard lock(state_lock);
        mouse_x=e.button.x;mouse_y=e.button.y;
        if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){mouse_buttons|=bit;if(!capture)mouse_edges|=bit;}
        else mouse_buttons&=~bit;
    }
    if(e.type==SDL_EVENT_MOUSE_WHEEL && e.wheel.y!=0){device=InputDevice::KeyboardMouse;
        if(!host_bind_capture_mouse(e.wheel.y>0?6:7)){std::lock_guard lock(state_lock);wheel+=e.wheel.y;}}
}
void Pump(SDL_Window* w,bool editing) {
    text_open=editing;if(!active)return;
    int ww,hh;SDL_GetWindowSize(w,&ww,&hh);
    {std::lock_guard lock(state_lock);window_w=ww;window_h=hh;}
    const bool want=wanted_relative && BbSettings::Get().pc_controls && !editing && !BbOverlay::CapturesInput() && focused;
    if(relative!=want){
        if(SDL_SetWindowRelativeMouseMode(w,want)){relative=want;mouse_camera::g_counts=0;
            mouse_camera::g_flick_counts=0;mouse_camera::g_flick_updates=0;
            if(want)SDL_HideCursor();else SDL_ShowCursor();}
    }
}
}

extern "C" int bbgpu_controller_mode(){return BbSettings::Get().control_mode==1;}
extern "C" int bbgpu_pc_input_delivery(BbPcPad* d,const bool* keys,int nkeys,int gamepad_present,int delivery_blocked) {
    if(!active || !BbSettings::Get().pc_controls){wanted_relative=false;BbEngine::PcGuard::Input(false);return 0;}
    auto& s=BbSettings::Get();
    PadState p;p.buttons=d->buttons;
    p.lx=d->lx;p.ly=d->ly;p.rx=d->rx;p.ry=d->ry;p.l2=d->l2;p.r2=d->r2;
    if(!gamepad_present)p.lx=p.ly=p.rx=p.ry=kStickRest;
    p.touch_count=std::min<uint8_t>(d->touch_count,2);
    for(int i=0;i<p.touch_count;i++)p.touch[i]={d->touch[i].x,d->touch[i].y,d->touch[i].id,true};
    const bool menu=menu_pointer_in_menu();
    const bool camera=s.control_mode==0 && s.mouse_camera && mouse_camera_installed() && !menu && !host_text_entry_open() && focused;
    wanted_relative=camera && device==InputDevice::KeyboardMouse;
    mouse_camera_publish(s.mouse_sensitivity,s.mouse_invert_x,s.mouse_invert_y,
        camera && device==InputDevice::KeyboardMouse && !s.mouse_auto_rotation);
    uint32_t buttons,edges;float scroll;
    std::array<bool, SDL_SCANCODE_COUNT> sampled_keys{};
    {std::lock_guard lock(state_lock);buttons=mouse_buttons;edges=mouse_edges;scroll=wheel;mouse_edges=0;wheel=0;
        for(int i=0;i<SDL_SCANCODE_COUNT;++i)sampled_keys[i]=key_edges[i] || (keys && i<nkeys && keys[i]);
        key_edges.fill(false);
    }
    keys=sampled_keys.data();nkeys=SDL_SCANCODE_COUNT;
    bool any=buttons!=0;for(int i=0;keys&&i<nkeys;i++)any|=keys[i];
    host_bind_capture_note_held(any);
    if(!focused || host_text_entry_open() || BbOverlay::CapturesInput() || host_bind_capture_blocking()){p=PadState{};wanted_relative=false;pc_actions.Reset();short_press.Reset();controller_guard.Reset();controller_equipment.Reset();BbEngine::PcGuard::Input(false);}
    else if(s.control_mode==1){
        device=InputDevice::Pad;wanted_relative=false;pc_actions.Reset();short_press.Reset();
        const bool held_guard=controller_guard.Apply(p,menu,s.pad_guard_mode);
        BbEngine::PcGuard::Input(held_guard && !delivery_blocked);
        if(!menu)p.buttons&=~0x8; // Start opens the host system menu via its SDL event.
        controller_equipment.Apply(p,menu,SDL_GetTicks());
        ControllerFaces(p,menu_region_confirm_button()==0x2000);
    }else {
        p=PadState{};device=InputDevice::KeyboardMouse;controller_guard.Reset();controller_equipment.Reset();
        bool held[kBindCount]={};
        if(keys)host_bindings_keys_held(keys,nkeys,held);
        if(device==InputDevice::KeyboardMouse) {
            if(p.lx==128)p.lx=127;if(p.ly==128)p.ly=127;
            if(p.rx==128)p.rx=127;if(p.ry==128)p.ry=127;
        }
        // Separate immutable menu navigation from gameplay keys (only while a menu has the screen:
        // menu_pointer_navigating).
        const bool navigating=menu && menu_pointer_navigating();
        if(navigating && keys) {
            for(int i=kBindLookU;i<=kBindLookR;i++)held[i]=false;
            if(keys[SDL_SCANCODE_UP]||held[kBindMoveF])p.buttons|=0x10;
            if(keys[SDL_SCANCODE_DOWN]||held[kBindMoveB])p.buttons|=0x40;
            if(keys[SDL_SCANCODE_LEFT]||held[kBindMoveL])p.buttons|=0x80;
            if(keys[SDL_SCANCODE_RIGHT]||held[kBindMoveR])p.buttons|=0x20;
            if(keys[SDL_SCANCODE_RETURN])p.buttons|=menu_confirm_button();
            if(keys[SDL_SCANCODE_BACKSPACE])p.buttons|=menu_back_button();
            for(int i=kBindMoveF;i<=kBindMoveR;i++)held[i]=false;
        }
        if(menu)held[kBindStrongMod]=false;
        if(menu && keys[SDL_SCANCODE_ESCAPE]){held[kBindMenu]=false;p.buttons|=menu_back_button();}
        // Esc is the independent PC game menu; Tab remains the native equipment menu.
        if(!menu) {
            bool mouse_held[kBindCount]={};
            host_bindings_mouse_held(buttons|edges|(scroll>0?32:scroll<0?64:0),mouse_held,keys,nkeys);
            for(int i=0;i<kBindCount;++i)held[i]=held[i]||mouse_held[i];
        }
        pc_actions.Apply(held,menu,s.melee_guard_toggle);
        BbEngine::PcGuard::Input(!delivery_blocked && !menu && held[kBindMeleeGuard]);
        host_bindings_apply(held,false,p);
        if(menu)short_press.Reset();else short_press.Apply(p,SDL_GetTicks());
        if(menu) {
            if(s.mouse_menu){
                if(edges&1)menu_pointer_click();
                if(edges&SDL_BUTTON_RMASK)p.buttons|=menu_back_button();
                if(navigating && scroll>0)p.buttons|=0x10;if(navigating && scroll<0)p.buttons|=0x40;
                p.buttons|=menu_pointer_take_press();
            }
        }else {
            float x,y;if(camera && mouse_camera_flick(x,y)){
                p.rx=uint8_t(std::clamp(127+int(127*x),0,254));p.ry=uint8_t(std::clamp(127+int(127*y),0,254));}
        }
    }
    {std::lock_guard lock(state_lock);snapshot=p;}
    d->buttons=p.buttons;d->lx=p.lx;d->ly=p.ly;d->rx=p.rx;d->ry=p.ry;d->l2=p.l2;d->r2=p.r2;
    d->touch_count=p.touch_count;for(int i=0;i<p.touch_count;i++)d->touch[i]={p.touch[i].x,p.touch[i].y,p.touch[i].id};
    return 1;
}
extern "C" int bbgpu_pc_input(BbPcPad* d,const bool* keys,int nkeys,int gamepad_present) {
    return bbgpu_pc_input_delivery(d,keys,nkeys,gamepad_present,0);
}

