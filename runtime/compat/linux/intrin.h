// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: the MSVC intrinsics the runtime uses, on GCC/Clang builtins.
#pragma once
#ifndef _WIN32
#include <x86intrin.h>
#include <cpuid.h>
inline void __cpuid(int out[4], int leaf) { __cpuid_count(leaf, 0, out[0], out[1], out[2], out[3]); }
inline void __cpuidex(int out[4], int leaf, int sub) { __cpuid_count(leaf, sub, out[0], out[1], out[2], out[3]); }
inline unsigned long long _xgetbv_compat(unsigned int x) { unsigned int a, d; __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(x)); return (static_cast<unsigned long long>(d) << 32) | a; }
#define _ReadWriteBarrier() __asm__ volatile("" ::: "memory")
#endif
