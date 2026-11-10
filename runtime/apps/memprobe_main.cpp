// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "nxrt/log.h"
#include "nxrt/memory.h"
int main() {
    nxrt::LogOptions lo;
    lo.path = "memprobe.log";
    nxrt::log_init(lo);
    nxrt::mem::Config c;
    const bool ok = nxrt::mem::init(c);
    std::printf("mem::init %s\n", ok ? "ok" : "FAILED");
    const bool h = nxrt::mem::heap_create(6ull << 30);
    std::printf("heap_create(6GiB) %s\n", h ? "ok" : "FAILED");
    const bool committed = h && nxrt::mem::heap_commit(0, 1 << 20);
    const uint64_t alias = committed ? nxrt::mem::heap_alias(0, 1 << 16) : 0;
    std::printf("heap_commit %s, heap_alias 0x%llx %s\n", committed ? "ok" : "FAILED", static_cast<unsigned long long>(alias),
                alias ? "ok" : "FAILED");
    if (alias) {
        auto* p = reinterpret_cast<volatile uint8_t*>(alias);
        p[0] = 0x5a;
        std::printf("alias write ok, read back 0x%02x\n", p[0]);
    }
    // The four module images at their lifted bases with their real sizes (from the loader's log), every guest page written:
    // the first Mac run died with SIGBUS inside sdk's bss, so this is the pattern the loader's memcpy and the guest use.
    struct ImageBase {
        uint64_t base;
        uint64_t size;
        const char* name;
    };
    const ImageBase image_bases[] = {{nxrt::mem::kMainBase, 0x3d20000ull, "main"}, {0x7500000000ull, 0xff0000ull, "sdk"},
                                     {0x7600000000ull, 0x140000ull, "subsdk0"}, {0x7700000000ull, 0x20000ull, "rtld"}};
    bool images = true;
    for (const ImageBase& im : image_bases) {
        const bool r = nxrt::mem::reserve_fixed(im.base, im.size, im.name) && nxrt::mem::commit(im.base, im.size);
        if (r) {
            for (uint64_t off = 0; off < im.size; off += 0x1000) *reinterpret_cast<volatile uint8_t*>(im.base + off) = 0x11;
        }
        std::printf("image %s at 0x%llx (+0x%llx) %s\n", im.name, static_cast<unsigned long long>(im.base),
                    static_cast<unsigned long long>(im.size), r ? "ok" : "FAILED");
        images = images && r;
    }
    nxrt::log_flush();
    return ok && h && committed && alias && images ? 0 : 1;
}
