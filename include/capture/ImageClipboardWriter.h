#pragma once

#include <Windows.h>

#include <string>

#include "capture/CapturedImage.h"

namespace capture {

class ImageClipboardWriter {
public:
    bool CopyToClipboard(HWND owner,
                         const CapturedImage& image,
                         std::wstring& error_message) const;
};

}  // namespace capture
