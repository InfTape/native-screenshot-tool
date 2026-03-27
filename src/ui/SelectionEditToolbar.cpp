#include "ui/SelectionEditToolbar.h"

#include <algorithm>

namespace {

constexpr int kToolbarPadding = 8;
constexpr int kButtonWidth = 88;
constexpr int kButtonHeight = 34;
constexpr int kButtonSpacing = 8;
constexpr int kToolbarOffset = 12;
constexpr COLORREF kToolbarBackground = RGB(20, 20, 20);
constexpr COLORREF kToolbarBorder = RGB(235, 235, 235);
constexpr COLORREF kToolbarText = RGB(255, 255, 255);
constexpr COLORREF kToolbarActive = RGB(28, 150, 242);
constexpr COLORREF kToolbarAction = RGB(242, 154, 28);
constexpr COLORREF kToolbarDisabled = RGB(72, 72, 72);

RECT CreateRect(int left, int top, int width, int height) {
    return RECT{left, top, left + width, top + height};
}

bool PointInRectInclusive(const RECT& rect, const POINT& point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
           point.y < rect.bottom;
}

bool IsButtonEnabled(ui::SelectionToolbarAction action, bool can_undo) {
    return action != ui::SelectionToolbarAction::Undo || can_undo;
}

}  // namespace

namespace ui {

void SelectionEditToolbar::UpdateLayout(const RECT& selection, const RECT& client_bounds) {
    const int toolbar_width =
        (kButtonCount * kButtonWidth) + ((kButtonCount - 1) * kButtonSpacing) +
        (kToolbarPadding * 2);
    const int toolbar_height = kButtonHeight + (kToolbarPadding * 2);

    int left = selection.left + ((selection.right - selection.left - toolbar_width) / 2);
    left = std::clamp(left,
                      static_cast<int>(client_bounds.left) + 12,
                      std::max(static_cast<int>(client_bounds.left) + 12,
                               static_cast<int>(client_bounds.right) - toolbar_width - 12));

    int top = selection.bottom + kToolbarOffset;
    if ((top + toolbar_height) > (client_bounds.bottom - 12)) {
        top = selection.top - toolbar_height - kToolbarOffset;
    }
    top = std::clamp(top,
                     static_cast<int>(client_bounds.top) + 12,
                     std::max(static_cast<int>(client_bounds.top) + 12,
                              static_cast<int>(client_bounds.bottom) - toolbar_height - 12));

    bounds_ = CreateRect(left, top, toolbar_width, toolbar_height);

    static constexpr SelectionToolbarAction kActions[kButtonCount] = {
        SelectionToolbarAction::Select,
        SelectionToolbarAction::Rectangle,
        SelectionToolbarAction::Mosaic,
        SelectionToolbarAction::Arrow,
        SelectionToolbarAction::Undo,
        SelectionToolbarAction::Confirm,
        SelectionToolbarAction::Cancel,
    };
    static constexpr const wchar_t* kLabels[kButtonCount] = {
        L"\u9009\u62e9",
        L"\u77e9\u5f62",
        L"\u9a6c\u8d5b\u514b",
        L"\u7bad\u5934",
        L"\u64a4\u9500",
        L"\u5b8c\u6210",
        L"\u53d6\u6d88",
    };

    int button_left = bounds_.left + kToolbarPadding;
    for (int index = 0; index < kButtonCount; ++index) {
        buttons_[index].action = kActions[index];
        buttons_[index].label = kLabels[index];
        buttons_[index].bounds =
            CreateRect(button_left, bounds_.top + kToolbarPadding, kButtonWidth, kButtonHeight);
        button_left += kButtonWidth + kButtonSpacing;
    }

    visible_ = true;
}

void SelectionEditToolbar::Hide() {
    visible_ = false;
    bounds_ = RECT{};
    for (auto& button : buttons_) {
        button = Button{};
    }
}

bool SelectionEditToolbar::IsVisible() const {
    return visible_;
}

const RECT& SelectionEditToolbar::Bounds() const {
    return bounds_;
}

SelectionToolbarAction SelectionEditToolbar::HitTest(const POINT& point) const {
    if (!visible_ || !PointInRectInclusive(bounds_, point)) {
        return SelectionToolbarAction::None;
    }

    for (const auto& button : buttons_) {
        if (PointInRectInclusive(button.bounds, point)) {
            return button.action;
        }
    }

    return SelectionToolbarAction::None;
}

void SelectionEditToolbar::Paint(HDC hdc,
                                 SelectionToolbarAction active_action,
                                 bool can_undo) const {
    if (!visible_) {
        return;
    }

    HBRUSH background_brush = CreateSolidBrush(kToolbarBackground);
    HBRUSH border_brush = CreateSolidBrush(kToolbarBorder);
    FillRect(hdc, &bounds_, background_brush);
    FrameRect(hdc, &bounds_, border_brush);
    DeleteObject(border_brush);
    DeleteObject(background_brush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kToolbarText);

    for (const auto& button : buttons_) {
        const bool is_active = button.action == active_action;
        const bool is_primary_action = button.action == SelectionToolbarAction::Confirm;
        const bool is_enabled = IsButtonEnabled(button.action, can_undo);
        const COLORREF fill_color = !is_enabled
                                        ? kToolbarDisabled
                                        : (is_active ? kToolbarActive
                                                     : (is_primary_action ? kToolbarAction
                                                                          : kToolbarBackground));
        HBRUSH button_brush = CreateSolidBrush(fill_color);
        HBRUSH button_border = CreateSolidBrush(kToolbarBorder);
        FillRect(hdc, &button.bounds, button_brush);
        FrameRect(hdc, &button.bounds, button_border);
        DeleteObject(button_border);
        DeleteObject(button_brush);

        RECT text_rect = button.bounds;
        InflateRect(&text_rect, -6, -4);
        DrawTextW(hdc, button.label, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

}  // namespace ui
