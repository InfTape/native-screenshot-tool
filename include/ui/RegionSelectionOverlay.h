#pragma once

#include <Windows.h>

#include <optional>
#include <string>

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "ui/SelectionEditToolbar.h"

namespace ui {

struct RegionSelectionResult {
    RECT region{};
    capture::CapturedImage image;
};

class RegionSelectionOverlay {
public:
    explicit RegionSelectionOverlay(HINSTANCE instance);

    std::optional<RegionSelectionResult> SelectRegion(const capture::DesktopSnapshot& snapshot,
                                                      std::wstring& error_message);

private:
    enum class EditTool {
        Select,
        Mosaic,
        Arrow,
    };

    enum class InteractionMode {
        None,
        Selecting,
        Mosaic,
        Arrow,
    };

    static constexpr wchar_t kClassName[] = L"NativeScreenshot.RegionSelectionOverlay";

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    bool RegisterWindowClass() const;
    bool CreateBackBuffer(std::wstring& error_message);
    void DestroyBackBuffer();
    bool CreateBaseBuffer(HDC reference_dc, std::wstring& error_message);
    void DestroyBaseBuffer();
    bool CreateDimmedBuffer(HDC reference_dc, std::wstring& error_message);
    void DestroyDimmedBuffer();
    void RenderBaseBuffer();
    void RenderDimmedBuffer();
    void RebuildSourceBuffers();

    RECT CurrentSelection() const;
    bool HasSelection() const;
    RECT CurrentPreviewRect() const;
    bool HasPreviewRect() const;
    RECT SelectionLabelRect(const RECT& selection, const RECT& client_rect) const;
    RECT SelectionVisualRect(const RECT& selection, const RECT& client_rect) const;
    RECT ArrowPreviewRect() const;

    void FinishSelection(bool accepted);
    void CommitSelection(const RECT& selection);
    void ResetInteraction();
    void RefreshFullFrame();
    void RefreshDirtyRect(const RECT& dirty_rect);
    void FlushPendingDirtyRegion();
    void UpdateBackBuffer(const RECT& dirty_rect);
    void InvalidateSelectionChange(const RECT& previous_selection, bool previous_has_selection);
    void InvalidatePreviewChange(const RECT& previous_preview, bool previous_has_preview);
    void UpdateToolbarLayout();
    void SetActiveTool(EditTool tool);
    SelectionToolbarAction ActiveToolbarAction() const;
    bool ApplyPendingMosaic(std::wstring& error_message);
    bool ApplyPendingArrow(std::wstring& error_message);
    void ShowEditError(const std::wstring& error_message) const;

    void PaintBaseImage(HDC hdc) const;
    void PaintOverlay(HDC hdc, const RECT& client_rect) const;
    void PaintFrame(HDC hdc, const RECT& client_rect) const;
    void DrawInstructions(HDC hdc, const RECT& bounds) const;
    void DrawSelectionBorder(HDC hdc, const RECT& selection) const;
    void DrawSelectionLabel(HDC hdc, const RECT& selection, const RECT& client_rect) const;
    void DrawMosaicPreview(HDC hdc) const;
    void DrawArrowPreview(HDC hdc) const;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    const capture::DesktopSnapshot* snapshot_ = nullptr;
    capture::CapturedImage working_image_;
    bool finished_ = false;
    bool accepted_ = false;
    POINT anchor_{};
    POINT current_{};
    RECT selection_{};
    EditTool active_tool_ = EditTool::Select;
    InteractionMode interaction_mode_ = InteractionMode::None;
    POINT preview_start_{};
    POINT preview_current_{};
    SelectionEditToolbar toolbar_;
    HDC base_buffer_dc_ = nullptr;
    HBITMAP base_buffer_bitmap_ = nullptr;
    HGDIOBJ base_buffer_previous_bitmap_ = nullptr;
    HDC dimmed_buffer_dc_ = nullptr;
    HBITMAP dimmed_buffer_bitmap_ = nullptr;
    HGDIOBJ dimmed_buffer_previous_bitmap_ = nullptr;
    HDC back_buffer_dc_ = nullptr;
    HBITMAP back_buffer_bitmap_ = nullptr;
    HGDIOBJ back_buffer_previous_bitmap_ = nullptr;
    HRGN pending_dirty_region_ = nullptr;
    SIZE back_buffer_size_{};
};

}  // namespace ui
