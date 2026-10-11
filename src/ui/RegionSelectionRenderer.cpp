#include "ui/RegionSelectionRenderer.h"

#include <algorithm>

#include "common/Direct2D.h"
#include "common/RectUtils.h"
#include "ui/OverlayPrimitives.h"

namespace {

constexpr int kInstructionHeight = 54;
constexpr int kInstructionMargin = 16;
constexpr COLORREF kMarkupRedColor = RGB(255, 0, 0);
constexpr COLORREF kRectangleColor = kMarkupRedColor;
constexpr int kRectangleThickness = 4;
constexpr COLORREF kArrowColor = kMarkupRedColor;
constexpr int kArrowThickness = 4;
constexpr COLORREF kBrushColor = kMarkupRedColor;
constexpr int kBrushThickness = 4;
constexpr int kMosaicBlockSize = 14;

}  // namespace

namespace ui {

RegionSelectionRenderer::RegionSelectionRenderer() : theme_(DefaultOverlayTheme()) {}

COLORREF RegionSelectionRenderer::RectangleColor() const {
    return kRectangleColor;
}

int RegionSelectionRenderer::RectangleThickness() const {
    return kRectangleThickness;
}

COLORREF RegionSelectionRenderer::ArrowColor() const {
    return kArrowColor;
}

int RegionSelectionRenderer::ArrowThickness() const {
    return kArrowThickness;
}

COLORREF RegionSelectionRenderer::BrushColor() const {
    return kBrushColor;
}

int RegionSelectionRenderer::BrushThickness() const {
    return kBrushThickness;
}

int RegionSelectionRenderer::MosaicBlockSize() const {
    return kMosaicBlockSize;
}

void RegionSelectionRenderer::PaintBaseImage(HDC hdc, const capture::CapturedImage& image) const {
    const BITMAPINFO bitmap_info = image.CreateBitmapInfo();
    SetDIBitsToDevice(hdc,
                      0,
                      0,
                      image.Width(),
                      image.Height(),
                      0,
                      0,
                      0,
                      image.Height(),
                      image.Pixels().data(),
                      &bitmap_info,
                      DIB_RGB_COLORS);
}

void RegionSelectionRenderer::DrawInstructions(HDC hdc,
                                               const RECT& bounds,
                                               bool start_with_full_selection, bool for_recording, UINT dpi, bool ocr_mode) const {
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    RECT instruction_rect = bounds;
    instruction_rect.left += scale(kInstructionMargin);
    instruction_rect.top += scale(kInstructionMargin);
    instruction_rect.right =
        std::min<LONG>(instruction_rect.left + scale(560), bounds.right - scale(kInstructionMargin));
    instruction_rect.bottom = instruction_rect.top + scale(kInstructionHeight);

    OverlayPanelStyle panel_style{};
    panel_style.background_mode = OverlayPanelStyle::BackgroundMode::Solid;
    panel_style.background_color = theme_.panel_color;
    panel_style.has_border = true;
    panel_style.border_color = theme_.panel_border_color;
    panel_style.text_color = theme_.panel_text_color;
    PaintPanel(hdc, instruction_rect, panel_style);

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -scale(12), -scale(8));
    if (ocr_mode) {
        DrawTextBlock(hdc, text_rect,
            L"直接拖动选择截图上的文字，双击选择词语。Ctrl+A 全选，Ctrl+C 或右键复制。点击“返回”继续编辑，Esc 退出。",
            DT_LEFT | DT_VCENTER | DT_WORDBREAK, panel_style.text_color, dpi);
        return;
    }
    if (for_recording) {
        DrawTextBlock(hdc, text_rect,
                      L"拖动鼠标框选录屏区域，可移动或调整范围。按 Enter 或点击“开始录制”开始，按 Esc 或右键取消。",
                      DT_LEFT | DT_VCENTER | DT_WORDBREAK, panel_style.text_color, dpi);
        return;
    }
    if (start_with_full_selection) {
        DrawTextBlock(hdc,
                      text_rect,
                      L"可直接使用画笔、马赛克、矩形或箭头标注，也可拖动或调整范围。按 Enter 完成，按 Esc 或右键取消。",
                      DT_LEFT | DT_VCENTER | DT_WORDBREAK,
                      panel_style.text_color, dpi);
        return;
    }

    DrawTextBlock(hdc,
                  text_rect,
                  L"拖动鼠标框选截图区域，松开后可继续做画笔、矩形、马赛克或箭头标注。按 Enter 完成，按 Esc 或右键取消。",
                  DT_LEFT | DT_VCENTER | DT_WORDBREAK,
                  panel_style.text_color, dpi);
}

