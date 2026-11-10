// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: the Win32 subset declared in compat/linux/*.h, on POSIX and Linux system calls.
#ifndef _WIN32
#include "windows.h"
#include "psapi.h"
#include "tlhelp32.h"
#include "compat_internal.h"
#include "iphlpapi.h"
#include "shellapi.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

// ---- errors ------------------------------------------------------------------------------------------------------
thread_local DWORD t_last_error = 0;

// ---- handles -----------------------------------------------------------------------------------------------------
// Every HANDLE the compat layer gives out points at one of these. The pseudo handles (-1 process, -2 thread) never do.
enum class Kind { File, Thread, Event, Semaphore, Timer, Mapping, Find, Std, Mutex, Snapshot, Process };
struct Handle {
    Kind kind;
    explicit Handle(Kind k) : kind(k) {}
    virtual ~Handle() = default;
};
struct FileH : Handle { int fd; explicit FileH(int f) : Handle(Kind::File), fd(f) {} ~FileH() override { if (fd >= 0) ::close(fd); } };
struct WaitH : Handle {   // events, semaphores, timers and threads share one waitable state
    std::mutex m; pthread_cond_t cv; bool manual = true; long count = 0; long max = 1;
    explicit WaitH(Kind k) : Handle(k) {
        pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
        pthread_cond_init(&cv, &a); pthread_condattr_destroy(&a);
    }
    ~WaitH() override { pthread_cond_destroy(&cv); }
};
struct ThreadH : WaitH {
    pthread_t t{}; LPTHREAD_START_ROUTINE fn = nullptr; LPVOID arg = nullptr; DWORD exit_code = 259; std::atomic<int> tid{0};
    bool suspended = false; std::atomic<int> refs{2};   // the caller's handle + the running thread
    ThreadH() : WaitH(Kind::Thread) { manual = true; max = 1; }
};
struct TimerH : WaitH {
    timer_t timer{}; bool armed = false;
    TimerH() : WaitH(Kind::Timer) {}
    ~TimerH() override { if (armed) timer_delete(timer); }
};
struct MappingH : Handle { int fd; uint64_t size; explicit MappingH(int f, uint64_t s) : Handle(Kind::Mapping), fd(f), size(s) {} ~MappingH() override { ::close(fd); } };
struct ProcessH : WaitH { pid_t pid; explicit ProcessH(pid_t p) : WaitH(Kind::Process), pid(p) {} };
struct SnapshotH : Handle { std::vector<DWORD> tids; size_t next = 0; SnapshotH() : Handle(Kind::Snapshot) {} };
// A named mutex is a lock file held with flock: another process asking for the same name finds it taken (ERROR_ALREADY_EXISTS),
// which is how the runtime spreads several instances over the CPU. Unnamed ones are an in-process lock.
struct MutexH : WaitH { int fd = -1; MutexH() : WaitH(Kind::Mutex) { manual = false; count = 1; max = 1; } ~MutexH() override { if (fd >= 0) ::close(fd); } };
struct FindH : Handle {
    std::vector<std::string> names; std::string dir; size_t next = 0;
    FindH() : Handle(Kind::Find) {}
};

HANDLE as_handle(Handle* h) { return static_cast<HANDLE>(h); }
Handle* of(HANDLE h) {
    auto v = reinterpret_cast<intptr_t>(h);
    if (v == -1 || v == -2 || v == 0) return nullptr;
    return static_cast<Handle*>(h);
}

timespec deadline_after(DWORD ms) {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += ms / 1000; t.tv_nsec += static_cast<long>(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return t;
}

// ---- text --------------------------------------------------------------------------------------------------------
std::string to_utf8(LPCWSTR w, int n = -1) {
    if (!w) return {};
    std::string out;
    for (int i = 0; n < 0 ? w[i] != 0 : i < n; ++i) {
        auto c = static_cast<uint32_t>(w[i]);
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}
// Decodes UTF-8; returns false on a malformed sequence.
bool from_utf8(const char* s, int n, std::wstring& out) {
    out.clear();
    int len = n < 0 ? static_cast<int>(std::strlen(s)) : n;
    for (int i = 0; i < len;) {
        auto b = static_cast<unsigned char>(s[i]);
        uint32_t c; int extra;
        if (b < 0x80) { c = b; extra = 0; }
        else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
        else return false;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= len) return false;
            auto cb = static_cast<unsigned char>(s[i + k]);
            if ((cb & 0xC0) != 0x80) return false;
            c = (c << 6) | (cb & 0x3F);
        }
        out += static_cast<wchar_t>(c);
        i += extra + 1;
    }
    return true;
}
// Win32 paths may use backslashes; Linux needs forward slashes.
std::string fix_path(std::string p) { for (auto& ch : p) if (ch == '\\') ch = '/'; return p; }

void unix_to_filetime(int64_t ns_since_1970, LPFILETIME ft) {
    uint64_t v = static_cast<uint64_t>(ns_since_1970 / 100) + 116444736000000000ULL;   // 100 ns ticks since 1601
    ft->dwLowDateTime = static_cast<DWORD>(v); ft->dwHighDateTime = static_cast<DWORD>(v >> 32);
}
void ns_to_filetime_span(int64_t ns, LPFILETIME ft) {
    uint64_t v = static_cast<uint64_t>(ns / 100); ft->dwLowDateTime = static_cast<DWORD>(v); ft->dwHighDateTime = static_cast<DWORD>(v >> 32);
}

int prot_of(DWORD p) {
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE: case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE: return PROT_EXEC;
    case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_READ | PROT_WRITE;
    }
}

long futex(volatile void* addr, int op, uint32_t val, const timespec* ts) {
    return syscall(SYS_futex, addr, op, val, ts, nullptr, 0);
}

} // namespace

// ---- errors ------------------------------------------------------------------------------------------------------
DWORD GetLastError() { return t_last_error; }
void SetLastError(DWORD e) { t_last_error = e; }
DWORD nx_win32_error_from_errno(int e) {
    switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
    case EBADF: return ERROR_INVALID_HANDLE;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case ENOSPC: return ERROR_HANDLE_DISK_FULL;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case EBUSY: return ERROR_SHARING_VIOLATION;
    case ETIMEDOUT: return ERROR_TIMEOUT;
    case ENOTSUP: return ERROR_NOT_SUPPORTED;
    default: return ERROR_GEN_FAILURE;
    }
}
static BOOL fail_errno() { t_last_error = nx_win32_error_from_errno(errno); return FALSE; }

// ---- time --------------------------------------------------------------------------------------------------------
void Sleep(DWORD ms) {
    if (ms == 0) { sched_yield(); return; }
    timespec t{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}
DWORD SleepEx(DWORD ms, BOOL) { Sleep(ms); return 0; }
ULONGLONG GetTickCount64() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return static_cast<ULONGLONG>(t.tv_sec) * 1000 + t.tv_nsec / 1000000; }
DWORD GetTickCount() { return static_cast<DWORD>(GetTickCount64()); }
BOOL QueryPerformanceCounter(LARGE_INTEGER* c) { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); c->QuadPart = static_cast<LONGLONG>(t.tv_sec) * 1000000000LL + t.tv_nsec; return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000LL; return TRUE; }
void GetSystemTimeAsFileTime(LPFILETIME ft) { timespec t; clock_gettime(CLOCK_REALTIME, &t); unix_to_filetime(static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec, ft); }
void GetSystemTimePreciseAsFileTime(LPFILETIME ft) { GetSystemTimeAsFileTime(ft); }

// ---- processes and threads ---------------------------------------------------------------------------------------
HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)); }
HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2)); }
DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
DWORD GetCurrentThreadId() { return static_cast<DWORD>(syscall(SYS_gettid)); }
DWORD GetCurrentProcessorNumber() { int c = sched_getcpu(); return c < 0 ? 0 : static_cast<DWORD>(c); }
BOOL SwitchToThread() { sched_yield(); return TRUE; }

