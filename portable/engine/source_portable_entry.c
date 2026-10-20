// SPDX-License-Identifier: GPL-3.0-or-later
// Silent native game entry. Paths are relative to this executable, never a
// saved drive letter. All rendering happens in the bundled game engine.
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#include <windows.h>
#include <wchar.h>
#include "startup_notice.h"
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR arguments,int show) {
    (void)instance;(void)previous;(void)show;
    int notice=bb_notice_confirm(1);
    if(notice<=0)return notice==0?0:1;
    wchar_t root[32768],python[32768],command[32768];
    DWORD n=GetModuleFileNameW(NULL,root,32768);
    if(!n||n>=32768)return 2;
    wchar_t* slash=wcsrchr(root,L'\\');if(!slash)return 2;*slash=0;
    if(swprintf(python,32768,L"%ls\\runtime\\pythonw.exe",root)<0)return 2;
    if(swprintf(command,32768,L"\"%ls\" -I \"%ls\\engine\\portable_game.py\" %ls",python,root,arguments)<0)return 2;
    STARTUPINFOW startup={0};startup.cb=sizeof startup;
    PROCESS_INFORMATION child={0};
    if(!CreateProcessW(python,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,root,&startup,&child)) {
        MessageBoxW(NULL,L"无法启动游戏运行环境。请保持游戏目录完整，并确认 runtime 文件夹与游戏在同一目录。",L"血源诅咒",MB_OK|MB_ICONERROR);
        return 2;
    }
    CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
    return (int)code;
}
