#include "ui/RegionSelectionOverlay.h"

#include <algorithm>

#include "common/RectUtils.h"

namespace ui {

RECT RegionSelectionOverlay::CurrentSelection() const {
    if (session_.interaction_mode == InteractionMode::Selecting) {
        RECT normalized = common::NormalizeRect(session_.anchor, session_.current);
        return common::ClampRectToBounds(normalized, snapshot_->image.Width(), snapshot_->image.Height());
    }

    return session_.selection;
}

bool RegionSelectionOverlay::HasSelection() const {
    return common::HasArea(CurrentSelection());
}

RECT RegionSelectionOverlay::CurrentPreviewRect() const {
    if (session_.interaction_mode != InteractionMode::Mosaic &&
        session_.interaction_mode != InteractionMode::Rectangle) {
        return RECT{};
    }

    const RECT preview = common::NormalizeRect(session_.preview_start, session_.preview_current);
    RECT clipped{};
    IntersectRect(&clipped, &preview, &session_.selection);
    return clipped;
}

RECT RegionSelectionOverlay::PreviewVisualRect(const RECT& preview_rect) const {
    if (!common::HasArea(preview_rect)) {
        return RECT{};
    }

    const SIZE buffer_size = buffers_.BufferSize();
    RECT visual_rect = preview_rect;
    InflateRect(&visual_rect, kPreviewInvalidationPadding, kPreviewInvalidationPadding);
    return common::ClampRectToBounds(visual_rect, buffer_size.cx, buffer_size.cy);
}

bool RegionSelectionOverlay::HasPreviewRect() const {
    return common::HasArea(CurrentPreviewRect());
}

RegionSelectionOverlay::SelectionAdjustHandle RegionSelectionOverlay::HitTestSelectionHandle(
    const POINT& point) const {
    if (!common::HasArea(session_.selection)) {
        return SelectionAdjustHandle::None;
    }

    static constexpr SelectionAdjustHandle kResizeHandles[] = {
        SelectionAdjustHandle::TopLeft,
        SelectionAdjustHandle::Top,
        SelectionAdjustHandle::TopRight,
        SelectionAdjustHandle::Right,
        SelectionAdjustHandle::BottomRight,
        SelectionAdjustHandle::Bottom,
        SelectionAdjustHandle::BottomLeft,
        SelectionAdjustHandle::Left,
    };

    for (const auto handle : kResizeHandles) {
        RECT hit_rect = SelectionHandleRect(session_.selection, handle);
        InflateRect(&hit_rect, kSelectionHandleHitPadding, kSelectionHandleHitPadding);
        if (PtInRect(&hit_rect, point) != FALSE) {
            return handle;
        }
    }

    if (PtInRect(&session_.selection, point) != FALSE) {
        return SelectionAdjustHandle::Move;
    }

    return SelectionAdjustHandle::None;
}

RECT RegionSelectionOverlay::SelectionHandleRect(const RECT& selection,
                                                 SelectionAdjustHandle handle) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    const LONG center_x = (selection.left + selection.right) / 2;
    const LONG center_y = (selection.top + selection.bottom) / 2;
    const LONG half = kSelectionHandleSize / 2;
    auto make_handle_rect = [half](LONG center_x_value, LONG center_y_value) {
        return RECT{center_x_value - half,
                    center_y_value - half,
                    center_x_value + half,
                    center_y_value + half};
    };

    switch (handle) {
    case SelectionAdjustHandle::Left:
        return make_handle_rect(selection.left, center_y);
    case SelectionAdjustHandle::Top:
        return make_handle_rect(center_x, selection.top);
    case SelectionAdjustHandle::Right:
        return make_handle_rect(selection.right, center_y);
    case SelectionAdjustHandle::Bottom:
        return make_handle_rect(center_x, selection.bottom);
    case SelectionAdjustHandle::TopLeft:
        return make_handle_rect(selection.left, selection.top);
    case SelectionAdjustHandle::TopRight:
        return make_handle_rect(selection.right, selection.top);
    case SelectionAdjustHandle::BottomLeft:
        return make_handle_rect(selection.left, selection.bottom);
    case SelectionAdjustHandle::BottomRight:
        return make_handle_rect(selection.right, selection.bottom);
    case SelectionAdjustHandle::Move:
        return selection;
    case SelectionAdjustHandle::None:
    default:
        return RECT{};
    }
}

