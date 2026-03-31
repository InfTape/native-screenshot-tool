#include "ui/RegionSelectionOverlay.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>

#include "common/RectUtils.h"

namespace {

bool IsPointInsideRect(const RECT& rect, const POINT& point) {
    return common::HasArea(rect) && PtInRect(&rect, point) != FALSE;
}

POINT ClampPointToRect(const POINT& point, const RECT& rect) {
    POINT clamped{};
    clamped.x = std::clamp(point.x, rect.left, rect.right - 1);
    clamped.y = std::clamp(point.y, rect.top, rect.bottom - 1);
    return clamped;
}

bool SamePoint(const POINT& left, const POINT& right) {
    return left.x == right.x && left.y == right.y;
}

std::optional<ui::SelectionSession::InteractionMode> PreviewInteractionModeForTool(
    editing::MarkupTool tool) {
    switch (tool) {
    case editing::MarkupTool::Rectangle:
        return ui::SelectionSession::InteractionMode::Rectangle;
    case editing::MarkupTool::Mosaic:
        return ui::SelectionSession::InteractionMode::Mosaic;
    case editing::MarkupTool::Arrow:
        return ui::SelectionSession::InteractionMode::Arrow;
    case editing::MarkupTool::Brush:
        return ui::SelectionSession::InteractionMode::Brush;
    case editing::MarkupTool::Select:
    default:
        return std::nullopt;
    }
}

}  // namespace

namespace ui {

bool RegionSelectionOverlay::HandleSetCursor() const {
    if (window_ != nullptr) {
        POINT cursor_point{};
        GetCursorPos(&cursor_point);
        ScreenToClient(window_, &cursor_point);

        if (!toolbar_.HitTest(cursor_point).IsNone()) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return true;
        }

        if (session_.active_tool == EditTool::Select && HasSelection()) {
            const SelectionAdjustHandle handle = session_.interaction_mode == InteractionMode::Adjusting
                                                     ? session_.adjust_handle
                                                     : HitTestSelectionHandle(cursor_point);
            const auto cursor_for_handle = [](SelectionAdjustHandle selection_handle) {
                switch (selection_handle) {
                case SelectionAdjustHandle::Move:
                    return IDC_SIZEALL;
                case SelectionAdjustHandle::Left:
                case SelectionAdjustHandle::Right:
                    return IDC_SIZEWE;
                case SelectionAdjustHandle::Top:
                case SelectionAdjustHandle::Bottom:
                    return IDC_SIZENS;
                case SelectionAdjustHandle::TopLeft:
                case SelectionAdjustHandle::BottomRight:
                    return IDC_SIZENWSE;
                case SelectionAdjustHandle::TopRight:
                case SelectionAdjustHandle::BottomLeft:
                    return IDC_SIZENESW;
                case SelectionAdjustHandle::None:
                default:
                    return IDC_CROSS;
                }
            };

            SetCursor(LoadCursorW(nullptr, cursor_for_handle(handle)));
            return true;
        }
    }

    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    return true;
}

bool RegionSelectionOverlay::HandleKeyDown(WPARAM w_param) {
    if (w_param == VK_ESCAPE) {
        FinishSelection(false);
        return true;
    }

    if ((GetKeyState(VK_CONTROL) < 0) && (w_param == 'Z') && CanUndoLastEdit() &&
        session_.interaction_mode != InteractionMode::Selecting) {
        ResetInteraction();
        UndoLastEdit();
        return true;
    }

    if (w_param == VK_RETURN && common::HasArea(session_.selection)) {
        FinishSelection(true);
        return true;
    }

    return false;
}

bool RegionSelectionOverlay::HandleToolbarAction(SelectionToolbarButtonId toolbar_action) {
    if (toolbar_action.IsTool()) {
        SetActiveTool(toolbar_action.tool);
        return true;
    }

    if (toolbar_action.IsCommand(SelectionToolbarCommand::Undo)) {
        if (CanUndoLastEdit()) {
            ResetInteraction();
            UndoLastEdit();
        }
        return true;
    }

    if (toolbar_action.IsCommand(SelectionToolbarCommand::Confirm)) {
        FinishSelection(true);
        return true;
    }

    if (toolbar_action.IsCommand(SelectionToolbarCommand::Cancel)) {
        FinishSelection(false);
        return true;
    }

    return false;
}

