#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <vector>

#include "capture/DesktopSnapshot.h"
#include "capture/WindowInfo.h"
#include "capture/WindowLocator.h"

namespace ui {

class WindowSelectionOverlay {
public:
    explicit WindowSelectionOverlay(HINSTANCE instance);

    std::optional<capture::WindowInfo> SelectWindow(
        const capture::DesktopSnapshot& snapshot,
        const std::vector<HWND>& excluded_windows,
        std::wstring& error_message);

private:
    static constexpr wchar_t kClassName[] = L"NativeScreenshot.WindowSelectionOverlay";

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    bool RegisterWindowClass() const;
    bool UpdateHoveredWindow(const POINT& client_point);
    RECT HoveredWindowRectInClient() const;
    void FinishSelection(bool accepted);
    void DrawDimmedRect(HDC hdc, const RECT& rect) const;
    void DrawHoveredWindow(HDC hdc) const;
    void DrawInstructions(HDC hdc, const RECT& bounds) const;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    const capture::DesktopSnapshot* snapshot_ = nullptr;
    std::vector<HWND> excluded_windows_;
    capture::WindowLocator window_locator_;
    bool finished_ = false;
    bool accepted_ = false;
    std::optional<capture::WindowInfo> hovered_window_;
};

}  // namespace ui
