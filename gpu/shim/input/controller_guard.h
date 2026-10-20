// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "compat.h"
struct ControllerGuard {
    bool chord=false,trigger_held=false,suppress_transform=false;
    int previous_mode=-1;
    void Reset(){chord=trigger_held=suppress_transform=false;previous_mode=-1;}
    bool Apply(PadState& p,bool menu,int mode=0){
        if(menu){Reset();return false;}
        if(previous_mode!=mode){Reset();previous_mode=mode;}
        // Digital shoulder guarding responds in the current sample. Legacy
        // chords keep a small trigger hysteresis to avoid release chatter.
        const bool l1=(p.buttons&0x400)!=0,l3=(p.buttons&0x2)!=0;
        trigger_held=(p.buttons&0x100)!=0 || p.l2>(trigger_held?12:24);
        const bool l2=trigger_held;
        const bool cancelled=(p.buttons&(0x2000|0x800|0x200|0x1000|0x8000|0x8|0x100000))!=0;
        const bool held=l1&&(mode==0 || l2)&&!cancelled;
        if(l1&&l2)chord=true;
        // A released chord never becomes an unintended shot/transformation.
        if(chord){p.buttons&=~(0x400|0x100);p.l2=0;if(!l1&&!l2)chord=false;}
        if(mode==0){
            // L1/LB guards; physical L3 is weapon transform. Do not transform
            // after an accidental stick click made while the guard is held.
            if(!l3)suppress_transform=false;
            else if(l1)suppress_transform=true;
            p.buttons&=~(0x400|0x2);
            if(l3&&!l1&&!suppress_transform)p.buttons|=0x400;
        }
        if(held){p.buttons=(p.buttons&~0xc00)|0x102;p.l2=255;}
        return held;
    }
};
inline void ControllerFaces(PadState& p,bool circle_confirms){
    if(!circle_confirms)return;
    const auto face=p.buttons&0x6000;p.buttons&=~0x6000;
    if(face&0x4000)p.buttons|=0x2000;if(face&0x2000)p.buttons|=0x4000;
}
struct ControllerEquipment {
    bool was=false,long_press=false;std::uint64_t started=0,pulse_until=0;
    void Reset(){*this={};}
    void Apply(PadState& p,bool menu,std::uint64_t now){
        if(menu){Reset();return;}
        const bool down=(p.buttons&0x100000)!=0;
        if(down){
            if(!was){started=now;long_press=false;}
            if(now-started>=350)long_press=true;
            if(!long_press){p.buttons&=~0x100000;p.touch_count=0;}
        }else if(was&&!long_press)pulse_until=now+75;
        if(now<pulse_until)p.buttons|=0x8;
        was=down;
    }
};
