#include <Windows.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "capture/ImageFileWriter.h"
#include "capture/ScreenCaptureService.h"
#include "capture/WgcScreenshotService.h"
#include "capture/WindowCaptureService.h"
#include "capture/WindowLocator.h"
#include "common/DpiAwareness.h"

using Microsoft::WRL::ComPtr;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    // Deliberately do not implement WM_PRINT/WM_PRINTCLIENT: this fixture must
    // come from the compositor, even when another window completely covers it.
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const bool covering = GetWindowLongPtrW(window, GWLP_USERDATA) == 1;
        HBRUSH top = CreateSolidBrush(covering ? RGB(0, 0, 255) : RGB(0, 255, 0));
        HBRUSH bottom = CreateSolidBrush(covering ? RGB(0, 0, 255) : RGB(255, 0, 0));
        RECT upper{0, 0, bounds.right, bounds.bottom / 2};
        RECT lower{0, bounds.bottom / 2, bounds.right, bounds.bottom};
        FillRect(dc, &upper, top);
        FillRect(dc, &lower, bottom);
        DeleteObject(top);
        DeleteObject(bottom);
        if (!covering) {
            for (int x = 20; x < 100; x += 2) {
                RECT stripe{x, 20, x + 1, 50};
                FillRect(dc, &stripe, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_APP + 1) { ShowWindow(window, SW_HIDE); return 0; }
    if (message == WM_APP + 2) { ShowWindow(window, SW_MINIMIZE); return 0; }
    if (message == WM_APP + 3) { ShowWindow(window, SW_SHOWNOACTIVATE); return 0; }
    if (message == WM_APP + 4) {
        SetWindowPos(window, nullptr, 0, 0, 243, 163, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

struct Fixture {
    HWND target = nullptr;
    HWND covering = nullptr;
    HWND framed = nullptr;
    DWORD thread_id = 0;
    std::thread thread;

    Fixture() {
        std::promise<void> ready;
        auto future = ready.get_future();
        thread = std::thread([this, signal = std::move(ready)]() mutable {
            thread_id = GetCurrentThreadId();
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW cls{};
            cls.lpfnWndProc = FixtureProc;
            cls.hInstance = instance;
            cls.lpszClassName = L"NativeScreenshot.ScreenshotTest";
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassW(&cls);
            target = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
                L"WGC screenshot target", WS_POPUP, 350, 180, 241, 161, nullptr, nullptr, instance, nullptr);
            ShowWindow(target, SW_SHOWNOACTIVATE);
            UpdateWindow(target);
            covering = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                cls.lpszClassName, L"Covering window", WS_POPUP, 350, 180, 241, 161,
                nullptr, nullptr, instance, nullptr);
            SetWindowLongPtrW(covering, GWLP_USERDATA, 1);
            ShowWindow(covering, SW_SHOWNOACTIVATE);
            UpdateWindow(covering);
            framed = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
                L"Native rounded window", WS_OVERLAPPEDWINDOW, 650, 180, 480, 300,
                nullptr, nullptr, instance, nullptr);
            ShowWindow(framed, SW_SHOWNOACTIVATE);
            UpdateWindow(framed);
            DwmFlush();
            signal.set_value();
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            DestroyWindow(framed);
            DestroyWindow(covering);
            DestroyWindow(target);
        });
        future.get();
        Require(target && covering && framed, "create screenshot fixture windows");
    }

    ~Fixture() {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        if (thread.joinable()) thread.join();
    }
};

void CheckPattern(const capture::CapturedImage& image) {
    Require(image.Width() == 241 && image.Height() == 161, "odd screenshot dimensions must be preserved");
    for (int y = 2; y < 159; ++y) {
        for (int x = 2; x < 239; ++x) {
            const bool stripe = x >= 20 && x < 100 && x % 2 == 0 && y >= 20 && y < 50;
            const auto offset = static_cast<size_t>(y) * image.RowStride() + x * 4;
            const auto& pixels = image.Pixels();
            Require(pixels[offset] == 0 && pixels[offset + 1] == (!stripe && y < 80 ? 255 : 0) &&
                pixels[offset + 2] == (!stripe && y >= 80 ? 255 : 0) && pixels[offset + 3] == 255,
                "window screenshot must preserve native 1px details, BGRA order and orientation under occlusion");
        }
    }
}

void CheckFile(const std::filesystem::path& path, const capture::CapturedImage& image,
               capture::ImageFileFormat format) {
    auto saved = capture::ImageFileWriter{}.Write(path.wstring(), image, format);
    Require(saved.HasValue(), "save WGC screenshot");
    ComPtr<IWICImagingFactory> factory;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))), "create image decoder");
    ComPtr<IWICBitmapDecoder> decoder;
    Require(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)), "decode screenshot file");
    ComPtr<IWICBitmapFrameDecode> frame;
    Require(SUCCEEDED(decoder->GetFrame(0, &frame)), "read screenshot frame");
    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);
    Require(width == static_cast<UINT>(image.Width()) && height == static_cast<UINT>(image.Height()),
        "file must retain original odd dimensions");
    ComPtr<IWICBitmapSource> bgra;
    Require(SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &bgra)),
        "convert decoded screenshot");
    std::vector<BYTE> pixels(image.Pixels().size());
    Require(SUCCEEDED(bgra->CopyPixels(nullptr, static_cast<UINT>(image.RowStride()),
        static_cast<UINT>(pixels.size()), pixels.data())), "read screenshot pixels");
    Require(pixels == image.Pixels(), "saved screenshot must match WGC pixels without loss or flipping");
}

