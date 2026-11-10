// SPDX-License-Identifier: GPL-3.0-or-later
// The album's PNG encoder (album_write_png, services_internal.h), on the Windows Imaging Component. album_png_linux.cpp is the Linux one.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "services_internal.h"

#include <windows.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs")
#pragma comment(lib, "ole32")

namespace nxrt::services {

namespace {

template <typename T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

}  // namespace

bool album_write_png(const std::filesystem::path& file, const uint8_t* rgba, uint32_t width, uint32_t height, std::string* err) {
    // 24-bit BGR rows (the album keeps no alpha; the game's alpha channel is whatever its render target held).
    const size_t stride = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> bgr(stride * height);
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        bgr[i * 3 + 0] = rgba[i * 4 + 2];
        bgr[i * 3 + 1] = rgba[i * 4 + 1];
        bgr[i * 3 + 2] = rgba[i * 4 + 0];
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // S_FALSE / RPC_E_CHANGED_MODE: COM is already usable here
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    const char* step = "CoCreateInstance(WICImagingFactory)";
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) { step = "CreateStream"; hr = factory->CreateStream(&stream); }
    if (SUCCEEDED(hr)) { step = "InitializeFromFilename"; hr = stream->InitializeFromFilename(file.wstring().c_str(), GENERIC_WRITE); }
    if (SUCCEEDED(hr)) { step = "CreateEncoder(PNG)"; hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder); }
    if (SUCCEEDED(hr)) { step = "Encoder Initialize"; hr = encoder->Initialize(stream, WICBitmapEncoderNoCache); }
    if (SUCCEEDED(hr)) { step = "CreateNewFrame"; hr = encoder->CreateNewFrame(&frame, &props); }
    if (SUCCEEDED(hr)) { step = "Frame Initialize"; hr = frame->Initialize(props); }
    if (SUCCEEDED(hr)) { step = "SetSize"; hr = frame->SetSize(width, height); }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) { step = "SetPixelFormat"; hr = frame->SetPixelFormat(&fmt); }
    if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat24bppBGR) { step = "SetPixelFormat (24bpp BGR refused)"; hr = E_FAIL; }
    if (SUCCEEDED(hr)) {
        step = "WritePixels";
        hr = frame->WritePixels(height, static_cast<UINT>(stride), static_cast<UINT>(bgr.size()), bgr.data());
    }
    if (SUCCEEDED(hr)) { step = "Frame Commit"; hr = frame->Commit(); }
    if (SUCCEEDED(hr)) { step = "Encoder Commit"; hr = encoder->Commit(); }
    release(props);
    release(frame);
    release(encoder);
    release(stream);
    release(factory);
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) {
        if (err) {
            char b[96];
            std::snprintf(b, sizeof b, "%s failed (HRESULT 0x%08lX)", step, static_cast<unsigned long>(hr));
            *err = b;
        }
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return false;
    }
    return true;
}

}  // namespace nxrt::services
