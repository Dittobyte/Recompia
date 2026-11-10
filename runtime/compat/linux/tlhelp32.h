// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: thread snapshots of this process, from /proc/self/task.
#pragma once
#ifndef _WIN32
#include "windows.h"
#define TH32CS_SNAPTHREAD 0x00000004u
typedef struct tagTHREADENTRY32 {
    DWORD dwSize; DWORD cntUsage; DWORD th32ThreadID; DWORD th32OwnerProcessID; LONG tpBasePri; LONG tpDeltaPri; DWORD dwFlags;
} THREADENTRY32, *LPTHREADENTRY32;
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid);   // TH32CS_SNAPTHREAD of this process only
BOOL Thread32First(HANDLE snap, LPTHREADENTRY32 te);
BOOL Thread32Next(HANDLE snap, LPTHREADENTRY32 te);
#endif
