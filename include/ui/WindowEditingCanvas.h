#pragma once

#include "capture/DesktopSnapshot.h"
#include "common/Result.h"

namespace ui {

struct WindowEditingCanvas {
    capture::DesktopSnapshot snapshot;
    RECT selection{};
    RECT desktop_bounds{};
};

// Keep the frozen desktop behind a native-resolution window capture. Canvas
// coordinates may extend beyond the desktop to preserve off-screen window pixels.
common::Result<WindowEditingCanvas> CreateWindowEditingCanvas(
    const capture::DesktopSnapshot& desktop,
    const capture::CapturedImage& window_image, const POINT& window_origin);

RECT WindowEditingInstructionArea(const RECT& desktop_bounds, const RECT& selection, UINT dpi);

}  // namespace ui
