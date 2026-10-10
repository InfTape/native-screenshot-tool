#pragma once

#include <Windows.h>
#include <string>
#include "common/Result.h"

namespace ui {

// A click-through live mask and recording toolbar, all excluded from screen capture.
class RecordingOverlay {
public:
    explicit RecordingOverlay(HINSTANCE instance) : instance_(instance) {}
    ~RecordingOverlay() { Close(); }
    common::Result<void> Show(HWND owner, UINT stop_message, const RECT& region,
        const std::wstring& mode);
    void Update(const RECT& region, ULONGLONG seconds, bool stopping);
    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    void Layout();
    void UpdateFont();
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    UINT stop_message_ = 0;
    HWND mask_ = nullptr;
    HWND border_ = nullptr;
    HWND bar_ = nullptr;
    HWND label_ = nullptr;
    HWND stop_button_ = nullptr;
    HFONT font_ = nullptr;
    RECT region_{};
    std::wstring mode_;
    bool bar_moved_ = false;
};

}  // namespace ui
