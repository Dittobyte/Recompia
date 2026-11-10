// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build only: Win32 exceptions on signals.
//   Faults (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP) become an EXCEPTION_RECORD + CONTEXT and go to the vectored handlers in the
//   order Windows calls them; EXCEPTION_CONTINUE_EXECUTION resumes with the (possibly changed) registers. With no taker, the
//   unhandled-exception filter runs; after it the signal's default action ends the process (core dump), as an unhandled
//   exception would. RaiseException goes the same way from the caller's registers.
//   SuspendThread stops the target in a SIGRTMIN+3 handler that publishes its registers and waits for ResumeThread.
// Every handler here runs on the thread's signal stack (sigaltstack), so a stack overflow can still be reported.
#ifndef _WIN32
#include "windows.h"
#include "compat_internal.h"
#include "tlhelp32.h"

#include <atomic>
#include <mutex>
#include <dirent.h>
#include <setjmp.h>
#include <linux/futex.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

namespace {

constexpr int kFaultSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
const int kSuspendSignal = SIGRTMIN + 3;

// ---- vectored handlers: fixed table, written under a lock, read lock-free from signal handlers -------------------------------
constexpr int kMaxHandlers = 32;
std::atomic<PVECTORED_EXCEPTION_HANDLER> g_handlers[kMaxHandlers];   // in call order: g_handlers[0] first
std::mutex g_handlers_lock;
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_filter{nullptr};
std::once_flag g_installed;

// ---- signal stacks -------------------------------------------------------------------------------------------------------
constexpr size_t kAltStack = 256 * 1024;   // the crash report formats text and walks stacks on it
thread_local bool t_altstack = false;

void ensure_altstack() {
    if (t_altstack) return;
    void* mem = mmap(nullptr, kAltStack, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (mem == MAP_FAILED) return;
    stack_t ss{}; ss.ss_sp = mem; ss.ss_size = kAltStack; ss.ss_flags = 0;
    if (sigaltstack(&ss, nullptr) == 0) t_altstack = true;
    else munmap(mem, kAltStack);
}

// ---- registers <-> CONTEXT -------------------------------------------------------------------------------------------------
void to_context(const mcontext_t& m, CONTEXT* c) {
    std::memset(c, 0, sizeof(*c));
    c->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    const greg_t* g = m.gregs;
    c->Rax = g[REG_RAX]; c->Rcx = g[REG_RCX]; c->Rdx = g[REG_RDX]; c->Rbx = g[REG_RBX]; c->Rsp = g[REG_RSP]; c->Rbp = g[REG_RBP];
    c->Rsi = g[REG_RSI]; c->Rdi = g[REG_RDI]; c->R8 = g[REG_R8]; c->R9 = g[REG_R9]; c->R10 = g[REG_R10]; c->R11 = g[REG_R11];
    c->R12 = g[REG_R12]; c->R13 = g[REG_R13]; c->R14 = g[REG_R14]; c->R15 = g[REG_R15]; c->Rip = g[REG_RIP];
    c->EFlags = static_cast<DWORD>(g[REG_EFL]);
    c->SegCs = static_cast<WORD>(g[REG_CSGSFS] & 0xffff);
}
void from_context(const CONTEXT* c, mcontext_t& m) {
    greg_t* g = m.gregs;
    g[REG_RAX] = c->Rax; g[REG_RCX] = c->Rcx; g[REG_RDX] = c->Rdx; g[REG_RBX] = c->Rbx; g[REG_RSP] = c->Rsp; g[REG_RBP] = c->Rbp;
    g[REG_RSI] = c->Rsi; g[REG_RDI] = c->Rdi; g[REG_R8] = c->R8; g[REG_R9] = c->R9; g[REG_R10] = c->R10; g[REG_R11] = c->R11;
    g[REG_R12] = c->R12; g[REG_R13] = c->R13; g[REG_R14] = c->R14; g[REG_R15] = c->R15; g[REG_RIP] = c->Rip;
    g[REG_EFL] = c->EFlags;
}

// ---- guarded calls (nx_fault_guarded): the innermost active guard of the thread ------------------------------------------
struct FaultGuard {
    sigjmp_buf jump;
    bool any;
    FaultGuard* outer;
};
thread_local FaultGuard* t_guard = nullptr;

bool guard_takes(const FaultGuard* g, DWORD code) {
    return g->any || code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR;
}

// Runs the handlers, then a guarded call's guard (as Windows runs the __except frames), then the filter. Returns
// EXCEPTION_CONTINUE_EXECUTION to resume, anything else to give up; a guard that takes the fault does not return.
LONG dispatch(EXCEPTION_POINTERS* ep) {
    for (auto& h : g_handlers) {
        PVECTORED_EXCEPTION_HANDLER fn = h.load(std::memory_order_acquire);
        if (fn && fn(ep) == EXCEPTION_CONTINUE_EXECUTION) return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (FaultGuard* g = t_guard; g && guard_takes(g, ep->ExceptionRecord->ExceptionCode)) {
        t_guard = g->outer;
        siglongjmp(g->jump, 1);   // off the signal stack, back into nx_fault_guarded; the saved mask is restored
    }
    if (LPTOP_LEVEL_EXCEPTION_FILTER f = g_filter.load(std::memory_order_acquire)) return f(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void record_of(int sig, const siginfo_t* si, const mcontext_t& m, EXCEPTION_RECORD* r) {
    std::memset(r, 0, sizeof(*r));
    r->ExceptionAddress = reinterpret_cast<PVOID>(m.gregs[REG_RIP]);
    switch (sig) {
    case SIGSEGV: case SIGBUS: {
        auto addr = reinterpret_cast<uintptr_t>(si->si_addr);
        auto sp = static_cast<uintptr_t>(m.gregs[REG_RSP]);
        // A fault just below the stack pointer is the guard page being hit: report it as Windows does.
        bool overflow = sig == SIGSEGV && addr < sp + 4096 && addr + 64 * 1024 > sp;
        r->ExceptionCode = overflow ? EXCEPTION_STACK_OVERFLOW : sig == SIGBUS ? EXCEPTION_IN_PAGE_ERROR : EXCEPTION_ACCESS_VIOLATION;
        // ExceptionInformation[0]: 0 read, 1 write, 8 execute (page-fault error code bits 1 and 4).
        greg_t err = m.gregs[REG_ERR];
        r->ExceptionInformation[0] = (err & 0x10) ? 8 : (err & 0x2) ? 1 : 0;
        r->ExceptionInformation[1] = addr;
        r->NumberParameters = 2;
        break;
    }
    case SIGILL: r->ExceptionCode = si->si_code == ILL_PRVOPC ? EXCEPTION_PRIV_INSTRUCTION : EXCEPTION_ILLEGAL_INSTRUCTION; break;
    case SIGFPE:
        r->ExceptionCode = si->si_code == FPE_INTDIV ? EXCEPTION_INT_DIVIDE_BY_ZERO : si->si_code == FPE_INTOVF ? EXCEPTION_INT_OVERFLOW
                         : si->si_code == FPE_FLTDIV ? EXCEPTION_FLT_DIVIDE_BY_ZERO : EXCEPTION_FLT_INVALID_OPERATION;
        break;
    default: r->ExceptionCode = EXCEPTION_BREAKPOINT; break;
    }
}

void on_fault(int sig, siginfo_t* si, void* uc_) {
    auto* uc = static_cast<ucontext_t*>(uc_);
    EXCEPTION_RECORD rec; CONTEXT ctx;
    record_of(sig, si, uc->uc_mcontext, &rec);
    to_context(uc->uc_mcontext, &ctx);
    EXCEPTION_POINTERS ep{&rec, &ctx};
    if (dispatch(&ep) == EXCEPTION_CONTINUE_EXECUTION) { from_context(&ctx, uc->uc_mcontext); return; }
    // Nobody took it: the default action (core dump) happens when the faulting instruction runs again.
    signal(sig, SIG_DFL);
    if (sig == SIGTRAP || si->si_code <= 0) raise(sig);   // a trap or a sent signal doesn't come back by itself
}

void install() {
    std::call_once(g_installed, [] {
        struct sigaction sa{};
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;   // NODEFER: a fault inside a handler is reported too, as on Windows
        sigemptyset(&sa.sa_mask);
        for (int s : kFaultSignals) sigaction(s, &sa, nullptr);
    });
    ensure_altstack();
}

// ---- SuspendThread / GetThreadContext ------------------------------------------------------------------------------------
// One slot per suspended thread. The target publishes its registers and sleeps on `state` until ResumeThread sets it to 0.
struct SuspendSlot {
    std::atomic<pid_t> tid{0};
    std::atomic<uint32_t> state{0};   // 0 free/resumed, 1 asked, 2 stopped (ctx valid)
    CONTEXT ctx;
};
constexpr int kMaxSuspended = 256;
SuspendSlot g_slots[kMaxSuspended];
std::mutex g_suspend_lock;
std::once_flag g_suspend_installed;

long futex(void* addr, int op, uint32_t val) { return syscall(SYS_futex, addr, op, val, nullptr, nullptr, 0); }

void on_suspend(int, siginfo_t*, void* uc_) {
    int saved_errno = errno;
    pid_t me = static_cast<pid_t>(syscall(SYS_gettid));
    for (auto& s : g_slots) {
        if (s.tid.load(std::memory_order_acquire) != me || s.state.load(std::memory_order_acquire) != 1) continue;
        to_context(static_cast<ucontext_t*>(uc_)->uc_mcontext, &s.ctx);
        s.state.store(2, std::memory_order_release);
        futex(&s.state, FUTEX_WAKE_PRIVATE, INT_MAX);
        while (s.state.load(std::memory_order_acquire) == 2) futex(&s.state, FUTEX_WAIT_PRIVATE, 2);
        break;
    }
    errno = saved_errno;
}

SuspendSlot* slot_of(pid_t tid) {
    for (auto& s : g_slots) if (s.tid.load(std::memory_order_acquire) == tid && s.state.load() != 0) return &s;
    return nullptr;
}

} // namespace

void nx_fault_thread_setup() { ensure_altstack(); }

bool nx_fault_guarded(void (*fn)(void*), void* ctx, bool any) {
    install();
    FaultGuard g;
    g.any = any;
    g.outer = t_guard;
    if (sigsetjmp(g.jump, 1)) return false;   // t_guard was reset by dispatch
    t_guard = &g;
    fn(ctx);
    t_guard = g.outer;
    return true;
}

PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER fn) {
    install();
    std::lock_guard<std::mutex> g(g_handlers_lock);
    int n = 0; while (n < kMaxHandlers && g_handlers[n].load()) ++n;
    if (n == kMaxHandlers) return nullptr;
    if (first) { for (int i = n; i > 0; --i) g_handlers[i].store(g_handlers[i - 1].load()); g_handlers[0].store(fn); }
    else g_handlers[n].store(fn);
    return reinterpret_cast<PVOID>(fn);
}
ULONG RemoveVectoredExceptionHandler(PVOID h) {
    std::lock_guard<std::mutex> g(g_handlers_lock);
    for (int i = 0; i < kMaxHandlers; ++i)
        if (reinterpret_cast<PVOID>(g_handlers[i].load()) == h) {
            for (int k = i; k + 1 < kMaxHandlers; ++k) g_handlers[k].store(g_handlers[k + 1].load());
            g_handlers[kMaxHandlers - 1].store(nullptr);
            return 1;
        }
    return 0;
}
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER fn) { install(); return g_filter.exchange(fn); }

void RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR* args) {
    ucontext_t uc; getcontext(&uc);
    EXCEPTION_RECORD rec{}; CONTEXT ctx;
    rec.ExceptionCode = code; rec.ExceptionFlags = flags; rec.ExceptionAddress = __builtin_return_address(0);
    rec.NumberParameters = nargs > EXCEPTION_MAXIMUM_PARAMETERS ? EXCEPTION_MAXIMUM_PARAMETERS : nargs;
    for (DWORD i = 0; i < rec.NumberParameters; ++i) rec.ExceptionInformation[i] = args[i];
    to_context(uc.uc_mcontext, &ctx);
    ctx.Rip = reinterpret_cast<DWORD64>(rec.ExceptionAddress);
    EXCEPTION_POINTERS ep{&rec, &ctx};
    dispatch(&ep);
    // The runtime raises only non-continuable codes (fatal errors) and never expects to come back.
    std::fprintf(stderr, "unhandled exception 0x%08x\n", code);
    std::abort();
}

BOOL SetThreadStackGuarantee(PULONG) { install(); return TRUE; }

BOOL TerminateProcess(HANDLE p, UINT code) {
    if (reinterpret_cast<intptr_t>(p) == -1) _exit(static_cast<int>(code & 0xff));   // like Windows: no atexit, no flushing
    SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
}

// process_vm_readv on our own pid fails with EFAULT instead of faulting, which makes it a safe probe.
BOOL ReadProcessMemory(HANDLE, LPCVOID from, LPVOID to, SIZE_T n, SIZE_T* got) {
    iovec local{to, n}, remote{const_cast<void*>(from), n};
    ssize_t r = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (got) *got = r > 0 ? static_cast<SIZE_T>(r) : 0;
    if (r < 0 || static_cast<SIZE_T>(r) != n) { SetLastError(299); return FALSE; }   // ERROR_PARTIAL_COPY
    return TRUE;
}
BOOL IsBadReadPtr(const void* p, UINT_PTR size) {
    if (!p) return TRUE;
    unsigned char buf[256];
    for (UINT_PTR off = 0; off < size; off += sizeof buf) {
        SIZE_T n = size - off < sizeof buf ? size - off : sizeof buf;
        if (!ReadProcessMemory(GetCurrentProcess(), static_cast<const char*>(p) + off, buf, n, nullptr)) return TRUE;
    }
    return FALSE;
}
BOOL IsBadWritePtr(LPVOID p, UINT_PTR size) {
    if (!p) return TRUE;
    MEMORY_BASIC_INFORMATION m;
    for (uintptr_t a = reinterpret_cast<uintptr_t>(p), end = a + size; a < end;) {
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(a), &m, sizeof m) || m.State != MEM_COMMIT ||
            !(m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return TRUE;
        a = reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
    }
    return FALSE;
}

HANDLE OpenThread(DWORD, BOOL, DWORD tid) {
    char path[64]; std::snprintf(path, sizeof path, "/proc/self/task/%u", tid);
    if (access(path, F_OK) != 0) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    return nx_compat_thread_handle(static_cast<pid_t>(tid));
}

DWORD SuspendThread(HANDLE h) {
    pid_t tid = nx_compat_thread_tid(h);
    if (!tid || tid == static_cast<pid_t>(syscall(SYS_gettid))) { SetLastError(ERROR_INVALID_PARAMETER); return static_cast<DWORD>(-1); }
    std::call_once(g_suspend_installed, [] {
        struct sigaction sa{}; sa.sa_sigaction = on_suspend; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        sigemptyset(&sa.sa_mask); sigaction(kSuspendSignal, &sa, nullptr);
    });
    SuspendSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> g(g_suspend_lock);
        if (slot_of(tid)) return 1;   // already suspended (counts are not nested further)
        for (auto& s : g_slots) if (s.state.load() == 0) { slot = &s; break; }
        if (!slot) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return static_cast<DWORD>(-1); }
        slot->tid.store(tid); slot->state.store(1, std::memory_order_release);
    }
    if (syscall(SYS_tgkill, getpid(), tid, kSuspendSignal) != 0) { slot->state.store(0); SetLastError(ERROR_INVALID_HANDLE); return static_cast<DWORD>(-1); }
    // Wait until it has stopped (a thread blocked in a system call takes the signal straight away; give up after 200 ms).
    ULONGLONG until = GetTickCount64() + 200;
    while (slot->state.load(std::memory_order_acquire) == 1) {
        if (GetTickCount64() > until) {
            uint32_t asked = 1;
            if (slot->state.compare_exchange_strong(asked, 0)) { SetLastError(ERROR_TIMEOUT); return static_cast<DWORD>(-1); }
            break;
        }
        sched_yield();
    }
    return 0;
}
bool nx_fault_resume(pid_t tid, DWORD* prev) {
    SuspendSlot* s;
    { std::lock_guard<std::mutex> g(g_suspend_lock); s = slot_of(tid); }
    if (!s) return false;
    while (s->state.load(std::memory_order_acquire) == 1) sched_yield();   // let a pending stop finish first
    s->state.store(0, std::memory_order_release);
    futex(&s->state, FUTEX_WAKE_PRIVATE, INT_MAX);
    if (prev) *prev = 1;
    return true;
}
BOOL GetThreadContext(HANDLE h, PCONTEXT c) {
    pid_t tid = nx_compat_thread_tid(h);
    std::lock_guard<std::mutex> g(g_suspend_lock);
    SuspendSlot* s = slot_of(tid);
    if (!s || s->state.load(std::memory_order_acquire) != 2) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DWORD want = c->ContextFlags; *c = s->ctx; c->ContextFlags = want;
    return TRUE;
}
BOOL SetThreadPriorityBoost(HANDLE, BOOL) { return TRUE; }   // Linux has no priority boost to switch off

BOOL GetProcessHandleCount(HANDLE, PDWORD count) {
    DIR* d = opendir("/proc/self/fd"); if (!d) return FALSE;
    DWORD n = 0; while (dirent* e = readdir(d)) if (e->d_name[0] != '.') ++n;
    closedir(d); *count = n ? n - 1 : 0;   // minus the directory's own fd
    return TRUE;
}
#endif // !_WIN32
