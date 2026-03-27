#include "editing/ImageMarkupService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common/RectUtils.h"

namespace {

constexpr double kArrowHeadAngleRadians = 0.5235987755982988;  // 30 degrees

RECT ClampToImage(const capture::CapturedImage& image, const RECT& rect) {
    return common::ClampRectToBounds(rect, image.Width(), image.Height());
}

POINT ClampPointToRect(const POINT& point, const RECT& rect) {
    POINT clamped{};
    clamped.x = std::clamp(point.x, rect.left, rect.right - 1);
    clamped.y = std::clamp(point.y, rect.top, rect.bottom - 1);
    return clamped;
}

void WritePixel(capture::CapturedImage& image, int x, int y, COLORREF color) {
    if (x < 0 || x >= image.Width() || y < 0 || y >= image.Height()) {
        return;
    }

    auto& pixels = image.Pixels();
    const std::size_t offset = (static_cast<std::size_t>(y) * image.RowStride()) +
                               (static_cast<std::size_t>(x) * 4);
    pixels[offset + 0] = GetBValue(color);
    pixels[offset + 1] = GetGValue(color);
    pixels[offset + 2] = GetRValue(color);
    pixels[offset + 3] = 0xFF;
}

void StampDisk(capture::CapturedImage& image,
               int center_x,
               int center_y,
               int radius,
               COLORREF color,
               const RECT& clip_rect) {
    const int clamped_radius = std::max(radius, 1);
    for (int y = center_y - clamped_radius; y <= center_y + clamped_radius; ++y) {
        for (int x = center_x - clamped_radius; x <= center_x + clamped_radius; ++x) {
            if (x < clip_rect.left || x >= clip_rect.right || y < clip_rect.top ||
                y >= clip_rect.bottom) {
                continue;
            }

            const int dx = x - center_x;
            const int dy = y - center_y;
            if ((dx * dx) + (dy * dy) <= (clamped_radius * clamped_radius)) {
                WritePixel(image, x, y, color);
            }
        }
    }
}

void DrawStrokeLine(capture::CapturedImage& image,
                    const POINT& start,
                    const POINT& end,
                    int thickness,
                    COLORREF color,
                    const RECT& clip_rect) {
    const int radius = std::max(1, thickness / 2);
    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    const double max_distance = std::max(std::abs(dx), std::abs(dy));

    if (max_distance < 1.0) {
        StampDisk(image, start.x, start.y, radius, color, clip_rect);
        return;
    }

    const int steps = static_cast<int>(std::ceil(max_distance));
    for (int index = 0; index <= steps; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(steps);
        const int x = static_cast<int>(std::lround(static_cast<double>(start.x) + (dx * t)));
        const int y = static_cast<int>(std::lround(static_cast<double>(start.y) + (dy * t)));
        StampDisk(image, x, y, radius, color, clip_rect);
    }
}

}  // namespace