static void* thread_entry(void* p) {
    auto* t = static_cast<ThreadH*>(p);
    t->tid = static_cast<int>(syscall(SYS_gettid));
    nx_fault_thread_setup();
    {
        std::unique_lock<std::mutex> g(t->m);   // CREATE_SUSPENDED: wait for ResumeThread
        while (t->suspended) { g.unlock(); Sleep(1); g.lock(); }
    }
    DWORD code = t->fn(t->arg);
    {
        std::lock_guard<std::mutex> g(t->m);
        t->exit_code = code; t->count = 1;
        pthread_cond_broadcast(&t->cv);
    }
    if (--t->refs == 0) delete t;
    return nullptr;
}
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD tid) {
    auto* t = new ThreadH(); t->fn = fn; t->arg = arg; t->suspended = (flags & CREATE_SUSPENDED) != 0;
    pthread_attr_t a; pthread_attr_init(&a);
    if (stack) pthread_attr_setstacksize(&a, stack < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : stack);
    int r = pthread_create(&t->t, &a, thread_entry, t);
    pthread_attr_destroy(&a);
    if (r != 0) { delete t; t_last_error = nx_win32_error_from_errno(r); return nullptr; }
    pthread_detach(t->t);
    while (t->tid == 0 && !t->suspended) sched_yield();
    if (tid) *tid = static_cast<DWORD>(t->tid.load());
    return as_handle(t);
}
DWORD ResumeThread(HANDLE h) {
    auto* t = dynamic_cast<ThreadH*>(of(h));
    if (!t) return static_cast<DWORD>(-1);
    DWORD prev = 0;
    if (nx_fault_resume(t->tid.load(), &prev)) return prev;   // a SuspendThread stop
    std::lock_guard<std::mutex> g(t->m);
    DWORD was = t->suspended ? 1 : 0; t->suspended = false; return was;
}
static pid_t tid_of(HANDLE h) {
    if (reinterpret_cast<intptr_t>(h) == -2) return static_cast<pid_t>(syscall(SYS_gettid));
    auto* t = dynamic_cast<ThreadH*>(of(h));
    return t ? t->tid.load() : 0;
}
// Win32 priorities map to nice values for this thread (Linux schedules threads by nice without extra rights).
BOOL SetThreadPriority(HANDLE h, int prio) {
    pid_t tid = tid_of(h); if (!tid) return FALSE;
    int nice = prio >= THREAD_PRIORITY_TIME_CRITICAL ? -10 : prio <= THREAD_PRIORITY_IDLE ? 19 : -prio * 2;
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice) != 0 && nice < 0) setpriority(PRIO_PROCESS, static_cast<id_t>(tid), 0);
    return TRUE;
}
int GetThreadPriority(HANDLE h) {
    pid_t tid = tid_of(h); if (!tid) return THREAD_PRIORITY_ERROR_RETURN;
    errno = 0; int n = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
    return errno ? THREAD_PRIORITY_NORMAL : -n / 2;
}
DWORD_PTR SetThreadAffinityMask(HANDLE h, DWORD_PTR mask) {
    pid_t tid = tid_of(h); if (!tid) return 0;
    cpu_set_t old, s; CPU_ZERO(&s); sched_getaffinity(tid, sizeof(old), &old);
    for (int i = 0; i < 64; ++i) if (mask & (DWORD_PTR(1) << i)) CPU_SET(i, &s);
    if (sched_setaffinity(tid, sizeof(s), &s) != 0) return 0;
    DWORD_PTR prev = 0; for (int i = 0; i < 64; ++i) if (CPU_ISSET(i, &old)) prev |= DWORD_PTR(1) << i;
    return prev ? prev : 1;
}
HRESULT SetThreadDescription(HANDLE h, PCWSTR name) {
    std::string n = to_utf8(name).substr(0, 15);
    if (reinterpret_cast<intptr_t>(h) == -2) { pthread_setname_np(pthread_self(), n.c_str()); return S_OK; }
    auto* t = dynamic_cast<ThreadH*>(of(h)); if (t && t->fn) pthread_setname_np(t->t, n.c_str());
    return S_OK;
}
static BOOL times_of(clockid_t clk, LPFILETIME kernel, LPFILETIME user) {
    timespec ts; if (clock_gettime(clk, &ts) != 0) return fail_errno();
    int64_t ns = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    if (kernel) ns_to_filetime_span(0, kernel);   // CPU clocks give user+kernel together; reported as user time
    if (user) ns_to_filetime_span(ns, user);
    return TRUE;
}
BOOL GetThreadTimes(HANDLE h, LPFILETIME create, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
    if (create) ns_to_filetime_span(0, create);
    if (exit) ns_to_filetime_span(0, exit);
    if (reinterpret_cast<intptr_t>(h) == -2) return times_of(CLOCK_THREAD_CPUTIME_ID, kernel, user);
    auto* t = dynamic_cast<ThreadH*>(of(h)); if (!t) return FALSE;
    clockid_t c; if (pthread_getcpuclockid(t->t, &c) != 0) return FALSE;
    return times_of(c, kernel, user);
}
BOOL GetProcessTimes(HANDLE, LPFILETIME create, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
    rusage r; getrusage(RUSAGE_SELF, &r);
    if (create) ns_to_filetime_span(0, create);
    if (exit) ns_to_filetime_span(0, exit);
    if (kernel) ns_to_filetime_span(static_cast<int64_t>(r.ru_stime.tv_sec) * 1000000000LL + r.ru_stime.tv_usec * 1000LL, kernel);
    if (user) ns_to_filetime_span(static_cast<int64_t>(r.ru_utime.tv_sec) * 1000000000LL + r.ru_utime.tv_usec * 1000LL, user);
    return TRUE;
}
// Process priority stays with the desktop (nice is per thread); the class is only remembered for GetPriorityClass.
std::atomic<DWORD> g_priority_class{NORMAL_PRIORITY_CLASS};
BOOL SetPriorityClass(HANDLE, DWORD cls) { g_priority_class = cls; return TRUE; }
DWORD GetPriorityClass(HANDLE) { return g_priority_class; }
BOOL GetExitCodeThread(HANDLE h, LPDWORD code) {
    auto* t = dynamic_cast<ThreadH*>(of(h)); if (!t) return FALSE;
    std::lock_guard<std::mutex> g(t->m); *code = t->exit_code; return TRUE;
}
void ExitThread(DWORD) { pthread_exit(nullptr); }
void ExitProcess(UINT code) { std::exit(static_cast<int>(code)); }
BOOL CloseHandle(HANDLE h) {
    Handle* p = of(h); if (!p) return reinterpret_cast<intptr_t>(h) < 0 ? TRUE : FALSE;
    if (p->kind == Kind::Std) return TRUE;
    if (auto* t = dynamic_cast<ThreadH*>(p)) { if (--t->refs == 0) delete t; return TRUE; }
    delete p; return TRUE;
}
BOOL DuplicateHandle(HANDLE, HANDLE src, HANDLE, HANDLE* out, DWORD, BOOL, DWORD) {
    // Only used to turn the current-thread pseudo handle into a real one that other threads can use.
    if (reinterpret_cast<intptr_t>(src) == -2) {
        auto* t = new ThreadH(); t->t = pthread_self(); t->tid = static_cast<int>(syscall(SYS_gettid)); t->refs = 1;
        *out = as_handle(t); return TRUE;
    }
    if (auto* t = dynamic_cast<ThreadH*>(of(src))) { ++t->refs; *out = src; return TRUE; }
    t_last_error = ERROR_NOT_SUPPORTED; return FALSE;
}
BOOL IsDebuggerPresent() {
    FILE* f = std::fopen("/proc/self/status", "r"); if (!f) return FALSE;
    char line[256]; int pid = 0;
    while (std::fgets(line, sizeof line, f)) if (std::sscanf(line, "TracerPid: %d", &pid) == 1) break;
    std::fclose(f); return pid != 0;
}
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD size) {
    const char* v = std::getenv(name); if (!v) { t_last_error = 203; return 0; }   // ERROR_ENVVAR_NOT_FOUND
    DWORD n = static_cast<DWORD>(std::strlen(v));
    if (!buf || size <= n) return n + 1;
    std::memcpy(buf, v, n + 1); return n;
}
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buf, DWORD size) {
    const char* v = std::getenv(to_utf8(name).c_str()); if (!v) { t_last_error = 203; return 0; }
    std::wstring w; from_utf8(v, -1, w);
    DWORD n = static_cast<DWORD>(w.size());
    if (!buf || size <= n) return n + 1;
    std::wmemcpy(buf, w.c_str(), n + 1); return n;
}
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) { return (value ? setenv(name, value, 1) : unsetenv(name)) == 0; }

