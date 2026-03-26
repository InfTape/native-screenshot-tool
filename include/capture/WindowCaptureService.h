#pragma once

#include <optional>
#include <string>

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "capture/WindowInfo.h"

namespace capture {

class WindowCaptureService {
public:
    std::optional<CapturedImage> CaptureWindow(const WindowInfo& window,
                                               const DesktopSnapshot* fallback_snapshot,
                                               std::wstring& error_message) const;

private:
    std::optional<CapturedImage> CaptureWithPrintWindow(const WindowInfo& window,
                                                        std::wstring& error_message) const;
    std::optional<CapturedImage> CaptureFromSnapshot(const WindowInfo& window,
                                                     const DesktopSnapshot& snapshot,
                                                     std::wstring& error_message) const;
};

}  // namespace capture
