// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: ShellExecute "open" hands the URL or file to xdg-open (the desktop's default handler).
#pragma once
#ifndef _WIN32
#include "windows.h"
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
HINSTANCE ShellExecuteA(HWND, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);   // > 32 on success, like Win32
HINSTANCE ShellExecuteW(HWND, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show);
#endif
