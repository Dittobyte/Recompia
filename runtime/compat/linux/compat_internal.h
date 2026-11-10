// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: what win32_compat.cpp and win32_fault.cpp share, and what the runtime's Linux twins call (src/diag/*_linux.cpp).
#pragma once
#ifndef _WIN32
#include "windows.h"
#include <sys/types.h>
pid_t nx_compat_thread_tid(HANDLE t);     // kernel thread id behind a thread handle (or the current-thread pseudo handle); 0 if none
HANDLE nx_compat_thread_handle(pid_t tid); // a new thread handle for an existing thread (OpenThread)
void nx_fault_thread_setup();             // the signal stack for the calling thread (every CreateThread thread gets one)
bool nx_fault_resume(pid_t tid, DWORD* prev);   // ends a SuspendThread of tid; false if it isn't suspended
// Calls fn(ctx); a fault no vectored handler takes (a memory fault, or any when `any`) returns false here instead of going to the
// unhandled-exception filter: the __try / __except of include/nxrt/fault_guard.h. Nests.
bool nx_fault_guarded(void (*fn)(void*), void* ctx, bool any);
#endif
