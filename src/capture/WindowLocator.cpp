#include "capture/WindowLocator.h"

#include <Windows.h>
#include <dwmapi.h>

#include <array>

#include "common/RectUtils.h"

namespace {

bool TryGetExtendedFrameBounds(HWND window, RECT& bounds) {
    if (SUCCEEDED(DwmGetWindowAttribute(
            window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        return common::HasArea(bounds);
    }

    return false;
}

bool IsCloaked(HWND window) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
           cloaked != 0;
}

bool IsIgnoredWindowClass(HWND window) {
    std::array<wchar_t, 128> class_name{};
    if (GetClassNameW(window, class_name.data(), static_cast<int>(class_name.size())) == 0) {
        return false;
    }

    return wcscmp(class_name.data(), L"Progman") == 0 ||
           wcscmp(class_name.data(), L"WorkerW") == 0 ||
           wcscmp(class_name.data(), L"Shell_TrayWnd") == 0 ||
           wcscmp(class_name.data(), L"#32768") == 0;
}

std::wstring ReadWindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return L"未命名窗口";
    }

    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<std::size_t>(std::max(0, copied)));
    return title;
}

}  // namespace

namespace capture {

std::optional<WindowInfo> WindowLocator::FindTopLevelWindowAtPoint(
    const POINT& screen_point,
    const std::vector<HWND>& excluded_windows) const {
    for (HWND current = GetTopWindow(nullptr); current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (!IsCapturableTopLevelWindow(current, excluded_windows)) {
            continue;
        }

        auto window_info = DescribeWindow(current);
        if (!window_info.has_value()) {
            continue;
        }

        if (PtInRect(&window_info->bounds, screen_point)) {
            return window_info;
        }
    }

    return std::nullopt;
}

std::optional<WindowInfo> WindowLocator::DescribeWindow(HWND window) const {
    if (window == nullptr || !IsWindow(window)) {
        return std::nullopt;
    }

    RECT bounds{};
    if (!TryGetExtendedFrameBounds(window, bounds) && !GetWindowRect(window, &bounds)) {
        return std::nullopt;
    }

    if (!common::HasArea(bounds)) {
        return std::nullopt;
    }

    WindowInfo info{};
    info.handle = window;
    info.bounds = bounds;
    info.title = ReadWindowTitle(window);
    return info;
}

bool WindowLocator::IsCapturableTopLevelWindow(HWND window,
                                               const std::vector<HWND>& excluded_windows) const {
    if (window == nullptr || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) ||
        IsCloaked(window) || IsIgnoredWindowClass(window) ||
        IsExcludedWindow(window, excluded_windows)) {
        return false;
    }

    if (GetAncestor(window, GA_ROOT) != window) {
        return false;
    }

    const LONG_PTR ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    return true;
}

bool WindowLocator::IsExcludedWindow(HWND window,
                                     const std::vector<HWND>& excluded_windows) const {
    for (const HWND excluded : excluded_windows) {
        if (excluded != nullptr && window == excluded) {
            return true;
        }
    }

    return false;
}

}  // namespace capture
