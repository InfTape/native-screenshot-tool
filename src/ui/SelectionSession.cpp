#include "ui/SelectionSession.h"

#include <algorithm>
#include <new>
#include <utility>

#include "editing/ImageMarkupService.h"

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

common::Result<void> SelectionSession::ApplyEdit(capture::CapturedImage& image,
                                               const editing::MarkupCommand& command,
                                               std::size_t max_undo_steps) {
    if (max_undo_steps == 0) {
        return common::Result<void>::Failure(L"撤销步数必须大于零。");
    }

    try {
        editing::MarkupCommand stored_command = command;
        // Allocate history storage before rendering. Failed edits leave both
        // the displayed image and the available undo steps unchanged.
        edit_history_.reserve((std::max)(edit_history_.size(), max_undo_steps));
        capture::CapturedImage next_base = CanUndo() ? history_base_ : image;
        const std::size_t fold_count = edit_history_.size() >= max_undo_steps
            ? edit_history_.size() - max_undo_steps + 1 : 0;
        for (std::size_t index = 0; index < fold_count; ++index) {
            auto result = editing::ImageMarkupService::ApplyCommand(next_base, edit_history_[index]);
            if (!result) {
                return result;
            }
        }

        // CapturedImage uses copy-on-write, so the previous image remains
        // intact until the edit succeeds, without retaining it in history.
        capture::CapturedImage next_image = image;
        auto result = editing::ImageMarkupService::ApplyCommand(next_image, command);
        if (!result) {
            return result;
        }

        edit_history_.erase(edit_history_.begin(), edit_history_.begin() + fold_count);
        edit_history_.push_back(std::move(stored_command));
        history_base_ = std::move(next_base);
        image = std::move(next_image);
        return common::Result<void>::Success();
    } catch (const std::bad_alloc&) {
        return common::Result<void>::Failure(L"内存不足，无法完成标注。");
    }
}

common::Result<void> SelectionSession::UndoLastEdit(capture::CapturedImage& image) {
    if (!CanUndo()) {
        return common::Result<void>::Success();
    }

    try {
        capture::CapturedImage restored = history_base_;
        // Replay in order: mosaic must sample the pixels produced by the
        // preceding annotations, exactly as it did when originally applied.
        for (std::size_t index = 0; index + 1 < edit_history_.size(); ++index) {
            auto result = editing::ImageMarkupService::ApplyCommand(restored, edit_history_[index]);
            if (!result) {
                return result;
            }
        }

        image = std::move(restored);
        edit_history_.pop_back();
        return common::Result<void>::Success();
    } catch (const std::bad_alloc&) {
        return common::Result<void>::Failure(L"内存不足，无法撤销标注。");
    }
}

void SelectionSession::ClearHistory() {
    edit_history_.clear();
    history_base_ = {};
}

}  // namespace ui
