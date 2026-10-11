#pragma once

#include <Windows.h>

#include <array>
#include <optional>
#include <string>
#include <future>
#include <cstdint>

#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "common/GdiResources.h"
#include "common/Result.h"
#include "editing/MarkupCommand.h"
#include "ui/RegionSelectionBuffers.h"
#include "ui/RegionSelectionRenderer.h"
#include "ui/SelectionEditToolbar.h"
#include "ui/SelectionSession.h"
#include "ui/OcrTextSelection.h"
#include "ocr/OcrWorker.h"

namespace ui {

struct RegionSelectionResult {
    RECT region{};
    capture::CapturedImage image;
    bool text_only = false;
};

class RegionSelectionOverlay {
public:
    explicit RegionSelectionOverlay(HINSTANCE instance);

    common::Result<std::optional<RegionSelectionResult>> SelectRegion(
        const capture::DesktopSnapshot& snapshot, bool for_recording = false);
    common::Result<std::optional<RegionSelectionResult>> EditImage(
        const capture::DesktopSnapshot& snapshot);
    common::Result<std::optional<RegionSelectionResult>> EditWindow(
        const capture::DesktopSnapshot& desktop,
        const capture::CapturedImage& window_image, const POINT& window_origin);

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
        bool start_with_full_selection,
        const RECT* initial_selection = nullptr, const RECT* desktop_bounds = nullptr);
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
    RECT BrushPreviewRect() const;
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
    common::Result<void> ApplyPendingBrush();
    void AppendBrushPoint(const POINT& point);
    bool HandleSetCursor() const;
    bool HandleKeyDown(WPARAM w_param);
    bool HandleToolbarAction(SelectionToolbarButtonId toolbar_action);
    void HandleLeftButtonDown(const POINT& point);
    void HandleMouseMove(const POINT& point);
    void HandleLeftButtonUp(const POINT& point);
    void ShowEditError(const std::wstring& error_message) const;
    void StartOcr();
    void LeaveOcr();
    void PollOcr();
    void LaunchOcr();
    void RefreshOcrSelection();
    void CopyOcrText();
    void ShowOcrContextMenu(POINT screen_point);
    bool MeasureOcrMenuItem(MEASUREITEMSTRUCT& item) const;
    bool DrawOcrMenuItem(const DRAWITEMSTRUCT& item) const;
    void PaintOcrSelection(HDC hdc, const RECT& client_rect) const;
    ocr::Point OcrPoint(const POINT& point) const;

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
    bool for_recording_ = false;
    bool window_editing_ = false;
    bool text_only_ = false;
    bool ocr_mode_ = false, ocr_busy_ = false, ocr_dragging_ = false;
    bool ocr_menu_open_ = false;
    bool ocr_launch_pending_ = false;
    std::wstring ocr_status_;
    common::UniqueGdiObject<HFONT> ocr_menu_font_;
    UINT ocr_menu_dpi_ = 96;
    OcrTextSelection ocr_selection_;
    std::uint64_t ocr_generation_ = 0, ocr_job_generation_ = 0;
    static constexpr UINT_PTR kOcrPollTimer = 0x4F43;
    // Cancellation kills only this request's child process. The UI owns no OCR
    // model or inference runtime, and need not wait for recognition to finish.
    std::stop_source ocr_stop_;
    std::future<common::Result<ocr::Document>> ocr_future_;
    RECT instruction_area_{};
    RegionSelectionBuffers buffers_;
    RegionSelectionRenderer renderer_;
    common::UniqueRegion pending_dirty_region_;
};

}  // namespace ui
