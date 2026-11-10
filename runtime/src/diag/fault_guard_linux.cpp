// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of fault_guard.cpp: guarded calls on the compat layer's fault handling (compat/linux/win32_fault.cpp), which jumps back
// here when no vectored handler took the fault, where Windows would run the __except block.
#include "nxrt/fault_guard.h"

#include "compat_internal.h"

namespace nxrt {

bool fault_guarded(void (*fn)(void* ctx), void* ctx, FaultCatch what) {
    return nx_fault_guarded(fn, ctx, what == FaultCatch::Any);
}

}  // namespace nxrt
