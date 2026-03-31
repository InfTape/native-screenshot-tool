#include "editing/ImageMarkupService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common/Direct2D.h"
#include "common/GdiResources.h"
#include "common/RectUtils.h"

namespace {

RECT ClampToImage(const capture::CapturedImage& image, const RECT& rect) {
    return common::ClampRectToBounds(rect, image.Width(), image.Height());
}

POINT ClampPointToRect(const POINT& point, const RECT& rect) {
    POINT clamped{};
    clamped.x = std::clamp(point.x, rect.left, rect.right - 1);
    clamped.y = std::clamp(point.y, rect.top, rect.bottom - 1);
    return clamped;
}

void SetAlphaOpaque(capture::CapturedImage& image, const RECT& rect) {
    const RECT clamped_rect = ClampToImage(image, rect);
    if (!common::HasArea(clamped_rect)) {
        return;
    }

    auto& pixels = image.Pixels();
    for (int y = clamped_rect.top; y < clamped_rect.bottom; ++y) {
        for (int x = clamped_rect.left; x < clamped_rect.right; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * image.RowStride()) + (static_cast<std::size_t>(x) * 4);
            pixels[offset + 3] = 0xFF;
        }
    }
}

}  // namespace

namespace editing {

common::Result<void> ImageMarkupService::ApplyCommand(capture::CapturedImage& image,
                                                      const MarkupCommand& command) {
    switch (command.tool) {
    case MarkupTool::Rectangle:
        return DrawRectangle(
            image, command.clip_bounds, command.rect, command.color, command.thickness);
    case MarkupTool::Mosaic:
        return ApplyMosaic(image, command.rect, command.block_size);
    case MarkupTool::Arrow:
        return DrawArrow(
            image, command.clip_bounds, command.start, command.end, command.color, command.thickness);
    case MarkupTool::Select:
    default:
        return common::Result<void>::Failure(L"不支持的标注操作。");
    }
}

common::Result<void> ImageMarkupService::ApplyMosaic(capture::CapturedImage& image,
                                                     const RECT& region,
                                                     int block_size) {
    if (image.IsEmpty()) {
        return common::Result<void>::Failure(L"当前没有可编辑的图像。");
    }

    const RECT clamped = ClampToImage(image, region);
    if (!common::HasArea(clamped)) {
        return common::Result<void>::Failure(L"马赛克区域无效。");
    }

    const int tile_size = std::max(4, block_size);
    auto& pixels = image.Pixels();
    const std::size_t stride = image.RowStride();

    for (int top = clamped.top; top < clamped.bottom; top += tile_size) {
        for (int left = clamped.left; left < clamped.right; left += tile_size) {
            const int right = std::min(static_cast<int>(clamped.right), left + tile_size);
            const int bottom = std::min(static_cast<int>(clamped.bottom), top + tile_size);

            std::uint64_t blue_sum = 0;
            std::uint64_t green_sum = 0;
            std::uint64_t red_sum = 0;
            std::uint64_t alpha_sum = 0;
            std::uint64_t pixel_count = 0;

            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * stride) + (static_cast<std::size_t>(x) * 4);
                    blue_sum += pixels[offset + 0];
                    green_sum += pixels[offset + 1];
                    red_sum += pixels[offset + 2];
                    alpha_sum += pixels[offset + 3];
                    ++pixel_count;
                }
            }

            if (pixel_count == 0) {
                continue;
            }

            const std::uint8_t average_blue = static_cast<std::uint8_t>(blue_sum / pixel_count);
            const std::uint8_t average_green = static_cast<std::uint8_t>(green_sum / pixel_count);
            const std::uint8_t average_red = static_cast<std::uint8_t>(red_sum / pixel_count);
            const std::uint8_t average_alpha = static_cast<std::uint8_t>(alpha_sum / pixel_count);

            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * stride) + (static_cast<std::size_t>(x) * 4);
                    pixels[offset + 0] = average_blue;
                    pixels[offset + 1] = average_green;
                    pixels[offset + 2] = average_red;
                    pixels[offset + 3] = average_alpha;
                }
            }
        }
    }

    return common::Result<void>::Success();
}

