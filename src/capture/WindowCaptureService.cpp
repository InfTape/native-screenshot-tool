#include "capture/WindowCaptureService.h"

#include "capture/WgcScreenshotService.h"

namespace capture {

common::Result<CapturedImage> WindowCaptureService::CaptureWindow(const WindowInfo& window) const {
    if (!IsWindow(window.handle) || !IsWindowVisible(window.handle) || IsIconic(window.handle)) {
        return common::Result<CapturedImage>::Failure(L"请选择一个可见且未最小化的窗口。");
    }
    return WgcScreenshotService{}.Capture(window.bounds, window.handle);
}

}  // namespace capture