// ---- waiting and synchronisation ---------------------------------------------------------------------------------
static HANDLE make_wait(Kind k, bool manual, long count, long max) {
    auto* w = new WaitH(k); w->manual = manual; w->count = count; w->max = max; return as_handle(w);
}
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCSTR) { return make_wait(Kind::Event, manual, initial ? 1 : 0, 1); }
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCWSTR) { return make_wait(Kind::Event, manual, initial ? 1 : 0, 1); }
BOOL SetEvent(HANDLE h) {
    auto* w = dynamic_cast<WaitH*>(of(h)); if (!w) return FALSE;
    std::lock_guard<std::mutex> g(w->m); w->count = 1; pthread_cond_broadcast(&w->cv); return TRUE;
}
BOOL ResetEvent(HANDLE h) {
    auto* w = dynamic_cast<WaitH*>(of(h)); if (!w) return FALSE;
    std::lock_guard<std::mutex> g(w->m); w->count = 0; return TRUE;
}
HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES, LONG initial, LONG max, LPCWSTR) { return make_wait(Kind::Semaphore, false, initial, max); }
BOOL ReleaseSemaphore(HANDLE h, LONG n, PLONG prev) {
    auto* w = dynamic_cast<WaitH*>(of(h)); if (!w) return FALSE;
    std::lock_guard<std::mutex> g(w->m);
    if (prev) *prev = static_cast<LONG>(w->count);
    if (w->count + n > w->max) { t_last_error = 298; return FALSE; }   // ERROR_TOO_MANY_POSTS
    w->count += n; pthread_cond_broadcast(&w->cv); return TRUE;
}
static void timer_fired(union sigval v) {
    auto* t = static_cast<TimerH*>(v.sival_ptr);
    std::lock_guard<std::mutex> g(t->m); t->count = 1; pthread_cond_broadcast(&t->cv);
}
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD flags, DWORD) {
    auto* t = new TimerH(); t->manual = (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0;
    sigevent se{}; se.sigev_notify = SIGEV_THREAD; se.sigev_notify_function = timer_fired; se.sigev_value.sival_ptr = t;
    if (timer_create(CLOCK_MONOTONIC, &se, &t->timer) != 0) { delete t; fail_errno(); return nullptr; }
    t->armed = true; return as_handle(t);
}
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCWSTR name) {
    return CreateWaitableTimerExW(sa, name, manual ? CREATE_WAITABLE_TIMER_MANUAL_RESET : 0, TIMER_ALL_ACCESS);
}
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER* due, LONG period, PTIMERAPCROUTINE, LPVOID, BOOL) {
    auto* t = dynamic_cast<TimerH*>(of(h)); if (!t) return FALSE;
    { std::lock_guard<std::mutex> g(t->m); t->count = 0; }
    itimerspec its{}; int fl = 0;
    LONGLONG d = due->QuadPart;   // negative: relative, in 100 ns; positive: absolute FILETIME
    if (d < 0) { LONGLONG ns = -d * 100; its.it_value.tv_sec = ns / 1000000000LL; its.it_value.tv_nsec = ns % 1000000000LL; }
    else {
        FILETIME now; GetSystemTimeAsFileTime(&now);
        LONGLONG nowv = (static_cast<LONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
        LONGLONG ns = d > nowv ? (d - nowv) * 100 : 1;
        its.it_value.tv_sec = ns / 1000000000LL; its.it_value.tv_nsec = ns % 1000000000LL;
    }
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) its.it_value.tv_nsec = 1;
    if (period > 0) { its.it_interval.tv_sec = period / 1000; its.it_interval.tv_nsec = (period % 1000) * 1000000L; }
    return timer_settime(t->timer, fl, &its, nullptr) == 0 ? TRUE : fail_errno();
}
BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER* due, LONG period, PTIMERAPCROUTINE fn, LPVOID arg, void*, ULONG) {
    return SetWaitableTimer(h, due, period, fn, arg, FALSE);
}
BOOL CancelWaitableTimer(HANDLE h) {
    auto* t = dynamic_cast<TimerH*>(of(h)); if (!t) return FALSE;
    itimerspec its{}; timer_settime(t->timer, 0, &its, nullptr); return TRUE;
}
static DWORD wait_one(WaitH* w, DWORD ms) {
    std::unique_lock<std::mutex> g(w->m);
    timespec dl = deadline_after(ms == INFINITE ? 0 : ms);
    while (w->count == 0) {
        if (ms == 0) return WAIT_TIMEOUT;
        int r = ms == INFINITE ? pthread_cond_wait(&w->cv, g.mutex()->native_handle())
                               : pthread_cond_timedwait(&w->cv, g.mutex()->native_handle(), &dl);
        if (r == ETIMEDOUT && w->count == 0) return WAIT_TIMEOUT;
    }
    if (!w->manual && w->kind != Kind::Thread) w->count--;
    return WAIT_OBJECT_0;
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    auto* w = dynamic_cast<WaitH*>(of(h)); if (!w) { t_last_error = ERROR_INVALID_HANDLE; return WAIT_FAILED; }
    if (auto* p = dynamic_cast<ProcessH*>(w)) {   // polled: only used to ask whether another instance is still running
        ULONGLONG end = ms == INFINITE ? ~0ULL : GetTickCount64() + ms;
        for (;;) {
            if (kill(p->pid, 0) != 0 && errno == ESRCH) return WAIT_OBJECT_0;
            if (GetTickCount64() >= end) return WAIT_TIMEOUT;
            Sleep(ms == 0 ? 0 : 10);
            if (ms == 0) return WAIT_TIMEOUT;
        }
    }
    return wait_one(w, ms);
}
DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL) { return WaitForSingleObject(h, ms); }
DWORD WaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL all, DWORD ms) {
    // Polls in 1 ms steps: the runtime waits on a handful of handles at shutdown and in tooling, never in a frame.
    ULONGLONG end = ms == INFINITE ? ~0ULL : GetTickCount64() + ms;
    for (;;) {
        DWORD ready = 0;
        for (DWORD i = 0; i < n; ++i) {
            if (wait_one(dynamic_cast<WaitH*>(of(hs[i])), 0) == WAIT_OBJECT_0) { if (!all) return WAIT_OBJECT_0 + i; ++ready; }
        }
        if (all && ready == n) return WAIT_OBJECT_0;
        if (GetTickCount64() >= end) return WAIT_TIMEOUT;
        Sleep(1);
    }
}

// SRW lock: one 32-bit futex word in Ptr. Bit 31 = writer, low bits = readers. Writers spin then sleep on the word.
static std::atomic<uint32_t>& srw_word(PSRWLOCK l) { return *reinterpret_cast<std::atomic<uint32_t>*>(&l->Ptr); }
static constexpr uint32_t kWriter = 0x80000000u;
void InitializeSRWLock(PSRWLOCK l) { l->Ptr = nullptr; }
void AcquireSRWLockExclusive(PSRWLOCK l) {
    auto& w = srw_word(l);
    for (int spin = 0;; ++spin) {
        uint32_t v = 0;
        if (w.compare_exchange_weak(v, kWriter, std::memory_order_acquire)) return;
        if (spin < 64) { _mm_pause(); continue; }
        futex(&w, FUTEX_WAIT_PRIVATE, v, nullptr);
    }
}
void ReleaseSRWLockExclusive(PSRWLOCK l) { auto& w = srw_word(l); w.store(0, std::memory_order_release); futex(&w, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr); }
void AcquireSRWLockShared(PSRWLOCK l) {
    auto& w = srw_word(l);
    for (int spin = 0;; ++spin) {
        uint32_t v = w.load(std::memory_order_relaxed);
        if (!(v & kWriter) && w.compare_exchange_weak(v, v + 1, std::memory_order_acquire)) return;
        if (spin < 64) { _mm_pause(); continue; }
        if (v & kWriter) futex(&w, FUTEX_WAIT_PRIVATE, v, nullptr);
    }
}
void ReleaseSRWLockShared(PSRWLOCK l) { auto& w = srw_word(l); if (w.fetch_sub(1, std::memory_order_release) == 1) futex(&w, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr); }
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK l) { uint32_t v = 0; return srw_word(l).compare_exchange_strong(v, kWriter, std::memory_order_acquire); }
BOOLEAN TryAcquireSRWLockShared(PSRWLOCK l) { auto& w = srw_word(l); uint32_t v = w.load(); return !(v & kWriter) && w.compare_exchange_strong(v, v + 1, std::memory_order_acquire); }

// Condition variable: a 32-bit sequence word; a waiter reads it before releasing the lock, so a wake in between is never lost.
static std::atomic<uint32_t>& cv_word(PCONDITION_VARIABLE c) { return *reinterpret_cast<std::atomic<uint32_t>*>(&c->Ptr); }
void InitializeConditionVariable(PCONDITION_VARIABLE c) { c->Ptr = nullptr; }
static BOOL cv_wait(PCONDITION_VARIABLE c, DWORD ms, void (*unlock)(void*), void (*lock)(void*), void* l) {
    auto& w = cv_word(c); uint32_t seq = w.load(std::memory_order_acquire);
    unlock(l);
    timespec rel{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
    long r = futex(&w, FUTEX_WAIT_PRIVATE, seq, ms == INFINITE ? nullptr : &rel);
    int e = errno;
    lock(l);
    if (r != 0 && e == ETIMEDOUT) { t_last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE c, PSRWLOCK l, DWORD ms, ULONG flags) {
    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
        return cv_wait(c, ms, [](void* p) { ReleaseSRWLockShared(static_cast<PSRWLOCK>(p)); }, [](void* p) { AcquireSRWLockShared(static_cast<PSRWLOCK>(p)); }, l);
    return cv_wait(c, ms, [](void* p) { ReleaseSRWLockExclusive(static_cast<PSRWLOCK>(p)); }, [](void* p) { AcquireSRWLockExclusive(static_cast<PSRWLOCK>(p)); }, l);
}
void WakeConditionVariable(PCONDITION_VARIABLE c) { auto& w = cv_word(c); w.fetch_add(1, std::memory_order_release); futex(&w, FUTEX_WAKE_PRIVATE, 1, nullptr); }
void WakeAllConditionVariable(PCONDITION_VARIABLE c) { auto& w = cv_word(c); w.fetch_add(1, std::memory_order_release); futex(&w, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr); }

static_assert(sizeof(pthread_mutex_t) <= sizeof(CRITICAL_SECTION), "CRITICAL_SECTION holds a pthread mutex");
static pthread_mutex_t* cs_mutex(LPCRITICAL_SECTION c) { return reinterpret_cast<pthread_mutex_t*>(c); }
void InitializeCriticalSection(LPCRITICAL_SECTION c) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs_mutex(c), &a); pthread_mutexattr_destroy(&a);
}
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION c, DWORD) { InitializeCriticalSection(c); return TRUE; }
void DeleteCriticalSection(LPCRITICAL_SECTION c) { pthread_mutex_destroy(cs_mutex(c)); }
void EnterCriticalSection(LPCRITICAL_SECTION c) { pthread_mutex_lock(cs_mutex(c)); }
void LeaveCriticalSection(LPCRITICAL_SECTION c) { pthread_mutex_unlock(cs_mutex(c)); }
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION c) { return pthread_mutex_trylock(cs_mutex(c)) == 0; }
BOOL SleepConditionVariableCS(PCONDITION_VARIABLE c, LPCRITICAL_SECTION cs, DWORD ms) {
    return cv_wait(c, ms, [](void* p) { LeaveCriticalSection(static_cast<LPCRITICAL_SECTION>(p)); }, [](void* p) { EnterCriticalSection(static_cast<LPCRITICAL_SECTION>(p)); }, cs);
}

