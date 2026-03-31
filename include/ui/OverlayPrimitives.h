#pragma once

#include <Windows.h>

#include <string_view>

namespace ui {

struct OverlayTheme {
    BYTE overlay_alpha = 120;
    COLORREF panel_color = RGB(24, 24, 24);
    COLORREF panel_border_color = RGB(255, 255, 255);
    COLORREF panel_text_color = RGB(255, 255, 255);
    COLORREF selection_handle_fill_color = RGB(255, 255, 255);
    COLORREF selection_handle_border_color = RGB(32, 32, 32);
    COLORREF preview_accent_color = RGB(242, 154, 28);
};

struct OverlayPanelStyle {
    enum class BackgroundMode {
        Solid,
        Dimmed,
    };

    BackgroundMode background_mode = BackgroundMode::Solid;
    COLORREF background_color = RGB(24, 24, 24);
    BYTE background_alpha = 255;
    bool has_border = false;
    COLORREF border_color = RGB(255, 255, 255);
    COLORREF text_color = RGB(255, 255, 255);
};

[[nodiscard]] const OverlayTheme& DefaultOverlayTheme();

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color);
bool AlphaFillRect(HDC hdc, const RECT& rect, BYTE alpha);
void DrawOutlineRect(HDC hdc, const RECT& rect, COLORREF color, int thickness);
void DrawHandleSquare(HDC hdc, const RECT& rect, COLORREF fill_color, COLORREF border_color);
void PaintPanel(HDC hdc, const RECT& rect, const OverlayPanelStyle& style);
void DrawTextBlock(HDC hdc,
                   const RECT& rect,
                   std::wstring_view text,
                   UINT format,
                   COLORREF text_color);

}  // namespace ui
