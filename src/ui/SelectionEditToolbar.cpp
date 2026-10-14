#include "ui/SelectionEditToolbar.h"

#include <algorithm>

#include "common/GdiResources.h"
#include "ui/UiTypography.h"

namespace {

constexpr int kToolbarPadding = 4;
constexpr int kButtonWidth = 64;
constexpr int kButtonHeight = 28;
constexpr int kButtonSpacing = 4;
constexpr int kToolbarOffset = 8;
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

bool IsButtonEnabled(const ui::SelectionToolbarButtonId& button_id, bool can_undo, bool can_copy_text, bool can_translate) {
    return (!button_id.IsCommand(ui::SelectionToolbarCommand::Undo) || can_undo) &&
           (!button_id.IsCommand(ui::SelectionToolbarCommand::CopyText) || can_copy_text) &&
           (!button_id.IsCommand(ui::SelectionToolbarCommand::TranslateText) || can_translate);
}

}  // namespace

namespace ui {

void SelectionEditToolbar::UpdateLayout(const RECT& selection, const RECT& client_bounds,
                                       bool for_recording, UINT dpi, bool ocr_mode) {
    ocr_mode_ = ocr_mode;
    dpi = dpi ? dpi : 96;
    if (!font_ || dpi_ != dpi) {
        font_.Reset(CreateUiFont(dpi));
        dpi_ = dpi;
    }
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int padding = scale(kToolbarPadding);
    const int spacing = scale(kButtonSpacing);
    const int offset = scale(kToolbarOffset);
    const int button_count = for_recording ? 3 : kButtonCount;
    // Keep all actions reachable on smaller displays at high DPI.
    const int available_width = static_cast<int>(client_bounds.right - client_bounds.left) -
                                2 * offset - 2 * padding - (button_count - 1) * spacing;
    const int button_width = std::max(1, std::min(scale(kButtonWidth), available_width / button_count));
    const int button_height = scale(kButtonHeight);
    buttons_ = {};
    const int toolbar_width =
        (button_count * button_width) + ((button_count - 1) * spacing) +
        (padding * 2);
    const int toolbar_height = button_height + (padding * 2);

    int left = selection.left + ((selection.right - selection.left - toolbar_width) / 2);
    left = std::clamp(left,
                      static_cast<int>(client_bounds.left) + offset,
                      std::max(static_cast<int>(client_bounds.left) + offset,
                               static_cast<int>(client_bounds.right) - toolbar_width - offset));

    int top = selection.bottom + offset;
    if ((top + toolbar_height) > (client_bounds.bottom - offset)) {
        top = selection.top - toolbar_height - offset;
    }
    top = std::clamp(top,
                     static_cast<int>(client_bounds.top) + offset,
                     std::max(static_cast<int>(client_bounds.top) + offset,
                              static_cast<int>(client_bounds.bottom) - toolbar_height - offset));

    bounds_ = CreateRect(left, top, toolbar_width, toolbar_height);

    static constexpr SelectionToolbarButtonId kButtonIds[kButtonCount] = {
        SelectionToolbarButtonId::Tool(editing::MarkupTool::Select),
        SelectionToolbarButtonId::Tool(editing::MarkupTool::Rectangle),
        SelectionToolbarButtonId::Tool(editing::MarkupTool::Mosaic),
        SelectionToolbarButtonId::Tool(editing::MarkupTool::Arrow),
        SelectionToolbarButtonId::Tool(editing::MarkupTool::Brush),
        SelectionToolbarButtonId::Command(SelectionToolbarCommand::Undo),
        SelectionToolbarButtonId::Command(SelectionToolbarCommand::Ocr),
        SelectionToolbarButtonId::Command(SelectionToolbarCommand::Confirm),
        SelectionToolbarButtonId::Command(SelectionToolbarCommand::Cancel),
    };
    static constexpr const wchar_t* kLabels[kButtonCount] = {
        L"选择",
        L"矩形",
        L"马赛克",
        L"箭头",
        L"画笔",
        L"撤销",
        L"OCR",
        L"完成",
        L"取消",
    };

    int button_left = bounds_.left + padding;
    for (int index = 0; index < button_count; ++index) {
        const int source_index = for_recording && index > 0 ? index + kButtonCount - 3 : index;
        buttons_[index].id = kButtonIds[source_index];
        buttons_[index].label = for_recording && index == 1 ? L"开始录制" : kLabels[source_index];
        if (ocr_mode && source_index == 0) {
            buttons_[index].id = SelectionToolbarButtonId::Command(SelectionToolbarCommand::TranslateText);
            buttons_[index].label = L"翻译";
        }
        if (ocr_mode && source_index == 5) {
            buttons_[index].id = SelectionToolbarButtonId::Command(SelectionToolbarCommand::CopyText);
            buttons_[index].label = L"复制";
        }
        if (ocr_mode && source_index == 6) buttons_[index].label = L"返回";
        if (ocr_mode && source_index == 7) buttons_[index].label = L"关闭";
        buttons_[index].bounds =
            CreateRect(button_left, bounds_.top + padding, button_width, button_height);
        button_left += button_width + spacing;
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

SelectionToolbarButtonId SelectionEditToolbar::HitTest(const POINT& point) const {
    if (!visible_ || !PointInRectInclusive(bounds_, point)) {
        return SelectionToolbarButtonId::None();
    }

    for (const auto& button : buttons_) {
        if (PointInRectInclusive(button.bounds, point)) {
            return button.id;
        }
    }

    return SelectionToolbarButtonId::None();
}

void SelectionEditToolbar::Paint(HDC hdc, editing::MarkupTool active_tool, bool can_undo, bool can_copy_text, bool can_translate) const {
    if (!visible_) {
        return;
    }

    common::UniqueBrush background_brush{CreateSolidBrush(kToolbarBackground)};
    common::UniqueBrush border_brush{CreateSolidBrush(kToolbarBorder)};
    if (!background_brush || !border_brush) {
        return;
    }

    FillRect(hdc, &bounds_, background_brush.Get());
    FrameRect(hdc, &bounds_, border_brush.Get());

    common::ScopedSelectObject selected_font(hdc, font_.Get());
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kToolbarText);

    for (const auto& button : buttons_) {
        if (button.id.IsNone()) continue;
        const bool is_active = ocr_mode_ ? button.id.IsCommand(SelectionToolbarCommand::Ocr) :
                               button.id.IsTool() && button.id.tool == active_tool;
        const bool is_primary_action = button.id.IsCommand(SelectionToolbarCommand::Confirm);
        const bool is_enabled = IsButtonEnabled(button.id, can_undo, can_copy_text, can_translate);
        const COLORREF fill_color = !is_enabled
                                        ? kToolbarDisabled
                                        : (is_active ? kToolbarActive
                                                     : (is_primary_action ? kToolbarAction
                                                                          : kToolbarBackground));
        common::UniqueBrush button_brush{CreateSolidBrush(fill_color)};
        common::UniqueBrush button_border{CreateSolidBrush(kToolbarBorder)};
        if (!button_brush || !button_border) {
            continue;
        }

        FillRect(hdc, &button.bounds, button_brush.Get());
        FrameRect(hdc, &button.bounds, button_border.Get());

        RECT text_rect = button.bounds;
        InflateRect(&text_rect, -MulDiv(6, dpi_, 96), -MulDiv(4, dpi_, 96));
        DrawTextW(hdc, button.label, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

}  // namespace ui
