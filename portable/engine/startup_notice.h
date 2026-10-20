// SPDX-License-Identifier: GPL-3.0-or-later
// Per-launch notice. A live process holds confirmation only for this session.
// This is a user-facing startup gate, not DRM or protection against modified code.
#ifndef BB_STARTUP_NOTICE_H
#define BB_STARTUP_NOTICE_H
#include <windows.h>
#include <wchar.h>

static const wchar_t bb_notice_text[] =
    L"本项目修改代码开源，游戏更新完全免费。\n"
    L"请勿冒充作者倒卖本项目或收取更新费用。\n\n"
    L"作者：灰灰（呆呆）\n"
    L"交流群：721775768\n"
    L"发现倒卖的请及时联系作者。\n\n"
    L"项目地址：\nhttps://github.com/S983860/bloodborne-pc-zh\n\n"
    L"原版游戏资源不属于本项目的开源内容。\n"
    L"开源代码的使用与分发遵循仓库许可证。\n\n"
    L"点击“确定”后检测硬件，再选择键鼠或手柄。\n"
    L"点击“取消”或关闭窗口将退出，不启动游戏。";

static int bb_notice_has_confirmation(void) {
    wchar_t owner[32], name[96];
    DWORD length = GetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID", owner, 32);
    if (!length || length >= 32) return 0;
    unsigned long long pid = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (owner[i] < L'0' || owner[i] > L'9') return 0;
        pid = pid * 10 + (unsigned)(owner[i] - L'0');
        if (pid > 0xffffffffULL) return 0;
    }
    if (!pid) return 0;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!process) return 0;
    int alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    if (!alive) return 0;
    if (swprintf(name, 96, L"Local\\BloodborneFreeNoticeV1_%lu", (unsigned long)pid) < 0) return 0;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!event) return 0;
    int confirmed = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    CloseHandle(event);
    return confirmed;
}

// 1: confirmed, 0: cancelled, -1: UI/session setup failed. No saved preference.
static int bb_notice_confirm(int fresh_launch) {
    static HANDLE session_event;
    if (!fresh_launch && bb_notice_has_confirmation()) return 1;
    int answer = MessageBoxW(NULL, bb_notice_text, L"血源诅咒 · 免费项目说明",
        MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2 | MB_SETFOREGROUND);
    if (!answer) return -1;
    if (answer != IDOK) return 0;
    wchar_t owner[32], name[96];
    DWORD pid = GetCurrentProcessId();
    if (swprintf(owner, 32, L"%lu", (unsigned long)pid) < 0 ||
        swprintf(name, 96, L"Local\\BloodborneFreeNoticeV1_%lu", (unsigned long)pid) < 0) return -1;
    if (!session_event) session_event = CreateEventW(NULL, TRUE, FALSE, name);
    if (!session_event || !SetEvent(session_event) ||
        !SetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID", owner)) return -1;
    // Keep the event open until process exit so child processes can check it.
    return 1;
}
#endif
