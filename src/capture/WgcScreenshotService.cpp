#include "capture/WgcScreenshotService.h"

#include <dwmapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <future>
#include <limits>
#include <stdexcept>

#include "capture/WgcCaptureSource.h"

namespace capture {
namespace {

struct CaptureApartment {
    CaptureApartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~CaptureApartment() {
        winrt::clear_factory_cache();
        winrt::uninit_apartment();
    }
};

common::Result<CapturedImage> CaptureFrame(const RECT& region, HWND window, int width, int height) {
    try {
        CaptureApartment apartment;
        DwmFlush();
        WgcCaptureSource source;
        source.Initialize(region, window, static_cast<UINT>(width), static_cast<UINT>(height), false);
        std::vector<BYTE> pixels;
        if (!source.ReadFrame(pixels, WgcRowOrder::TopDown)) {
            return common::Result<CapturedImage>::Failure(L"目标窗口已关闭、隐藏、最小化或改变尺寸，请重新选择。");
        }
        // WGC window surfaces have premultiplied alpha. WIC PNG takes straight
        // BGRA; preserve native rounded corners without darkening their edges.
        if (window) {
            for (size_t offset = 0; offset < pixels.size(); offset += 4) {
                const unsigned alpha = pixels[offset + 3];
                if (alpha == 0 || alpha == 255) continue;
                for (size_t channel = 0; channel < 3; ++channel) {
                    pixels[offset + channel] = static_cast<BYTE>(std::min(255u,
                        (static_cast<unsigned>(pixels[offset + channel]) * 255 + alpha / 2) / alpha));
                }
            }
        }
        return common::Result<CapturedImage>::Success(CapturedImage(width, height, std::move(pixels)));
    } catch (const winrt::hresult_error& error) {
        return common::Result<CapturedImage>::Failure(std::wstring(L"WGC 截图失败：") + error.message().c_str());
    } catch (const std::exception&) {
        return common::Result<CapturedImage>::Failure(L"无法分配截图资源。");
    }
}

}  // namespace

common::Result<CapturedImage> WgcScreenshotService::Capture(const RECT& region, HWND window) const {
    const auto width = static_cast<long long>(region.right) - region.left;
    const auto height = static_cast<long long>(region.bottom) - region.top;
    if (width <= 0 || height <= 0 || width >= INT_MAX || height >= INT_MAX ||
        width * height > std::numeric_limits<DWORD>::max() / 4) {
        return common::Result<CapturedImage>::Failure(L"截图范围无效或过大。");
    }
    try {
        // The Win32 UI thread need not host a WinRT dispatcher/apartment. The
        // free-threaded frame pool can deliver frames while this call waits.
        return std::async(std::launch::async, CaptureFrame, region, window,
                          static_cast<int>(width), static_cast<int>(height)).get();
    } catch (const std::exception&) {
        return common::Result<CapturedImage>::Failure(L"无法启动截图线程。");
    }
}

}  // namespace capture
