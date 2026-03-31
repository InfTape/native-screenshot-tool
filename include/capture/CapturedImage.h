#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/Result.h"

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
    common::Result<CapturedImage> Crop(const RECT& region) const;

    BITMAPINFO CreateBitmapInfo() const;

private:
    void EnsureUniquePixels();

    int width_ = 0;
    int height_ = 0;
    std::shared_ptr<std::vector<std::uint8_t>> pixels_;
};

}  // namespace capture
