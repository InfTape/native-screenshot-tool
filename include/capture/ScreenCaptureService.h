#pragma once

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "common/Result.h"

namespace capture {

class ScreenCaptureService {
public:
    common::Result<CapturedImage> CaptureDesktop() const;
    common::Result<DesktopSnapshot> CaptureDesktopSnapshot() const;
};

}  // namespace capture
