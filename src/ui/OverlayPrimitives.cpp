#include "ui/OverlayPrimitives.h"

#include "common/GdiResources.h"
#include "common/RectUtils.h"

namespace {

constexpr ui::OverlayTheme kDefaultOverlayTheme{};

}  // namespace

namespace ui {

const OverlayTheme& DefaultOverlayTheme() {
    return kDefaultOverlayTheme;
}

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    if (!common::HasArea(rect)) {
        return;
    }

    SetDCBrushColor(hdc, color);
    FillRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

bool AlphaFillRect(HDC hdc, const RECT& rect, BYTE alpha) {
    if (!common::HasArea(rect)) {
        return true;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = 1;
    bitmap_info.bmiHeader.biHeight = -1;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    common::UniqueBitmap bitmap{
        CreateDIBSection(hdc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    if (!bitmap || bits == nullptr) {
        return false;
    }

    *static_cast<DWORD*>(bits) = 0x00000000;

    common::UniqueDc memory_dc{CreateCompatibleDC(hdc)};
    if (!memory_dc) {
        return false;
    }

    common::ScopedSelectObject scoped_bitmap(memory_dc.Get(), bitmap.Get());
    if (!scoped_bitmap.IsValid()) {
        return false;
    }

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;

    return AlphaBlend(hdc,
                      rect.left,
                      rect.top,
                      common::RectWidth(rect),
                      common::RectHeight(rect),
                      memory_dc.Get(),
                      0,
                      0,
                      1,
                      1,
                      blend) != FALSE;
}

void DrawOutlineRect(HDC hdc, const RECT& rect, COLORREF color, int thickness) {
    if (!common::HasArea(rect) || thickness <= 0) {
        return;
    }

    common::UniquePen border_pen{CreatePen(PS_SOLID, thickness, color)};
    if (!border_pen) {
        return;
    }

    common::ScopedSelectObject scoped_pen(hdc, border_pen.Get());
    common::ScopedSelectObject scoped_brush(hdc, GetStockObject(HOLLOW_BRUSH));
    if (!scoped_pen.IsValid() || !scoped_brush.IsValid()) {
        return;
    }

    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
}

void DrawHandleSquare(HDC hdc, const RECT& rect, COLORREF fill_color, COLORREF border_color) {
    if (!common::HasArea(rect)) {
        return;
    }

    common::UniqueBrush fill_brush{CreateSolidBrush(fill_color)};
    common::UniqueBrush border_brush{CreateSolidBrush(border_color)};
    if (!fill_brush || !border_brush) {
        return;
    }

    FillRect(hdc, &rect, fill_brush.Get());
    FrameRect(hdc, &rect, border_brush.Get());
}

void PaintPanel(HDC hdc, const RECT& rect, const OverlayPanelStyle& style) {
    if (!common::HasArea(rect)) {
        return;
    }

    if (style.background_mode == OverlayPanelStyle::BackgroundMode::Solid) {
        FillSolidRect(hdc, rect, style.background_color);
    } else {
        AlphaFillRect(hdc, rect, style.background_alpha);
    }

    if (style.has_border) {
        DrawOutlineRect(hdc, rect, style.border_color, 1);
    }
}

void DrawTextBlock(HDC hdc,
                   const RECT& rect,
                   std::wstring_view text,
                   UINT format,
                   COLORREF text_color) {
    if (!common::HasArea(rect) || text.empty()) {
        return;
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    DrawTextW(hdc, text.data(), static_cast<int>(text.size()), const_cast<RECT*>(&rect), format);
}

}  // namespace ui
