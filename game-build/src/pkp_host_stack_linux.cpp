// SPDX-License-Identifier: GPL-3.0-or-later
// Linux side of pkp.exe's /STACK:134217728: the host main thread becomes the guest main thread and lifted code nests deeply, so it
// needs a 128 MiB stack. Linux sizes the main thread's stack by RLIMIT_STACK (often 8 MiB) when it grows, and keeps at least 128 MiB
// free below it for that, so raising the soft limit before main() runs is enough. A hard limit below 128 MiB caps it there.
#include <sys/resource.h>

namespace {

constexpr rlim_t kMainStack = 134217728;

[[gnu::constructor]] void raise_main_stack() {
    rlimit rl{};
    if (getrlimit(RLIMIT_STACK, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= kMainStack) return;
    rl.rlim_cur = rl.rlim_max == RLIM_INFINITY || rl.rlim_max >= kMainStack ? kMainStack : rl.rlim_max;
    setrlimit(RLIMIT_STACK, &rl);
}

}  // namespace
