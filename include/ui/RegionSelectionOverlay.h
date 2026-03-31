#pragma once

#include <Windows.h>

#include <array>
#include <optional>
#include <string>

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "common/GdiResources.h"
#include "common/Result.h"
#include "editing/MarkupCommand.h"
#include "ui/RegionSelectionBuffers.h"
#include "ui/RegionSelectionRenderer.h"
#include "ui/SelectionEditToolbar.h"
#include "ui/SelectionSession.h"

namespace ui {

struct RegionSelectionResult {
    RECT region{};
    capture::CapturedImage image;
};

class RegionSelectionOverlay {
public:
    explicit RegionSelectionOverlay(HINSTANCE instance);

    common::Result<std::optional<RegionSelectionResult>> SelectRegion(
        const capture::DesktopSnapshot& snapshot);
    common::Result<std::optional<RegionSelectionResult>> EditImage(
        const capture::DesktopSnapshot& snapshot);

private:
    using EditTool = editing::MarkupTool;
    using InteractionMode = SelectionSession::InteractionMode;
    using SelectionAdjustHandle = SelectionSession::SelectionAdjustHandle;

    static constexpr wchar_t kClassName[] = L"NativeScreenshot.RegionSelectionOverlay";
    static constexpr std::size_t kMaxUndoSteps = 20;
    static constexpr int kSelectionHandleSize = 8;
    static constexpr int kSelectionHandleHitPadding = 6;
    static constexpr int kMinimumSelectionSize = 8;
    static constexpr int kPreviewInvalidationPadding = 4;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    common::Result<std::optional<RegionSelectionResult>> RunSession(
        const capture::DesktopSnapshot& snapshot,
        bool start_with_full_selection);
    void InitializeSessionState();
    common::Result<void> RegisterWindowClass() const;
    common::Result<void> CreateBackBuffer();
    void DestroyBackBuffer();
    void RebuildSourceBuffers();

    RECT CurrentSelection() const;
    bool HasSelection() const;
    RECT CurrentPreviewRect() const;
    RECT PreviewVisualRect(const RECT& preview_rect) const;
    bool HasPreviewRect() const;
    SelectionAdjustHandle HitTestSelectionHandle(const POINT& point) const;
    RECT SelectionHandleRect(const RECT& selection, SelectionAdjustHandle handle) const;
    RECT SelectionLabelRect(const RECT& selection, const RECT& client_rect) const;
    RECT SelectionVisualRect(const RECT& selection,
                             const RECT& client_rect,
                             const RECT& toolbar_rect,
                             bool include_toolbar) const;
    RECT ArrowPreviewRect() const;
    RECT AdjustSelectionFromDrag(const POINT& point) const;

    void FinishSelection(bool accepted);
    void CommitSelection(const RECT& selection);
    void ResetInteraction();
    void RefreshFullFrame();
    void RefreshDirtyRect(const RECT& dirty_rect);
    void FlushPendingDirtyRegion();
    void UpdateBackBuffer(const RECT& dirty_rect);
    void InvalidateSelectionChange(const RECT& previous_selection,
                                   bool previous_has_selection,
                                   const RECT& previous_toolbar_rect,
                                   bool previous_toolbar_visible);
    void InvalidatePreviewChange(const RECT& previous_preview, bool previous_has_preview);
    void UpdateToolbarLayout();
    void SetActiveTool(EditTool tool);
    bool CanUndoLastEdit() const;
    void PushUndoState();
    void UndoLastEdit();
    common::Result<void> ApplyMarkupCommand(const editing::MarkupCommand& command);
    common::Result<void> ApplyPendingRectangle();
    common::Result<void> ApplyPendingMosaic();
    common::Result<void> ApplyPendingArrow();
    bool HandleSetCursor() const;
    bool HandleKeyDown(WPARAM w_param);
    bool HandleToolbarAction(SelectionToolbarButtonId toolbar_action);
    void HandleLeftButtonDown(const POINT& point);
    void HandleMouseMove(const POINT& point);
    void HandleLeftButtonUp(const POINT& point);
    void ShowEditError(const std::wstring& error_message) const;

    void PaintBaseImage(HDC hdc) const;
    void PaintOverlay(HDC hdc, const RECT& client_rect) const;
    void DrawInstructions(HDC hdc, const RECT& bounds) const;
    RegionSelectionRenderModel BuildRenderModel(const RECT& client_rect) const;
    std::array<RECT, 8> BuildSelectionHandleRects(const RECT& selection) const;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    const capture::DesktopSnapshot* snapshot_ = nullptr;
    capture::CapturedImage working_image_;
    SelectionSession session_;
    SelectionEditToolbar toolbar_;
    RegionSelectionBuffers buffers_;
    RegionSelectionRenderer renderer_;
    common::UniqueRegion pending_dirty_region_;
};

}  // namespace ui
