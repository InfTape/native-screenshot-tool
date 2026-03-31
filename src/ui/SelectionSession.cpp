#include "ui/SelectionSession.h"

#include <utility>

namespace ui {

void SelectionSession::Reset(bool start_with_full_selection_value) {
    ClearHistory();
    start_with_full_selection = start_with_full_selection_value;
    finished = false;
    accepted = false;
    anchor = POINT{};
    current = POINT{};
    selection = RECT{};
    active_tool = editing::MarkupTool::Select;
    ResetInteraction();
}

void SelectionSession::ResetInteraction() {
    interaction_mode = InteractionMode::None;
    adjust_handle = SelectionAdjustHandle::None;
    drag_start = POINT{};
    drag_origin_selection = RECT{};
    preview_start = POINT{};
    preview_current = POINT{};
    brush_points.clear();
}

void SelectionSession::Finish(bool accepted_value, const RECT& current_selection) {
    accepted = accepted_value;
    selection = current_selection;
    ResetInteraction();
    finished = true;
}

void SelectionSession::CommitSelection(const RECT& current_selection) {
    selection = current_selection;
    active_tool = editing::MarkupTool::Select;
}

bool SelectionSession::CanUndo() const {
    return !edit_history_.empty();
}

void SelectionSession::PushUndoState(const capture::CapturedImage& image, std::size_t max_undo_steps) {
    if (edit_history_.size() >= max_undo_steps) {
        edit_history_.erase(edit_history_.begin());
    }

    edit_history_.push_back(image);
}

bool SelectionSession::UndoLastEdit(capture::CapturedImage& image) {
    if (!CanUndo()) {
        return false;
    }

    image = std::move(edit_history_.back());
    edit_history_.pop_back();
    return true;
}

void SelectionSession::DiscardLastUndo() {
    if (CanUndo()) {
        edit_history_.pop_back();
    }
}

void SelectionSession::ClearHistory() {
    edit_history_.clear();
}

}  // namespace ui