// WaitOnAddress: futexes watch 32-bit words, so smaller or larger values wait on their aligned 32-bit word and
// re-check the full value after each wake (a wake is also sent to that word).
static volatile uint32_t* word_of(volatile void* a) { return reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(a) & ~uintptr_t(3)); }
BOOL WaitOnAddress(volatile void* addr, PVOID compare, SIZE_T size, DWORD ms) {
    auto same = [&] { return std::memcmp(const_cast<void*>(addr), compare, size) == 0; };
    if (!same()) return TRUE;
    volatile uint32_t* w = word_of(addr);
    uint32_t seen = *w;
    if (!same()) return TRUE;
    timespec rel{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
    long r = futex(w, FUTEX_WAIT_PRIVATE, seen, ms == INFINITE ? nullptr : &rel);
    if (r != 0 && errno == ETIMEDOUT) { t_last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
void WakeByAddressSingle(PVOID addr) { futex(word_of(addr), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr); }   // all: sub-word waiters share a word
void WakeByAddressAll(PVOID addr) { futex(word_of(addr), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr); }

DWORD TlsAlloc() { pthread_key_t k; return pthread_key_create(&k, nullptr) == 0 ? static_cast<DWORD>(k) : TLS_OUT_OF_INDEXES; }
LPVOID TlsGetValue(DWORD i) { t_last_error = 0; return pthread_getspecific(static_cast<pthread_key_t>(i)); }
BOOL TlsSetValue(DWORD i, LPVOID v) { return pthread_setspecific(static_cast<pthread_key_t>(i), v) == 0; }
BOOL TlsFree(DWORD i) { return pthread_key_delete(static_cast<pthread_key_t>(i)) == 0; }

// ---- virtual memory ----------------------------------------------------------------------------------------------
// Reserve = PROT_NONE mapping; commit = mprotect to the requested access (pages are zero-filled on first touch, as on Windows).
LPVOID VirtualAlloc(LPVOID at, SIZE_T size, DWORD type, DWORD protect) {
    int prot = (type & MEM_COMMIT) ? prot_of(protect) : PROT_NONE;
    if (at && (type & MEM_COMMIT) && !(type & MEM_RESERVE)) {   // commit inside an existing reservation
        uintptr_t a = reinterpret_cast<uintptr_t>(at) & ~uintptr_t(4095);
        size_t len = ((reinterpret_cast<uintptr_t>(at) + size + 4095) & ~uintptr_t(4095)) - a;
        if (mprotect(reinterpret_cast<void*>(a), len, prot) != 0) { fail_errno(); return nullptr; }
        return at;
    }
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    if (at) flags |= MAP_FIXED_NOREPLACE;
    void* p = mmap(at, size, prot, flags, -1, 0);
    if (p == MAP_FAILED) { fail_errno(); return nullptr; }
    if (at && p != at) { munmap(p, size); t_last_error = ERROR_INVALID_PARAMETER; return nullptr; }
    return p;
}
BOOL VirtualFree(LPVOID at, SIZE_T size, DWORD type) {
    if (type & MEM_RELEASE) {
        // Win32 releases the whole reservation with size 0; Linux needs a length, so look the mapping up.
        if (size == 0) {
            MEMORY_BASIC_INFORMATION m; if (!VirtualQuery(at, &m, sizeof m)) return FALSE;
            size = m.RegionSize;
        }
        return munmap(at, size) == 0 ? TRUE : fail_errno();
    }
    if (type & MEM_DECOMMIT) {
        if (madvise(at, size, MADV_DONTNEED) != 0) return fail_errno();
        return mprotect(at, size, PROT_NONE) == 0 ? TRUE : fail_errno();
    }
    return TRUE;
}
BOOL VirtualProtect(LPVOID at, SIZE_T size, DWORD protect, PDWORD old) {
    if (old) { MEMORY_BASIC_INFORMATION m; *old = VirtualQuery(at, &m, sizeof m) ? m.Protect : PAGE_READWRITE; }
    uintptr_t a = reinterpret_cast<uintptr_t>(at) & ~uintptr_t(4095);
    size_t len = ((reinterpret_cast<uintptr_t>(at) + size + 4095) & ~uintptr_t(4095)) - a;
    return mprotect(reinterpret_cast<void*>(a), len, prot_of(protect)) == 0 ? TRUE : fail_errno();
}
// From /proc/self/maps: the mapping that holds the address (or the free gap before the next one).
SIZE_T VirtualQuery(LPCVOID at, PMEMORY_BASIC_INFORMATION mbi, SIZE_T len) {
    if (len < sizeof(*mbi)) return 0;
    auto addr = reinterpret_cast<uintptr_t>(at) & ~uintptr_t(4095);
    FILE* f = std::fopen("/proc/self/maps", "r"); if (!f) return 0;
    char line[512]; uintptr_t prev_end = 0; bool found = false;
    std::memset(mbi, 0, sizeof(*mbi));
    while (std::fgets(line, sizeof line, f)) {
        unsigned long lo, hi; char perms[5] = {};
        if (std::sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
        if (addr < lo) {   // in the free gap before this mapping
            mbi->BaseAddress = reinterpret_cast<PVOID>(addr); mbi->RegionSize = lo - addr; mbi->State = MEM_FREE; mbi->Protect = PAGE_NOACCESS;
            found = true; break;
        }
        if (addr < hi) {
            bool r = perms[0] == 'r', w = perms[1] == 'w', x = perms[2] == 'x';
            DWORD p = !r && !w && !x ? PAGE_NOACCESS : x ? (w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ) : (w ? PAGE_READWRITE : PAGE_READONLY);
            mbi->BaseAddress = reinterpret_cast<PVOID>(addr); mbi->AllocationBase = reinterpret_cast<PVOID>(lo);
            mbi->RegionSize = hi - addr; mbi->Protect = p; mbi->AllocationProtect = p;
            mbi->State = p == PAGE_NOACCESS ? MEM_RESERVE : MEM_COMMIT; mbi->Type = perms[3] == 's' ? MEM_MAPPED : MEM_PRIVATE;
            found = true; break;
        }
        prev_end = hi;
    }
    std::fclose(f);
    (void)prev_end;
    return found ? sizeof(*mbi) : 0;
}
BOOL VirtualLock(LPVOID at, SIZE_T size) { return mlock(at, size) == 0; }
BOOL VirtualUnlock(LPVOID at, SIZE_T size) { return munlock(at, size) == 0; }
BOOL FlushInstructionCache(HANDLE, LPCVOID, SIZE_T) { return TRUE; }   // x86-64 keeps caches coherent
// A pagefile-backed section is a memfd; its views are shared mappings, so aliases see each other's writes as on Windows.
HANDLE CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES, DWORD, DWORD size_hi, DWORD size_lo, LPCWSTR) {
    uint64_t size = (static_cast<uint64_t>(size_hi) << 32) | size_lo;
    int fd;
    if (file == INVALID_HANDLE_VALUE) {
        fd = memfd_create("nxrt-section", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) != 0) { fail_errno(); if (fd >= 0) ::close(fd); return nullptr; }
    } else {
        auto* f = dynamic_cast<FileH*>(of(file)); if (!f) { t_last_error = ERROR_INVALID_HANDLE; return nullptr; }
        fd = dup(f->fd);
        if (size == 0) { struct stat st; fstat(fd, &st); size = static_cast<uint64_t>(st.st_size); }
    }
    return as_handle(new MappingH(fd, size));
}
HANDLE CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD hi, DWORD lo, LPCSTR) { return CreateFileMappingW(file, sa, protect, hi, lo, nullptr); }
LPVOID MapViewOfFileEx(HANDLE map, DWORD access, DWORD off_hi, DWORD off_lo, SIZE_T size, LPVOID at) {
    auto* m = dynamic_cast<MappingH*>(of(map)); if (!m) { t_last_error = ERROR_INVALID_HANDLE; return nullptr; }
    uint64_t off = (static_cast<uint64_t>(off_hi) << 32) | off_lo;
    if (size == 0) size = static_cast<SIZE_T>(m->size - off);
    int prot = PROT_READ | ((access & FILE_MAP_WRITE) || access == FILE_MAP_ALL_ACCESS ? PROT_WRITE : 0) | ((access & FILE_MAP_EXECUTE) ? PROT_EXEC : 0);
    int flags = MAP_SHARED | (at ? MAP_FIXED_NOREPLACE : 0);
    void* p = mmap(at, size, prot, flags, m->fd, static_cast<off_t>(off));
    if (p == MAP_FAILED) { fail_errno(); return nullptr; }
    if (at && p != at) { munmap(p, size); t_last_error = ERROR_INVALID_PARAMETER; return nullptr; }
    return p;
}
LPVOID MapViewOfFile(HANDLE map, DWORD access, DWORD off_hi, DWORD off_lo, SIZE_T size) { return MapViewOfFileEx(map, access, off_hi, off_lo, size, nullptr); }
BOOL UnmapViewOfFile(LPCVOID at) {
    MEMORY_BASIC_INFORMATION m; if (!VirtualQuery(at, &m, sizeof m)) return FALSE;
    return munmap(const_cast<void*>(at), m.RegionSize) == 0 ? TRUE : fail_errno();
}
BOOL FlushViewOfFile(LPCVOID at, SIZE_T size) { return msync(const_cast<void*>(at), size, MS_SYNC) == 0; }
void GetSystemInfo(LPSYSTEM_INFO si) {
    std::memset(si, 0, sizeof(*si));
    si->wProcessorArchitecture = 9;   // PROCESSOR_ARCHITECTURE_AMD64
    si->dwPageSize = static_cast<DWORD>(sysconf(_SC_PAGESIZE));
    si->dwAllocationGranularity = 65536;   // the runtime aligns reservations to this, like Windows
    si->dwNumberOfProcessors = static_cast<DWORD>(sysconf(_SC_NPROCESSORS_ONLN));
    si->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(0x10000);
    si->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(0x7FFFFFFEFFFFull);
    DWORD_PTR mask = 0; for (DWORD i = 0; i < si->dwNumberOfProcessors && i < 64; ++i) mask |= DWORD_PTR(1) << i;
    si->dwActiveProcessorMask = mask;
}
void GetNativeSystemInfo(LPSYSTEM_INFO si) { GetSystemInfo(si); }
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX m) {
    struct sysinfo s; if (sysinfo(&s) != 0) return fail_errno();
    m->ullTotalPhys = static_cast<DWORDLONG>(s.totalram) * s.mem_unit;
    m->ullAvailPhys = static_cast<DWORDLONG>(s.freeram + s.bufferram) * s.mem_unit;
    m->ullTotalPageFile = m->ullTotalPhys + static_cast<DWORDLONG>(s.totalswap) * s.mem_unit;
    m->ullAvailPageFile = m->ullAvailPhys + static_cast<DWORDLONG>(s.freeswap) * s.mem_unit;
    m->ullTotalVirtual = 0x7FFFFFFF0000ull; m->ullAvailVirtual = m->ullTotalVirtual; m->ullAvailExtendedVirtual = 0;
    m->dwMemoryLoad = m->ullTotalPhys ? static_cast<DWORD>(100 - (m->ullAvailPhys * 100 / m->ullTotalPhys)) : 0;
    return TRUE;
}
BOOL GetProcessMemoryInfo(HANDLE, PPROCESS_MEMORY_COUNTERS out, DWORD cb) {
    std::memset(out, 0, cb);
    FILE* f = std::fopen("/proc/self/status", "r"); if (!f) return FALSE;
    char line[256]; unsigned long kb;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "VmRSS: %lu kB", &kb) == 1) out->WorkingSetSize = kb * 1024;
        else if (std::sscanf(line, "VmHWM: %lu kB", &kb) == 1) out->PeakWorkingSetSize = kb * 1024;
        else if (std::sscanf(line, "VmData: %lu kB", &kb) == 1) out->PagefileUsage = kb * 1024;
    }
    std::fclose(f);
    out->PeakPagefileUsage = out->PagefileUsage;
    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX*>(out)->PrivateUsage = out->PagefileUsage;
    out->cb = cb; return TRUE;
}

