#pragma once

#include <string>

#include "capture/BitmapFileWriter.h"
#include "capture/CapturedImage.h"
#include "capture/ImageFileFormat.h"
#include "common/Result.h"

namespace capture {

class ImageFileWriter {
public:
    common::Result<void> Write(const std::wstring& path,
                               const CapturedImage& image,
                               ImageFileFormat format) const;

private:
    common::Result<void> WritePng(const std::wstring& path, const CapturedImage& image) const;

    BitmapFileWriter bitmap_writer_;
};

}  // namespace capture
