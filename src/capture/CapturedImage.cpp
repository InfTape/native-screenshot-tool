#include "capture/CapturedImage.h"

#include <algorithm>
#include <memory>

#include "common/RectUtils.h"

namespace {

const std::vector<std::uint8_t> kEmptyPixels;

}  // namespace

namespace capture {

CapturedImage::CapturedImage(int width, int height, std::vector<std::uint8_t> pixels)
    : width_(width),
      height_(height),
      pixels_(std::make_shared<std::vector<std::uint8_t>>(std::move(pixels))) {}

bool CapturedImage::IsEmpty() const {
    return width_ <= 0 || height_ <= 0 || pixels_ == nullptr || pixels_->empty();
}

int CapturedImage::Width() const {
    return width_;
}

int CapturedImage::Height() const {
    return height_;
}

std::size_t CapturedImage::RowStride() const {
    return static_cast<std::size_t>(width_) * 4;
}

std::vector<std::uint8_t>& CapturedImage::Pixels() {
    EnsureUniquePixels();
    return *pixels_;
}

const std::vector<std::uint8_t>& CapturedImage::Pixels() const {
    return pixels_ != nullptr ? *pixels_ : kEmptyPixels;
}

void CapturedImage::EnsureUniquePixels() {
    if (pixels_ == nullptr) {
        pixels_ = std::make_shared<std::vector<std::uint8_t>>();
        return;
    }

    if (pixels_.use_count() != 1) {
        pixels_ = std::make_shared<std::vector<std::uint8_t>>(*pixels_);
    }
}

std::optional<CapturedImage> CapturedImage::Crop(const RECT& region,
                                                 std::wstring& error_message) const {
    if (IsEmpty()) {
        error_message = L"当前没有可裁剪的图像。";
        return std::nullopt;
    }

    const RECT clamped = common::ClampRectToBounds(region, width_, height_);
    if (!common::HasArea(clamped)) {
        error_message = L"选区无效，无法生成截图。";
        return std::nullopt;
    }

    if (clamped.left == 0 && clamped.top == 0 && clamped.right == width_ &&
        clamped.bottom == height_) {
        return *this;
    }

    const int cropped_width = common::RectWidth(clamped);
    const int cropped_height = common::RectHeight(clamped);
    const std::size_t cropped_row_stride = static_cast<std::size_t>(cropped_width) * 4;
    const auto& source_pixels = Pixels();

    std::vector<std::uint8_t> cropped_pixels(cropped_row_stride *
                                             static_cast<std::size_t>(cropped_height));

    for (int row = 0; row < cropped_height; ++row) {
        const std::size_t src_offset =
            (static_cast<std::size_t>(clamped.top + row) * RowStride()) +
            (static_cast<std::size_t>(clamped.left) * 4);
        const std::size_t dst_offset = static_cast<std::size_t>(row) * cropped_row_stride;
        std::copy_n(source_pixels.data() + src_offset,
                    cropped_row_stride,
                    cropped_pixels.data() + dst_offset);
    }

    return CapturedImage(cropped_width, cropped_height, std::move(cropped_pixels));
}

BITMAPINFO CapturedImage::CreateBitmapInfo() const {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

}  // namespace capture
