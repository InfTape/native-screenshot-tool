#pragma once

#include <Windows.h>

#include <array>

namespace ui {

enum class SelectionToolbarAction {
    None,
    Select,
    Mosaic,
    Arrow,
    Undo,
    Confirm,
    Cancel,
};

class SelectionEditToolbar {
public:
    void UpdateLayout(const RECT& selection, const RECT& client_bounds);
    void Hide();

    bool IsVisible() const;
    const RECT& Bounds() const;
    SelectionToolbarAction HitTest(const POINT& point) const;
    void Paint(HDC hdc, SelectionToolbarAction active_action, bool can_undo) const;

private:
    struct Button {
        SelectionToolbarAction action = SelectionToolbarAction::None;
        RECT bounds{};
        const wchar_t* label = L"";
    };

    static constexpr int kButtonCount = 6;

    std::array<Button, kButtonCount> buttons_{};
    RECT bounds_{};
    bool visible_ = false;
};

}  // namespace ui
