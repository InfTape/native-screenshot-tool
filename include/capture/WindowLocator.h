#pragma once

#include <Windows.h>

#include <optional>
#include <vector>

#include "capture/WindowInfo.h"

namespace capture {

class WindowLocator {
public:
    std::optional<WindowInfo> FindTopLevelWindowAtPoint(
        const POINT& screen_point,
        const std::vector<HWND>& excluded_windows) const;
    std::optional<WindowInfo> DescribeWindow(HWND window) const;

private:
    bool IsCapturableTopLevelWindow(HWND window,
                                    const std::vector<HWND>& excluded_windows) const;
    bool IsExcludedWindow(HWND window, const std::vector<HWND>& excluded_windows) const;
};

}  // namespace capture
