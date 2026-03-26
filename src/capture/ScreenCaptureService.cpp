#include "capture/ScreenCaptureService.h"

#include <Windows.h>

#include <cstring>
#include <vector>

#include "common/Win32Error.h"

namespace {

struct ScreenDcHandle {
    HDC value = nullptr;

    ~ScreenDcHandle() {
        if (value != nullptr) {
            ReleaseDC(nullptr, value);
        }
    }
};

struct MemoryDcHandle {
    HDC value = nullptr;

    ~MemoryDcHandle() {
        if (value != nullptr) {
            DeleteDC(value);
        }
    }
};

struct BitmapHandle {
    HBITMAP value = nullptr;

    ~BitmapHandle() {
        if (value != nullptr) {
            DeleteObject(value);
        }
    }
};

}  // namespace

namespace capture {

std::optional<CapturedImage> ScreenCaptureService::CaptureDesktop(
    std::wstring& error_message) const {
    auto snapshot = CaptureDesktopSnapshot(error_message);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    return std::move(snapshot->image);
}

std::optional<DesktopSnapshot> ScreenCaptureService::CaptureDesktopSnapshot(
    std::wstring& error_message) const {
    const int origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width <= 0 || height <= 0) {
        error_message = L"无法获取桌面尺寸。";
        return std::nullopt;
    }

    ScreenDcHandle screen_dc{GetDC(nullptr)};
    if (screen_dc.value == nullptr) {
        error_message = L"获取屏幕 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    MemoryDcHandle memory_dc{CreateCompatibleDC(screen_dc.value)};
    if (memory_dc.value == nullptr) {
        error_message = L"创建内存 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* raw_pixels = nullptr;
    BitmapHandle bitmap{
        CreateDIBSection(screen_dc.value, &bitmap_info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0)};
    if (bitmap.value == nullptr || raw_pixels == nullptr) {
        error_message = L"创建截图缓冲区失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    const HGDIOBJ previous_object = SelectObject(memory_dc.value, bitmap.value);
    if (previous_object == nullptr || previous_object == HGDI_ERROR) {
        error_message = L"选择截图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    const BOOL copied = BitBlt(memory_dc.value,
                               0,
                               0,
                               width,
                               height,
                               screen_dc.value,
                               origin_x,
                               origin_y,
                               SRCCOPY | CAPTUREBLT);
    SelectObject(memory_dc.value, previous_object);

    if (!copied) {
        error_message = L"拷贝桌面图像失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    const std::size_t pixel_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> pixels(pixel_bytes);
    std::memcpy(pixels.data(), raw_pixels, pixel_bytes);

    DesktopSnapshot snapshot{};
    snapshot.origin_x = origin_x;
    snapshot.origin_y = origin_y;
    snapshot.image = CapturedImage(width, height, std::move(pixels));
    return snapshot;
}

}  // namespace capture
