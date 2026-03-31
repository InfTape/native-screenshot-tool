#pragma once

#include <string>

#include "capture/CapturedImage.h"
#include "common/Result.h"

namespace capture {

class BitmapFileWriter {
public:
    common::Result<void> WriteBmp(const std::wstring& path, const CapturedImage& image) const;
};

}  // namespace capture
