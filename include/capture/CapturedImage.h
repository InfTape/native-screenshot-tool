#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace capture {

class CapturedImage {
public:
    CapturedImage() = default;
    CapturedImage(int width, int height, std::vector<std::uint8_t> pixels);

    bool IsEmpty() const;
    int Width() const;
    int Height() const;
    std::size_t RowStride() const;
    std::vector<std::uint8_t>& Pixels();
    const std::vector<std::uint8_t>& Pixels() const;
    std::optional<CapturedImage> Crop(const RECT& region, std::wstring& error_message) const;

    BITMAPINFO CreateBitmapInfo() const;

private:
    void EnsureUniquePixels();

    int width_ = 0;
    int height_ = 0;
    std::shared_ptr<std::vector<std::uint8_t>> pixels_;
};

}  // namespace capture
