#include "capture/ScreenCaptureService.h"

#include <Windows.h>

#include <cstring>
#include <vector>

#include "common/GdiResources.h"
#include "common/Win32Error.h"

namespace capture {

common::Result<CapturedImage> ScreenCaptureService::CaptureDesktop() const {
    auto snapshot = CaptureDesktopSnapshot();
    if (!snapshot) {
        return common::Result<CapturedImage>::Failure(snapshot.Error());
    }

    return common::Result<CapturedImage>::Success(std::move(snapshot.Value().image));
}

common::Result<DesktopSnapshot> ScreenCaptureService::CaptureDesktopSnapshot() const {
    const int origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width <= 0 || height <= 0) {
        return common::Result<DesktopSnapshot>::Failure(L"无法获取桌面尺寸。");
    }

    common::WindowDcHandle screen_dc(nullptr, GetDC(nullptr));
    if (!screen_dc) {
        return common::Result<DesktopSnapshot>::Failure(
            L"获取屏幕 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    common::UniqueDc memory_dc{CreateCompatibleDC(screen_dc.Get())};
    if (!memory_dc) {
        return common::Result<DesktopSnapshot>::Failure(
            L"创建内存 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
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
        return common::Result<DesktopSnapshot>::Failure(
            L"创建桌面截图缓冲区失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    common::ScopedSelectObject selected_bitmap(memory_dc.Get(), bitmap.Get());
    if (!selected_bitmap.IsValid()) {
        return common::Result<DesktopSnapshot>::Failure(
            L"选择截图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    const BOOL copied = BitBlt(memory_dc.Get(),
                               0,
                               0,
                               width,
                               height,
                               screen_dc.Get(),
                               origin_x,
                               origin_y,
                               SRCCOPY | CAPTUREBLT);
    if (!copied) {
        return common::Result<DesktopSnapshot>::Failure(
            L"复制桌面图像失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    const std::size_t pixel_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> pixels(pixel_bytes);
    std::memcpy(pixels.data(), raw_pixels, pixel_bytes);

    DesktopSnapshot snapshot{};
    snapshot.origin_x = origin_x;
    snapshot.origin_y = origin_y;
    snapshot.image = CapturedImage(width, height, std::move(pixels));
    return common::Result<DesktopSnapshot>::Success(std::move(snapshot));
}

}  // namespace capture
