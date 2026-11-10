// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: the subset of the Win32 API the runtime uses, implemented on POSIX/Linux in win32_compat.cpp.
// The Windows build never sees this folder (it is added to the include path only when building for Linux), so every
// runtime file keeps its Windows code as it is. Semantics follow the Win32 documentation for the cases the runtime uses;
// anything outside those cases is not attempted.
#pragma once
#ifndef _WIN32

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <ctime>
#include <cerrno>
#include <climits>
#include <x86intrin.h>
#include "msvc_crt.h"

// ---- basic types -------------------------------------------------------------------------------------------------
typedef int BOOL;
typedef unsigned char BYTE, BOOLEAN, UCHAR, *PUCHAR;
typedef unsigned short WORD, USHORT, LANGID;
typedef int32_t LONG, INT, *PLONG;
typedef uint32_t DWORD, ULONG, UINT, UINT32, *PDWORD, *LPDWORD, *PULONG;
typedef int64_t LONGLONG, INT64;
typedef uint64_t ULONGLONG, DWORD64, ULONG64, UINT64, *PULONG64, *PDWORD64;
typedef uintptr_t ULONG_PTR, DWORD_PTR, SIZE_T, UINT_PTR, *PULONG_PTR, *PSIZE_T, *PDWORD_PTR;
typedef intptr_t LONG_PTR, INT_PTR, SSIZE_T;
typedef void *HANDLE, *PVOID, *LPVOID, *HMODULE, *HINSTANCE, *HWND, *FARPROC;
typedef const void *LPCVOID;
typedef char CHAR, *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t WCHAR, *LPWSTR, *PWSTR;
typedef const wchar_t *LPCWSTR, *PCWSTR;
typedef int32_t HRESULT, NTSTATUS;
typedef DWORD *LPDWORD;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    struct { DWORD LowPart_; LONG HighPart_; };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;
typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;

#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define NTAPI
#define APIENTRY
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))
#define MAKEWORD(a, b) (static_cast<WORD>((static_cast<BYTE>(a)) | (static_cast<WORD>(static_cast<BYTE>(b)) << 8)))
#define LOWORD(l) (static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xffff))
#define HIWORD(l) (static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xffff))
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define YieldProcessor() _mm_pause()

// ---- errors ------------------------------------------------------------------------------------------------------
#define ERROR_SUCCESS 0u
#define NO_ERROR 0u
#define ERROR_INVALID_FUNCTION 1u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_GEN_FAILURE 31u
#define ERROR_SHARING_VIOLATION 32u
#define ERROR_LOCK_VIOLATION 33u
#define ERROR_HANDLE_EOF 38u
#define ERROR_HANDLE_DISK_FULL 39u
#define ERROR_NOT_SUPPORTED 50u
#define ERROR_FILE_EXISTS 80u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_BUFFER_OVERFLOW 111u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_COMMITMENT_LIMIT 1455u
#define ERROR_TIMEOUT 1460u
DWORD GetLastError();
void SetLastError(DWORD e);
DWORD nx_win32_error_from_errno(int e);   // compat helper: errno -> ERROR_*

// ---- time --------------------------------------------------------------------------------------------------------
void Sleep(DWORD ms);
DWORD SleepEx(DWORD ms, BOOL alertable);
ULONGLONG GetTickCount64();
DWORD GetTickCount();
BOOL QueryPerformanceCounter(LARGE_INTEGER* c);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* f);
void GetSystemTimeAsFileTime(LPFILETIME ft);
void GetSystemTimePreciseAsFileTime(LPFILETIME ft);
typedef unsigned int MMRESULT;
inline MMRESULT timeBeginPeriod(UINT) { return 0; }   // Linux timers are already fine grained
inline MMRESULT timeEndPeriod(UINT) { return 0; }

