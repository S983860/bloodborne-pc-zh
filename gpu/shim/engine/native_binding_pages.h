// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../input/bindings.h"
namespace BbEngine::BindingPages {
// Put equipment and consumables on the first page, so they are visible as
// soon as Key Bindings opens. IDs remain unchanged in saved configurations.
inline constexpr int Order[kBindCount]={
    kBindSwitchRight,kBindSwitchLeft,kBindSwitchItem,kBindUseItem,kBindBloodVial,kBindFirearm,kBindLockOn,
    kBindMoveF,kBindMoveB,kBindMoveL,kBindMoveR,kBindWalk,kBindRoll,kBindStrongMod,
    kBindMeleeGuard,kBindAttack,kBindStrong,kBindTransform,kBindInteract,kBindL3,kBindGestures,
    kBindLookU,kBindLookD,kBindLookL,kBindLookR,kBindMenu,kBindConfirm,kBindBack,
    kBindDpadUp,kBindMinimap,kBindEffects,kBindDebugMenu
};
inline constexpr const char* Names[kBindPages]={"武器与药品","移动与奔跑","战斗与交互","镜头与菜单","其他与默认"};
inline constexpr int Action(int page,int row){
    const int index=page*kBindPerPage+row-1;
    return index>=0 && index<kBindCount?Order[index]:kBindCount;
}
static_assert([]{bool seen[kBindCount]{};for(int id:Order){if(id<0||id>=kBindCount||seen[id])return false;seen[id]=true;}return true;}(),
              "Every saved action must appear exactly once in the native menu");
}