common::Result<void> ImageMarkupService::DrawArrow(capture::CapturedImage& image,
                                                   const RECT& clip_bounds,
                                                   const POINT& start,
                                                   const POINT& end,
                                                   COLORREF color,
                                                   int thickness) {
    if (image.IsEmpty()) {
        return common::Result<void>::Failure(L"当前没有可编辑的图像。");
    }

    const RECT clamped_clip = ClampToImage(image, clip_bounds);
    if (!common::HasArea(clamped_clip)) {
        return common::Result<void>::Failure(L"箭头绘制区域无效。");
    }

    const POINT clamped_start = ClampPointToRect(start, clamped_clip);
    const POINT clamped_end = ClampPointToRect(end, clamped_clip);
    const double dx = static_cast<double>(clamped_end.x - clamped_start.x);
    const double dy = static_cast<double>(clamped_end.y - clamped_start.y);
    const double length = std::hypot(dx, dy);
    if (length < 1.0) {
        return common::Result<void>::Failure(L"箭头长度过短。");
    }

    common::UniqueDc memory_dc{CreateCompatibleDC(nullptr)};
    if (!memory_dc) {
        return common::Result<void>::Failure(L"创建箭头绘制缓冲失败。");
    }

    const BITMAPINFO bitmap_info = image.CreateBitmapInfo();
    void* bitmap_bits = nullptr;
    common::UniqueBitmap bitmap{
        CreateDIBSection(memory_dc.Get(), &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0)};
    if (!bitmap || bitmap_bits == nullptr) {
        return common::Result<void>::Failure(L"创建箭头绘制位图失败。");
    }

    common::ScopedSelectObject scoped_bitmap(memory_dc.Get(), bitmap.Get());
    if (!scoped_bitmap.IsValid()) {
        return common::Result<void>::Failure(L"绑定箭头绘制位图失败。");
    }

    auto& pixels = image.Pixels();
    std::copy(pixels.begin(), pixels.end(), static_cast<std::uint8_t*>(bitmap_bits));

    const RECT full_image_rect{0, 0, image.Width(), image.Height()};
    auto draw_result = common::DrawArrowOnHdc(memory_dc.Get(),
                                              full_image_rect,
                                              &clamped_clip,
                                              clamped_start,
                                              clamped_end,
                                              color,
                                              static_cast<float>(std::max(2, thickness)));
    if (!draw_result) {
        return common::Result<void>::Failure(draw_result.Error().empty()
                                                 ? L"Direct2D 绘制箭头失败。"
                                                 : draw_result.Error());
    }

    std::copy(static_cast<const std::uint8_t*>(bitmap_bits),
              static_cast<const std::uint8_t*>(bitmap_bits) + pixels.size(),
              pixels.begin());

    RECT dirty_rect = common::NormalizeRect(clamped_start, clamped_end);
    InflateRect(&dirty_rect, 28, 28);
    SetAlphaOpaque(image, dirty_rect);
    return common::Result<void>::Success();
}

common::Result<void> ImageMarkupService::DrawRectangle(capture::CapturedImage& image,
                                                       const RECT& clip_bounds,
                                                       const RECT& rect,
                                                       COLORREF color,
                                                       int thickness) {
    if (image.IsEmpty()) {
        return common::Result<void>::Failure(L"当前没有可编辑的图像。");
    }

    const RECT clamped_clip = ClampToImage(image, clip_bounds);
    if (!common::HasArea(clamped_clip)) {
        return common::Result<void>::Failure(L"矩形绘制区域无效。");
    }

    const RECT normalized_rect =
        common::NormalizeRect(POINT{rect.left, rect.top}, POINT{rect.right, rect.bottom});
    RECT outline_rect{};
    IntersectRect(&outline_rect, &normalized_rect, &clamped_clip);
    if (!common::HasArea(outline_rect)) {
        return common::Result<void>::Failure(L"矩形区域无效。");
    }

    common::UniqueDc memory_dc{CreateCompatibleDC(nullptr)};
    if (!memory_dc) {
        return common::Result<void>::Failure(L"创建矩形绘制缓冲失败。");
    }

    const BITMAPINFO bitmap_info = image.CreateBitmapInfo();
    void* bitmap_bits = nullptr;
    common::UniqueBitmap bitmap{
        CreateDIBSection(memory_dc.Get(), &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0)};
    if (!bitmap || bitmap_bits == nullptr) {
        return common::Result<void>::Failure(L"创建矩形绘制位图失败。");
    }

    common::ScopedSelectObject scoped_bitmap(memory_dc.Get(), bitmap.Get());
    if (!scoped_bitmap.IsValid()) {
        return common::Result<void>::Failure(L"绑定矩形绘制位图失败。");
    }

    auto& pixels = image.Pixels();
    std::copy(pixels.begin(), pixels.end(), static_cast<std::uint8_t*>(bitmap_bits));

    const RECT full_image_rect{0, 0, image.Width(), image.Height()};
    const int stroke_thickness = std::max(2, thickness);
    auto draw_result = common::DrawRectangleOnHdc(memory_dc.Get(),
                                                  full_image_rect,
                                                  &clamped_clip,
                                                  outline_rect,
                                                  color,
                                                  static_cast<float>(stroke_thickness));
    if (!draw_result) {
        return common::Result<void>::Failure(draw_result.Error().empty()
                                                 ? L"Direct2D 绘制矩形失败。"
                                                 : draw_result.Error());
    }

    std::copy(static_cast<const std::uint8_t*>(bitmap_bits),
              static_cast<const std::uint8_t*>(bitmap_bits) + pixels.size(),
              pixels.begin());

    RECT dirty_rect = outline_rect;
    InflateRect(&dirty_rect, stroke_thickness + 2, stroke_thickness + 2);
    SetAlphaOpaque(image, dirty_rect);
    return common::Result<void>::Success();
}

}  // namespace editing