namespace editing {

bool ImageMarkupService::ApplyMosaic(capture::CapturedImage& image,
                                     const RECT& region,
                                     int block_size,
                                     std::wstring& error_message) {
    if (image.IsEmpty()) {
        error_message = L"当前没有可编辑的图像。";
        return false;
    }

    const RECT clamped = ClampToImage(image, region);
    if (!common::HasArea(clamped)) {
        error_message = L"马赛克区域无效。";
        return false;
    }

    const int tile_size = std::max(4, block_size);
    auto& pixels = image.Pixels();
    const std::size_t stride = image.RowStride();

    for (int top = clamped.top; top < clamped.bottom; top += tile_size) {
        for (int left = clamped.left; left < clamped.right; left += tile_size) {
            const int right =
                std::min(static_cast<int>(clamped.right), left + tile_size);
            const int bottom =
                std::min(static_cast<int>(clamped.bottom), top + tile_size);

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

            const std::uint8_t average_blue =
                static_cast<std::uint8_t>(blue_sum / pixel_count);
            const std::uint8_t average_green =
                static_cast<std::uint8_t>(green_sum / pixel_count);
            const std::uint8_t average_red = static_cast<std::uint8_t>(red_sum / pixel_count);
            const std::uint8_t average_alpha =
                static_cast<std::uint8_t>(alpha_sum / pixel_count);

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

    return true;
}

bool ImageMarkupService::DrawArrow(capture::CapturedImage& image,
                                   const RECT& clip_bounds,
                                   const POINT& start,
                                   const POINT& end,
                                   COLORREF color,
                                   int thickness,
                                   std::wstring& error_message) {
    if (image.IsEmpty()) {
        error_message = L"当前没有可编辑的图像。";
        return false;
    }

    const RECT clamped_clip = ClampToImage(image, clip_bounds);
    if (!common::HasArea(clamped_clip)) {
        error_message = L"箭头绘制区域无效。";
        return false;
    }

    POINT clamped_start = ClampPointToRect(start, clamped_clip);
    POINT clamped_end = ClampPointToRect(end, clamped_clip);
    const double dx = static_cast<double>(clamped_end.x - clamped_start.x);
    const double dy = static_cast<double>(clamped_end.y - clamped_start.y);
    const double length = std::hypot(dx, dy);

    if (length < 1.0) {
        error_message = L"箭头长度过短。";
        return false;
    }

    const int stroke_thickness = std::max(2, thickness);
    DrawStrokeLine(image, clamped_start, clamped_end, stroke_thickness, color, clamped_clip);

    const double unit_x = dx / length;
    const double unit_y = dy / length;
    const double head_length = std::max(14.0, static_cast<double>(stroke_thickness) * 4.0);
    const double back_x = -unit_x;
    const double back_y = -unit_y;

    const auto rotate = [](double x, double y, double angle) {
        POINT result{};
        result.x = static_cast<LONG>(
            std::lround((x * std::cos(angle)) - (y * std::sin(angle))));
        result.y = static_cast<LONG>(
            std::lround((x * std::sin(angle)) + (y * std::cos(angle))));
        return result;
    };

    const POINT left_direction = rotate(back_x * head_length, back_y * head_length, kArrowHeadAngleRadians);
    const POINT right_direction =
        rotate(back_x * head_length, back_y * head_length, -kArrowHeadAngleRadians);

    const POINT left_point{clamped_end.x + left_direction.x, clamped_end.y + left_direction.y};
    const POINT right_point{clamped_end.x + right_direction.x, clamped_end.y + right_direction.y};

    DrawStrokeLine(image, clamped_end, left_point, stroke_thickness, color, clamped_clip);
    DrawStrokeLine(image, clamped_end, right_point, stroke_thickness, color, clamped_clip);
    return true;
}

bool ImageMarkupService::DrawRectangle(capture::CapturedImage& image,
                                       const RECT& clip_bounds,
                                       const RECT& rect,
                                       COLORREF color,
                                       int thickness,
                                       std::wstring& error_message) {
    if (image.IsEmpty()) {
        error_message = L"当前没有可编辑的图像。";
        return false;
    }

    const RECT clamped_clip = ClampToImage(image, clip_bounds);
    if (!common::HasArea(clamped_clip)) {
        error_message = L"矩形绘制区域无效。";
        return false;
    }

    const RECT normalized_rect =
        common::NormalizeRect(POINT{rect.left, rect.top}, POINT{rect.right, rect.bottom});
    RECT outline_rect{};
    IntersectRect(&outline_rect, &normalized_rect, &clamped_clip);
    if (!common::HasArea(outline_rect)) {
        error_message = L"矩形区域无效。";
        return false;
    }

    const int stroke_thickness = std::max(2, thickness);
    const POINT top_left{outline_rect.left, outline_rect.top};
    const POINT top_right{outline_rect.right - 1, outline_rect.top};
    const POINT bottom_left{outline_rect.left, outline_rect.bottom - 1};
    const POINT bottom_right{outline_rect.right - 1, outline_rect.bottom - 1};

    DrawStrokeLine(image, top_left, top_right, stroke_thickness, color, clamped_clip);
    DrawStrokeLine(image, top_right, bottom_right, stroke_thickness, color, clamped_clip);
    DrawStrokeLine(image, bottom_right, bottom_left, stroke_thickness, color, clamped_clip);
    DrawStrokeLine(image, bottom_left, top_left, stroke_thickness, color, clamped_clip);
    return true;
}

}  // namespace editing
