#include "capture/WindowCaptureService.h"

#include <Windows.h>

#include <cstring>
#include <vector>

#include "common/GdiResources.h"
#include "common/RectUtils.h"
#include "common/Win32Error.h"

namespace {

constexpr UINT kPrintWindowRenderFullContent = 0x00000002;

}  // namespace

namespace capture {

common::Result<CapturedImage> WindowCaptureService::CaptureWindow(
    const WindowInfo& window,
    const DesktopSnapshot* fallback_snapshot) const {
    auto print_window_result = CaptureWithPrintWindow(window);
    if (print_window_result) {
        return print_window_result;
    }

    if (fallback_snapshot == nullptr) {
        return print_window_result;
    }

    auto snapshot_result = CaptureFromSnapshot(window, *fallback_snapshot);
    if (snapshot_result) {
        return snapshot_result;
    }

    return common::Result<CapturedImage>::Failure(L"窗口采集失败。\n\nPrintWindow 错误：\n" +
                                                  print_window_result.Error() +
                                                  L"\n\n回退到桌面裁剪也失败：\n" +
                                                  snapshot_result.Error());
}

common::Result<CapturedImage> WindowCaptureService::CaptureWithPrintWindow(
    const WindowInfo& window) const {
    const RECT capture_bounds =
        common::HasArea(window.window_rect) ? window.window_rect : window.bounds;
    const int width = common::RectWidth(capture_bounds);
    const int height = common::RectHeight(capture_bounds);
    if (width <= 0 || height <= 0) {
        return common::Result<CapturedImage>::Failure(L"目标窗口尺寸无效。");
    }

    common::WindowDcHandle screen_dc(nullptr, GetDC(nullptr));
    if (!screen_dc) {
        return common::Result<CapturedImage>::Failure(
            L"获取屏幕 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    common::UniqueDc memory_dc{CreateCompatibleDC(screen_dc.Get())};
    if (!memory_dc) {
        return common::Result<CapturedImage>::Failure(
            L"创建窗口截图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* raw_pixels = nullptr;
    common::UniqueBitmap bitmap{
        CreateDIBSection(screen_dc.Get(), &bitmap_info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0)};
    if (!bitmap || raw_pixels == nullptr) {
        return common::Result<CapturedImage>::Failure(
            L"创建窗口截图缓冲区失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    common::ScopedSelectObject selected_bitmap(memory_dc.Get(), bitmap.Get());
    if (!selected_bitmap.IsValid()) {
        return common::Result<CapturedImage>::Failure(
            L"选择窗口截图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    RECT fill_rect{0, 0, width, height};
    FillRect(memory_dc.Get(), &fill_rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    const BOOL printed =
        PrintWindow(window.handle, memory_dc.Get(), kPrintWindowRenderFullContent);
    if (!printed) {
        return common::Result<CapturedImage>::Failure(
            L"PrintWindow 调用失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    const std::size_t pixel_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> pixels(pixel_bytes);
    std::memcpy(pixels.data(), raw_pixels, pixel_bytes);

    CapturedImage captured_image(width, height, std::move(pixels));

    RECT visible_bounds = window.bounds;
    OffsetRect(&visible_bounds, -capture_bounds.left, -capture_bounds.top);

    auto cropped_image = captured_image.Crop(visible_bounds);
    if (!cropped_image) {
        return common::Result<CapturedImage>::Failure(L"PrintWindow 结果裁剪失败。\n\n" +
                                                      cropped_image.Error());
    }

    return common::Result<CapturedImage>::Success(std::move(cropped_image.Value()));
}

common::Result<CapturedImage> WindowCaptureService::CaptureFromSnapshot(
    const WindowInfo& window,
    const DesktopSnapshot& snapshot) const {
    RECT relative_bounds = window.bounds;
    OffsetRect(&relative_bounds, -snapshot.origin_x, -snapshot.origin_y);

    auto cropped_image = snapshot.image.Crop(relative_bounds);
    if (!cropped_image) {
        return common::Result<CapturedImage>::Failure(cropped_image.Error());
    }

    return common::Result<CapturedImage>::Success(std::move(cropped_image.Value()));
}

}  // namespace capture
