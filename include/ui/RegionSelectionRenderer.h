#pragma once

#include <Windows.h>

#include <array>
#include <string>

#include "capture/CapturedImage.h"
#include "ui/OverlayPrimitives.h"
#include "ui/SelectionEditToolbar.h"
#include "ui/SelectionSession.h"

namespace ui {

struct ArrowPreviewModel {
    RECT clip_rect{};
    POINT start{};
    POINT end{};
};

struct RegionSelectionRenderModel {
    bool has_selection = false;
    RECT selection{};
    bool show_selection_handles = false;
    std::array<RECT, 8> selection_handle_rects{};
    bool has_selection_label = false;
    RECT selection_label_rect{};
    std::wstring selection_label_text;
    bool starts_with_full_selection = false;

    bool has_rectangle_preview = false;
    RECT rectangle_preview{};
    bool has_mosaic_preview = false;
    RECT mosaic_preview{};
    bool has_arrow_preview = false;
    ArrowPreviewModel arrow_preview{};

    editing::MarkupTool active_tool = editing::MarkupTool::Select;
    bool can_undo = false;
};

class RegionSelectionRenderer {
public:
    RegionSelectionRenderer();

    [[nodiscard]] COLORREF RectangleColor() const;
    [[nodiscard]] int RectangleThickness() const;
    [[nodiscard]] COLORREF ArrowColor() const;
    [[nodiscard]] int ArrowThickness() const;
    [[nodiscard]] int MosaicBlockSize() const;

    void PaintBaseImage(HDC hdc, const capture::CapturedImage& image) const;
    void DrawInstructions(HDC hdc, const RECT& bounds, bool start_with_full_selection) const;
    void PaintOverlay(HDC hdc,
                      const RECT& client_rect,
                      const RegionSelectionRenderModel& model,
                      const SelectionEditToolbar& toolbar) const;

private:
    void DrawSelectionBorder(HDC hdc, const RECT& selection) const;
    void DrawSelectionHandles(HDC hdc, const std::array<RECT, 8>& handle_rects) const;
    void DrawSelectionLabel(HDC hdc, const RECT& label_rect, const std::wstring& text) const;
    void DrawRectanglePreview(HDC hdc,
                              const RECT& client_rect,
                              const RECT& preview_rect,
                              const RECT& clip_rect) const;
    void DrawMosaicPreview(HDC hdc, const RECT& preview_rect) const;
    void DrawArrowPreview(HDC hdc, const RECT& client_rect, const ArrowPreviewModel& preview) const;

    OverlayTheme theme_;
};

}  // namespace ui