// ---- files -------------------------------------------------------------------------------------------------------
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disp, DWORD, HANDLE) {
    int fl = O_CLOEXEC;
    bool r = (access & GENERIC_READ) != 0, w = (access & GENERIC_WRITE) != 0;
    fl |= r && w ? O_RDWR : w ? O_WRONLY : O_RDONLY;
    switch (disp) {
    case CREATE_NEW: fl |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: fl |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: fl |= O_CREAT; break;
    case TRUNCATE_EXISTING: fl |= O_TRUNC; break;
    default: break;
    }
    std::string p = fix_path(path);
    int fd = ::open(p.c_str(), fl, 0644);
    if (fd < 0) { fail_errno(); return INVALID_HANDLE_VALUE; }
    struct stat st; if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && w) { ::close(fd); t_last_error = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; }
    if (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS) t_last_error = 0;
    return as_handle(new FileH(fd));
}
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    return CreateFileA(path, access, share, sa, disp, flags, tmpl);
}
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    return CreateFileA(to_utf8(path).c_str(), access, share, sa, disp, flags, tmpl);
}
static int fd_of(HANDLE h) {
    auto v = reinterpret_cast<intptr_t>(h);
    if (v == -10 || v == -11 || v == -12) return static_cast<int>(-10 - v);   // std handles: 0, 1, 2
    auto* f = dynamic_cast<FileH*>(of(h)); return f ? f->fd : -1;
}
BOOL ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    int fd = fd_of(h); if (fd < 0) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    ssize_t r = ov ? pread(fd, buf, n, static_cast<off_t>((static_cast<uint64_t>(ov->OffsetHigh) << 32) | ov->Offset)) : ::read(fd, buf, n);
    if (r < 0) { if (got) *got = 0; return fail_errno(); }
    if (got) *got = static_cast<DWORD>(r); return TRUE;
}
BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED ov) {
    int fd = fd_of(h); if (fd < 0) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    ssize_t r = ov ? pwrite(fd, buf, n, static_cast<off_t>((static_cast<uint64_t>(ov->OffsetHigh) << 32) | ov->Offset)) : ::write(fd, buf, n);
    if (r < 0) { if (put) *put = 0; return fail_errno(); }
    if (put) *put = static_cast<DWORD>(r); return TRUE;
}
BOOL FlushFileBuffers(HANDLE h) { int fd = fd_of(h); return fd >= 0 && fsync(fd) == 0; }
BOOL GetFileSizeEx(HANDLE h, PLARGE_INTEGER size) {
    struct stat st; if (fstat(fd_of(h), &st) != 0) return fail_errno();
    size->QuadPart = st.st_size; return TRUE;
}
DWORD GetFileSize(HANDLE h, LPDWORD hi) {
    LARGE_INTEGER s; if (!GetFileSizeEx(h, &s)) return INVALID_FILE_SIZE;
    if (hi) *hi = static_cast<DWORD>(s.QuadPart >> 32); return static_cast<DWORD>(s.QuadPart);
}
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER pos, DWORD method) {
    int wh = method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET;
    off_t r = lseek(fd_of(h), static_cast<off_t>(dist.QuadPart), wh);
    if (r < 0) return fail_errno();
    if (pos) pos->QuadPart = r; return TRUE;
}
BOOL SetEndOfFile(HANDLE h) { int fd = fd_of(h); off_t at = lseek(fd, 0, SEEK_CUR); return at >= 0 && ftruncate(fd, at) == 0 ? TRUE : fail_errno(); }
static DWORD attrs_of(const struct stat& st, const std::string& name) {
    DWORD a = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (!(st.st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    auto slash = name.find_last_of('/');
    if (name[slash == std::string::npos ? 0 : slash + 1] == '.') a |= FILE_ATTRIBUTE_HIDDEN;
    return a ? a : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetFileAttributesA(LPCSTR path) {
    std::string p = fix_path(path); struct stat st;
    if (stat(p.c_str(), &st) != 0) { fail_errno(); return INVALID_FILE_ATTRIBUTES; }
    return attrs_of(st, p);
}
DWORD GetFileAttributesW(LPCWSTR path) { return GetFileAttributesA(to_utf8(path).c_str()); }
static void fill_times(const struct stat& st, LPWIN32_FILE_ATTRIBUTE_DATA d) {
    unix_to_filetime(static_cast<int64_t>(st.st_ctim.tv_sec) * 1000000000LL + st.st_ctim.tv_nsec, &d->ftCreationTime);
    unix_to_filetime(static_cast<int64_t>(st.st_atim.tv_sec) * 1000000000LL + st.st_atim.tv_nsec, &d->ftLastAccessTime);
    unix_to_filetime(static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec, &d->ftLastWriteTime);
}
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out) {
    std::string p = fix_path(path); struct stat st;
    if (stat(p.c_str(), &st) != 0) return fail_errno();
    auto* d = static_cast<LPWIN32_FILE_ATTRIBUTE_DATA>(out);
    d->dwFileAttributes = attrs_of(st, p); fill_times(st, d);
    d->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32); d->nFileSizeLow = static_cast<DWORD>(st.st_size);
    return TRUE;
}
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS l, LPVOID out) { return GetFileAttributesExA(to_utf8(path).c_str(), l, out); }
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES) { return mkdir(fix_path(path).c_str(), 0755) == 0 ? TRUE : fail_errno(); }
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa) { return CreateDirectoryA(to_utf8(path).c_str(), sa); }
BOOL RemoveDirectoryA(LPCSTR path) { return rmdir(fix_path(path).c_str()) == 0 ? TRUE : fail_errno(); }
BOOL RemoveDirectoryW(LPCWSTR path) { return RemoveDirectoryA(to_utf8(path).c_str()); }
BOOL DeleteFileA(LPCSTR path) { return unlink(fix_path(path).c_str()) == 0 ? TRUE : fail_errno(); }
BOOL DeleteFileW(LPCWSTR path) { return DeleteFileA(to_utf8(path).c_str()); }
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    std::string a = fix_path(from), b = fix_path(to);
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(b.c_str(), F_OK) == 0) { t_last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    if (rename(a.c_str(), b.c_str()) != 0) return fail_errno();
    if (flags & MOVEFILE_WRITE_THROUGH) { std::string dir = b.substr(0, b.find_last_of('/') == std::string::npos ? 0 : b.find_last_of('/'));
        int fd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY); if (fd >= 0) { fsync(fd); ::close(fd); } }
    return TRUE;
}
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) { return MoveFileExA(to_utf8(from).c_str(), to_utf8(to).c_str(), flags); }
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    int in = ::open(fix_path(from).c_str(), O_RDONLY | O_CLOEXEC); if (in < 0) return fail_errno();
    int out = ::open(fix_path(to).c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
    if (out < 0) { ::close(in); return fail_errno(); }
    char buf[1 << 16]; ssize_t n; BOOL ok = TRUE;
    while ((n = ::read(in, buf, sizeof buf)) > 0) if (::write(out, buf, static_cast<size_t>(n)) != n) { ok = fail_errno(); break; }
    if (n < 0) ok = fail_errno();
    ::close(in); ::close(out); return ok;
}
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL f) { return CopyFileA(to_utf8(from).c_str(), to_utf8(to).c_str(), f); }

