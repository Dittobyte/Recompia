// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: BCryptGenRandom on getrandom(2).
#pragma once
#ifndef _WIN32
#include "windows.h"
#include <sys/random.h>
typedef void* BCRYPT_ALG_HANDLE;
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002u
#define STATUS_SUCCESS ((NTSTATUS)0)
#define BCRYPT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
inline NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE, PUCHAR buf, ULONG n, ULONG) {
    for (ULONG got = 0; got < n;) { ssize_t r = getrandom(buf + got, n - got, 0); if (r < 0) { if (errno == EINTR) continue; return (NTSTATUS)0xC0000001; } got += static_cast<ULONG>(r); }
    return 0;
}
#endif