// ---- processes and threads ---------------------------------------------------------------------------------------
#define CREATE_SUSPENDED 0x00000004u
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000u
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_ERROR_RETURN (0x7fffffff)
#define THREAD_ALL_ACCESS 0x001FFFFFu
#define THREAD_SUSPEND_RESUME 0x0002u
#define THREAD_GET_CONTEXT 0x0008u
#define THREAD_QUERY_INFORMATION 0x0040u
#define SYNCHRONIZE 0x00100000u
#define NORMAL_PRIORITY_CLASS 0x00000020u
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000u
#define HIGH_PRIORITY_CLASS 0x00000080u
#define DUPLICATE_SAME_ACCESS 0x00000002u
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
HANDLE GetCurrentProcess();
HANDLE GetCurrentThread();
DWORD GetCurrentProcessId();
DWORD GetCurrentThreadId();
DWORD GetCurrentProcessorNumber();
BOOL SwitchToThread();
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD tid);
DWORD ResumeThread(HANDLE t);
BOOL SetThreadPriority(HANDLE t, int prio);
int GetThreadPriority(HANDLE t);
DWORD_PTR SetThreadAffinityMask(HANDLE t, DWORD_PTR mask);
HRESULT SetThreadDescription(HANDLE t, PCWSTR name);
BOOL GetThreadTimes(HANDLE t, LPFILETIME create, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
BOOL GetProcessTimes(HANDLE p, LPFILETIME create, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
BOOL SetPriorityClass(HANDLE p, DWORD cls);
DWORD GetPriorityClass(HANDLE p);   // the class last set with SetPriorityClass (NORMAL_PRIORITY_CLASS at start)
BOOL GetExitCodeThread(HANDLE t, LPDWORD code);
[[noreturn]] void ExitThread(DWORD code);
[[noreturn]] void ExitProcess(UINT code);
BOOL CloseHandle(HANDLE h);
BOOL DuplicateHandle(HANDLE sp, HANDLE src, HANDLE tp, HANDLE* out, DWORD access, BOOL inherit, DWORD opts);
BOOL IsDebuggerPresent();
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD size);
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buf, DWORD size);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);

// ---- waiting and synchronisation ---------------------------------------------------------------------------------
#define WAIT_OBJECT_0 0u
#define WAIT_ABANDONED 0x80u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define TIMER_ALL_ACCESS 0x001F0003u
#define EVENT_ALL_ACCESS 0x001F0003u
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x00000001u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002u
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCSTR name);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCWSTR name);
#define CreateEvent CreateEventW
BOOL SetEvent(HANDLE e);
BOOL ResetEvent(HANDLE e);
HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES, LONG initial, LONG max, LPCWSTR name);
BOOL ReleaseSemaphore(HANDLE s, LONG n, PLONG prev);
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES, LPCWSTR name, DWORD flags, DWORD access);
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES, BOOL manual, LPCWSTR name);
typedef void (*PTIMERAPCROUTINE)(LPVOID, DWORD, DWORD);
BOOL SetWaitableTimer(HANDLE t, const LARGE_INTEGER* due, LONG period, PTIMERAPCROUTINE, LPVOID, BOOL resume);
BOOL SetWaitableTimerEx(HANDLE t, const LARGE_INTEGER* due, LONG period, PTIMERAPCROUTINE, LPVOID, void* reason, ULONG delay);
BOOL CancelWaitableTimer(HANDLE t);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alertable);
DWORD WaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL all, DWORD ms);

