// SPDX-License-Identifier: GPL-3.0-or-later
// Linux build: host-code symbolisation through the dynamic loader. The Linux build links with -rdynamic, so dladdr names
// functions of the main program as well as of shared libraries; names are demangled with __cxa_demangle. There is no
// linker map to load on Linux.
#include "nxrt/symbols.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nxrt {

namespace {
constexpr size_t kMaxDescribeName = 160;
std::string hex64(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); return b; }
}  // namespace

size_t symbols_load_map(const char*) { return 0; }
std::string symbols_map_path() { return ""; }
size_t symbols_load_map_text(const std::string&) { return 0; }

SymbolInfo symbols_lookup(uint64_t host_addr) {
    SymbolInfo info;
    info.module = "?";
    Dl_info d;
    if (!dladdr(reinterpret_cast<void*>(host_addr), &d) || !d.dli_fname) return info;
    const char* slash = std::strrchr(d.dli_fname, '/');
    info.module = slash ? slash + 1 : d.dli_fname;
    info.offset = host_addr - reinterpret_cast<uint64_t>(d.dli_fbase);
    if (d.dli_sname && d.dli_saddr) {
        int status = 0;
        char* dem = abi::__cxa_demangle(d.dli_sname, nullptr, nullptr, &status);
        info.symbol = status == 0 && dem ? dem : d.dli_sname;
        std::free(dem);
        info.sym_offset = host_addr - reinterpret_cast<uint64_t>(d.dli_saddr);
    }
    return info;
}

std::string symbols_describe(uint64_t host_addr) {
    SymbolInfo s = symbols_lookup(host_addr);
    if (s.module == "?") return "0x" + hex64(host_addr);
    std::string out = s.module + "+0x" + hex64(s.offset);
    if (!s.symbol.empty()) {
        std::string name = s.symbol.size() > kMaxDescribeName ? s.symbol.substr(0, kMaxDescribeName) : s.symbol;
        out += " (" + name + "+0x" + hex64(s.sym_offset) + ")";
    }
    return out;
}

}  // namespace nxrt
