#pragma once

#include "capture/CapturedImage.h"
#include "common/Result.h"

namespace capture {

class WgcScreenshotService {
public:
    // Initializes WinRT and collects one complete frame on a dedicated worker.
    common::Result<CapturedImage> Capture(const RECT& region, HWND window = nullptr) const;
};

}  // namespace capture