// Slim reader/writer lock and condition variable: zero-initialised like their Win32 counterparts (futex based).
typedef struct _RTL_SRWLOCK { void* Ptr; } SRWLOCK, *PSRWLOCK;
typedef struct _RTL_CONDITION_VARIABLE { void* Ptr; } CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define SRWLOCK_INIT {nullptr}
#define CONDITION_VARIABLE_INIT {nullptr}
void InitializeSRWLock(PSRWLOCK l);
void AcquireSRWLockExclusive(PSRWLOCK l);
void ReleaseSRWLockExclusive(PSRWLOCK l);
void AcquireSRWLockShared(PSRWLOCK l);
void ReleaseSRWLockShared(PSRWLOCK l);
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK l);
BOOLEAN TryAcquireSRWLockShared(PSRWLOCK l);
void InitializeConditionVariable(PCONDITION_VARIABLE c);
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE c, PSRWLOCK l, DWORD ms, ULONG flags);
void WakeConditionVariable(PCONDITION_VARIABLE c);
void WakeAllConditionVariable(PCONDITION_VARIABLE c);
typedef struct _RTL_CRITICAL_SECTION { void* impl[6]; } CRITICAL_SECTION, *LPCRITICAL_SECTION;   // recursive mutex inside
void InitializeCriticalSection(LPCRITICAL_SECTION c);
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION c, DWORD spin);
void DeleteCriticalSection(LPCRITICAL_SECTION c);
void EnterCriticalSection(LPCRITICAL_SECTION c);
void LeaveCriticalSection(LPCRITICAL_SECTION c);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION c);
BOOL SleepConditionVariableCS(PCONDITION_VARIABLE c, LPCRITICAL_SECTION cs, DWORD ms);
// Address waits (futex): sizes 1, 2, 4 and 8 like WaitOnAddress.
BOOL WaitOnAddress(volatile void* addr, PVOID compare, SIZE_T size, DWORD ms);
void WakeByAddressSingle(PVOID addr);
void WakeByAddressAll(PVOID addr);
DWORD TlsAlloc();
LPVOID TlsGetValue(DWORD i);
BOOL TlsSetValue(DWORD i, LPVOID v);
BOOL TlsFree(DWORD i);
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu

// ---- virtual memory ----------------------------------------------------------------------------------------------
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_DECOMMIT 0x00004000u
#define MEM_RELEASE 0x00008000u
#define MEM_FREE 0x00010000u
#define MEM_RESET 0x00080000u
#define MEM_TOP_DOWN 0x00100000u
#define MEM_WRITE_WATCH 0x00200000u
#define MEM_LARGE_PAGES 0x20000000u
#define MEM_PRIVATE 0x00020000u
#define MEM_MAPPED 0x00040000u
#define MEM_IMAGE 0x01000000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_EXECUTE_WRITECOPY 0x80u
#define PAGE_GUARD 0x100u
#define SEC_RESERVE 0x04000000u
#define SEC_COMMIT 0x08000000u
#define FILE_MAP_WRITE 0x0002u
#define FILE_MAP_READ 0x0004u
#define FILE_MAP_ALL_ACCESS 0x000F001Fu
#define FILE_MAP_EXECUTE 0x0020u
typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress; PVOID AllocationBase; DWORD AllocationProtect; WORD PartitionId; SIZE_T RegionSize;
    DWORD State; DWORD Protect; DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;
