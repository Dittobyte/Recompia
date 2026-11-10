// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: the Microsoft C runtime extras the runtime uses. Forced into every C++ file (-include), because on Windows
// they come with the C runtime itself, without <windows.h>.
#pragma once
#if !defined(_WIN32) && defined(__cplusplus)
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <strings.h>
inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : EINVAL; }
inline int gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : EINVAL; }
inline unsigned long long _strtoui64(const char* s, char** e, int b) { return strtoull(s, e, b); }
inline long long _strtoi64(const char* s, char** e, int b) { return strtoll(s, e, b); }
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
// "NAME=value" sets, "NAME=" removes (as the CRT does). putenv keeps the pointer, so the text is copied for the process's life.
inline int _putenv(const char* assignment) {
    const char* eq = std::strchr(assignment, '=');
    if (!eq) return -1;
    const std::string name(assignment, static_cast<size_t>(eq - assignment));
    return eq[1] ? setenv(name.c_str(), eq + 1, 1) : unsetenv(name.c_str());
}
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
// Wide file names are converted to UTF-8; a path::c_str() (char on Linux) is taken as it is.
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
FILE* _wfopen(const char* path, const wchar_t* mode);
int _wremove(const wchar_t* path);
int _wremove(const char* path);
int _wrename(const wchar_t* from, const wchar_t* to);
int _wrename(const char* from, const char* to);
#endif
