#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <dwmapi.h>
#include <codecapi.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "capture/ScreenRecordingService.h"
#include "capture/WindowCaptureService.h"
#include "capture/WindowLocator.h"
#include "common/GdiResources.h"
#include "common/DpiAwareness.h"
#include "settings/SettingsRepository.h"
#include "ui/RecordingOverlay.h"

using Microsoft::WRL::ComPtr;
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ValidateGif(const std::filesystem::path& path, UINT width, UINT height, bool check_colors = true) {
    ComPtr<IWICImagingFactory> factory;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))), "GIF decoder factory");
    ComPtr<IWICBitmapDecoder> decoder;
    Require(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)), "open actual GIF recording");
    UINT count = 0;
    Require(SUCCEEDED(decoder->GetFrameCount(&count)) && count > 0, "GIF recording has frames");
    ComPtr<IWICBitmapFrameDecode> frame;
    Require(SUCCEEDED(decoder->GetFrame(0, &frame)), "first GIF recording frame");
    UINT actual_width = 0, actual_height = 0;
    Require(SUCCEEDED(frame->GetSize(&actual_width, &actual_height)) && actual_width == width &&
        actual_height == height, "GIF preserves unpadded source dimensions");
    if (check_colors) {
        ComPtr<IWICBitmapSource> rgba;
        Require(SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &rgba)), "read GIF frame");
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        Require(SUCCEEDED(rgba->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())), "GIF pixels");
        const size_t top = (static_cast<size_t>(height / 4) * width + width / 2) * 4;
        const size_t bottom = (static_cast<size_t>(3 * height / 4) * width + width / 2) * 4;
        Require(pixels[top + 1] > 240 && pixels[top] < 16 && pixels[top + 2] < 16,
                "GIF top green and mask exclusion");
        Require(pixels[bottom + 2] > 240 && pixels[bottom] < 16 && pixels[bottom + 1] < 16,
                "GIF bottom red orientation");
    }
}

LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_DESTROY && GetWindowLongPtrW(window, GWLP_USERDATA) == 2) { PostQuitMessage(0); return 0; }
    if (message == WM_TIMER) {
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_APP + 11) {
        SetWindowPos(window, nullptr, 420, 80, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        return 0;
    }
    if (message == WM_APP + 12) { ShowWindow(window, SW_MINIMIZE); return 0; }
    if (message == WM_APP + 14) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 1);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        return 0;
    }
    if (message == WM_APP + 13) {
        SetWindowPos(window, nullptr, 0, 0, 481, 241, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
        return 0;
    }
    if (message == WM_PAINT || message == WM_PRINT || message == WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};
        const HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(w_param);
        RECT bounds{};
        GetClientRect(window, &bounds);
        if (GetWindowLongPtrW(window, GWLP_USERDATA) == 4) {
            // Changing detailed content exercises rate control, unlike a static desktop.
            const DWORD phase = GetTickCount() / 33;
            for (int y = 0; y < bounds.bottom; y += 8) {
                for (int x = 0; x < bounds.right; x += 8) {
                    DWORD value = (static_cast<DWORD>(x / 8) * 73856093u) ^
                        (static_cast<DWORD>(y / 8) * 19349663u) ^ (phase * 83492791u);
                    value = (value ^ (value >> 16)) * 0x7feb352du;
                    value = (value ^ (value >> 15)) * 0x846ca68bu;
                    value ^= value >> 16;
                    const HBRUSH brush = CreateSolidBrush(RGB(value & 255, (value >> 8) & 255, (value >> 16) & 255));
                    RECT tile{x, y, x + 8, y + 8};
                    FillRect(dc, &tile, brush);
                    DeleteObject(brush);
                }
            }
            if (message == WM_PAINT) EndPaint(window, &paint);
            return 0;
        }
        if (GetWindowLongPtrW(window, GWLP_USERDATA) == 3) {
            FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            // Thin text and alternating single-pixel lines reveal resampling and
            // compression damage which the solid-color orientation fixture cannot.
            for (int x = 24; x < 536; x += 2) {
                RECT stripe{x, 24, x + 1, 88};
                FillRect(dc, &stripe, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));
            const wchar_t* text = L"录屏画质测试：保持原始像素，中文文字与细线应清晰。Native pixels 0123456789";
            for (int row = 0; row < 6; ++row) {
                common::UniqueGdiObject<HFONT> font(CreateFontW(-(12 + row * 4), 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"));
                common::ScopedSelectObject selected(dc, font.Get());
                RECT text_bounds{24, 104 + row * 40, bounds.right - 24, 144 + row * 40};
                DrawTextW(dc, text, -1, &text_bounds, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (message == WM_PAINT) EndPaint(window, &paint);
            return 0;
        }
        const LONG middle = bounds.bottom / 2;
        RECT top{0, 0, bounds.right, middle};
        RECT bottom{0, middle, bounds.right, bounds.bottom};
        const bool occluder = GetWindowLongPtrW(window, GWLP_USERDATA) == 1;
        const HBRUSH green = CreateSolidBrush(occluder ? RGB(0, 0, 255) : RGB(0, 255, 0));
        const HBRUSH red = CreateSolidBrush(occluder ? RGB(0, 0, 255) : RGB(255, 0, 0));
        FillRect(dc, &top, green);
        FillRect(dc, &bottom, red);
        DeleteObject(green);
        DeleteObject(red);
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

struct WindowFixture {
    HWND window = nullptr;
    DWORD thread_id = 0;
    std::thread thread;
    WindowFixture(int width = 321, int height = 241, bool quality = false, DWORD style = WS_POPUP,
                  bool motion = false) {
        std::promise<std::pair<HWND, DWORD>> ready;
        auto future = ready.get_future();
        thread = std::thread([signal = std::move(ready), width, height, quality, style, motion]() mutable {
            HWND target = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                L"NativeScreenshot.RecordingTest", L"Window recording test", style,
                350, 60, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (quality) SetWindowLongPtrW(target, GWLP_USERDATA, 3);
            if (motion) {
                SetWindowLongPtrW(target, GWLP_USERDATA, 4);
                SetTimer(target, 1, 33, nullptr);
            }
            ShowWindow(target, SW_SHOWNOACTIVATE);
            UpdateWindow(target);
            DwmFlush();
            signal.set_value({target, GetCurrentThreadId()});
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (target) DestroyWindow(target);
        });
        const auto value = future.get();
        window = value.first;
        thread_id = value.second;
    }
    ~WindowFixture() {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        if (thread.joinable()) thread.join();
    }
};

void CheckTextQuality(const BYTE* decoded, LONG stride, const capture::CapturedImage& source) {
    double squared_error = 0;
    unsigned count = 0, dark_pixels = 0, retained_dark_pixels = 0;
    CURSORINFO cursor{sizeof(CURSORINFO)};
    GetCursorInfo(&cursor);
    // Fixtures are positioned at (350,60); exclude the cursor from pixel comparisons.
    const int cursor_x = cursor.ptScreenPos.x - 350;
    const int cursor_y = cursor.ptScreenPos.y - 60;
    for (int y = 24; y < 344; ++y) {
        for (int x = 24; x < 1200; ++x) {
            if (std::abs(x - cursor_x) < 64 && std::abs(y - cursor_y) < 64) continue;
            const auto offset = static_cast<size_t>(y) * source.RowStride() + x * 4;
            const auto& rgb = source.Pixels();
            const int expected = 16 + ((66 * rgb[offset + 2] + 129 * rgb[offset + 1] +
                                       25 * rgb[offset] + 128) >> 8);
            const int actual = decoded[static_cast<ptrdiff_t>(y) * stride + x];
            const double difference = actual - expected;
            squared_error += difference * difference;
            ++count;
            if (expected < 48) {
                ++dark_pixels;
                if (actual < 64) ++retained_dark_pixels;
            }
        }
    }
    const double psnr = squared_error > 0 ? 10 * std::log10(255.0 * 255.0 * count / squared_error) : 99;
    const double retention = dark_pixels ? static_cast<double>(retained_dark_pixels) / dark_pixels : 0;
    std::cout << "Text/1px detail PSNR=" << psnr << " dB, dark-pixel retention=" << retention * 100 << "%\n";
    Require(psnr >= 36, "decoded text and single-pixel detail must closely match the source");
    Require(retention >= 0.99, "thin dark strokes must survive encoding");
}

double ValidateVideo(const std::filesystem::path& path, UINT32 expected_width, UINT32 expected_height,
                   bool check_orientation = false, double minimum_seconds = 0.0,
                   const capture::CapturedImage* quality_reference = nullptr) {
    ComPtr<IMFAttributes> attributes;
    Require(SUCCEEDED(MFCreateAttributes(&attributes, 1)), "reader attributes");
    Require(SUCCEEDED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)), "reader processing");
    ComPtr<IMFSourceReader> reader;
    Require(SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader)), "MP4 must be readable");
    ComPtr<IMFMediaType> native;
    Require(SUCCEEDED(reader->GetNativeMediaType(kVideoStream, 0, &native)), "native video type");
    GUID codec{};
    native->GetGUID(MF_MT_SUBTYPE, &codec);
    Require(codec == MFVideoFormat_H264, "video must use H.264");
    if (quality_reference) {
        UINT32 profile = 0;
        Require(SUCCEEDED(native->GetUINT32(MF_MT_MPEG2_PROFILE, &profile)) &&
                profile == eAVEncH264VProfile_High, "quality recording must use H.264 High Profile");
    }
    UINT32 width = 0, height = 0;
    MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (expected_width || expected_height)
        Require(width == expected_width && height == expected_height, "encoded size must match even dimensions");
    ComPtr<IMFMediaType> decoded;
    MFCreateMediaType(&decoded);
    decoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    decoded->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    Require(SUCCEEDED(reader->SetCurrentMediaType(kVideoStream, nullptr, decoded.Get())), "decode to NV12");

    unsigned int frames = 0;
    LONGLONG last_time = -1;
    LONGLONG last_duration = 0;
    while (true) {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        Require(SUCCEEDED(reader->ReadSample(kVideoStream, 0, nullptr,
                                             &flags, &time, &sample)), "decode every sample");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        Require(time > last_time, "frame timestamps must increase");
        last_time = time;
        sample->GetSampleDuration(&last_duration);
        if (quality_reference) {
            ComPtr<IMFMediaBuffer> buffer;
            Require(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)), "quality frame buffer");
            ComPtr<IMF2DBuffer> two_dimensional;
            BYTE* pixels = nullptr;
            LONG stride = static_cast<LONG>(width);
            const bool locked_2d = SUCCEEDED(buffer.As(&two_dimensional)) &&
                                  SUCCEEDED(two_dimensional->Lock2D(&pixels, &stride));
            if (!locked_2d) Require(SUCCEEDED(buffer->Lock(&pixels, nullptr, nullptr)), "lock quality pixels");
            CheckTextQuality(pixels, stride, *quality_reference);
            if (locked_2d) two_dimensional->Unlock2D();
            else buffer->Unlock();
        }
        if (check_orientation) {
            ComPtr<IMFMediaBuffer> buffer;
            sample->ConvertToContiguousBuffer(&buffer);
            BYTE* pixels = nullptr;
            DWORD length = 0;
            buffer->GetCurrentLength(&length);
            ComPtr<IMF2DBuffer> two_dimensional;
            LONG stride = static_cast<LONG>(width);
            const bool locked_2d = SUCCEEDED(buffer.As(&two_dimensional)) &&
                                  SUCCEEDED(two_dimensional->Lock2D(&pixels, &stride));
            if (!locked_2d) Require(SUCCEEDED(buffer->Lock(&pixels, nullptr, nullptr)), "lock decoded pixels");
            Require(length >= width * height * 3 / 2, "NV12 buffer size");
            // Green has higher luma than red. This catches vertically flipped RGB32 input.
            const BYTE top = pixels[(height / 4) * stride + width / 2];
            const BYTE bottom = pixels[(height * 3 / 4) * stride + width / 2];
            if (!(top < 200 && top > 110 && bottom > 40 && bottom < 120))
                std::cerr << "Unexpected pixels in frame " << frames << ": top=" << static_cast<unsigned>(top)
                    << " bottom=" << static_cast<unsigned>(bottom) << '\n';
            Require(top < 200 && top > 110 && bottom > 40 && bottom < 120,
                "Recording contains overlay or wrong target pixels");
            if (locked_2d) two_dimensional->Unlock2D();
            else buffer->Unlock();
            Require(top > 120 && bottom < 110 && top > bottom + 30, "video orientation and colors");
        }
        ++frames;
    }
    Require(frames > 0, "recording must contain frames");
    Require((last_time + last_duration) / 10'000'000.0 >= minimum_seconds, "real recording duration");
    std::cout << "Decoded " << frames << " H.264 frames at " << width << "x" << height << '\n';
    const double seconds = (last_time + last_duration) / 10'000'000.0;
    std::cout << "Encoded bytes=" << std::filesystem::file_size(path) << ", duration=" << seconds
              << " s, average=" << std::filesystem::file_size(path) * 8 / seconds / 1'000'000 << " Mbps\n";
    return seconds;
}

