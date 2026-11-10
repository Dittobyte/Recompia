// SPDX-License-Identifier: GPL-3.0-or-later
// os_a2b_posix.cpp: astc2bc7 (tools/textures) for the macOS build.
//
// The transcoder is an x86 build: the ASTC decode side is ARM astcenc compiled once per ISA into astcdec-{avx2,sse4.1,sse2}
// shared libraries and picked at run time by CPUID, and the BC7 encode side is bc7e.ispc emitted for
// `--target=sse4-i32x4,avx2-i32x8,avx512skx-x16 --arch=x86-64` (tools/textures/CMakeLists.txt:15 pins the Windows ISPC).
// Neither exists for arm64 macOS.
//
// It is also not needed there: Metal samples ASTC natively, so nvn_texdata.cpp takes Mode::Passthrough for every ASTC
// texture when the device reports the format (docs/macos-port.md, D4), and nothing ever asks for a transcode. What this
// file provides is the half that *is* reachable -- the pure size and format helpers the upload path calls while it is
// still deciding what to do with a texture -- with the same formulas as tools/textures/src/a2b.cpp, plus honest failures
// for the three entry points that need the missing kernels. A texture that did ask for a transcode fails loudly
// (A2B_ERR_UNSUPPORTED, which the callers log) instead of silently uploading garbage.
#include "astc2bc7.h"

#if !defined(_WIN32)

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stddef.h>
#include <stdint.h>

#include "bc7enc.h"   // third_party/bc7enc: the BC7 encoder the texture patch needs (MIT)

namespace {

uint64_t div_up(uint64_t v, uint64_t d) { return (v + d - 1) / d; }

}  // namespace

extern "C" {

a2b_status a2b_init(const char* dll_dir) {
    (void)dll_dir;   // no per-ISA decoder libraries to locate
    return A2B_ERR_UNSUPPORTED;
}

const char* a2b_status_string(a2b_status s) {
    switch (s) {
        case A2B_OK: return "ok";
        case A2B_ERR_ARGS: return "bad arguments";
        case A2B_ERR_SIZE: return "buffer size does not match dimensions";
        case A2B_ERR_UNSUPPORTED: return "unsupported format (no ASTC transcoder on this platform: Metal samples ASTC)";
        case A2B_ERR_NO_ASTC_LIB: return "astcdec-*-shared.dll not found";
        case A2B_ERR_DECODE: return "astcenc decode error";
    }
    return "?";
}

int a2b_is_astc(a2b_format f) { return f == A2B_ASTC_UNORM || f == A2B_ASTC_SRGB; }

int a2b_is_srgb(a2b_format f) {
    return f == A2B_ASTC_SRGB || f == A2B_BC1_SRGB || f == A2B_BC2_SRGB || f == A2B_BC3_SRGB || f == A2B_BC7_SRGB;
}

uint32_t a2b_bcn_block_bytes(a2b_format f) {
    switch (f) {
        case A2B_BC1_UNORM:
        case A2B_BC1_SRGB:
        case A2B_BC4_UNORM:
        case A2B_BC4_SNORM: return 8;
        case A2B_BC2_UNORM:
        case A2B_BC2_SRGB:
        case A2B_BC3_UNORM:
        case A2B_BC3_SRGB:
        case A2B_BC5_UNORM:
        case A2B_BC5_SNORM:
        case A2B_BC6H_UF16:
        case A2B_BC6H_SF16:
        case A2B_BC7_UNORM:
        case A2B_BC7_SRGB: return 16;
        default: return 0;
    }
}

uint64_t a2b_astc_size(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh) {
    if (!w || !h || bw < 4 || bw > 12 || bh < 4 || bh > 12) return 0;
    return div_up(w, bw) * div_up(h, bh) * 16;
}

uint64_t a2b_output_size(const a2b_input* in) {
    if (!in || !in->width || !in->height) return 0;
    if (a2b_is_astc(in->format)) return div_up(in->width, 4) * div_up(in->height, 4) * 16;
    const uint32_t bb = a2b_bcn_block_bytes(in->format);
    return bb ? div_up(in->width, 4) * div_up(in->height, 4) * bb : 0;
}

a2b_status a2b_transcode(const a2b_input* in, const void*, size_t, const a2b_options*, a2b_format* out_format,
                         void*, size_t, a2b_stats*) {
    if (!in) return A2B_ERR_ARGS;
    if (out_format) *out_format = in->format;   // BCn input would pass through unchanged; ASTC never reaches here
    return A2B_ERR_UNSUPPORTED;
}

a2b_status a2b_decode_astc_rgba8(const a2b_input* in, const void*, size_t, int, uint8_t*, a2b_stats*) {
    return in ? A2B_ERR_UNSUPPORTED : A2B_ERR_ARGS;
}

a2b_status a2b_encode_bc7(uint32_t w, uint32_t h, const uint8_t* rgba, const a2b_options* opt, void* out, size_t out_size, a2b_stats*) {
    if (!rgba || !out || !w || !h) return A2B_ERR_ARGS;
    const uint32_t bx = static_cast<uint32_t>((w + 3) / 4), by = static_cast<uint32_t>((h + 3) / 4);
    if (out_size < static_cast<size_t>(bx) * by * 16) return A2B_ERR_ARGS;
    // bc7enc fills its tables once. Mode 6 only (no partitions): the patches are flat-coloured glyphs with alpha, which mode 6
    // carries exactly, and it keeps a full 84x84 prompt rectangle around 20 ms.
    static std::once_flag once;
    std::call_once(once, [] { bc7enc_compress_block_init(); });
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_max_partitions_mode = 0;
    params.m_try_least_squares = BC7ENC_FALSE;
    if (opt && opt->quality < 50) params.m_uber_level = 0;
    uint8_t* dst = static_cast<uint8_t*>(out);
    uint8_t block[64];
    for (uint32_t y = 0; y < by; ++y)
        for (uint32_t x = 0; x < bx; ++x) {
            for (uint32_t py = 0; py < 4; ++py)
                for (uint32_t px = 0; px < 4; ++px) {
                    const uint32_t sx = std::min(w - 1, x * 4 + px), sy = std::min(h - 1, y * 4 + py);
                    std::memcpy(block + (py * 4 + px) * 4, rgba + (static_cast<size_t>(sy) * w + sx) * 4, 4);
                }
            bc7enc_compress_block(dst, block, &params);
            dst += 16;
        }
    return A2B_OK;
}

}  // extern "C"

#endif  // !_WIN32
