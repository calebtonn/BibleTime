#include "Render.h"

#include <d2d1.h>
#include <wincodec.h>

#include <cmath>
#include <cstdio>

#include "OptionsDialog.h"
#include "Saver.h"
#include "Typeface.h"
#include "Win32.h"

namespace bt::app {

namespace {

void fail(const char* what, HRESULT hr) { std::fprintf(stderr, "BibleTime: %s failed (0x%08lx)\n", what, static_cast<unsigned long>(hr)); }

bool runningUnderWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

ComPtr<IWICImagingFactory> wicFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

/// Encode `source` as a PNG file.
HRESULT savePng(IWICImagingFactory* wic, IWICBitmapSource* source, const std::wstring& path) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = source->GetSize(&w, &h);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    // Convert whatever the source holds (premultiplied BGRA, or 32bpp BGR from a DIB) to the negotiated format.
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(source, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(converter.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

}  // namespace

int renderFrame(const RenderOptions& options) {
    auto wic = wicFactory();
    ID2D1Factory* d2d = SaverContext::shared().d2d();
    if (!wic || !d2d) {
        fail("WIC/Direct2D initialization", E_FAIL);
        return 1;
    }
    const UINT pw = static_cast<UINT>(std::lround(options.width * options.scale));
    const UINT ph = static_cast<UINT>(std::lround(options.height * options.scale));
    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr)) {
        fail("CreateBitmap", hr);
        return 1;
    }
    const float dpi = static_cast<float>(96 * options.scale);
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    ComPtr<ID2D1RenderTarget> target;
    hr = d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target);
    if (FAILED(hr)) {
        fail("CreateWicBitmapRenderTarget", hr);
        return 1;
    }
    // An alpha target cannot use ClearType; grayscale matches macOS text rendering anyway.
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);

    SaverView view(options.preview, 0);  // display index comes from BIBLETIME_DISPLAY when set
    view.setBounds(pw / options.scale, ph / options.scale, options.scale);
    target->BeginDraw();
    view.render(target.Get(), brush.Get());
    hr = target->EndDraw();
    if (FAILED(hr)) {
        fail("EndDraw", hr);
        return 1;
    }
    hr = savePng(wic.Get(), bitmap.Get(), options.output);
    if (FAILED(hr)) {
        fail("PNG encoding", hr);
        return 1;
    }
    // Which families the system resolved (useful when a machine lacks a font).
    Typography& typo = Typography::shared();
    TypefaceChoice face = SaverContext::shared().preferences().typeface;
    std::printf("%s: %ux%u px; body \"%s\", reference \"%s\", clock \"%s\"\n", narrow(options.output).c_str(), pw, ph,
                narrow(typo.body(face).family).c_str(), narrow(typo.reference(face).family).c_str(),
                narrow(typo.clock().family).c_str());
    return 0;
}

int renderOptionsDialog(const std::wstring& output) {
    auto wic = wicFactory();
    HWND dialog = createOptionsDialogForCapture();
    if (!wic || !dialog) {
        fail("creating the Options dialog", E_FAIL);
        return 1;
    }
    RECT rc{};
    GetWindowRect(dialog, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const bool wine = runningUnderWine();
    if (wine) {
        // Wine's PrintWindow paints only the frame, so show the dialog on the (virtual) screen and copy its pixels.
        SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        // Far off screen and never activated: controls still paint, nothing flashes on the desktop.
        SetWindowPos(dialog, HWND_BOTTOM, GetSystemMetrics(SM_XVIRTUALSCREEN) - w - 100,
                     GetSystemMetrics(SM_YVIRTUALSCREEN) - h - 100, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(dialog, SW_SHOWNOACTIVATE);
    }
    UpdateWindow(dialog);
    // Let every control paint (themes animate their first frame).
    for (DWORD start = GetTickCount(); GetTickCount() - start < (wine ? 800u : 200u);) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    BOOL printed = FALSE;
    if (wine) {
        HDC windowDC = GetWindowDC(dialog);
        printed = BitBlt(dc, 0, 0, w, h, windowDC, 0, 0, SRCCOPY | CAPTUREBLT);
        ReleaseDC(dialog, windowDC);
    } else {
        printed = PrintWindow(dialog, dc, 2 /* PW_RENDERFULLCONTENT: DWM's copy, exactly what is on screen */);
        if (!printed) printed = PrintWindow(dialog, dc, 0);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    int code = 1;
    if (printed) {
        // DIB pixels have an undefined alpha byte: read them as 32bpp BGR (opaque).
        ComPtr<IWICBitmap> wicBitmap;
        HRESULT hr = wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &wicBitmap);
        if (SUCCEEDED(hr)) hr = savePng(wic.Get(), wicBitmap.Get(), output);
        if (SUCCEEDED(hr)) code = 0;
        else fail("PNG encoding", hr);
    } else {
        fail("PrintWindow", HRESULT_FROM_WIN32(GetLastError()));
    }
    DeleteObject(bitmap);
    DestroyWindow(dialog);
    return code;
}

}  // namespace bt::app