LPVOID VirtualAlloc(LPVOID at, SIZE_T size, DWORD type, DWORD protect);
BOOL VirtualFree(LPVOID at, SIZE_T size, DWORD type);
BOOL VirtualProtect(LPVOID at, SIZE_T size, DWORD protect, PDWORD old);
SIZE_T VirtualQuery(LPCVOID at, PMEMORY_BASIC_INFORMATION mbi, SIZE_T len);
BOOL VirtualLock(LPVOID at, SIZE_T size);
BOOL VirtualUnlock(LPVOID at, SIZE_T size);
BOOL FlushInstructionCache(HANDLE p, LPCVOID at, SIZE_T size);
HANDLE CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES, DWORD protect, DWORD size_hi, DWORD size_lo, LPCWSTR name);
HANDLE CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES, DWORD protect, DWORD size_hi, DWORD size_lo, LPCSTR name);
#define CreateFileMapping CreateFileMappingW
LPVOID MapViewOfFile(HANDLE map, DWORD access, DWORD off_hi, DWORD off_lo, SIZE_T size);
LPVOID MapViewOfFileEx(HANDLE map, DWORD access, DWORD off_hi, DWORD off_lo, SIZE_T size, LPVOID at);
BOOL UnmapViewOfFile(LPCVOID at);
BOOL FlushViewOfFile(LPCVOID at, SIZE_T size);
typedef struct _SYSTEM_INFO {
    union { DWORD dwOemId; struct { WORD wProcessorArchitecture; WORD wReserved; }; };
    DWORD dwPageSize; LPVOID lpMinimumApplicationAddress; LPVOID lpMaximumApplicationAddress; DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors; DWORD dwProcessorType; DWORD dwAllocationGranularity; WORD wProcessorLevel; WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
void GetSystemInfo(LPSYSTEM_INFO si);
void GetNativeSystemInfo(LPSYSTEM_INFO si);
typedef ULONGLONG DWORDLONG;
typedef struct _MEMORYSTATUSEX {
    DWORD dwLength; DWORD dwMemoryLoad; DWORDLONG ullTotalPhys; DWORDLONG ullAvailPhys; DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile; DWORDLONG ullTotalVirtual; DWORDLONG ullAvailVirtual; DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX m);

// ---- files -------------------------------------------------------------------------------------------------------
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define FILE_SHARE_DELETE 0x4u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_READONLY 0x1u
#define FILE_ATTRIBUTE_HIDDEN 0x2u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define MOVEFILE_REPLACE_EXISTING 0x1u
#define MOVEFILE_WRITE_THROUGH 0x8u
#define FIND_FIRST_EX_LARGE_FETCH 0x2u
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;
typedef enum _FINDEX_INFO_LEVELS { FindExInfoStandard, FindExInfoBasic } FINDEX_INFO_LEVELS;
typedef enum _FINDEX_SEARCH_OPS { FindExSearchNameMatch, FindExSearchLimitToDirectories } FINDEX_SEARCH_OPS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh; DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh;
    DWORD nFileSizeLow; DWORD dwReserved0; DWORD dwReserved1; WCHAR cFileName[MAX_PATH]; WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh;
    DWORD nFileSizeLow; DWORD dwReserved0; DWORD dwReserved1; CHAR cFileName[MAX_PATH]; CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef struct _OVERLAPPED { ULONG_PTR Internal; ULONG_PTR InternalHigh; union { struct { DWORD Offset; DWORD OffsetHigh; }; PVOID Pointer; }; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES, DWORD disp, DWORD flags, HANDLE tmpl);
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES, DWORD disp, DWORD flags, HANDLE tmpl);
BOOL ReadFile(HANDLE f, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov);
BOOL WriteFile(HANDLE f, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED ov);
BOOL FlushFileBuffers(HANDLE f);
BOOL GetFileSizeEx(HANDLE f, PLARGE_INTEGER size);
DWORD GetFileSize(HANDLE f, LPDWORD hi);
BOOL SetFilePointerEx(HANDLE f, LARGE_INTEGER dist, PLARGE_INTEGER pos, DWORD method);
BOOL SetEndOfFile(HANDLE f);
DWORD GetFileAttributesA(LPCSTR path);
DWORD GetFileAttributesW(LPCWSTR path);
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out);
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out);
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES);
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES);
BOOL RemoveDirectoryA(LPCSTR path);
BOOL RemoveDirectoryW(LPCWSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL DeleteFileW(LPCWSTR path);
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists);
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists);
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA out);
HANDLE FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW out);
HANDLE FindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS, LPVOID out, FINDEX_SEARCH_OPS, LPVOID, DWORD flags);
BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA out);
BOOL FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW out);
BOOL FindClose(HANDLE h);
DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD size);
DWORD GetModuleFileNameW(HMODULE m, LPWSTR buf, DWORD size);
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR buf, LPSTR* file_part);
DWORD GetFullPathNameW(LPCWSTR path, DWORD size, LPWSTR buf, LPWSTR* file_part);
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buf);
BOOL SetCurrentDirectoryA(LPCSTR path);
BOOL SetCurrentDirectoryW(LPCWSTR path);
DWORD GetTempPathA(DWORD size, LPSTR buf);

