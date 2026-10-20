// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: Windows implementations of bbport_platform.h and bbport_threads.h.

#ifdef _WIN32
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <windows.h>
#include <psapi.h>
#include "bbport_platform.h"
#include "bbport_threads.h"

namespace BbPlatform {

static std::uint64_t FileTimeUs(const FILETIME& t) {
    return ((std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10;
}

bool GetUsage(bool thread, Usage& out) {
    FILETIME creation, exit, kernel, user;
    const BOOL ok = thread ? GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)
                           : GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    if (!ok) {
        return false;
    }
    out = {};
    out.user_us = FileTimeUs(user);
    out.sys_us = FileTimeUs(kernel);
    if (!thread) {
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
            out.minor_faults = counters.PageFaultCount;
        }
    }
    return true;
}

bool GetMemory(Memory& out) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)) ||
        !GlobalMemoryStatusEx(&status)) {
        return false;
    }
    out.private_bytes = counters.PrivateUsage;
    out.working_set = counters.WorkingSetSize;
    out.peak_working_set = counters.PeakWorkingSetSize;
    out.free_phys = status.ullAvailPhys;
    out.total_phys = status.ullTotalPhys;
    out.commit_left = status.ullAvailPageFile;
    return true;
}

std::uint32_t CurrentThreadId() {
    return static_cast<std::uint32_t>(GetCurrentThreadId());
}

int ThreadCpuClock() {
    HANDLE handle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                         THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        return -1;
    }
    return static_cast<int>(reinterpret_cast<std::intptr_t>(handle));
}

std::uint64_t ReadThreadCpuClockNs(int clock) {
    FILETIME creation, exit, kernel, user;
    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(clock));
    if (!GetThreadTimes(handle, &creation, &exit, &kernel, &user)) {
        return 0;
    }
    return (FileTimeUs(kernel) + FileTimeUs(user)) * 1000;
}

void DescribeAddress(const void* address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, name, sizeof(name))) {
        std::snprintf(out, size, "%s+0x%llx", name,
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) -
                                                      reinterpret_cast<std::uintptr_t>(module)));
        return;
    }
    std::snprintf(out, size, "%p", address);
}

std::size_t CaptureStack(void** frames, std::size_t count) {
    return CaptureStackBackTrace(0, static_cast<DWORD>(count), frames, nullptr);
}

bool ReadProcessMemory(const void* source, void* destination, std::size_t size) {
    SIZE_T read = 0;
    return ::ReadProcessMemory(GetCurrentProcess(), source, destination, size, &read) && read == size;
}

} // namespace BbPlatform

namespace BbThreads {

unsigned Available() {
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        return std::max(1, std::popcount(static_cast<unsigned long long>(process)));
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

void MakeBackground() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

// The game runs dozens of threads at normal priority (cloth, character, Havok, job pools); on
// 12 or fewer hardware threads Windows gives a ready thread of equal priority a core only when
// another one's quantum ends (tens of ms), so a render-chain thread queued behind them delays
// the frame. Its idle spins are bounded (BB_GPU_SPIN_US, BB_PIPE_SPIN_US, recording: 200 us)
// before it sleeps, so the higher priority takes no lasting time from the game's threads.
namespace {
bool Raising() {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_GPU_THREAD_PRIORITY");
        return !(env && env[0] == '0');
    }();
    return enabled;
}
} // namespace

void MakeCritical(bool bulk) {
    if (Raising() && (!bulk || Available() >= 16)) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
}

unsigned CapSpinUs(unsigned wanted) {
    return Raising() && Available() < 16 ? std::min(wanted, 50u) : wanted;
}

} // namespace BbThreads
#endif
