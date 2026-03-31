#pragma once

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "capture/WindowInfo.h"
#include "common/Result.h"

namespace capture {

class WindowCaptureService {
public:
    common::Result<CapturedImage> CaptureWindow(const WindowInfo& window,
                                                const DesktopSnapshot* fallback_snapshot) const;

private:
    common::Result<CapturedImage> CaptureWithPrintWindow(const WindowInfo& window) const;
    common::Result<CapturedImage> CaptureFromSnapshot(const WindowInfo& window,
                                                      const DesktopSnapshot& snapshot) const;
};

}  // namespace capture
