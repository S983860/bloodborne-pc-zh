// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup_notice.h"
__declspec(dllexport) int BBConfirmStartupNotice(void) {
    return bb_notice_confirm(0);
}
__declspec(dllexport) unsigned long BBStartupNoticeOwnerPid(void) {
    wchar_t owner[32];
    if (!bb_notice_has_confirmation() ||
        !GetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID",owner,32)) return 0;
    return wcstoul(owner,NULL,10);
}
