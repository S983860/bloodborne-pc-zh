// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bindings.h"

// PC Dodge is a press, whereas Sprint is held. The guest originally multiplexes
// both on a single face button. Never let a held Dodge become a sprint.
struct PcActionFilter {
    bool dodge_was_down=false;
    bool heavy_owns_click=false;
    bool guard_was_down=false,guard_latched=false,guard_toggle_was=false;
    void Reset(){dodge_was_down=heavy_owns_click=guard_was_down=guard_latched=guard_toggle_was=false;}
    void Apply(bool held[kBindCount],bool menu,bool guard_toggle=false) {
        if(menu){Reset();held[kBindRoll]=held[kBindStrongMod]=held[kBindMeleeGuard]=false;return;}
        const bool guard_down=held[kBindMeleeGuard];
        if(guard_toggle!=guard_toggle_was){guard_latched=false;guard_was_down=guard_down;}
        if(guard_toggle && guard_down && !guard_was_down)guard_latched=!guard_latched;
        guard_was_down=guard_down;guard_toggle_was=guard_toggle;
        if(guard_toggle)held[kBindMeleeGuard]=guard_latched;
        const bool dodge=held[kBindRoll];
        held[kBindRoll]=dodge && !dodge_was_down;
        dodge_was_down=dodge;
        // Guard has its own binding. Actions which leave the guard pose take
        // priority, and its private L3+L2 transport must never fire a gun.
        if(held[kBindRoll] || held[kBindL3] || held[kBindInteract] || held[kBindMenu] ||
           held[kBindBloodVial] || held[kBindUseItem] || held[kBindSwitchRight] || held[kBindSwitchLeft] ||
           (guard_toggle && (held[kBindAttack] || held[kBindStrong] || held[kBindFirearm] || held[kBindTransform]))) {
            held[kBindMeleeGuard]=false;
            guard_latched=false;
        }
        if(held[kBindMeleeGuard])
            held[kBindAttack]=held[kBindStrong]=held[kBindFirearm]=held[kBindTransform]=false;
        // Heavy and light share Mouse1. Once a heavy chord owns this click,
        // releasing Shift first must not start a second, light attack.
        if(!held[kBindAttack])heavy_owns_click=false;
        if(held[kBindStrong] && held[kBindAttack])heavy_owns_click=true;
        if(heavy_owns_click)held[kBindAttack]=false;
        const bool moving=held[kBindMoveF]||held[kBindMoveB]||held[kBindMoveL]||held[kBindMoveR];
        held[kBindStrongMod]=held[kBindStrongMod] && moving;
    }
};
