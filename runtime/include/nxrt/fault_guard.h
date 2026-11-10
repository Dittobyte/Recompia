// SPDX-License-Identifier: GPL-3.0-or-later
// Guarded calls: run a piece of code that touches memory which may go away under it (a guest page the game unmapped or decommitted) and
// get false back instead of a crash. On Windows this is SEH (__try / __except, src/diag/fault_guard.cpp); on Linux the compat layer's
// fault handling (src/diag/fault_guard_linux.cpp). Vectored handlers (the write watch, the guest fault paths) still see the fault first.
// As with __except, a fault skips the rest of the call without running destructors: keep the guarded code to plain copies and reads,
// with no C++ objects that own memory created inside it.
#pragma once

namespace nxrt {

enum class FaultCatch {
    AccessViolation,   // memory faults only (access violation, in-page error); anything else still crashes
    Any,               // every exception (EXCEPTION_EXECUTE_HANDLER)
};

// Calls fn(ctx). False when it raised a fault of the kind `what`; true when it returned.
bool fault_guarded(void (*fn)(void* ctx), void* ctx, FaultCatch what = FaultCatch::AccessViolation);

// The same for a callable: fault_guarded([&] { std::memcpy(d, s, n); }).
template <typename F>
bool fault_guarded(F&& f, FaultCatch what = FaultCatch::AccessViolation) {
    return fault_guarded([](void* p) { (*static_cast<decltype(&f)>(p))(); }, &f, what);
}

}  // namespace nxrt