int wmain(int argument_count, wchar_t** arguments) {
    if (argument_count == 2 && wcscmp(arguments[1], L"--fixture-ui") == 0) {
        common::EnableDpiAwareness();
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = FixtureProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"NativeScreenshot.RecordingTest";
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&window_class);
        HWND target = CreateWindowExW(0, window_class.lpszClassName, L"录制测试窗口", WS_OVERLAPPEDWINDOW,
            180, 140, 760, 540, nullptr, nullptr, window_class.hInstance, nullptr);
        if (!target) return 1;
        SetWindowLongPtrW(target, GWLP_USERDATA, 2);
        ShowWindow(target, SW_SHOW);
        // The test runner may start with a hidden console; explicitly show its GUI fixture.
        ShowWindow(target, SW_SHOW);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    }
    if (argument_count == 5 && std::wstring(arguments[1]) == L"--validate") {
        try {
            Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize test COM");
            Require(SUCCEEDED(MFStartup(MF_VERSION)), "initialize decoder");
            ValidateVideo(arguments[2], static_cast<UINT32>(std::stoul(arguments[3])),
                           static_cast<UINT32>(std::stoul(arguments[4])));
            MFShutdown();
            CoUninitialize();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    common::EnableDpiAwareness();
    HWND fixture = nullptr;
    const auto root = (argument_count > 1 ? std::filesystem::path(arguments[1]) : std::filesystem::temp_directory_path()) /
        (L"NativeScreenshotRecordingTest_" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root);
        // Keep persistence tests isolated from the user's real settings.
        SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
        settings::SettingsRepository settings_repository;
        settings::AppSettings settings;
        settings.launch_at_startup = false;
        settings.screen_recording_hotkey = {MOD_ALT | MOD_SHIFT, 'V'};
        settings.region_recording_hotkey = {MOD_CONTROL | MOD_ALT, VK_F7};
        settings.window_recording_hotkey = {MOD_CONTROL | MOD_ALT, VK_F8};
        settings.recording_format = capture::RecordingFileFormat::Gif;
        Require(static_cast<bool>(settings_repository.Save(settings)), "save recording hotkey");
        const auto loaded = settings_repository.Load();
        Require(loaded && loaded.Value().screen_recording_hotkey.virtual_key == 'V' &&
                loaded.Value().screen_recording_hotkey.modifiers == (MOD_ALT | MOD_SHIFT), "persist recording hotkey");
        Require(loaded && loaded.Value().region_recording_hotkey.virtual_key == VK_F7 &&
                loaded.Value().region_recording_hotkey.modifiers == (MOD_CONTROL | MOD_ALT), "persist region recording hotkey");
        Require(loaded && loaded.Value().window_recording_hotkey.virtual_key == VK_F8 &&
                loaded.Value().window_recording_hotkey.modifiers == (MOD_CONTROL | MOD_ALT), "persist window recording hotkey");
        Require(loaded && loaded.Value().recording_format == capture::RecordingFileFormat::Gif, "persist GIF recording format");
        std::filesystem::remove(root / L"NativeScreenshot" / L"settings.ini");
        const auto defaults = settings_repository.Load();
        Require(defaults && defaults.Value().screen_recording_hotkey.virtual_key == 'R', "backwards compatible default");
        Require(defaults && defaults.Value().recording_format == capture::RecordingFileFormat::Mp4, "old settings default to MP4");
        Require(defaults && !hotkey::IsValid(defaults.Value().region_recording_hotkey) &&
                !hotkey::IsValid(defaults.Value().window_recording_hotkey), "new recording shortcuts default to unassigned");
        Require(static_cast<bool>(settings_repository.Save(defaults.Value())), "save unassigned recording shortcuts");
        const auto unassigned = settings_repository.Load();
        Require(unassigned && !hotkey::IsValid(unassigned.Value().region_recording_hotkey) &&
                !hotkey::IsValid(unassigned.Value().window_recording_hotkey), "persist unassigned recording shortcuts");

        capture::ScreenRecordingService recorder;
        Require(static_cast<bool>(recorder.Stop()), "stop idle recorder");
        Require(!recorder.Start(RECT{0, 0, 1, 240}, (root / L"invalid.mp4").wstring()), "reject invalid region");
        Require(!recorder.Start(RECT{0, 0, 320, 240}, (root / L"invalid.bmp").wstring()), "reject invalid extension");
        Require(!recorder.Start(RECT{0, 0, 320, 240}, (root / L"missing" / L"video.mp4").wstring()), "report unavailable directory");
        Require(!recorder.IsActive(), "failed startup releases worker");
        Require(!recorder.Start(RECT{-100000, -100000, -99680, -99760}, (root / L"outside-monitors.gif").wstring()),
                "WGC rejects a region outside all monitors without falling back");
        Require(!std::filesystem::exists(root / L"outside-monitors.gif"), "failed WGC initialization removes empty output");
        const auto existing = root / L"existing.mp4";
        { std::ofstream output(existing); output << "keep existing file"; }
        Require(!recorder.Start(RECT{0, 0, 320, 240}, existing.wstring()), "refuse overwriting files");
        Require(std::filesystem::file_size(existing) == 18, "existing file stays intact");

        WNDCLASSW window_class{};
        window_class.lpfnWndProc = FixtureProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"NativeScreenshot.RecordingTest";
        RegisterClassW(&window_class);
        fixture = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            window_class.lpszClassName, L"Recording integration test", WS_POPUP,
            60, 60, 321, 241, nullptr, nullptr, window_class.hInstance, nullptr);
        Require(fixture != nullptr, "create color fixture");
        ShowWindow(fixture, SW_SHOWNOACTIVATE);
        UpdateWindow(fixture);
        RECT region{};
        GetWindowRect(fixture, &region);
        ui::RecordingOverlay live_overlay(GetModuleHandleW(nullptr));
        Require(static_cast<bool>(live_overlay.Show(fixture, WM_APP + 20, region, L"选区录制")), "show live prompts for exclusion test");
        const auto path = root / L"录屏测试.mp4";
        const auto started = recorder.Start(region, path.wstring());
        if (!started) std::wcerr << started.Error() << '\n';
        Require(static_cast<bool>(started), "start real capture");
        Require(!recorder.Start(region, (root / L"duplicate.mp4").wstring()), "reject concurrent capture");
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        recorder.RequestStop();
        Require(static_cast<bool>(recorder.Stop()), "finalize MP4");
        live_overlay.Close();
        Require(!recorder.IsActive(), "stop releases worker");

        Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize test COM");
        Require(SUCCEEDED(MFStartup(MF_VERSION)), "initialize decoder");
        ValidateVideo(path, 322, 242, true, 1.0);

        const auto gif_path = root / L"原始尺寸录屏.gif";
        Require(static_cast<bool>(live_overlay.Show(fixture, WM_APP + 20, region, L"GIF 录制")), "GIF capture prompts");
        const auto gif_started = recorder.Start(region, gif_path.wstring());
        if (!gif_started) std::wcerr << gif_started.Error() << '\n';
        Require(static_cast<bool>(gif_started), "start native GIF screen capture");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        Require(static_cast<bool>(recorder.Stop()), "save actual GIF file");
        live_overlay.Close();
        ValidateGif(gif_path, 321, 241);
        const auto gif_size = std::filesystem::file_size(gif_path);
        Require(!recorder.Start(region, gif_path.wstring()), "refuse overwrite existing GIF");
        Require(std::filesystem::file_size(gif_path) == gif_size, "existing GIF unchanged");
        Require(!recorder.Start(region, (root / L"missing" / L"video.gif").wstring()), "GIF unavailable directory");
        const auto quick_gif = root / L"quick.gif";
        Require(static_cast<bool>(recorder.Start(region, quick_gif.wstring())), "start immediate GIF");
        Require(static_cast<bool>(recorder.Stop()), "immediate GIF stop");
        ValidateGif(quick_gif, 321, 241);

        const auto quick = root / L"quick.mp4";
        Require(static_cast<bool>(recorder.Start(region, quick.wstring())), "restart recorder");
        Require(static_cast<bool>(recorder.Stop()), "immediate stop still creates playable MP4");
        ValidateVideo(quick, 322, 242);
        const auto native_resolution = root / L"native-resolution.mp4";
        Require(static_cast<bool>(recorder.Start(RECT{0, 0, 2560, 1440}, native_resolution.wstring())), "capture larger area");
        Require(static_cast<bool>(recorder.Stop()), "stop native-resolution capture");
        ValidateVideo(native_resolution, 2560, 1440);
        const RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        const auto full_desktop = root / L"full-desktop-wgc.mp4";
        const auto full_started = recorder.Start(desktop, full_desktop.wstring());
        if (!full_started) std::wcerr << full_started.Error() << '\n';
        Require(static_cast<bool>(full_started), "WGC captures the complete virtual desktop");
        Require(static_cast<bool>(recorder.Stop()), "save full desktop WGC recording");
        ValidateVideo(full_desktop, (desktop.right - desktop.left + 1) & ~1,
                                   (desktop.bottom - desktop.top + 1) & ~1);
        const auto shutdown = root / L"shutdown.mp4";
        {
            capture::ScreenRecordingService scope_recorder;
            Require(static_cast<bool>(scope_recorder.Start(region, shutdown.wstring())), "start destructor capture");
        }
        ValidateVideo(shutdown, 322, 242);

        Require(!recorder.StartWindow(nullptr, (root / L"invalid-window.mp4").wstring()), "reject invalid window target");
        WindowFixture window_target;
        Require(window_target.window != nullptr, "create independent window fixture");
        ShowWindow(fixture, SW_HIDE);
        const auto window_video = root / L"窗口录制.mp4";
        Require(static_cast<bool>(recorder.StartWindow(window_target.window, window_video.wstring())), "start window capture");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        PostMessageW(window_target.window, WM_APP + 11, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const RECT tracked = recorder.CurrentRegion();
        Require(tracked.left == 420 && tracked.right - tracked.left == 321, "window movement not tracked");
        PostMessageW(window_target.window, WM_APP + 12, 0, 0);
        for (int attempt = 0; attempt < 100 && !recorder.IsFinished(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Require(recorder.IsFinished(), "minimized window recording did not finish automatically");
        Require(static_cast<bool>(recorder.Stop()), "finalize window MP4");
        ValidateVideo(window_video, 322, 242, true, 0.5);
        ShowWindow(window_target.window, SW_SHOWNOACTIVATE);
        const auto window_gif = root / L"窗口录制.gif";
        Require(static_cast<bool>(recorder.StartWindow(window_target.window, window_gif.wstring())), "window GIF recording");
        Require(static_cast<bool>(recorder.Stop()), "save window GIF");
        ValidateGif(window_gif, 321, 241);
        {
            WindowFixture framed_target(760, 540, false, WS_OVERLAPPEDWINDOW);
            const auto framed_info = capture::WindowLocator{}.DescribeWindow(framed_target.window);
            Require(framed_info.has_value(), "describe framed window");
            const auto framed_gif = root / L"wgc-framed-window.gif";
            const auto framed_started = recorder.StartWindow(framed_target.window, framed_gif.wstring());
            if (!framed_started) std::wcerr << framed_started.Error() << '\n';
            Require(static_cast<bool>(framed_started), "WGC captures standard caption and DWM window bounds");
            Require(static_cast<bool>(recorder.Stop()), "save framed window GIF");
            ValidateGif(framed_gif, framed_info->bounds.right - framed_info->bounds.left,
                framed_info->bounds.bottom - framed_info->bounds.top);
        }
        const auto resized_video = root / L"resize-ends-recording.mp4";
        Require(static_cast<bool>(recorder.StartWindow(window_target.window, resized_video.wstring())), "start fixed-size window recording");
        PostMessageW(window_target.window, WM_APP + 13, 0, 0);
        for (int attempt = 0; attempt < 100 && !recorder.IsFinished(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Require(recorder.IsFinished(), "window resizing must finish the fixed-size recording");
        Require(static_cast<bool>(recorder.Stop()), "save video on window size change");
        ValidateVideo(resized_video, 322, 242);

        WindowFixture changing_target;
        const auto changing_gif = root / L"wgc-live-updates.gif";
        Require(static_cast<bool>(recorder.StartWindow(changing_target.window, changing_gif.wstring())),
                "start WGC window live-update recording");
        PostMessageW(changing_target.window, WM_APP + 14, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        Require(static_cast<bool>(recorder.Stop()), "save changed WGC frames");
        {
            ComPtr<IWICImagingFactory> factory;
            Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory))), "create live-update GIF decoder");
            ComPtr<IWICBitmapDecoder> decoder;
            Require(SUCCEEDED(factory->CreateDecoderFromFilename(changing_gif.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder)), "decode live-update GIF");
            UINT frame_count = 0;
            Require(SUCCEEDED(decoder->GetFrameCount(&frame_count)) && frame_count >= 2,
                    "WGC captures changes after the first frame");
            ComPtr<IWICBitmapFrameDecode> last_frame;
            Require(SUCCEEDED(decoder->GetFrame(frame_count - 1, &last_frame)), "read last changed frame");
            ComPtr<IWICBitmapSource> rgba;
            Require(SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, last_frame.Get(), &rgba)),
                    "convert changed GIF frame");
            UINT frame_width = 0, frame_height = 0;
            Require(SUCCEEDED(rgba->GetSize(&frame_width, &frame_height)), "changed frame dimensions");
            std::vector<BYTE> changed_pixels(static_cast<size_t>(frame_width) * frame_height * 4);
            Require(SUCCEEDED(rgba->CopyPixels(nullptr, frame_width * 4, static_cast<UINT>(changed_pixels.size()),
                changed_pixels.data())), "read changed pixels");
            const size_t middle = (static_cast<size_t>(frame_height / 2) * frame_width + frame_width / 2) * 4;
            Require(changed_pixels[middle] > 240 && changed_pixels[middle + 1] < 16 && changed_pixels[middle + 2] < 16,
                    "updated WGC window content is blue despite the occluding window");
        }

        const auto quality_path = root / L"text-quality-4k.mp4";
        WindowFixture quality_target(3840, 2160, true);
        const auto quality_info = capture::WindowLocator{}.DescribeWindow(quality_target.window);
        Require(quality_info.has_value(), "describe quality fixture");
        const auto quality_reference = capture::WindowCaptureService{}.CaptureWindow(*quality_info);
        Require(static_cast<bool>(quality_reference), "capture lossless quality reference");
        const auto quality_started = recorder.StartWindow(quality_target.window, quality_path.wstring());
        if (!quality_started) std::wcerr << quality_started.Error() << '\n';
        Require(static_cast<bool>(quality_started), "start native 4K text recording");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Require(static_cast<bool>(recorder.Stop()), "save native 4K text recording");
        ValidateVideo(quality_path, 3840, 2160, false, 0, &quality_reference.Value());
        {
            WindowFixture motion_target(1920, 1080, false, WS_POPUP, true);
            const auto motion_path = root / L"motion-bitrate-1080p.mp4";
            Require(static_cast<bool>(recorder.StartWindow(motion_target.window, motion_path.wstring())),
                    "start changing detailed 1080p content");
            std::this_thread::sleep_for(std::chrono::seconds(4));
            Require(static_cast<bool>(recorder.Stop()), "save changing detailed content");
            const double seconds = ValidateVideo(motion_path, 1920, 1080, false, 3.8);
            // Allow startup/keyframe and container overhead above the 9.3 Mbps
            // requested peak, while catching excessive output growth.
            Require(std::filesystem::file_size(motion_path) * 8 / seconds < 12'000'000,
                    "detailed moving content must stay within the recording size budget");
        }
        MFShutdown();
        CoUninitialize();
        DestroyWindow(fixture);
        std::filesystem::remove_all(root);
        std::cout << "Recording integration checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        if (fixture) DestroyWindow(fixture);
        std::cerr << error.what() << '\n';
        std::wcerr << L"Test artifacts: " << root.wstring() << '\n';
        return 1;
    }
}