// Find*: lists the directory once and matches the pattern's last part with fnmatch (Win32 '*' and '?' work the same here).
static BOOL find_fill(FindH* h, LPWIN32_FIND_DATAW wout, LPWIN32_FIND_DATAA aout) {
    while (h->next < h->names.size()) {
        const std::string& n = h->names[h->next++];
        std::string full = h->dir + "/" + n; struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        WIN32_FILE_ATTRIBUTE_DATA d{}; fill_times(st, &d);
        DWORD attrs = attrs_of(st, n);
        auto sz = static_cast<uint64_t>(st.st_size);
        if (wout) {
            std::memset(wout, 0, sizeof(*wout)); wout->dwFileAttributes = attrs; wout->ftCreationTime = d.ftCreationTime;
            wout->ftLastAccessTime = d.ftLastAccessTime; wout->ftLastWriteTime = d.ftLastWriteTime;
            wout->nFileSizeHigh = static_cast<DWORD>(sz >> 32); wout->nFileSizeLow = static_cast<DWORD>(sz);
            std::wstring w; from_utf8(n.c_str(), -1, w); std::wcsncpy(wout->cFileName, w.c_str(), MAX_PATH - 1);
        } else {
            std::memset(aout, 0, sizeof(*aout)); aout->dwFileAttributes = attrs; aout->ftCreationTime = d.ftCreationTime;
            aout->ftLastAccessTime = d.ftLastAccessTime; aout->ftLastWriteTime = d.ftLastWriteTime;
            aout->nFileSizeHigh = static_cast<DWORD>(sz >> 32); aout->nFileSizeLow = static_cast<DWORD>(sz);
            std::strncpy(aout->cFileName, n.c_str(), MAX_PATH - 1);
        }
        return TRUE;
    }
    t_last_error = ERROR_NO_MORE_FILES; return FALSE;
}
static HANDLE find_first(const std::string& pattern, LPWIN32_FIND_DATAW wout, LPWIN32_FIND_DATAA aout) {
    std::string p = fix_path(pattern);
    auto slash = p.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : p.substr(0, slash), mask = slash == std::string::npos ? p : p.substr(slash + 1);
    if (mask == "*.*") mask = "*";
    DIR* d = opendir(dir.c_str()); if (!d) { fail_errno(); return INVALID_HANDLE_VALUE; }
    auto* h = new FindH(); h->dir = dir;
    while (dirent* e = readdir(d)) if (fnmatch(mask.c_str(), e->d_name, 0) == 0) h->names.emplace_back(e->d_name);
    closedir(d);
    if (!find_fill(h, wout, aout)) { delete h; t_last_error = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    return as_handle(h);
}
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA out) { return find_first(pattern, nullptr, out); }
HANDLE FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW out) { return find_first(to_utf8(pattern), out, nullptr); }
HANDLE FindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS, LPVOID out, FINDEX_SEARCH_OPS, LPVOID, DWORD) { return FindFirstFileW(pattern, static_cast<LPWIN32_FIND_DATAW>(out)); }
BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA out) { auto* f = dynamic_cast<FindH*>(of(h)); return f ? find_fill(f, nullptr, out) : FALSE; }
BOOL FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW out) { auto* f = dynamic_cast<FindH*>(of(h)); return f ? find_fill(f, out, nullptr) : FALSE; }
BOOL FindClose(HANDLE h) { delete of(h); return TRUE; }
DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD size) {
    std::string p;
    if (!m) { char tmp[4096]; ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp - 1); if (n < 0) return 0; p.assign(tmp, static_cast<size_t>(n)); }
    else { Dl_info i; if (!dladdr(m, &i) || !i.dli_fname) return 0; p = i.dli_fname; }
    DWORD n = static_cast<DWORD>(p.size());
    if (size == 0) return 0;
    if (n >= size) { std::memcpy(buf, p.c_str(), size - 1); buf[size - 1] = 0; t_last_error = ERROR_INSUFFICIENT_BUFFER; return size; }
    std::memcpy(buf, p.c_str(), n + 1); return n;
}
DWORD GetModuleFileNameW(HMODULE m, LPWSTR buf, DWORD size) {
    char tmp[4096]; DWORD n = GetModuleFileNameA(m, tmp, sizeof tmp); if (!n) return 0;
    std::wstring w; from_utf8(tmp, static_cast<int>(n), w);
    if (w.size() >= size) { if (size) { std::wmemcpy(buf, w.c_str(), size - 1); buf[size - 1] = 0; } return size; }
    std::wmemcpy(buf, w.c_str(), w.size() + 1); return static_cast<DWORD>(w.size());
}
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR buf, LPSTR* file_part) {
    std::string p = fix_path(path);
    if (p.empty() || p[0] != '/') { char cwd[4096]; if (!getcwd(cwd, sizeof cwd)) return 0; p = std::string(cwd) + "/" + p; }
    if (p.size() >= size) return static_cast<DWORD>(p.size() + 1);
    std::memcpy(buf, p.c_str(), p.size() + 1);
    if (file_part) { char* s = std::strrchr(buf, '/'); *file_part = s ? s + 1 : buf; }
    return static_cast<DWORD>(p.size());
}
DWORD GetFullPathNameW(LPCWSTR path, DWORD size, LPWSTR buf, LPWSTR* file_part) {
    char tmp[4096]; DWORD n = GetFullPathNameA(to_utf8(path).c_str(), sizeof tmp, tmp, nullptr); if (!n || n >= sizeof tmp) return 0;
    std::wstring w; from_utf8(tmp, static_cast<int>(n), w);
    if (w.size() >= size) return static_cast<DWORD>(w.size() + 1);
    std::wmemcpy(buf, w.c_str(), w.size() + 1);
    if (file_part) { wchar_t* s = std::wcsrchr(buf, L'/'); *file_part = s ? s + 1 : buf; }
    return static_cast<DWORD>(w.size());
}
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buf) {
    char cwd[4096]; if (!getcwd(cwd, sizeof cwd)) return 0;
    DWORD n = static_cast<DWORD>(std::strlen(cwd)); if (n >= size) return n + 1;
    std::memcpy(buf, cwd, n + 1); return n;
}
BOOL SetCurrentDirectoryA(LPCSTR path) { return chdir(fix_path(path).c_str()) == 0 ? TRUE : fail_errno(); }
BOOL SetCurrentDirectoryW(LPCWSTR path) { return SetCurrentDirectoryA(to_utf8(path).c_str()); }
DWORD GetTempPathA(DWORD size, LPSTR buf) {
    const char* t = std::getenv("TMPDIR"); std::string p = std::string(t && *t ? t : "/tmp") + "/";
    if (p.size() >= size) return static_cast<DWORD>(p.size() + 1);
    std::memcpy(buf, p.c_str(), p.size() + 1); return static_cast<DWORD>(p.size());
}
FILE* _wfopen(const wchar_t* path, const wchar_t* mode) {
    std::string m = to_utf8(mode); m.erase(std::remove(m.begin(), m.end(), 'b'), m.end()); m += 'b';   // binary is the only mode on Linux
    return std::fopen(fix_path(to_utf8(path)).c_str(), m.c_str());
}
FILE* _wfopen(const char* path, const wchar_t* mode) {
    std::string m = to_utf8(mode); m.erase(std::remove(m.begin(), m.end(), 'b'), m.end()); m += 'b';
    return std::fopen(fix_path(path).c_str(), m.c_str());
}
int _wremove(const char* path) { return std::remove(fix_path(path).c_str()); }
int _wrename(const char* from, const char* to) { return std::rename(fix_path(from).c_str(), fix_path(to).c_str()); }
int _wremove(const wchar_t* path) { return std::remove(fix_path(to_utf8(path)).c_str()); }
int _wrename(const wchar_t* from, const wchar_t* to) { return std::rename(fix_path(to_utf8(from)).c_str(), fix_path(to_utf8(to)).c_str()); }

// ---- dynamic libraries -------------------------------------------------------------------------------------------
// Win32 library names (vulkan-1.dll, ...) are mapped to their Linux sonames; others are tried as given.
static std::string linux_lib_name(std::string n) {
    std::string lower = n; for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "vulkan-1.dll" || lower == "vulkan-1") return "libvulkan.so.1";
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".dll") == 0) return "lib" + n.substr(0, n.size() - 4) + ".so";
    return n;
}
HMODULE LoadLibraryA(LPCSTR name) {
    void* h = dlopen(linux_lib_name(name).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) t_last_error = ERROR_FILE_NOT_FOUND;
    return h;
}
HMODULE LoadLibraryW(LPCWSTR name) { return LoadLibraryA(to_utf8(name).c_str()); }
HMODULE LoadLibraryExA(LPCSTR name, HANDLE, DWORD) { return LoadLibraryA(name); }
HMODULE LoadLibraryExW(LPCWSTR name, HANDLE, DWORD) { return LoadLibraryW(name); }
FARPROC GetProcAddress(HMODULE m, LPCSTR name) { return dlsym(m ? m : RTLD_DEFAULT, name); }
BOOL FreeLibrary(HMODULE m) { return dlclose(m) == 0; }
// Only "is this library loaded?" (and the main program for nullptr) is asked of GetModuleHandle.
HMODULE GetModuleHandleA(LPCSTR name) { return name ? dlopen(linux_lib_name(name).c_str(), RTLD_NOW | RTLD_NOLOAD) : dlopen(nullptr, RTLD_NOW); }
HMODULE GetModuleHandleW(LPCWSTR name) { return name ? GetModuleHandleA(to_utf8(name).c_str()) : GetModuleHandleA(nullptr); }
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* out) {
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        Dl_info i; if (!dladdr(reinterpret_cast<const void*>(name), &i)) return FALSE;
        *out = i.dli_fbase; return TRUE;
    }
    *out = GetModuleHandleW(name); return *out != nullptr;
}