RECT RegionSelectionOverlay::SelectionLabelRect(const RECT& selection, const RECT& client_rect) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    RECT label_rect{
        selection.left,
        std::max(client_rect.top, selection.top - 32),
        selection.left + 180,
        selection.top,
    };
    if (common::RectHeight(label_rect) < 24) {
        label_rect.top = selection.bottom;
        label_rect.bottom = std::min(client_rect.bottom, selection.bottom + 32);
    }

    label_rect.left = std::clamp(label_rect.left, client_rect.left, client_rect.right);
    label_rect.right = std::clamp(label_rect.right, client_rect.left, client_rect.right);
    label_rect.top = std::clamp(label_rect.top, client_rect.top, client_rect.bottom);
    label_rect.bottom = std::clamp(label_rect.bottom, client_rect.top, client_rect.bottom);
    return label_rect;
}

RECT RegionSelectionOverlay::SelectionVisualRect(const RECT& selection,
                                                 const RECT& client_rect,
                                                 const RECT& toolbar_rect,
                                                 bool include_toolbar) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    RECT selection_bounds = selection;
    InflateRect(&selection_bounds, kSelectionHandleSize + 3, kSelectionHandleSize + 3);
    selection_bounds.left = std::clamp(selection_bounds.left, client_rect.left, client_rect.right);
    selection_bounds.top = std::clamp(selection_bounds.top, client_rect.top, client_rect.bottom);
    selection_bounds.right =
        std::clamp(selection_bounds.right, client_rect.left, client_rect.right);
    selection_bounds.bottom =
        std::clamp(selection_bounds.bottom, client_rect.top, client_rect.bottom);

    const RECT label_rect = SelectionLabelRect(selection, client_rect);

    RECT selection_and_label{};
    UnionRect(&selection_and_label, &selection_bounds, &label_rect);
    if (!include_toolbar || !common::HasArea(toolbar_rect)) {
        return selection_and_label;
    }

    RECT visual_rect{};
    UnionRect(&visual_rect, &selection_and_label, &toolbar_rect);
    return visual_rect;
}

RECT RegionSelectionOverlay::ArrowPreviewRect() const {
    if (session_.interaction_mode != InteractionMode::Arrow) {
        return RECT{};
    }

    RECT bounds = common::NormalizeRect(session_.preview_start, session_.preview_current);
    InflateRect(&bounds, 28, 28);
    return common::ClampRectToBounds(bounds, snapshot_->image.Width(), snapshot_->image.Height());
}

RECT RegionSelectionOverlay::AdjustSelectionFromDrag(const POINT& point) const {
    RECT adjusted = session_.drag_origin_selection;
    const LONG dx = point.x - session_.drag_start.x;
    const LONG dy = point.y - session_.drag_start.y;
    const LONG minimum_size = static_cast<LONG>(kMinimumSelectionSize);
    const LONG max_width = static_cast<LONG>(snapshot_->image.Width());
    const LONG max_height = static_cast<LONG>(snapshot_->image.Height());

    switch (session_.adjust_handle) {
    case SelectionAdjustHandle::Move: {
        OffsetRect(&adjusted, dx, dy);
        const LONG width = adjusted.right - adjusted.left;
        const LONG height = adjusted.bottom - adjusted.top;

        if (adjusted.left < 0) {
            adjusted.left = 0;
            adjusted.right = width;
        }
        if (adjusted.top < 0) {
            adjusted.top = 0;
            adjusted.bottom = height;
        }
        if (adjusted.right > max_width) {
            adjusted.right = max_width;
            adjusted.left = adjusted.right - width;
        }
        if (adjusted.bottom > max_height) {
            adjusted.bottom = max_height;
            adjusted.top = adjusted.bottom - height;
        }
        break;
    }
    case SelectionAdjustHandle::Left:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        break;
    case SelectionAdjustHandle::Top:
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::Right:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        break;
    case SelectionAdjustHandle::Bottom:
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::TopLeft:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::TopRight:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::BottomLeft:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::BottomRight:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::None:
    default:
        break;
    }

    return adjusted;
}

}  // namespace ui
