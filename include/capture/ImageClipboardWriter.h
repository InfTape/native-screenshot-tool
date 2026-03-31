#pragma once

#include <Windows.h>

#include <string>

#include "capture/CapturedImage.h"
#include "common/Result.h"

namespace capture {

class ImageClipboardWriter {
public:
    common::Result<void> CopyToClipboard(HWND owner, const CapturedImage& image) const;
};

}  // namespace capture
