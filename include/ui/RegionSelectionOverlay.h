#pragma once

#include <Windows.h>

#include <optional>
#include <string>

#include "capture/DesktopSnapshot.h"

namespace ui {

class RegionSelectionOverlay {
public:
    explicit RegionSelectionOverlay(HINSTANCE instance);

    std::optional<RECT> SelectRegion(const capture::DesktopSnapshot& snapshot,
                                     std::wstring& error_message);

private:
    static constexpr wchar_t kClassName[] = L"NativeScreenshot.RegionSelectionOverlay";

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    bool RegisterWindowClass() const;
    RECT CurrentSelection() const;
    bool HasSelection() const;
    void FinishSelection(bool accepted);
    void DrawDimmedRect(HDC hdc, const RECT& rect) const;
    void DrawInstructions(HDC hdc, const RECT& bounds) const;
    void DrawSelection(HDC hdc) const;
    bool CreateBackBuffer(std::wstring& error_message);
    void DestroyBackBuffer();
    RECT SelectionLabelRect(const RECT& selection, const RECT& client_rect) const;
    RECT SelectionVisualRect(const RECT& selection, const RECT& client_rect) const;
    void UpdateBackBuffer(const RECT& dirty_rect);
    void RefreshDirtyRect(const RECT& dirty_rect);
    void InvalidateSelectionChange(const RECT& previous_selection, bool previous_has_selection);
    void PaintFrame(HDC hdc, const RECT& client_rect) const;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    const capture::DesktopSnapshot* snapshot_ = nullptr;
    bool finished_ = false;
    bool accepted_ = false;
    bool dragging_ = false;
    POINT anchor_{};
    POINT current_{};
    RECT selection_{};
    HDC back_buffer_dc_ = nullptr;
    HBITMAP back_buffer_bitmap_ = nullptr;
    HGDIOBJ back_buffer_previous_bitmap_ = nullptr;
    SIZE back_buffer_size_{};
};

}  // namespace ui
