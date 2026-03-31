#pragma once

#include <Windows.h>

#include <array>
#include <optional>

#include "editing/MarkupCommand.h"

namespace ui {

enum class SelectionToolbarCommand {
    None,
    Undo,
    Confirm,
    Cancel,
};

struct SelectionToolbarButtonId {
    enum class Kind {
        None,
        Tool,
        Command,
    };

    static constexpr SelectionToolbarButtonId None() {
        return SelectionToolbarButtonId{};
    }

    static constexpr SelectionToolbarButtonId Tool(editing::MarkupTool tool) {
        SelectionToolbarButtonId id{};
        id.kind = Kind::Tool;
        id.tool = tool;
        return id;
    }

    static constexpr SelectionToolbarButtonId Command(SelectionToolbarCommand command) {
        SelectionToolbarButtonId id{};
        id.kind = Kind::Command;
        id.command = command;
        return id;
    }

    [[nodiscard]] constexpr bool IsNone() const {
        return kind == Kind::None;
    }

    [[nodiscard]] constexpr bool IsTool() const {
        return kind == Kind::Tool;
    }

    [[nodiscard]] constexpr bool IsCommand(SelectionToolbarCommand value) const {
        return kind == Kind::Command && command == value;
    }

    Kind kind = Kind::None;
    editing::MarkupTool tool = editing::MarkupTool::Select;
    SelectionToolbarCommand command = SelectionToolbarCommand::None;
};

class SelectionEditToolbar {
public:
    void UpdateLayout(const RECT& selection, const RECT& client_bounds);
    void Hide();

    bool IsVisible() const;
    const RECT& Bounds() const;
    SelectionToolbarButtonId HitTest(const POINT& point) const;
    void Paint(HDC hdc, editing::MarkupTool active_tool, bool can_undo) const;

private:
    struct Button {
        SelectionToolbarButtonId id{};
        RECT bounds{};
        const wchar_t* label = L"";
    };

    static constexpr int kButtonCount = 8;

    std::array<Button, kButtonCount> buttons_{};
    RECT bounds_{};
    bool visible_ = false;
};

}  // namespace ui
