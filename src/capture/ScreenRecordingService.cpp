#include "capture/ScreenRecordingService.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <wrl/client.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "common/GdiResources.h"
#include "common/Win32Error.h"
#include "capture/WindowCaptureService.h"
#include "capture/WindowLocator.h"

namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT32 kFrameRate = 30;
constexpr LONGLONG kFrameDuration = 10'000'000 / kFrameRate;

void Check(HRESULT result) {
    if (FAILED(result)) {
        throw result;
    }
}

struct MediaRuntime {
    bool com_initialized = false;
    bool media_initialized = false;
    ~MediaRuntime() {
        if (media_initialized) MFShutdown();
        if (com_initialized) CoUninitialize();
    }
};

void SetVideoType(IMFMediaType* type, const GUID& format, UINT32 width, UINT32 height) {
    Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    Check(type->SetGUID(MF_MT_SUBTYPE, format));
    Check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    Check(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height));
    Check(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, kFrameRate, 1));
    Check(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
}

void ConfigureQuality(IMFSinkWriter* writer, DWORD stream_index) {
    ComPtr<ICodecAPI> codec;
    if (FAILED(writer->GetServiceForStream(stream_index, GUID_NULL, IID_PPV_ARGS(&codec)))) return;
    const auto set_uint = [&codec](const GUID& property, ULONG value) {
        VARIANT setting{};
        setting.vt = VT_UI4;
        setting.ulVal = value;
        return codec->SetValue(&property, &setting);
    };
    // Windows 8+ accepts these settings after the media types have been set.
    // Keep the generous resolution-dependent bitrate if quality VBR is unavailable.
    if (SUCCEEDED(set_uint(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_Quality))) {
        set_uint(CODECAPI_AVEncCommonQuality, 100);
        set_uint(CODECAPI_AVEncVideoEncodeQP, 16);
    }
    set_uint(CODECAPI_AVEncCommonQualityVsSpeed, 75);
    set_uint(CODECAPI_AVEncMPVGOPSize, kFrameRate * 2);
    VARIANT cabac{};
    cabac.vt = VT_BOOL;
    cabac.boolVal = VARIANT_TRUE;
    codec->SetValue(&CODECAPI_AVEncH264CABACEnable, &cabac);
}

void DrawCursor(HDC dc, const RECT& region, int width, int height) {
    CURSORINFO cursor{sizeof(CURSORINFO)};
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING)) return;
    ICONINFO icon{};
    if (!GetIconInfo(cursor.hCursor, &icon)) return;
    common::UniqueBitmap mask(icon.hbmMask);
    common::UniqueBitmap color(icon.hbmColor);
    const double scale_x = static_cast<double>(width) / (region.right - region.left);
    const double scale_y = static_cast<double>(height) / (region.bottom - region.top);
    BITMAP dimensions{};
    GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(dimensions), &dimensions);
    const int cursor_height = icon.hbmColor ? dimensions.bmHeight : dimensions.bmHeight / 2;
    DrawIconEx(dc,
               static_cast<int>((cursor.ptScreenPos.x - region.left - static_cast<LONG>(icon.xHotspot)) * scale_x),
               static_cast<int>((cursor.ptScreenPos.y - region.top - static_cast<LONG>(icon.yHotspot)) * scale_y),
               cursor.hCursor,
               std::max(1, static_cast<int>(dimensions.bmWidth * scale_x)),
               std::max(1, static_cast<int>(cursor_height * scale_y)),
               0, nullptr, DI_NORMAL);
}

}  // namespace

