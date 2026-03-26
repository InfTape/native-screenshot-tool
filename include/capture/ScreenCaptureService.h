#pragma once

#include <optional>
#include <string>

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"

namespace capture {

class ScreenCaptureService {
public:
    std::optional<CapturedImage> CaptureDesktop(std::wstring& error_message) const;
    std::optional<DesktopSnapshot> CaptureDesktopSnapshot(std::wstring& error_message) const;
};

}  // namespace capture
