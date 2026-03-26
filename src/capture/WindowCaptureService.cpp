#include "capture/WindowCaptureService.h"

#include <Windows.h>

#include <cstring>
#include <vector>

#include "common/RectUtils.h"
#include "common/Win32Error.h"

namespace {

constexpr UINT kPrintWindowRenderFullContent = 0x00000002;

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

std::optional<CapturedImage> WindowCaptureService::CaptureWindow(
    const WindowInfo& window,
    const DesktopSnapshot* fallback_snapshot,
    std::wstring& error_message) const {
    std::wstring print_window_error;
    auto captured = CaptureWithPrintWindow(window, print_window_error);
    if (captured.has_value()) {
        return captured;
    }

    if (fallback_snapshot != nullptr) {
        auto fallback = CaptureFromSnapshot(window, *fallback_snapshot, error_message);
        if (fallback.has_value()) {
            return fallback;
        }

        error_message = L"窗口采集失败。\n\nPrintWindow 错误：\n" + print_window_error +
                        L"\n\n回退到桌面裁剪也失败：\n" + error_message;
        return std::nullopt;
    }

    error_message = print_window_error;
    return std::nullopt;
}

std::optional<CapturedImage> WindowCaptureService::CaptureWithPrintWindow(
    const WindowInfo& window,
    std::wstring& error_message) const {
    const int width = common::RectWidth(window.bounds);
    const int height = common::RectHeight(window.bounds);
    if (width <= 0 || height <= 0) {
        error_message = L"目标窗口尺寸无效。";
        return std::nullopt;
    }

    ScreenDcHandle screen_dc{GetDC(nullptr)};
    if (screen_dc.value == nullptr) {
        error_message = L"获取屏幕 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    MemoryDcHandle memory_dc{CreateCompatibleDC(screen_dc.value)};
    if (memory_dc.value == nullptr) {
        error_message = L"创建窗口截图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
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
        error_message = L"创建窗口截图缓冲区失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    const HGDIOBJ previous_object = SelectObject(memory_dc.value, bitmap.value);
    if (previous_object == nullptr || previous_object == HGDI_ERROR) {
        error_message = L"选择窗口截图位图失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    RECT fill_rect{0, 0, width, height};
    FillRect(memory_dc.value, &fill_rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    const BOOL printed =
        PrintWindow(window.handle, memory_dc.value, kPrintWindowRenderFullContent);
    SelectObject(memory_dc.value, previous_object);

    if (!printed) {
        error_message = L"PrintWindow 调用失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    const std::size_t pixel_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> pixels(pixel_bytes);
    std::memcpy(pixels.data(), raw_pixels, pixel_bytes);

    return CapturedImage(width, height, std::move(pixels));
}

std::optional<CapturedImage> WindowCaptureService::CaptureFromSnapshot(
    const WindowInfo& window,
    const DesktopSnapshot& snapshot,
    std::wstring& error_message) const {
    RECT relative_bounds = window.bounds;
    OffsetRect(&relative_bounds, -snapshot.origin_x, -snapshot.origin_y);
    return snapshot.image.Crop(relative_bounds, error_message);
}

}  // namespace capture
