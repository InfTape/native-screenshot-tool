#pragma once

#include <Windows.h>

#include <cstddef>
#include <vector>

#include "capture/CapturedImage.h"
#include "editing/MarkupCommand.h"

namespace ui {

class SelectionSession {
public:
    enum class InteractionMode {
        None,
        Selecting,
        Adjusting,
        Rectangle,
        Mosaic,
        Arrow,
        Brush,
    };

    enum class SelectionAdjustHandle {
        None,
        Move,
        Left,
        Top,
        Right,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    void Reset(bool start_with_full_selection);
    void ResetInteraction();
    void Finish(bool accepted_value, const RECT& current_selection);
    void CommitSelection(const RECT& current_selection);

    [[nodiscard]] bool CanUndo() const;
    void PushUndoState(const capture::CapturedImage& image, std::size_t max_undo_steps);
    bool UndoLastEdit(capture::CapturedImage& image);
    void DiscardLastUndo();
    void ClearHistory();

    bool start_with_full_selection = false;
    bool finished = false;
    bool accepted = false;
    POINT anchor{};
    POINT current{};
    RECT selection{};
    editing::MarkupTool active_tool = editing::MarkupTool::Select;
    InteractionMode interaction_mode = InteractionMode::None;
    SelectionAdjustHandle adjust_handle = SelectionAdjustHandle::None;
    POINT drag_start{};
    RECT drag_origin_selection{};
    POINT preview_start{};
    POINT preview_current{};
    std::vector<POINT> brush_points;

private:
    std::vector<capture::CapturedImage> edit_history_;
};

}  // namespace ui
