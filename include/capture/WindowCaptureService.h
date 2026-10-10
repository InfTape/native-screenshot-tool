#pragma once

#include "capture/CapturedImage.h"
#include "capture/WindowInfo.h"
#include "common/Result.h"

namespace capture {

class WindowCaptureService {
public:
    common::Result<CapturedImage> CaptureWindow(const WindowInfo& window) const;
};

}  // namespace capture