int wmain() {
    common::EnableDpiAwareness();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    const auto root = std::filesystem::temp_directory_path() /
        (L"NativeScreenshot-WgcTest-" + std::to_wstring(GetCurrentProcessId()));
    int result = 0;
    try {
        std::filesystem::create_directories(root);
        Fixture fixture;
        auto info = capture::WindowLocator{}.DescribeWindow(fixture.target);
        Require(info.has_value(), "describe covered target window");
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto image = capture::WindowCaptureService{}.CaptureWindow(*info);
            if (!image) std::wcerr << image.Error() << L'\n';
            Require(image.HasValue(), "capture target through WGC repeatedly");
            CheckPattern(image.Value());
            if (attempt == 0) {
                CheckFile(root / L"截图.png", image.Value(), capture::ImageFileFormat::Png);
                CheckFile(root / L"截图.bmp", image.Value(), capture::ImageFileFormat::Bmp);
            }
        }
        auto desktop = capture::ScreenCaptureService{}.CaptureDesktopSnapshot();
        if (!desktop) std::wcerr << desktop.Error() << L'\n';
        Require(desktop.HasValue(), "capture full desktop with WGC");
        const auto& snapshot = desktop.Value();
        Require(snapshot.origin_x == GetSystemMetrics(SM_XVIRTUALSCREEN) &&
            snapshot.origin_y == GetSystemMetrics(SM_YVIRTUALSCREEN) &&
            snapshot.image.Width() == GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
            snapshot.image.Height() == GetSystemMetrics(SM_CYVIRTUALSCREEN), "virtual desktop native bounds");
        RECT crop{370 - snapshot.origin_x, 200 - snapshot.origin_y,
                  571 - snapshot.origin_x, 321 - snapshot.origin_y};
        auto region = snapshot.image.Crop(crop);
        Require(region.HasValue() && region.Value().Width() == 201 && region.Value().Height() == 121,
            "selected region retains odd native dimensions");
        for (size_t offset = 0; offset < region.Value().Pixels().size(); offset += 4) {
            const auto& pixels = region.Value().Pixels();
            Require(pixels[offset] == 255 && pixels[offset + 1] == 0 && pixels[offset + 2] == 0,
                "desktop and region screenshot must capture visible covering window");
        }
        auto framed_info = capture::WindowLocator{}.DescribeWindow(fixture.framed);
        Require(framed_info.has_value(), "describe native framed window");
        auto framed = capture::WindowCaptureService{}.CaptureWindow(*framed_info);
        Require(framed.HasValue() && framed.Value().Width() == framed_info->bounds.right - framed_info->bounds.left &&
            framed.Value().Height() == framed_info->bounds.bottom - framed_info->bounds.top,
            "framed window screenshot matches DWM visible bounds");
        CheckFile(root / L"圆角.png", framed.Value(), capture::ImageFileFormat::Png);
        SendMessageW(fixture.target, WM_APP + 1, 0, 0);
        Require(!capture::WindowCaptureService{}.CaptureWindow(*info), "hidden window must fail without fallback");
        SendMessageW(fixture.target, WM_APP + 3, 0, 0);
        SendMessageW(fixture.target, WM_APP + 2, 0, 0);
        Require(!capture::WindowCaptureService{}.CaptureWindow(*info), "minimized window must fail without fallback");
        SendMessageW(fixture.target, WM_APP + 3, 0, 0);
        SendMessageW(fixture.target, WM_APP + 4, 0, 0);
        Require(!capture::WindowCaptureService{}.CaptureWindow(*info), "stale selected dimensions must fail without scaling");
        Require(!capture::WindowCaptureService{}.CaptureWindow({}), "invalid window must fail");
        Require(!capture::WgcScreenshotService{}.Capture({0, 0, 0, 0}), "empty screenshot range must fail");
        Require(!capture::WgcScreenshotService{}.Capture({100000, 100000, 100201, 100121}),
            "range outside monitors must fail without GDI fallback");
        std::cout << "WGC screenshots: native pixels, occlusion, desktop/region, PNG/BMP, repeated sessions and invalid targets passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(root);
    CoUninitialize();
    return result;
}