// ---- dynamic libraries -------------------------------------------------------------------------------------------
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800u
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4u
HMODULE LoadLibraryA(LPCSTR name);
HMODULE LoadLibraryW(LPCWSTR name);
HMODULE LoadLibraryExA(LPCSTR name, HANDLE, DWORD flags);
HMODULE LoadLibraryExW(LPCWSTR name, HANDLE, DWORD flags);
#define LoadLibrary LoadLibraryW
FARPROC GetProcAddress(HMODULE m, LPCSTR name);
BOOL FreeLibrary(HMODULE m);
HMODULE GetModuleHandleA(LPCSTR name);
HMODULE GetModuleHandleW(LPCWSTR name);
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* out);

// ---- text --------------------------------------------------------------------------------------------------------
// Wide strings are UTF-32 on Linux (wchar_t is 4 bytes); these convert UTF-8 <-> wchar_t like the Win32 calls do with CP_UTF8.
#define CP_ACP 0u
#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 0x8u
#define WC_ERR_INVALID_CHARS 0x80u
int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR in, int in_len, LPWSTR out, int out_len);
int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR in, int in_len, LPSTR out, int out_len, LPCSTR def, BOOL* used_def);

// ---- console and messages ----------------------------------------------------------------------------------------
#define MB_OK 0x0u
#define MB_ICONERROR 0x10u
#define MB_ICONWARNING 0x30u
#define MB_ICONINFORMATION 0x40u
#define MB_YESNO 0x4u
#define IDYES 6
#define IDNO 7
#define IDOK 1
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type);   // SDL message box when SDL is up, else stderr
int MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type);
void OutputDebugStringA(LPCSTR s);
void OutputDebugStringW(LPCWSTR s);
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
HANDLE GetStdHandle(DWORD which);
BOOL AttachConsole(DWORD pid);
BOOL AllocConsole();
#define ATTACH_PARENT_PROCESS ((DWORD)-1)

// ---- language ----------------------------------------------------------------------------------------------------
#define MAKELANGID(p, s) (static_cast<WORD>((static_cast<WORD>(s) << 10) | static_cast<WORD>(p)))
#define PRIMARYLANGID(l) (static_cast<WORD>(l) & 0x3ff)
#define SUBLANGID(l) (static_cast<WORD>(l) >> 10)
#define LANG_CHINESE 0x04
#define LANG_DUTCH 0x13
#define LANG_ENGLISH 0x09
#define LANG_FRENCH 0x0c
#define LANG_GERMAN 0x07
#define LANG_ITALIAN 0x10
#define LANG_JAPANESE 0x11
#define LANG_KOREAN 0x12
#define LANG_PORTUGUESE 0x16
#define LANG_RUSSIAN 0x19
#define LANG_SPANISH 0x0a
#define SUBLANG_ENGLISH_US 0x01
#define SUBLANG_ENGLISH_CAN 0x04
#define SUBLANG_FRENCH_CANADIAN 0x03
#define SUBLANG_SPANISH 0x01
#define SUBLANG_SPANISH_MODERN 0x03
#define SUBLANG_PORTUGUESE_BRAZILIAN 0x01
#define SUBLANG_CHINESE_TRADITIONAL 0x01
#define SUBLANG_CHINESE_SIMPLIFIED 0x02
#define SUBLANG_CHINESE_HONGKONG 0x03
#define SUBLANG_CHINESE_MACAU 0x05
LANGID GetUserDefaultUILanguage();   // from LANG / LC_ALL / LC_MESSAGES