void RegionSelectionRenderer::PaintOverlay(HDC hdc,
                                           const RECT& client_rect,
                                           const RegionSelectionRenderModel& model,
                                           const SelectionEditToolbar& toolbar, UINT dpi) const {
    if (model.has_selection) {
        DrawSelectionBorder(hdc, model.selection);
        if (model.show_selection_handles) {
            DrawSelectionHandles(hdc, model.selection_handle_rects);
        }
        if (model.has_selection_label) {
            DrawSelectionLabel(hdc, model.selection_label_rect, model.selection_label_text, dpi);
        }
    }

    if (model.has_rectangle_preview) {
        DrawRectanglePreview(hdc, client_rect, model.rectangle_preview, model.selection);
    }
    if (model.has_mosaic_preview) {
        DrawMosaicPreview(hdc, model.mosaic_preview);
    }
    if (model.has_arrow_preview) {
        DrawArrowPreview(hdc, client_rect, model.arrow_preview);
    }
    if (model.has_brush_preview) {
        DrawBrushPreview(hdc, client_rect, model.brush_preview);
    }

    toolbar.Paint(hdc, model.active_tool, model.can_undo, model.can_copy_text);
}

void RegionSelectionRenderer::DrawSelectionBorder(HDC hdc, const RECT& selection) const {
    DrawOutlineRect(hdc, selection, theme_.panel_border_color, 2);
}

void RegionSelectionRenderer::DrawSelectionHandles(
    HDC hdc, const std::array<RECT, 8>& handle_rects) const {
    for (const RECT& handle_rect : handle_rects) {
        DrawHandleSquare(hdc,
                         handle_rect,
                         theme_.selection_handle_fill_color,
                         theme_.selection_handle_border_color);
    }
}

void RegionSelectionRenderer::DrawSelectionLabel(HDC hdc,
                                                 const RECT& label_rect,
                                                 const std::wstring& text, UINT dpi) const {
    OverlayPanelStyle panel_style{};
    panel_style.background_mode = OverlayPanelStyle::BackgroundMode::Solid;
    panel_style.background_color = theme_.panel_color;
    panel_style.has_border = true;
    panel_style.border_color = theme_.panel_border_color;
    panel_style.text_color = theme_.panel_text_color;
    PaintPanel(hdc, label_rect, panel_style);

    RECT text_rect = label_rect;
    InflateRect(&text_rect, -MulDiv(8, dpi, 96), -MulDiv(6, dpi, 96));
    DrawTextBlock(
        hdc, text_rect, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, panel_style.text_color, dpi);
}

void RegionSelectionRenderer::DrawRectanglePreview(HDC hdc,
                                                   const RECT& client_rect,
                                                   const RECT& preview_rect,
                                                   const RECT& clip_rect) const {
    (void)common::DrawRectangleOnHdc(hdc,
                                     client_rect,
                                     common::HasArea(clip_rect) ? &clip_rect : nullptr,
                                     preview_rect,
                                     RectangleColor(),
                                     static_cast<float>(RectangleThickness()));
}

void RegionSelectionRenderer::DrawMosaicPreview(HDC hdc, const RECT& preview_rect) const {
    DrawOutlineRect(hdc, preview_rect, theme_.preview_accent_color, 2);
}

void RegionSelectionRenderer::DrawArrowPreview(HDC hdc,
                                               const RECT& client_rect,
                                               const ArrowPreviewModel& preview) const {
    (void)common::DrawArrowOnHdc(hdc,
                                 client_rect,
                                 common::HasArea(preview.clip_rect) ? &preview.clip_rect : nullptr,
                                 preview.start,
                                 preview.end,
                                 ArrowColor(),
                                 static_cast<float>(ArrowThickness()));
}

void RegionSelectionRenderer::DrawBrushPreview(HDC hdc,
                                               const RECT& client_rect,
                                               const BrushPreviewModel& preview) const {
    if (preview.points == nullptr || preview.points->empty()) {
        return;
    }

    (void)common::DrawPolylineOnHdc(hdc,
                                    client_rect,
                                    common::HasArea(preview.clip_rect) ? &preview.clip_rect : nullptr,
                                    preview.points->data(),
                                    preview.points->size(),
                                    BrushColor(),
                                    static_cast<float>(BrushThickness()));
}

}  // namespace ui