void RegionSelectionOverlay::HandleLeftButtonDown(const POINT& point) {
    const SelectionToolbarButtonId toolbar_action = toolbar_.HitTest(point);
    if (!toolbar_action.IsNone() && HandleToolbarAction(toolbar_action)) {
        return;
    }

    if (session_.active_tool == EditTool::Select) {
        if (common::HasArea(session_.selection)) {
            const SelectionAdjustHandle hit_handle = HitTestSelectionHandle(point);
            if (hit_handle != SelectionAdjustHandle::None) {
                session_.adjust_handle = hit_handle;
                session_.drag_start = point;
                session_.drag_origin_selection = session_.selection;
                session_.interaction_mode = InteractionMode::Adjusting;
                SetCapture(window_);
                return;
            }
        }

        const RECT previous_selection = CurrentSelection();
        const bool previous_has_selection = HasSelection();
        const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
        const bool previous_toolbar_visible = toolbar_.IsVisible();
        toolbar_.Hide();
        session_.anchor = point;
        session_.current = point;
        session_.interaction_mode = InteractionMode::Selecting;
        SetCapture(window_);
        InvalidateSelectionChange(previous_selection,
                                  previous_has_selection,
                                  previous_toolbar_rect,
                                  previous_toolbar_visible);
        return;
    }

    if (!IsPointInsideRect(session_.selection, point)) {
        return;
    }

    session_.preview_start = point;
    session_.preview_current = point;
    if (const auto preview_mode = PreviewInteractionModeForTool(session_.active_tool);
        preview_mode.has_value()) {
        session_.interaction_mode = *preview_mode;
    } else {
        session_.interaction_mode = InteractionMode::None;
    }

    if (session_.interaction_mode == InteractionMode::Brush) {
        session_.brush_points.clear();
        AppendBrushPoint(point);
    }

    SetCapture(window_);
}

void RegionSelectionOverlay::HandleMouseMove(const POINT& point) {
    if (session_.interaction_mode == InteractionMode::Selecting) {
        const RECT previous_selection = CurrentSelection();
        const bool previous_has_selection = HasSelection();
        session_.current = point;
        InvalidateSelectionChange(previous_selection, previous_has_selection, RECT{}, false);
        return;
    }

    if (session_.interaction_mode == InteractionMode::Adjusting) {
        const RECT previous_selection = session_.selection;
        const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
        const bool previous_toolbar_visible = toolbar_.IsVisible();
        session_.selection = AdjustSelectionFromDrag(point);
        UpdateToolbarLayout();
        InvalidateSelectionChange(previous_selection,
                                  true,
                                  previous_toolbar_rect,
                                  previous_toolbar_visible);
        return;
    }

    if (session_.interaction_mode == InteractionMode::Rectangle ||
        session_.interaction_mode == InteractionMode::Mosaic ||
        session_.interaction_mode == InteractionMode::Arrow) {
        const RECT previous_preview = session_.interaction_mode == InteractionMode::Arrow
                                          ? ArrowPreviewRect()
                                          : CurrentPreviewRect();
        const bool previous_has_preview = common::HasArea(previous_preview);
        session_.preview_current = point;
        InvalidatePreviewChange(previous_preview, previous_has_preview);
        return;
    }

    if (session_.interaction_mode == InteractionMode::Brush) {
        const RECT previous_preview = BrushPreviewRect();
        const bool previous_has_preview = common::HasArea(previous_preview);
        AppendBrushPoint(point);
        InvalidatePreviewChange(previous_preview, previous_has_preview);
    }
}