// ---- exceptions (signals) ----------------------------------------------------------------------------------------
// Hardware faults arrive as signals (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP) and are turned into the same
// EXCEPTION_RECORD + CONTEXT that Windows gives a vectored handler. See win32_fault.cpp.
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_NONCONTINUABLE_EXCEPTION 0xC0000025u
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_INT_OVERFLOW 0xC0000095u
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096u
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu
#define EXCEPTION_FLT_DIVIDE_BY_ZERO 0xC000008Eu
#define EXCEPTION_FLT_INVALID_OPERATION 0xC0000090u
#define EXCEPTION_BREAKPOINT 0x80000003u
#define EXCEPTION_SINGLE_STEP 0x80000004u
#define EXCEPTION_NONCONTINUABLE 0x1u
#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_EXECUTE_HANDLER 1
#define CONTEXT_AMD64 0x00100000u
#define CONTEXT_CONTROL (CONTEXT_AMD64 | 0x1u)
#define CONTEXT_INTEGER (CONTEXT_AMD64 | 0x2u)
#define CONTEXT_SEGMENTS (CONTEXT_AMD64 | 0x4u)
#define CONTEXT_FLOATING_POINT (CONTEXT_AMD64 | 0x8u)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL (CONTEXT_FULL | CONTEXT_SEGMENTS)
// The general registers of an x86-64 thread, named as in the Win32 CONTEXT. Only these are kept (no FPU/XMM state).
typedef struct _CONTEXT {
    DWORD ContextFlags; DWORD MxCsr; WORD SegCs, SegDs, SegEs, SegFs, SegGs, SegSs; DWORD EFlags;
    DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8, R9, R10, R11, R12, R13, R14, R15, Rip;
} CONTEXT, *PCONTEXT;
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode; DWORD ExceptionFlags; struct _EXCEPTION_RECORD* ExceptionRecord; PVOID ExceptionAddress;
    DWORD NumberParameters; ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _EXCEPTION_POINTERS { PEXCEPTION_RECORD ExceptionRecord; PCONTEXT ContextRecord; } EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;
typedef LONG (*PVECTORED_EXCEPTION_HANDLER)(PEXCEPTION_POINTERS);
typedef LONG (*PTOP_LEVEL_EXCEPTION_FILTER)(PEXCEPTION_POINTERS);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER fn);
ULONG RemoveVectoredExceptionHandler(PVOID h);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER fn);
[[noreturn]] void RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR* args);
BOOL SetThreadStackGuarantee(PULONG size);   // installs this thread's signal stack (size is the reserve asked for)
BOOL TerminateProcess(HANDLE p, UINT code);
BOOL IsBadReadPtr(const void* p, UINT_PTR size);
BOOL IsBadWritePtr(LPVOID p, UINT_PTR size);
BOOL ReadProcessMemory(HANDLE p, LPCVOID from, LPVOID to, SIZE_T n, SIZE_T* got);
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD tid);
DWORD SuspendThread(HANDLE t);   // stops the thread in a signal handler until ResumeThread
BOOL GetThreadContext(HANDLE t, PCONTEXT c);   // only for a thread stopped by SuspendThread
BOOL SetThreadPriorityBoost(HANDLE, BOOL);
BOOL GetProcessHandleCount(HANDLE p, PDWORD count);
#define ERROR_DISK_FULL 112u
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL owned, LPCSTR name);
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL owned, LPCWSTR name);
BOOL ReleaseMutex(HANDLE m);

// ---- paths given as std::filesystem::path::c_str() ----------------------------------------------------------------
// On Windows path::c_str() is wide; on Linux it is UTF-8 char. These let the W calls take either.
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl);
inline BOOL DeleteFileW(const char* p) { return DeleteFileA(p); }
inline DWORD GetFileAttributesW(const char* p) { return GetFileAttributesA(p); }
inline BOOL GetFileAttributesExW(const char* p, GET_FILEEX_INFO_LEVELS l, LPVOID out) { return GetFileAttributesExA(p, l, out); }
inline BOOL CreateDirectoryW(const char* p, LPSECURITY_ATTRIBUTES sa) { return CreateDirectoryA(p, sa); }
inline BOOL RemoveDirectoryW(const char* p) { return RemoveDirectoryA(p); }
inline BOOL MoveFileExW(const char* a, const char* b, DWORD f) { return MoveFileExA(a, b, f); }
inline BOOL CopyFileW(const char* a, const char* b, BOOL f) { return CopyFileA(a, b, f); }