// ---- text --------------------------------------------------------------------------------------------------------
int MultiByteToWideChar(UINT, DWORD flags, LPCSTR in, int in_len, LPWSTR out, int out_len) {
    std::wstring w;
    if (!from_utf8(in, in_len, w) && (flags & MB_ERR_INVALID_CHARS)) { t_last_error = 1113; return 0; }   // ERROR_NO_UNICODE_TRANSLATION
    if (in_len < 0) w.push_back(L'\0');   // -1 counts the terminator, like Win32
    int n = static_cast<int>(w.size());
    if (out_len == 0) return n;
    if (n > out_len) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::wmemcpy(out, w.data(), static_cast<size_t>(n)); return n;
}
int WideCharToMultiByte(UINT, DWORD, LPCWSTR in, int in_len, LPSTR out, int out_len, LPCSTR, BOOL* used_def) {
    std::string s = to_utf8(in, in_len);
    if (in_len < 0) s.push_back('\0');
    if (used_def) *used_def = FALSE;
    int n = static_cast<int>(s.size());
    if (out_len == 0) return n;
    if (n > out_len) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::memcpy(out, s.data(), static_cast<size_t>(n)); return n;
}

// ---- console and messages ----------------------------------------------------------------------------------------
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT) {
    std::fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
    return IDOK;
}
int MessageBoxW(HWND h, LPCWSTR text, LPCWSTR caption, UINT type) { return MessageBoxA(h, to_utf8(text).c_str(), to_utf8(caption).c_str(), type); }
void OutputDebugStringA(LPCSTR s) { if (s) std::fputs(s, stderr); }
void OutputDebugStringW(LPCWSTR s) { OutputDebugStringA(to_utf8(s).c_str()); }
HANDLE GetStdHandle(DWORD which) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(static_cast<int32_t>(which))); }
BOOL AttachConsole(DWORD) { return TRUE; }   // a terminal, if there is one, is already attached
BOOL AllocConsole() { return TRUE; }

// ---- language ----------------------------------------------------------------------------------------------------
LANGID GetUserDefaultUILanguage() {
    const char* v = std::getenv("LC_ALL"); if (!v || !*v) v = std::getenv("LC_MESSAGES"); if (!v || !*v) v = std::getenv("LANG");
    std::string l = v && *v ? v : "en_US";
    auto is = [&](const char* p) { return l.compare(0, std::strlen(p), p) == 0; };
    if (is("en_CA")) return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_CAN);
    if (is("en")) return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    if (is("fr_CA")) return MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN);
    if (is("fr")) return MAKELANGID(LANG_FRENCH, 0x01);
    if (is("es_ES")) return MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN);
    if (is("es")) return MAKELANGID(LANG_SPANISH, 0x0a);   // Latin American Spanish
    if (is("pt_BR")) return MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN);
    if (is("pt")) return MAKELANGID(LANG_PORTUGUESE, 0x02);
    if (is("de")) return MAKELANGID(LANG_GERMAN, 0x01);
    if (is("it")) return MAKELANGID(LANG_ITALIAN, 0x01);
    if (is("nl")) return MAKELANGID(LANG_DUTCH, 0x01);
    if (is("ru")) return MAKELANGID(LANG_RUSSIAN, 0x01);
    if (is("ja")) return MAKELANGID(LANG_JAPANESE, 0x01);
    if (is("ko")) return MAKELANGID(LANG_KOREAN, 0x01);
    if (is("zh_TW")) return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
    if (is("zh_HK")) return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG);
    if (is("zh")) return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

// ---- shared with win32_fault.cpp ---------------------------------------------------------------------------------
pid_t nx_compat_thread_tid(HANDLE h) { return tid_of(h); }
HANDLE nx_compat_thread_handle(pid_t tid) {
    auto* t = new ThreadH(); t->tid = tid; t->refs = 1;   // an existing thread: waits on it are not supported, only suspend/context
    return as_handle(t);
}

// ---- thread snapshots --------------------------------------------------------------------------------------------
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD) {
    if (!(flags & TH32CS_SNAPTHREAD)) { t_last_error = ERROR_NOT_SUPPORTED; return INVALID_HANDLE_VALUE; }
    DIR* d = opendir("/proc/self/task"); if (!d) { fail_errno(); return INVALID_HANDLE_VALUE; }
    auto* s = new SnapshotH();
    while (dirent* e = readdir(d)) if (e->d_name[0] != '.') s->tids.push_back(static_cast<DWORD>(std::strtoul(e->d_name, nullptr, 10)));
    closedir(d); return as_handle(s);
}
static BOOL snapshot_next(HANDLE h, LPTHREADENTRY32 te) {
    auto* s = dynamic_cast<SnapshotH*>(of(h));
    if (!s || s->next >= s->tids.size()) { t_last_error = ERROR_NO_MORE_FILES; return FALSE; }
    DWORD size = te->dwSize; std::memset(te, 0, sizeof(*te)); te->dwSize = size;
    te->th32ThreadID = s->tids[s->next++]; te->th32OwnerProcessID = GetCurrentProcessId(); te->tpBasePri = 8;
    return TRUE;
}
BOOL Thread32First(HANDLE h, LPTHREADENTRY32 te) { if (auto* s = dynamic_cast<SnapshotH*>(of(h))) s->next = 0; return snapshot_next(h, te); }
BOOL Thread32Next(HANDLE h, LPTHREADENTRY32 te) { return snapshot_next(h, te); }

// ---- mutexes -----------------------------------------------------------------------------------------------------
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL owned, LPCSTR name) {
    auto* m = new MutexH();
    t_last_error = 0;
    if (name && *name) {
        std::string n = name; for (auto& c : n) if (c == '\\' || c == '/') c = '_';
        const char* rt = std::getenv("XDG_RUNTIME_DIR");
        std::string path = std::string(rt && *rt ? rt : "/tmp") + "/" + n + ".lock";
        m->fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (m->fd < 0) { delete m; fail_errno(); return nullptr; }
        if (flock(m->fd, LOCK_EX | LOCK_NB) != 0) t_last_error = ERROR_ALREADY_EXISTS;   // held by another process
    }
    if (owned) m->count = 0;
    return as_handle(m);
}
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owned, LPCWSTR name) { return CreateMutexA(sa, owned, name ? to_utf8(name).c_str() : nullptr); }
BOOL ReleaseMutex(HANDLE h) {
    auto* m = dynamic_cast<MutexH*>(of(h)); if (!m) return FALSE;
    std::lock_guard<std::mutex> g(m->m); m->count = 1; pthread_cond_broadcast(&m->cv); return TRUE;
}

// ---- more processes, threads and system information ---------------------------------------------------------------
HANDLE OpenProcess(DWORD, BOOL, DWORD pid) {
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) { t_last_error = ERROR_INVALID_PARAMETER; return nullptr; }
    return as_handle(new ProcessH(static_cast<pid_t>(pid)));
}
BOOL QueryThreadCycleTime(HANDLE h, PULONG64 cycles) {
    FILETIME k, u, c, e; if (!GetThreadTimes(h, &c, &e, &k, &u)) return FALSE;
    *cycles = ((static_cast<ULONG64>(u.dwHighDateTime) << 32) | u.dwLowDateTime) * 100;   // 100 ns units -> ns
    return TRUE;
}
void GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER pn) { pn->Group = 0; pn->Number = static_cast<BYTE>(GetCurrentProcessorNumber()); pn->Reserved = 0; }

static std::string read_line(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r"); if (!f) return {};
    char b[512] = {}; if (!std::fgets(b, sizeof b, f)) b[0] = 0; std::fclose(f);
    std::string s = b; while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back(); return s;
}
// "0-3,8,10-11" -> bit mask
static uint64_t cpu_list_mask(const std::string& list) {
    uint64_t m = 0; const char* p = list.c_str();
    while (*p) {
        char* e; long a = std::strtol(p, &e, 10), b = a; if (e == p) break; p = e;
        if (*p == '-') { b = std::strtol(p + 1, &e, 10); p = e; }
        for (long i = a; i <= b && i < 64; ++i) m |= uint64_t(1) << i;
        if (*p == ',') ++p;
    }
    return m;
}
BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP rel, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buf, PDWORD len) {
    if (rel != RelationProcessorCore && rel != RelationAll) { t_last_error = ERROR_NOT_SUPPORTED; return FALSE; }
    struct Core { uint64_t mask; long freq; };
    std::vector<Core> cores; uint64_t seen = 0;
    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < ncpu && cpu < 64; ++cpu) {
        if (seen & (uint64_t(1) << cpu)) continue;
        std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        if (access((base + "/topology").c_str(), F_OK) != 0) continue;   // offline
        std::string sib = read_line(base + "/topology/core_cpus_list");
        if (sib.empty()) sib = read_line(base + "/topology/thread_siblings_list");
        uint64_t m = sib.empty() ? (uint64_t(1) << cpu) : cpu_list_mask(sib);
        seen |= m;
        std::string f = read_line(base + "/cpufreq/cpuinfo_max_freq");
        cores.push_back({m, f.empty() ? 0 : std::atol(f.c_str())});
    }
    // EfficiencyClass: rank of the core's maximum frequency among the distinct ones (0 = slowest).
    std::vector<long> freqs; for (auto& c : cores) freqs.push_back(c.freq);
    std::sort(freqs.begin(), freqs.end()); freqs.erase(std::unique(freqs.begin(), freqs.end()), freqs.end());
    DWORD need = static_cast<DWORD>(cores.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
    if (!buf || *len < need) { *len = need; t_last_error = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    for (size_t i = 0; i < cores.size(); ++i) {
        auto& e = buf[i]; std::memset(&e, 0, sizeof e);
        e.Relationship = RelationProcessorCore; e.Size = sizeof e;
        e.Processor.Flags = __builtin_popcountll(cores[i].mask) > 1 ? 1 : 0;   // LTP_PC_SMT
        e.Processor.EfficiencyClass = static_cast<BYTE>(std::lower_bound(freqs.begin(), freqs.end(), cores[i].freq) - freqs.begin());
        e.Processor.GroupCount = 1; e.Processor.GroupMask[0].Mask = cores[i].mask; e.Processor.GroupMask[0].Group = 0;
    }
    *len = need; return TRUE;
}

// The one ini read the runtime does through Win32: section and key compared without case, as Windows does.
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR out, DWORD size, LPCSTR file) {
    auto put = [&](const std::string& v) { DWORD n = static_cast<DWORD>(std::min<size_t>(v.size(), size ? size - 1 : 0)); if (size) { std::memcpy(out, v.data(), n); out[n] = 0; } return n; };
    FILE* f = std::fopen(fix_path(file).c_str(), "r"); if (!f) return put(def ? def : "");
    char line[1024]; bool in = false; std::string val; bool found = false;
    auto trim = [](std::string s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); };
    while (!found && std::fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') { in = strcasecmp(trim(l.substr(1, l.find(']') - 1)).c_str(), section) == 0; continue; }
        auto eq = l.find('='); if (!in || eq == std::string::npos) continue;
        if (strcasecmp(trim(l.substr(0, eq)).c_str(), key) == 0) { val = trim(l.substr(eq + 1)); found = true; }
    }
    std::fclose(f);
    return put(found ? val : std::string(def ? def : ""));
}

HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR file, LPCSTR, LPCSTR, INT) {
    if (!file) return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(2));
    pid_t pid; const char* argv[] = {"xdg-open", file, nullptr};
    int r = posix_spawnp(&pid, "xdg-open", nullptr, nullptr, const_cast<char* const*>(argv), environ);
    return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(r == 0 ? 42 : 2));   // > 32 = success, 2 = file not found
}
HINSTANCE ShellExecuteW(HWND h, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show) {
    return ShellExecuteA(h, op ? to_utf8(op).c_str() : nullptr, file ? to_utf8(file).c_str() : nullptr, nullptr, nullptr, show);
}

ULONG GetAdaptersAddresses(ULONG family, ULONG, PVOID, PIP_ADAPTER_ADDRESSES out, PULONG len) {
    ifaddrs* list = nullptr; if (getifaddrs(&list) != 0) return ERROR_GEN_FAILURE;
    struct Iface { std::string name; unsigned flags; std::vector<std::pair<sockaddr_in, int>> v4; };
    std::vector<Iface> ifs;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        Iface* it = nullptr; for (auto& i : ifs) if (i.name == a->ifa_name) it = &i;
        if (!it) { ifs.push_back({a->ifa_name, a->ifa_flags, {}}); it = &ifs.back(); }
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && (family == AF_UNSPEC || family == AF_INET)) {
            int prefix = a->ifa_netmask ? __builtin_popcount(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr) : 0;
            it->v4.push_back({*reinterpret_cast<sockaddr_in*>(a->ifa_addr), prefix});
        }
    }
    freeifaddrs(list);
    size_t need = 0;
    for (auto& i : ifs) need += sizeof(IP_ADAPTER_ADDRESSES) + (i.name.size() + 1) * (1 + sizeof(wchar_t)) + i.v4.size() * (sizeof(IP_ADAPTER_UNICAST_ADDRESS) + sizeof(sockaddr_in)) + 16;
    if (!out || *len < need) { *len = static_cast<ULONG>(need); return ERROR_BUFFER_OVERFLOW; }
    std::memset(out, 0, *len);
    auto* p = reinterpret_cast<unsigned char*>(out);
    auto take = [&](size_t n) { void* r = p; p += (n + 15) & ~size_t(15); return r; };
    PIP_ADAPTER_ADDRESSES prev = nullptr;
    for (unsigned idx = 0; idx < ifs.size(); ++idx) {
        auto& i = ifs[idx];
        auto* a = static_cast<PIP_ADAPTER_ADDRESSES>(take(sizeof(IP_ADAPTER_ADDRESSES)));
        a->Length = sizeof(*a); a->IfIndex = if_nametoindex(i.name.c_str());
        a->AdapterName = static_cast<char*>(take(i.name.size() + 1)); std::memcpy(a->AdapterName, i.name.c_str(), i.name.size() + 1);
        a->FriendlyName = static_cast<PWSTR>(take((i.name.size() + 1) * sizeof(wchar_t)));
        for (size_t k = 0; k <= i.name.size(); ++k) a->FriendlyName[k] = static_cast<unsigned char>(i.name.c_str()[k]);
        a->Description = a->FriendlyName;
        a->IfType = (i.flags & IFF_LOOPBACK) ? IF_TYPE_SOFTWARE_LOOPBACK : access(("/sys/class/net/" + i.name + "/wireless").c_str(), F_OK) == 0 ? IF_TYPE_IEEE80211 : IF_TYPE_ETHERNET_CSMACD;
        a->OperStatus = (i.flags & IFF_UP) && (i.flags & IFF_RUNNING) ? IfOperStatusUp : IfOperStatusDown;
        PIP_ADAPTER_UNICAST_ADDRESS uprev = nullptr;
        for (auto& [sin, prefix] : i.v4) {
            auto* u = static_cast<PIP_ADAPTER_UNICAST_ADDRESS>(take(sizeof(IP_ADAPTER_UNICAST_ADDRESS)));
            auto* sa = static_cast<sockaddr_in*>(take(sizeof(sockaddr_in))); *sa = sin;
            u->Length = sizeof(*u); u->Address.lpSockaddr = reinterpret_cast<sockaddr*>(sa); u->Address.iSockaddrLength = sizeof(sockaddr_in);
            u->OnLinkPrefixLength = static_cast<UCHAR>(prefix);
            if (uprev) uprev->Next = u; else a->FirstUnicastAddress = u;
            uprev = u;
        }
        if (prev) prev->Next = a;
        prev = a;
    }
    return NO_ERROR;
}
// ---- registry, affinity --------------------------------------------------------------------------------------------
LSTATUS RegGetValueA(HKEY key, LPCSTR sub, LPCSTR value, DWORD flags, LPDWORD type, PVOID data, LPDWORD size) {
    static const struct { const char* value; const char* file; } kBios[] = {
        {"SystemManufacturer", "sys_vendor"}, {"SystemProductName", "product_name"}, {"SystemFamily", "product_family"},
        {"SystemVersion", "product_version"}, {"SystemSKU", "product_sku"}, {"BaseBoardManufacturer", "board_vendor"},
        {"BaseBoardProduct", "board_name"}, {"BaseBoardVersion", "board_version"}, {"BIOSVendor", "bios_vendor"},
        {"BIOSVersion", "bios_version"}, {"BIOSReleaseDate", "bios_date"}};
    if (key != HKEY_LOCAL_MACHINE || !sub || strcasecmp(sub, "HARDWARE\\DESCRIPTION\\System\\BIOS") != 0 || !(flags & RRF_RT_REG_SZ) || !value)
        return static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND);
    for (const auto& b : kBios) {
        if (strcasecmp(b.value, value) != 0) continue;
        std::string v = read_line(std::string("/sys/class/dmi/id/") + b.file);
        if (v.empty()) return static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND);
        if (type) *type = 1;   // REG_SZ
        DWORD need = static_cast<DWORD>(v.size() + 1);
        if (!data || *size < need) { *size = need; return static_cast<LSTATUS>(data ? 234 : ERROR_SUCCESS); }   // ERROR_MORE_DATA
        std::memcpy(data, v.c_str(), need); *size = need;
        return static_cast<LSTATUS>(ERROR_SUCCESS);
    }
    return static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND);
}
BOOL GetProcessAffinityMask(HANDLE, PDWORD_PTR proc, PDWORD_PTR sys) {
    cpu_set_t s; if (sched_getaffinity(0, sizeof s, &s) != 0) return fail_errno();
    DWORD_PTR m = 0; for (int i = 0; i < 64; ++i) if (CPU_ISSET(i, &s)) m |= DWORD_PTR(1) << i;
    DWORD_PTR all = 0; long n = sysconf(_SC_NPROCESSORS_CONF); for (long i = 0; i < n && i < 64; ++i) all |= DWORD_PTR(1) << i;
    if (proc) *proc = m; if (sys) *sys = all;
    return TRUE;
}
BOOL GetThreadIdealProcessorEx(HANDLE h, PPROCESSOR_NUMBER pn) {
    pid_t tid = tid_of(h); if (!tid) return FALSE;
    std::string stat = read_line("/proc/self/task/" + std::to_string(tid) + "/stat");
    auto close = stat.rfind(')'); if (close == std::string::npos) return FALSE;
    // field 39 (processor) counted after the command name, which ends at the last ')'
    const char* p = stat.c_str() + close + 2; int field = 3;
    while (*p && field < 39) { if (*p++ == ' ') ++field; }
    pn->Group = 0; pn->Number = static_cast<BYTE>(std::atoi(p)); pn->Reserved = 0;
    return TRUE;
}
#endif // !_WIN32
