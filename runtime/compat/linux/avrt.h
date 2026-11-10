// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: MMCSS (multimedia thread scheduling) has no Linux counterpart the runtime can use without extra rights;
// the calls succeed and the thread keeps its normal priority (SetThreadPriority still applies).
#pragma once
#ifndef _WIN32
#include "windows.h"
typedef enum _AVRT_PRIORITY { AVRT_PRIORITY_VERYLOW = -2, AVRT_PRIORITY_LOW, AVRT_PRIORITY_NORMAL, AVRT_PRIORITY_HIGH, AVRT_PRIORITY_CRITICAL } AVRT_PRIORITY;
inline HANDLE AvSetMmThreadCharacteristicsW(LPCWSTR, LPDWORD index) { if (index) *index = 0; static int token; return &token; }
inline HANDLE AvSetMmThreadCharacteristicsA(LPCSTR, LPDWORD index) { return AvSetMmThreadCharacteristicsW(nullptr, index); }
inline BOOL AvSetMmThreadPriority(HANDLE, AVRT_PRIORITY) { return TRUE; }
inline BOOL AvRevertMmThreadCharacteristics(HANDLE) { return TRUE; }
#endif