namespace capture {

ScreenRecordingService::~ScreenRecordingService() {
    Stop();
}

common::Result<void> ScreenRecordingService::Start(const RECT& region, const std::wstring& path) {
    return StartSource(region, nullptr, path);
}

common::Result<void> ScreenRecordingService::StartWindow(HWND window, const std::wstring& path) {
    DwmFlush();
    const auto info = WindowLocator{}.DescribeWindow(window);
    if (!info || !IsWindowVisible(window) || IsIconic(window)) {
        return common::Result<void>::Failure(L"请选择一个可见且未最小化的窗口。");
    }
    return StartSource(info->bounds, window, path);
}

RECT ScreenRecordingService::CurrentRegion() const {
    const std::lock_guard lock(region_mutex_);
    return current_region_;
}

common::Result<void> ScreenRecordingService::StartSource(const RECT& region, HWND window, const std::wstring& path) {
    if (IsActive()) return common::Result<void>::Failure(L"已有录屏任务，请先停止。");
    const auto width = static_cast<long long>(region.right) - region.left;
    const auto height = static_cast<long long>(region.bottom) - region.top;
    if (width < 2 || height < 2 || width >= INT_MAX || height >= INT_MAX || path.empty()) {
        return common::Result<void>::Failure(L"录屏区域或文件路径无效。");
    }
    if ((width + 1) * (height + 1) > MAXDWORD / 4) {
        return common::Result<void>::Failure(L"录屏区域过大，请选择单个屏幕、窗口或较小区域。");
    }
    if (std::filesystem::path(path).extension() != L".mp4") {
        return common::Result<void>::Failure(L"录屏文件必须使用 .mp4 扩展名。");
    }
    stop_requested_ = false;
    finished_ = false;
    error_.clear();
    path_ = path;
    { const std::lock_guard lock(region_mutex_); current_region_ = region; }
    std::promise<common::Result<void>> ready;
    auto initialized = ready.get_future();
    try {
        worker_ = std::thread(&ScreenRecordingService::Record, this, region, window, path, std::move(ready));
    } catch (const std::exception&) {
        return common::Result<void>::Failure(L"创建录屏线程失败。");
    }
    auto result = initialized.get();
    if (!result) worker_.join();
    return result;
}

void ScreenRecordingService::RequestStop() {
    stop_requested_ = true;
    wake_.notify_all();
}

common::Result<std::wstring> ScreenRecordingService::Stop() {
    RequestStop();
    if (worker_.joinable()) worker_.join();
    if (!error_.empty()) return common::Result<std::wstring>::Failure(error_);
    return common::Result<std::wstring>::Success(path_);
}

bool ScreenRecordingService::IsActive() const { return worker_.joinable(); }
bool ScreenRecordingService::IsFinished() const { return finished_.load(); }

void ScreenRecordingService::Record(RECT region, HWND window, std::wstring path,
                                    std::promise<common::Result<void>> ready) {
    bool ready_sent = false;
    bool writing = false;
    bool file_created = false;
    const wchar_t* stage = L"初始化 COM";
    MediaRuntime runtime;
    ComPtr<IMFSinkWriter> writer;
    try {
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        runtime.com_initialized = true;
        stage = L"初始化 Windows 媒体组件";
        Check(MFStartup(MF_VERSION));
        runtime.media_initialized = true;

        const int source_width = region.right - region.left;
        const int source_height = region.bottom - region.top;
        // H.264 requires even dimensions. Pad odd edges instead of resizing or
        // discarding a source row/column, so every captured pixel is preserved.
        const int width = (source_width + 1) & ~1;
        const int height = (source_height + 1) & ~1;

        common::WindowDcHandle screen(nullptr, GetDC(nullptr));
        common::UniqueDc frame_dc(CreateCompatibleDC(screen.Get()));
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        // RGB32 in Media Foundation uses bottom-up DIB memory.
        info.bmiHeader.biHeight = height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        common::UniqueBitmap bitmap(CreateDIBSection(screen.Get(), &info, DIB_RGB_COLORS,
                                                     &pixels, nullptr, 0));
        if (!screen || !frame_dc || !bitmap || !pixels) Check(E_OUTOFMEMORY);
        common::ScopedSelectObject selected(frame_dc.Get(), bitmap.Get());
        if (!selected.IsValid()) Check(E_FAIL);
        PatBlt(frame_dc.Get(), 0, 0, width, height, BLACKNESS);

        // Reserve a unique file instead of overwriting an existing recording.
        stage = L"创建 MP4 文件";
        ComPtr<IMFByteStream> stream;
        Check(MFCreateFile(MF_ACCESSMODE_WRITE, MF_OPENMODE_FAIL_IF_EXIST, MF_FILEFLAGS_NONE,
                           path.c_str(), &stream));
        file_created = true;
        stage = L"创建视频编码器";
        ComPtr<IMFAttributes> writer_attributes;
        Check(MFCreateAttributes(&writer_attributes, 1));
        Check(writer_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
        Check(MFCreateSinkWriterFromURL(path.c_str(), stream.Get(), writer_attributes.Get(), &writer));
        ComPtr<IMFMediaType> output;
        Check(MFCreateMediaType(&output));
        SetVideoType(output.Get(), MFVideoFormat_H264, width, height);
        Check(output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High));
        const auto bitrate = std::clamp<unsigned long long>(
            static_cast<unsigned long long>(width) * height * kFrameRate / 3,
            12'000'000, 200'000'000);
        Check(output->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate)));
        DWORD stream_index = 0;
        Check(writer->AddStream(output.Get(), &stream_index));
        ComPtr<IMFMediaType> input;
        Check(MFCreateMediaType(&input));
        SetVideoType(input.Get(), MFVideoFormat_RGB32, width, height);
        stage = L"配置视频编码器";
        Check(writer->SetInputMediaType(stream_index, input.Get(), nullptr));
        ConfigureQuality(writer.Get(), stream_index);
        Check(writer->BeginWriting());
        writing = true;
        stage = L"采集与编码画面";

