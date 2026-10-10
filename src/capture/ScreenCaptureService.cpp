#include "capture/ScreenCaptureService.h"

#include "capture/WgcScreenshotService.h"

namespace capture {

common::Result<CapturedImage> ScreenCaptureService::CaptureDesktop() const {
    auto snapshot = CaptureDesktopSnapshot();
    if (!snapshot) return common::Result<CapturedImage>::Failure(snapshot.Error());
    return common::Result<CapturedImage>::Success(std::move(snapshot.Value().image));
}

common::Result<DesktopSnapshot> ScreenCaptureService::CaptureDesktopSnapshot() const {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        return common::Result<DesktopSnapshot>::Failure(L"无法获取桌面尺寸。");
    }
    auto image = WgcScreenshotService{}.Capture(RECT{x, y, x + width, y + height});
    if (!image) return common::Result<DesktopSnapshot>::Failure(image.Error());
    return common::Result<DesktopSnapshot>::Success(DesktopSnapshot{x, y, std::move(image.Value())});
}

}  // namespace capture
