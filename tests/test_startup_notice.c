// SPDX-License-Identifier: GPL-3.0-or-later
// Real Windows events/process lifetime, with only the user dialog replaced.
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
static int dialog_count, dialog_result=IDOK;
static int WINAPI test_dialog(HWND owner,LPCWSTR text,LPCWSTR title,UINT flags) {
    (void)owner;(void)title;
    if (!wcsstr(text,L"721775768") || !wcsstr(text,L"灰灰（呆呆）") ||
        !wcsstr(text,L"https://github.com/S983860/bloodborne-pc-zh") ||
        !(flags & MB_OKCANCEL)) return 0;
    ++dialog_count;
    return dialog_result;
}
#define MessageBoxW test_dialog
#include "startup_notice.h"
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"failed at line %d\n",__LINE__);return 1; } } while (0)
int main(int argc,char** argv) {
    if (argc==2 && !strcmp(argv[1],"child")) {
        CHECK(bb_notice_confirm(0)==1);
        CHECK(dialog_count==0);
        return 0;
    }
    SetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID",NULL);
    dialog_result=IDCANCEL;
    CHECK(bb_notice_confirm(1)==0);
    CHECK(!bb_notice_has_confirmation());
    dialog_result=0;
    CHECK(bb_notice_confirm(1)==-1);
    CHECK(!bb_notice_has_confirmation());
    dialog_result=IDOK;
    CHECK(bb_notice_confirm(1)==1);
    CHECK(bb_notice_has_confirmation());
    CHECK(bb_notice_confirm(0)==1);
    CHECK(dialog_count==3);
    wchar_t executable[32768],command[32768];
    CHECK(GetModuleFileNameW(NULL,executable,32768)>0);
    CHECK(swprintf(command,32768,L"\"%ls\" child",executable)>0);
    STARTUPINFOW startup={0};startup.cb=sizeof startup;
    PROCESS_INFORMATION child={0};
    CHECK(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));
    CloseHandle(child.hThread);
    CHECK(WaitForSingleObject(child.hProcess,10000)==WAIT_OBJECT_0);
    DWORD code=1;CHECK(GetExitCodeProcess(child.hProcess,&code));CloseHandle(child.hProcess);
    CHECK(code==0);
    // A fresh top-level launch must prompt even with a live inherited owner.
    CHECK(bb_notice_confirm(1)==1);
    CHECK(dialog_count==4);
    SetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID",L"not-a-pid");
    CHECK(!bb_notice_has_confirmation());
    SetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID",L"999999999999999999999999999");
    CHECK(!bb_notice_has_confirmation());
    SetEnvironmentVariableW(L"BB_NOTICE_OWNER_PID",L"4294967295");
    CHECK(!bb_notice_has_confirmation());
    puts("PASS: cancel, UI failure, native session, child inheritance, fresh launch, malformed/stale owner");
    return 0;
}