        const DWORD byte_count = static_cast<DWORD>(static_cast<unsigned long long>(width) * height * 4);
        const auto started = std::chrono::steady_clock::now();
        LONGLONG previous_time = -1;
        do {
            const auto captured_at = std::chrono::steady_clock::now();
            if (window) {
                // A closed, hidden or minimized target ends the recording and finalizes its MP4.
                if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) break;
                const auto target = WindowLocator{}.DescribeWindow(window);
                if (!target) break;
                region = target->bounds;
                // Recording uses the starting window size. A resize ends and
                // saves this recording rather than resampling its text.
                if (region.right - region.left != source_width ||
                    region.bottom - region.top != source_height) break;
                { const std::lock_guard lock(region_mutex_); current_region_ = region; }
                const auto captured = WindowCaptureService{}.CaptureWindow(*target, nullptr);
                if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) break;
                if (!captured) Check(E_FAIL);
                const auto& image = captured.Value();
                if (image.Width() != source_width || image.Height() != source_height) break;
                const auto bitmap_info = image.CreateBitmapInfo();
                if (!SetDIBitsToDevice(frame_dc.Get(), 0, 0, source_width, source_height,
                    0, 0, 0, source_height, image.Pixels().data(),
                    &bitmap_info, DIB_RGB_COLORS)) Check(E_FAIL);
                CURSORINFO cursor{sizeof(cursor)};
                if (GetCursorInfo(&cursor) && PtInRect(&region, cursor.ptScreenPos) &&
                    GetAncestor(WindowFromPoint(cursor.ptScreenPos), GA_ROOT) == window) {
                    SaveDC(frame_dc.Get());
                    IntersectClipRect(frame_dc.Get(), 0, 0, source_width, source_height);
                    DrawCursor(frame_dc.Get(), region, source_width, source_height);
                    RestoreDC(frame_dc.Get(), -1);
                }
            } else {
                if (!BitBlt(frame_dc.Get(), 0, 0, source_width, source_height, screen.Get(), region.left,
                            region.top, SRCCOPY | CAPTUREBLT)) {
                    const DWORD error = GetLastError();
                    Check(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
                }
                SaveDC(frame_dc.Get());
                IntersectClipRect(frame_dc.Get(), 0, 0, source_width, source_height);
                DrawCursor(frame_dc.Get(), region, source_width, source_height);
                RestoreDC(frame_dc.Get(), -1);
            }
            GdiFlush();
            ComPtr<IMFMediaBuffer> buffer;
            Check(MFCreateMemoryBuffer(byte_count, &buffer));
            BYTE* destination = nullptr;
            Check(buffer->Lock(&destination, nullptr, nullptr));
            std::memcpy(destination, pixels, byte_count);
            Check(buffer->Unlock());
            Check(buffer->SetCurrentLength(byte_count));
            ComPtr<IMFSample> sample;
            Check(MFCreateSample(&sample));
            Check(sample->AddBuffer(buffer.Get()));
            // Wall-clock timestamps keep slow capture/encoding from speeding up playback.
            const LONGLONG elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                captured_at - started).count() / 100;
            const LONGLONG time = previous_time < 0 ? 0 : std::max(previous_time + 1, elapsed);
            Check(sample->SetSampleTime(time));
            Check(sample->SetSampleDuration(kFrameDuration));
            Check(writer->WriteSample(stream_index, sample.Get()));
            previous_time = time;
            if (!ready_sent) {
                ready.set_value(common::Result<void>::Success());
                ready_sent = true;
            }
            std::unique_lock lock(wait_mutex_);
            wake_.wait_until(lock, captured_at + std::chrono::nanoseconds(1'000'000'000 / kFrameRate),
                             [this] { return stop_requested_.load(); });
        } while (!stop_requested_);
        stage = L"保存视频";
        const HRESULT finalized = writer->Finalize();
        writing = false;
        Check(finalized);
    } catch (HRESULT result) {
        error_ = std::wstring(L"录屏失败（") + stage + L"）：" +
                 common::GetLastErrorMessage(static_cast<DWORD>(result)) +
                 L"\n文件：" + path + L"\n请检查保存目录、磁盘空间及 Windows 媒体组件。";
    } catch (const std::exception&) {
        error_ = L"录屏失败：无法分配录屏资源。";
    }
    if (writing) writer->Finalize();
    writer.Reset();
    if (!ready_sent && file_created) DeleteFileW(path.c_str());
    if (!ready_sent) ready.set_value(common::Result<void>::Failure(error_.empty() ? L"目标窗口已关闭或最小化，未开始录制。" : error_));
    finished_ = true;
}

}  // namespace capture
