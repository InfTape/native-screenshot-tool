#pragma once

#include <string>

#include "capture/CapturedImage.h"

namespace capture {

class BitmapFileWriter {
public:
    bool WriteBmp(const std::wstring& path,
                  const CapturedImage& image,
                  std::wstring& error_message) const;
};

}  // namespace capture
