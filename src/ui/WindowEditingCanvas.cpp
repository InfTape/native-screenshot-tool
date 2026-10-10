#include "ui/WindowEditingCanvas.h"

#include <algorithm>
#include <vector>

namespace ui {

common::Result<WindowEditingCanvas> CreateWindowEditingCanvas(
    const capture::DesktopSnapshot& desktop,
    const capture::CapturedImage& window_image, const POINT& window_origin) {
    if (desktop.image.IsEmpty() || window_image.IsEmpty()) {
        return common::Result<WindowEditingCanvas>::Failure(L"无法创建窗口截图编辑画布。");
    }
    const int left = std::min(desktop.origin_x, static_cast<int>(window_origin.x));
    const int top = std::min(desktop.origin_y, static_cast<int>(window_origin.y));
    const int right = std::max(desktop.origin_x + desktop.image.Width(),
                               static_cast<int>(window_origin.x) + window_image.Width());
    const int bottom = std::max(desktop.origin_y + desktop.image.Height(),
                                static_cast<int>(window_origin.y) + window_image.Height());
    const int width = right - left;
    const int height = bottom - top;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    const auto paste = [&](const capture::CapturedImage& image, int x, int y) {
        const auto& source = image.Pixels();
        for (int row = 0; row < image.Height(); ++row) {
            const std::size_t offset = (static_cast<std::size_t>(y + row) * width + x) * 4;
            std::copy_n(source.data() + static_cast<std::size_t>(row) * image.RowStride(),
                        image.RowStride(), pixels.data() + offset);
        }
    };
    paste(desktop.image, desktop.origin_x - left, desktop.origin_y - top);
    paste(window_image, window_origin.x - left, window_origin.y - top);
    WindowEditingCanvas canvas;
    canvas.snapshot.origin_x = left;
    canvas.snapshot.origin_y = top;
    canvas.snapshot.image = capture::CapturedImage(width, height, std::move(pixels));
    canvas.selection = {window_origin.x - left, window_origin.y - top,
                        window_origin.x - left + window_image.Width(),
                        window_origin.y - top + window_image.Height()};
    canvas.desktop_bounds = {desktop.origin_x - left, desktop.origin_y - top,
                              desktop.origin_x - left + desktop.image.Width(),
                              desktop.origin_y - top + desktop.image.Height()};
    return common::Result<WindowEditingCanvas>::Success(std::move(canvas));
}

RECT WindowEditingInstructionArea(const RECT& desktop_bounds, const RECT& selection, UINT dpi) {
    const int width = std::min<int>(desktop_bounds.right - desktop_bounds.left, MulDiv(592, dpi, 96));
    const int height = std::min<int>(desktop_bounds.bottom - desktop_bounds.top, MulDiv(86, dpi, 96));
    // DrawInstructions adds its own 16-DIP margin. Prefer a desktop corner that
    // does not cover the selected window; a maximized window uses the first corner.
    for (const RECT& area : {
             RECT{desktop_bounds.left, desktop_bounds.top,
                  desktop_bounds.left + width, desktop_bounds.top + height},
             RECT{desktop_bounds.right - width, desktop_bounds.top,
                  desktop_bounds.right, desktop_bounds.top + height},
             RECT{desktop_bounds.left, desktop_bounds.bottom - height,
                  desktop_bounds.left + width, desktop_bounds.bottom},
             RECT{desktop_bounds.right - width, desktop_bounds.bottom - height,
                  desktop_bounds.right, desktop_bounds.bottom}}) {
        RECT overlap{};
        if (!IntersectRect(&overlap, &area, &selection)) return area;
    }
    return desktop_bounds;
}

}  // namespace ui