// ---- more processes, threads and system information ---------------------------------------------------------------
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);   // waits on it say whether the process is still running
BOOL QueryThreadCycleTime(HANDLE t, PULONG64 cycles);        // CPU time of the thread in nanoseconds (Linux has no cycle count)
#define GR_GDIOBJECTS 0u
#define GR_USEROBJECTS 1u
inline DWORD GetGuiResources(HANDLE, DWORD) { return 0; }   // no GDI/USER objects on Linux
typedef struct _PROCESSOR_NUMBER { WORD Group; BYTE Number; BYTE Reserved; } PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;
void GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER pn);
typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP { RelationProcessorCore = 0, RelationNumaNode = 1, RelationCache = 2, RelationProcessorPackage = 3,
                                               RelationGroup = 4, RelationAll = 0xffff } LOGICAL_PROCESSOR_RELATIONSHIP;
typedef ULONG_PTR KAFFINITY;
typedef struct _GROUP_AFFINITY { KAFFINITY Mask; WORD Group; WORD Reserved[3]; } GROUP_AFFINITY, *PGROUP_AFFINITY;
typedef struct _PROCESSOR_RELATIONSHIP { BYTE Flags; BYTE EfficiencyClass; BYTE Reserved[20]; WORD GroupCount; GROUP_AFFINITY GroupMask[1]; } PROCESSOR_RELATIONSHIP;
typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship; DWORD Size;
    union { PROCESSOR_RELATIONSHIP Processor; BYTE Reserved_[76]; };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
// RelationProcessorCore only, from /sys/devices/system/cpu: one record per physical core with its logical CPUs. EfficiencyClass
// ranks the cores by their maximum frequency (performance cores of a hybrid CPU get the higher class, as on Windows).
BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP rel, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buf, PDWORD len);
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR out, DWORD size, LPCSTR file);

// ---- registry, process creation, power and affinity ----------------------------------------------------------------
// Registry: only HKLM\HARDWARE\DESCRIPTION\System\BIOS is answered, from /sys/class/dmi/id (the data Wine fills it with).
typedef struct HKEY__* HKEY;
typedef LONG LSTATUS;
#define HKEY_LOCAL_MACHINE (reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000002)))
#define HKEY_CURRENT_USER (reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000001)))
#define RRF_RT_REG_SZ 0x00000002u
#define RRF_RT_REG_DWORD 0x00000010u
LSTATUS RegGetValueA(HKEY key, LPCSTR sub, LPCSTR value, DWORD flags, LPDWORD type, PVOID data, LPDWORD size);
// CreateProcessW: the runtime only starts Windows helpers (steam.exe, winebrowser.exe), which don't exist on Linux; it then falls
// back to ShellExecute (xdg-open).
typedef struct _STARTUPINFOW { DWORD cb; LPWSTR lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2; BYTE* lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOW, *LPSTARTUPINFOW;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess; HANDLE hThread; DWORD dwProcessId; DWORD dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
#define CREATE_NO_WINDOW 0x08000000u
inline BOOL CreateProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) {
    SetLastError(ERROR_FILE_NOT_FOUND); return FALSE;
}
BOOL GetProcessAffinityMask(HANDLE p, PDWORD_PTR proc, PDWORD_PTR sys);
BOOL GetThreadIdealProcessorEx(HANDLE t, PPROCESSOR_NUMBER pn);   // Linux has no ideal processor: the one the thread runs on now
// The scheduler has no ideal-processor hint: the request is accepted and the affinity mask alone steers the thread.
inline BOOL SetThreadIdealProcessorEx(HANDLE t, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER prev) {
    return prev ? GetThreadIdealProcessorEx(t, prev) : TRUE;
}
// Power throttling (EcoQoS) is a Windows scheduler feature; SetProcessInformation reports it as not supported.
typedef enum _PROCESS_INFORMATION_CLASS { ProcessMemoryPriority, ProcessMemoryExhaustionInfo, ProcessAppMemoryInfo, ProcessInPrivateInfo,
                                          ProcessPowerThrottling } PROCESS_INFORMATION_CLASS;
typedef struct _PROCESS_POWER_THROTTLING_STATE { ULONG Version; ULONG ControlMask; ULONG StateMask; } PROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
inline BOOL SetProcessInformation(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }

#endif // !_WIN32
