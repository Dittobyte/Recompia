// SPDX-License-Identifier: GPL-3.0-or-later
// Guarded calls on SEH (include/nxrt/fault_guard.h). fault_guard_linux.cpp is the Linux one.
#include "nxrt/fault_guard.h"

#include <windows.h>

namespace nxrt {

bool fault_guarded(void (*fn)(void* ctx), void* ctx, FaultCatch what) {
    __try {
        fn(ctx);
        return true;
    } __except (what == FaultCatch::Any || GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

}  // namespace nxrt
