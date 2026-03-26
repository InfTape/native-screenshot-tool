#pragma once

#include <string>

#include "capture/BitmapFileWriter.h"
#include "capture/CapturedImage.h"
#include "capture/ImageFileFormat.h"

namespace capture {

class ImageFileWriter {
public:
    bool Write(const std::wstring& path,
               const CapturedImage& image,
               ImageFileFormat format,
               std::wstring& error_message) const;

private:
    bool WritePng(const std::wstring& path,
                  const CapturedImage& image,
                  std::wstring& error_message) const;

    BitmapFileWriter bitmap_writer_;
};

}  // namespace capture
