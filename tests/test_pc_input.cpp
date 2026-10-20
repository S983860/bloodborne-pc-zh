// SPDX-License-Identifier: GPL-3.0-or-later
#include "../gpu/shim/input/bindings.h"
#include "../gpu/shim/input/compat.h"
#include "../gpu/shim/input/mouse_camera_step.h"
#include "../gpu/shim/input/utf8_text.h"
#include "../gpu/shim/input/pc_action_filter.h"
#include "../gpu/shim/input/pc_short_press.h"
#include "../gpu/shim/input/controller_guard.h"
#include "../src/guard_sound.h"
#include "../gpu/shim/engine/pc_guard.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
extern "C" {std::uint8_t bb_mouse_camera_free_now=0;}
void host_log(const char*,...) {}
bool debug_menu_active(){return false;}
uint32_t menu_confirm_button(){return 0x4000;}
uint32_t menu_back_button(){return 0x2000;}
uint32_t test_region_button=0x4000;
uint32_t menu_region_confirm_button(){return test_region_button;}
int main(){
    {
        ControllerGuard filter;PadState p;p.buttons=0x400;
        assert(filter.Apply(p,false)&&p.buttons==0x102&&p.l2==255);
        p=PadState{};assert(!filter.Apply(p,false)&&p.buttons==0&&p.l2==0);
        p.buttons=0x100;p.l2=255;assert(!filter.Apply(p,false)&&p.buttons==0x100&&p.l2==255);
        p=PadState{};filter.Apply(p,false);p.buttons=0x2;
        assert(!filter.Apply(p,false)&&p.buttons==0x400);
        p=PadState{};filter.Apply(p,false);p.buttons=0x402;
        assert(filter.Apply(p,false)&&p.buttons==0x102);
        p=PadState{};p.buttons=0x2;assert(!filter.Apply(p,false)&&p.buttons==0);
        p=PadState{};filter.Apply(p,false);p.buttons=0x2;assert(!filter.Apply(p,false)&&p.buttons==0x400);
        filter.Reset();p=PadState{};p.buttons=0x400;p.l2=25;
        assert(filter.Apply(p,false,1));p=PadState{};p.buttons=0x400;p.l2=20;
        assert(filter.Apply(p,false,1));p=PadState{};p.buttons=0x400;p.l2=12;
        assert(!filter.Apply(p,false,1)&&p.buttons==0&&p.l2==0);
    }
    {
        ControllerGuard filter;PadState p;p.buttons=0x500;p.l2=255;
        assert(filter.Apply(p,false,1)&&p.buttons==0x102&&p.l2==255);
        p=PadState{};p.buttons=0x100;p.l2=255;
        assert(!filter.Apply(p,false,1)&&p.buttons==0&&p.l2==0);
        p=PadState{};assert(!filter.Apply(p,false,1));
        p.buttons=0x100;p.l2=255;assert(!filter.Apply(p,false,1)&&p.l2==255);
        p.buttons=0x2500;p.l2=255;assert(!filter.Apply(p,false,1)&&(p.buttons&0x2000)&&p.l2==0);
        p.buttons=0x500;p.l2=255;assert(!filter.Apply(p,true,1));
        p=PadState{};p.buttons=0x4000;ControllerFaces(p,true);assert(p.buttons==0x2000);ControllerFaces(p,true);assert(p.buttons==0x4000);
        ControllerEquipment equipment;p.buttons=0x100000;equipment.Apply(p,false,100);assert(p.buttons==0);
        p=PadState{};equipment.Apply(p,false,130);assert(p.buttons==0x8);
        p=PadState{};equipment.Apply(p,false,210);assert(p.buttons==0);
        p.buttons=0x100000;equipment.Apply(p,false,1000);assert(p.buttons==0);
        p.buttons=0x100000;equipment.Apply(p,false,1400);assert(p.buttons==0x100000);
        p=PadState{};equipment.Apply(p,false,1450);assert(p.buttons==0);
        equipment.Reset();p.buttons=0x100000;equipment.Apply(p,true,2000);assert(p.buttons==0x100000);
    }
    {
        using BbEngine::PcGuard::StaminaCost;using BbEngine::PcGuard::PerfectWindow;
        assert(StaminaCost(88)==9 && StaminaCost(100)==10 && StaminaCost(125)==13 && StaminaCost(0)==0);
        assert(PerfectWindow(1100,1000,250,true) && PerfectWindow(1250,1000,250,true));
        assert(!PerfectWindow(1251,1000,250,true) && !PerfectWindow(1100,1000,250,false));
        assert(!PerfectWindow(999,1000,250,true) && !PerfectWindow(1100,0,250,true));
        const float normal[]={.2f,-.2f,.1f},perfect[]={.5f,-.5f};GuardMixer m{};
        guard_voice_start(&m,normal,3,1);assert(std::abs(guard_voice_next(&m)-.2f)<1e-5);
        guard_voice_start(&m,perfect,2,1);assert(std::abs(guard_voice_next(&m)-.3f)<1e-5);
        assert(std::abs(guard_voice_next(&m)+.4f)<1e-5);assert(guard_voice_next(&m)==0);
        for(int i=0;i<100;++i)guard_voice_start(&m,normal,3,1);
        assert(guard_limit(guard_voice_next(&m))==1 && guard_limit(-2)==-1);
        for(int i=0;i<4;++i)guard_voice_next(&m);assert(guard_voice_next(&m)==0);
    }
    {
        using BbEngine::PcGuard::UsesRightHand;
        assert(UsesRightHand(true, 511, 7000000, 1, 1, 1));
        assert(UsesRightHand(true, 511, 7081000, 5, 1, 1));
        assert(!UsesRightHand(false, 511, 7000000, 1, 1, 1));
        assert(!UsesRightHand(true, 0, 7000000, 1, 1, 1));
        assert(!UsesRightHand(true, 511, 7090000, 1, 1, 1));
        assert(!UsesRightHand(true, 511, 19000000, 1, 1, 1));
        assert(!UsesRightHand(true, 511, 7000000, 3, 1, 1));
        assert(!UsesRightHand(true, 511, 7000000, 1, 1, 0));
        assert(!UsesRightHand(true, 511, 7000000, 1, 0, 1));
    }
    {
        PcShortPress pulse;
        PadState p;p.ly=0;p.buttons=0x2000;pulse.Apply(p,100);
        p=PadState{};pulse.Apply(p,116);assert(p.ly==0 && (p.buttons&0x2000));
        p=PadState{};pulse.Apply(p,180);assert(p.ly==127 && p.buttons==0);
        // Long holds release without an extra delay; opposite direction wins.
        p=PadState{};p.lx=254;pulse.Apply(p,200);
        p=PadState{};p.lx=254;pulse.Apply(p,300);
        p=PadState{};pulse.Apply(p,316);assert(p.lx==127);
        p.lx=0;pulse.Apply(p,400);p.lx=254;pulse.Apply(p,416);assert(p.lx==254);
        pulse.Reset();p=PadState{};pulse.Apply(p,420);assert(p.lx==127 && p.buttons==0);
    }
    PcActionFilter actions;
    for(int poll=0;poll<10;++poll){
        bool held[kBindCount]={};held[kBindRoll]=true;actions.Apply(held,false);
        assert(held[kBindRoll]==(poll==0));
    }
    bool released[kBindCount]={};actions.Apply(released,false);
    released[kBindRoll]=true;actions.Apply(released,false);assert(released[kBindRoll]);
    for(int poll=0;poll<10;++poll){
        bool held[kBindCount]={};held[kBindStrongMod]=held[kBindMoveF]=true;
        actions.Apply(held,false);assert(held[kBindStrongMod] && held[kBindMoveF]);
    }
    bool idle[kBindCount]={};idle[kBindStrongMod]=true;actions.Apply(idle,false);assert(!idle[kBindStrongMod]);
    char16_t chinese[32];pc_utf8_to_utf16(chinese,"键鼠操作中文");
    assert(std::u16string(chinese)==u"键鼠操作中文");
    char16_t bounded[3];pc_utf8_to_utf16(bounded,"中\xf0\x9f\x8e\xae");
    assert(std::u16string(bounded)==u"中");
    char16_t invalid[8];pc_utf8_to_utf16(invalid,"\xc0x");
    assert(std::u16string(invalid)==u"\ufffdx");
    host_bindings_load_defaults();
    assert(host_binding_key(kBindWalk)==SDL_SCANCODE_LALT);
    assert(host_binding_mouse(kBindAttack)==1);
    assert(host_binding_ini_value(kBindAttack)=="Mouse1");
    assert(host_binding_key(kBindMenu)==SDL_SCANCODE_TAB);
    assert(host_binding_key(kBindMinimap)==SDL_SCANCODE_M);
    assert(host_bindings_parse("minimap","N"));assert(host_binding_key(kBindMinimap)==SDL_SCANCODE_N);
    assert(host_bindings_parse("minimap","M"));
    assert(host_binding_key(kBindStrongMod)==SDL_SCANCODE_CAPSLOCK);
    assert(host_binding_mouse(kBindLockOn)==3 && host_binding_mouse(kBindFirearm)==2);
    assert(host_binding_key(kBindFirearm)==SDL_SCANCODE_X);
    assert(host_binding_key(kBindMeleeGuard)==SDL_SCANCODE_LCTRL);
    assert(host_binding_key(kBindSwitchRight)==SDL_SCANCODE_1);
    assert(host_binding_key(kBindSwitchLeft)==SDL_SCANCODE_2);
    assert(host_binding_ini_value(kBindSwitchItem)=="Down, WheelBoth");
    host_binding_clear(kBindSwitchItem);
    assert(host_bindings_parse("switch_item","Down, WheelBoth"));
    assert(host_binding_ini_value(kBindStrong)=="Left Shift+Mouse1");
    assert(host_binding_key(kBindStrong)==0);
    assert(host_binding_mouse_modifier(kBindStrong)==SDL_SCANCODE_LSHIFT);
    char chord_description[96];host_binding_describe(kBindStrong,chord_description,sizeof chord_description);
    assert(std::string(chord_description)=="左Shift＋鼠标左键");
    const auto chord=host_binding_ini_value(kBindStrong);
    host_binding_clear(kBindStrong);
    assert(host_bindings_parse("strong_attack",chord.c_str()));
    bool keyboard[SDL_SCANCODE_COUNT]={};
    bool mouse_actions[kBindCount]={};
    host_bindings_mouse_held(SDL_BUTTON_RMASK,mouse_actions);
    assert(!mouse_actions[kBindLockOn] && mouse_actions[kBindFirearm]);
    host_bindings_mouse_held(SDL_BUTTON_MMASK,mouse_actions);
    assert(mouse_actions[kBindLockOn] && !mouse_actions[kBindFirearm]);
    // Actual keyboard + SDL mouse states must produce R1 or R2, never both.
    PcActionFilter combat;
    auto read=[&](uint32_t mouse){
        bool h[kBindCount]={},m[kBindCount]={};
        host_bindings_keys_held(keyboard,SDL_SCANCODE_COUNT,h);
        host_bindings_mouse_held(mouse,m,keyboard,SDL_SCANCODE_COUNT);
        for(int i=0;i<kBindCount;++i)h[i]|=m[i];
        combat.Apply(h,false);PadState pad;host_bindings_apply(h,false,pad);return pad;
    };
    assert(read(SDL_BUTTON_LMASK).buttons==0x800);
    auto gun=read(SDL_BUTTON_RMASK);assert(gun.buttons==0x100 && gun.l2==255 && gun.r2==0);
    // The dedicated guard has a distinct transport. Holding attack/shoot
    // with it never emits an attack; releasing it fully clears the trigger.
    keyboard[SDL_SCANCODE_LCTRL]=true;
    for(int poll=0;poll<10;++poll){auto guard=read(SDL_BUTTON_LMASK|SDL_BUTTON_RMASK);assert(guard.buttons==0x102 && guard.l2==255 && guard.r2==0);}
    keyboard[SDL_SCANCODE_LCTRL]=false;assert(read(0).buttons==0 && read(0).l2==0);
    for(int action:{kBindRoll,kBindL3,kBindInteract,kBindMenu,kBindUseItem,kBindBloodVial,kBindSwitchRight,kBindSwitchLeft}){
        PcActionFilter guard_filter;bool held[kBindCount]={};held[kBindMeleeGuard]=held[action]=true;
        guard_filter.Apply(held,false);assert(!held[kBindMeleeGuard] && held[action]);
    }
    {bool held[kBindCount]={};held[kBindMeleeGuard]=true;combat.Apply(held,true);assert(!held[kBindMeleeGuard]);}
    // Toggle mode follows real press edges, releases on combat/menu interruption,
    // and cannot remain latched after losing focus (Reset).
    {
        PcActionFilter toggle;
        auto poll=[&](bool down,bool menu=false,int action=-1){bool h[kBindCount]={};h[kBindMeleeGuard]=down;if(action>=0)h[action]=true;toggle.Apply(h,menu,true);return h[kBindMeleeGuard];};
        assert(!poll(false));assert(poll(true));assert(poll(true));assert(poll(false));
        assert(!poll(true));assert(!poll(false));assert(poll(true));assert(poll(false));
        assert(!poll(false,false,kBindRoll));assert(!poll(false));
        assert(poll(true));assert(!poll(false,false,kBindAttack));assert(!poll(false));
        assert(poll(true));assert(!poll(false,true));assert(!poll(false));
        assert(poll(true));toggle.Reset();assert(!poll(false));
        // Changing back to hold mode drops the old latch.
        assert(poll(true));bool h[kBindCount]={};toggle.Apply(h,false,false);assert(!h[kBindMeleeGuard]);
    }
    assert(host_binding_ini_value(kBindMeleeGuard)=="Left Ctrl");
    assert(host_bindings_parse("melee_guard","B"));assert(host_binding_key(kBindMeleeGuard)==SDL_SCANCODE_B);
    assert(host_bindings_parse("melee_guard","Left Ctrl"));
    host_bind_capture_begin(kBindMeleeGuard);
    assert(host_bind_capture_key(SDL_SCANCODE_LGUI) && host_bind_capturing()==kBindMeleeGuard);
    assert(host_bind_capture_key(SDL_SCANCODE_RGUI) && host_bind_capturing()==kBindMeleeGuard);
    assert(host_binding_key(kBindMeleeGuard)==SDL_SCANCODE_LCTRL);
    assert(host_bind_capture_key(SDL_SCANCODE_B));host_bind_capture_note_held(false);
    assert(host_binding_key(kBindMeleeGuard)==SDL_SCANCODE_B);
    assert(host_bindings_parse("melee_guard","Left Ctrl"));
    // Middle button locks on without shooting; the right button never locks on.
    auto lock=read(SDL_BUTTON_MMASK);assert(lock.buttons==0x4 && lock.l2==0 && lock.r2==0);
    assert(read(0).buttons==0 && read(0).l2==0);
    keyboard[SDL_SCANCODE_X]=true;gun=read(0);assert(gun.buttons==0x100 && gun.l2==255);
    keyboard[SDL_SCANCODE_X]=false;
    // A held shield action stays held, then releases fully instead of latching a trigger.
    for(int poll=0;poll<10;++poll){gun=read(SDL_BUTTON_RMASK);assert(gun.buttons==0x100 && gun.l2==255);}
    gun=read(0);assert(gun.buttons==0 && gun.l2==0);
    // Rebinding keeps a keyboard alternative and both buttons distinct after a round trip.
    assert(host_binding_ini_value(kBindFirearm)=="X, Mouse2");
    host_binding_clear(kBindFirearm);assert(host_bindings_parse("firearm","X, Mouse2"));
    gun=read(SDL_BUTTON_RMASK);assert(gun.buttons==0x100 && gun.l2==255);read(0);
    read(0);keyboard[SDL_SCANCODE_LSHIFT]=true;
    assert(read(0).buttons==0); // Shift alone neither attacks nor sprints.
    for(int poll=0;poll<10;++poll){auto pad=read(SDL_BUTTON_LMASK);assert(pad.buttons==0x200 && pad.r2==255);}
    keyboard[SDL_SCANCODE_LSHIFT]=false;
    assert(read(SDL_BUTTON_LMASK).buttons==0); // releasing Shift first cannot start a light attack.
    read(0);assert(read(SDL_BUTTON_LMASK).buttons==0x800);read(0);
    keyboard[SDL_SCANCODE_W]=keyboard[SDL_SCANCODE_CAPSLOCK]=true;
    for(int poll=0;poll<10;++poll){const auto pad=read(0);assert(pad.ly==0 && pad.buttons==0x2000);}
    keyboard[SDL_SCANCODE_CAPSLOCK]=false;assert(read(0).buttons==0);
    keyboard[SDL_SCANCODE_W]=false;
    // Weapon cycling cannot also transform/attack. Either wheel direction
    // selects the next quick item without using it or making blood bullets.
    keyboard[SDL_SCANCODE_1]=true;assert(read(0).buttons==0x20);
    keyboard[SDL_SCANCODE_1]=false;keyboard[SDL_SCANCODE_2]=true;assert(read(0).buttons==0x80);
    keyboard[SDL_SCANCODE_2]=false;
    assert(read(32).buttons==0x40);read(0);assert(read(64).buttons==0x40);read(0);
    for(int sc=SDL_SCANCODE_3;sc<=SDL_SCANCODE_9;++sc){keyboard[sc]=true;assert(read(0).buttons==0);keyboard[sc]=false;}
    host_bind_capture_begin(kBindStrong);
    assert(host_bind_capture_key(SDL_SCANCODE_LSHIFT) && host_bind_capturing()==kBindStrong);
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    assert(host_bind_capture_mouse(1));
    assert(host_binding_ini_value(kBindStrong)=="Left Shift+Mouse1" && host_binding_mouse(kBindAttack)==1);
    host_bind_capture_note_held(false);
    host_bind_capture_begin(kBindWalk);
    assert(host_bind_capture_key(SDL_SCANCODE_LALT));
    assert(host_bind_capture_key_up(SDL_SCANCODE_LALT));
    assert(host_binding_key(kBindWalk)==SDL_SCANCODE_LALT);
    host_bind_capture_note_held(false);
    // Gameplay uses region-specific face buttons; PC action keys stay the same.
    for (uint32_t confirm : {0x4000u,0x2000u}) {
        test_region_button=confirm;
        bool actions[kBindCount]={};actions[kBindInteract]=true;
        PadState result;host_bindings_apply(actions,false,result);
        assert(result.buttons==confirm);
        actions[kBindInteract]=false;actions[kBindRoll]=true;result=PadState{};
        host_bindings_apply(actions,false,result);
        assert(result.buttons==(confirm==0x2000u?0x4000u:0x2000u));
        actions[kBindRoll]=false;actions[kBindStrongMod]=true;result=PadState{};
        host_bindings_apply(actions,false,result);
        assert(result.buttons==(confirm==0x2000u?0x4000u:0x2000u));
    }
    test_region_button=0x4000;
    bool h[kBindCount]={};h[kBindMoveF]=true;
    PadState p;host_bindings_apply(h,false,p);assert(p.lx==127 && p.ly==0);
    h[kBindMoveR]=true;p=PadState{};host_bindings_apply(h,false,p);assert(p.lx==254 && p.ly==0);
    h[kBindWalk]=true;p=PadState{};host_bindings_apply(h,false,p);
    const float diagonal=std::hypot(float(p.lx)-127,float(p.ly)-127);
    h[kBindMoveR]=false;p=PadState{};host_bindings_apply(h,false,p);
    assert(std::abs(diagonal-(127-p.ly))<=1);
    std::memset(h,0,sizeof h);h[kBindAttack]=true;p=PadState{};host_bindings_apply(h,true,p);
    assert(p.buttons==0x200 && p.r2==255);
    assert(host_bindings_parse("attack","E, Mouse2"));
    assert(host_binding_ini_value(kBindAttack)=="E, Mouse2");
    host_bind_capture_begin(kBindAttack);assert(host_bind_capture_key(SDL_SCANCODE_R));
    assert(host_binding_key(kBindAttack)==SDL_SCANCODE_R && host_binding_key(kBindUseItem)==0);
    assert(host_bind_capture_blocking());host_bind_capture_note_held(false);assert(!host_bind_capture_blocking());
    host_bind_capture_begin(kBindAttack);host_bind_capture_key(SDL_SCANCODE_ESCAPE);
    assert(host_binding_key(kBindAttack)==SDL_SCANCODE_R);host_bind_capture_note_held(false);
    host_binding_clear(kBindAttack);assert(host_binding_ini_value(kBindAttack)=="none");
    // PC default angular scale is one fifth of the old console-style camera.
    float sensitivity=mouse_camera::degrees_per_count(5);assert(std::abs(sensitivity-.05f)<1e-6);
    uint32_t bits;std::memcpy(&bits,&sensitivity,4);mouse_camera::g_degrees_per_count=bits;
    auto turn=[](int frames){
        float pitch=0,yaw=0;
        for(int i=0;i<frames;i++){
            mouse_camera::add(mouse_camera::g_counts,120.0f/frames,60.0f/frames);
            mouse_camera::begin_update();mouse_camera::turn(0,0,pitch,yaw,-1,1);
        }
        return std::pair{pitch,yaw};
    };
    const auto a=turn(30),b=turn(60),c=turn(90);
    assert(std::abs(a.first-b.first)<1e-5 && std::abs(a.second-c.second)<1e-5);
    float pitch=0,yaw=0;mouse_camera::g_frame={10,10};
    assert(!mouse_camera::turn(1,0,pitch,yaw,-1,1) && pitch==0 && yaw==0);
    mouse_camera::g_flags=mouse_camera::kInvertX|mouse_camera::kInvertY;
    assert(mouse_camera::turn(0,0,pitch,yaw,-1,1) && pitch<0 && yaw<0);
    mouse_camera::g_frame={10000,10000};mouse_camera::turn(0,0,pitch,yaw,-.5,.5);
    assert(pitch==-.5f && yaw>=-3.141593f && yaw<=3.141593f);
    std::puts("PC input: PASS (walk/sprint axes, remapping, capture, triggers, 30/60/90 camera, invert/limits)");
}
