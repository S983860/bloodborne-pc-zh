// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "compat.h"
#include <cstdint>

// The host may poll more than once before the guest consumes a game frame.
// Keep a very short movement/dodge tap available for a few frames. A held
// key still releases immediately once its initial minimum time has passed.
struct PcShortPress {
    struct Axis {
        std::uint8_t raw=127,value=127;
        std::uint64_t until=0;
        std::uint8_t Apply(std::uint8_t next,std::uint64_t now) {
            const bool neutral=next==127 || next==128;
            if(!neutral){if(next!=raw){value=next;until=now+75;}raw=next;return next;}
            raw=127;return now<until?value:127;
        }
    } axes[4];
    bool circle_was=false;
    std::uint64_t circle_until=0;
    void Reset(){*this=PcShortPress{};}
    void Apply(PadState& pad,std::uint64_t now) {
        pad.lx=axes[0].Apply(pad.lx,now);pad.ly=axes[1].Apply(pad.ly,now);
        pad.rx=axes[2].Apply(pad.rx,now);pad.ry=axes[3].Apply(pad.ry,now);
        const bool circle=(pad.buttons&0x2000u)!=0;
        if(circle && !circle_was)circle_until=now+75;
        circle_was=circle;
        if(now<circle_until)pad.buttons|=0x2000u;
    }
};
