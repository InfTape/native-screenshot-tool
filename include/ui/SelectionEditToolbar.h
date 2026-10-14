#pragma once

#include <Windows.h>

#include <array>
#include <optional>

#include "common/GdiResources.h"
#include "editing/MarkupCommand.h"

namespace ui {

enum class SelectionToolbarCommand {
    None,
    Undo,
    Ocr,
    CopyText,
    TranslateText,
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
    void UpdateLayout(const RECT& selection, const RECT& client_bounds,
                      bool for_recording = false, UINT dpi = 96, bool ocr_mode = false);
    void Hide();

    bool IsVisible() const;
    const RECT& Bounds() const;
    SelectionToolbarButtonId HitTest(const POINT& point) const;
    void Paint(HDC hdc, editing::MarkupTool active_tool, bool can_undo, bool can_copy_text = false, bool can_translate = false) const;

private:
    struct Button {
        SelectionToolbarButtonId id{};
        RECT bounds{};
        const wchar_t* label = L"";
    };

    static constexpr int kButtonCount = 9;

    std::array<Button, kButtonCount> buttons_{};
    RECT bounds_{};
    bool visible_ = false;
    bool ocr_mode_ = false;
    UINT dpi_ = 96;
    common::UniqueGdiObject<HFONT> font_;
};

}  // namespace ui