void RegionSelectionOverlay::HandleLeftButtonUp(const POINT& point) {
    if (session_.interaction_mode == InteractionMode::Selecting) {
        session_.current = point;
        const RECT final_selection = CurrentSelection();
        ResetInteraction();
        if (common::HasArea(final_selection)) {
            CommitSelection(final_selection);
        } else {
            session_.selection = RECT{};
            toolbar_.Hide();
            RefreshFullFrame();
        }
        return;
    }

    if (session_.interaction_mode == InteractionMode::Adjusting) {
        const RECT previous_selection = session_.selection;
        const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
        const bool previous_toolbar_visible = toolbar_.IsVisible();
        session_.selection = AdjustSelectionFromDrag(point);
        UpdateToolbarLayout();
        InvalidateSelectionChange(previous_selection,
                                  true,
                                  previous_toolbar_rect,
                                  previous_toolbar_visible);
        ResetInteraction();
        return;
    }

    if (session_.interaction_mode == InteractionMode::Rectangle ||
        session_.interaction_mode == InteractionMode::Mosaic ||
        session_.interaction_mode == InteractionMode::Arrow) {
        session_.preview_current = point;
        const RECT previous_preview = session_.interaction_mode == InteractionMode::Arrow
                                          ? ArrowPreviewRect()
                                          : CurrentPreviewRect();
        const bool previous_has_preview = common::HasArea(previous_preview);

        common::Result<void> apply_result = common::Result<void>::Success();
        if (session_.interaction_mode == InteractionMode::Rectangle) {
            apply_result = ApplyPendingRectangle();
        } else if (session_.interaction_mode == InteractionMode::Mosaic) {
            apply_result = ApplyPendingMosaic();
        } else {
            apply_result = ApplyPendingArrow();
        }

        ResetInteraction();
        if (!apply_result) {
            ShowEditError(apply_result.Error());
        }
        InvalidatePreviewChange(previous_preview, previous_has_preview);
        RefreshFullFrame();
        return;
    }

    if (session_.interaction_mode == InteractionMode::Brush) {
        AppendBrushPoint(point);
        const RECT previous_preview = BrushPreviewRect();
        const bool previous_has_preview = common::HasArea(previous_preview);

        common::Result<void> apply_result = ApplyPendingBrush();
        ResetInteraction();
        if (!apply_result) {
            ShowEditError(apply_result.Error());
        }
        InvalidatePreviewChange(previous_preview, previous_has_preview);
        RefreshFullFrame();
    }
}

void RegionSelectionOverlay::AppendBrushPoint(const POINT& point) {
    if (!common::HasArea(session_.selection)) {
        return;
    }

    const POINT clamped_point = ClampPointToRect(point, session_.selection);

    if (session_.brush_points.empty()) {
        session_.brush_smooth_x = static_cast<double>(clamped_point.x);
        session_.brush_smooth_y = static_cast<double>(clamped_point.y);
        session_.brush_points.push_back(clamped_point);
        return;
    }

    constexpr double kAlphaMin = 0.1;   // 慢速时：强平滑
    constexpr double kAlphaMax = 0.6;   // 快速时：跟手
    constexpr double kSpeedLow = 3.0;   // 低于此距离视为慢速
    constexpr double kSpeedHigh = 20.0; // 高于此距离视为快速

    const double raw_dx = static_cast<double>(clamped_point.x) - session_.brush_smooth_x;
    const double raw_dy = static_cast<double>(clamped_point.y) - session_.brush_smooth_y;
    const double distance = std::sqrt(raw_dx * raw_dx + raw_dy * raw_dy);

    const double t = std::clamp((distance - kSpeedLow) / (kSpeedHigh - kSpeedLow), 0.0, 1.0);
    const double alpha = kAlphaMin + t * (kAlphaMax - kAlphaMin);

    session_.brush_smooth_x += alpha * raw_dx;
    session_.brush_smooth_y += alpha * raw_dy;

    POINT smoothed_point{};
    smoothed_point.x = static_cast<LONG>(session_.brush_smooth_x + 0.5);
    smoothed_point.y = static_cast<LONG>(session_.brush_smooth_y + 0.5);

    if (SamePoint(session_.brush_points.back(), smoothed_point)) {
        return;
    }

    session_.brush_points.push_back(smoothed_point);
}

}  // namespace ui
