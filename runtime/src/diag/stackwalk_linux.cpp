// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build: host stack capture by following the frame-pointer chain (the Linux build keeps frame pointers,
// -fno-omit-frame-pointer). Each frame is read with process_vm_readv, so a broken chain ends the walk instead of faulting.
#include "stackwalk.h"

#include <sys/uio.h>
#include <unistd.h>

namespace nxrt::detail {

namespace {
bool read_pair(uint64_t at, uint64_t out[2]) {
    iovec local{out, 16}, remote{reinterpret_cast<void*>(at), 16};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == 16;
}
}  // namespace

size_t capture_stack(const CONTEXT* ctx, uint64_t* frames, size_t max) {
    size_t n = 0;
    if (max == 0 || !ctx->Rip) return 0;
    frames[n++] = ctx->Rip;
    uint64_t fp = ctx->Rbp;
    while (n < max && fp >= 0x10000 && (fp & 7) == 0) {
        uint64_t pair[2];   // [0] = caller's frame pointer, [1] = return address
        if (!read_pair(fp, pair) || pair[1] == 0) break;
        frames[n++] = pair[1];
        if (pair[0] <= fp) break;   // the chain only grows towards older frames
        fp = pair[0];
    }
    return n;
}

}  // namespace nxrt::detail
