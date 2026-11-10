// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of album_png.cpp: the album's PNG encoder (album_write_png, services_internal.h) on miniz's PNG writer. Like the Windows
// encoder, the file is 24-bit RGB (the album keeps no alpha; the game's alpha channel is whatever its render target held).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <miniz.h>

#include "services_internal.h"

namespace nxrt::services {

bool album_write_png(const std::filesystem::path& file, const uint8_t* rgba, uint32_t width, uint32_t height, std::string* err) {
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    size_t len = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), static_cast<int>(width), static_cast<int>(height), 3, &len,
                                                           MZ_DEFAULT_LEVEL, MZ_FALSE);
    if (!png) {
        if (err) *err = "PNG encoding failed";
        return false;
    }
    bool ok = false;
    if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
        ok = std::fwrite(png, 1, len, f) == len;
        ok = std::fclose(f) == 0 && ok;
    }
    mz_free(png);
    if (!ok) {
        if (err) *err = "cannot write " + file.string();
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
    return ok;
}

}  // namespace nxrt::services
