#include "ui/RegionSelectionOverlay.h"

#include <d2d1.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <string_view>
#include "ui/UiTypography.h"

namespace ui {
namespace {
std::pair<std::wstring_view, std::wstring_view> MenuText(const wchar_t* label) {
    const std::wstring_view text(label);
    const auto tab = text.find(L'\t');
    if (tab == std::wstring_view::npos) return {text, {}};
    return {text.substr(0, tab), text.substr(tab + 1)};
}
}
ocr::Point RegionSelectionOverlay::OcrPoint(const POINT& point) const {
    return {static_cast<float>(point.x - session_.selection.left),
            static_cast<float>(point.y - session_.selection.top)};
}
void RegionSelectionOverlay::StartOcr() {
    ResetInteraction();
    ++ocr_generation_;
    ocr_mode_ = ocr_busy_ = ocr_launch_pending_ = true;
    ocr_dragging_ = false;
    ocr_selection_.Clear();
    ocr_status_ = L"正在识别文字…";
    SetWindowTextW(window_, ocr_status_.c_str());
    UpdateToolbarLayout();
    RebuildSourceBuffers();
    RefreshFullFrame();
    if (!SetTimer(window_, kOcrPollTimer, 40, nullptr)) {
        ocr_busy_ = ocr_launch_pending_ = false;
        ocr_status_ = L"无法启动识别，请返回编辑后重试。";
        RefreshFullFrame();
        return;
    }
    // A cancelled job may still be completing. Queue this request rather than
    // replacing a running std::future (which would block the message thread).
    PollOcr();
}
void RegionSelectionOverlay::LeaveOcr() {
    ++ocr_generation_;
    ocr_mode_ = ocr_busy_ = ocr_launch_pending_ = ocr_dragging_ = false;
    ocr_selection_.Clear();
    SetWindowTextW(window_, L"选区截图");
    ResetInteraction();
    UpdateToolbarLayout();
    RebuildSourceBuffers();
    RefreshFullFrame();
}
void RegionSelectionOverlay::LaunchOcr() {
    ocr_launch_pending_ = false;
    auto image = working_image_.Crop(session_.selection);
    if (!image) {
        ocr_busy_ = false;
        ocr_status_ = image.Error();
        RefreshFullFrame();
        return;
    }
    ocr_job_generation_ = ocr_generation_;
    try {
        ocr_future_ = std::async(std::launch::async, [this, image = std::move(image.Value())]() {
            return ocr_service_.RecognizeDocument(image);
        });
    } catch (const std::exception&) {
        ocr_busy_ = false;
        ocr_status_ = L"无法启动识别，请返回编辑后重试。";
        RefreshFullFrame();
    }
}
void RegionSelectionOverlay::PollOcr() {
    if (ocr_future_.valid()) {
        if (ocr_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        auto result = common::Result<ocr::Document>::Failure(L"文字识别失败，请返回编辑后重试。");
        try { result = ocr_future_.get(); }
        catch (const std::exception&) { /* Report worker failures in the existing overlay. */ }
        if (ocr_mode_ && ocr_job_generation_ == ocr_generation_) {
            ocr_busy_ = false;
            if (result) {
                ocr_selection_.SetDocument(std::move(result.Value()));
                ocr_status_ = ocr_selection_.Empty() ? L"未识别到文字，点击“返回”继续编辑。" :
                    L"拖动选择文字 · Ctrl+C 复制 · Ctrl+A 全选";
            } else {
                ocr_status_ = result.Error();
            }
            SetWindowTextW(window_, ocr_status_.c_str());
            UpdateToolbarLayout();
            RefreshFullFrame();
        }
    }
    if (ocr_mode_ && ocr_launch_pending_) LaunchOcr();
    if (!ocr_future_.valid() && !ocr_launch_pending_) KillTimer(window_, kOcrPollTimer);
}
void RegionSelectionOverlay::RefreshOcrSelection() {
    if (!ocr_selection_.Empty()) ocr_status_ = L"拖动选择文字 · Ctrl+C 复制 · Ctrl+A 全选";
    RECT client{};
    GetClientRect(window_, &client);
    RefreshDirtyRect(SelectionVisualRect(session_.selection, client, toolbar_.Bounds(), true));
}
void RegionSelectionOverlay::CopyOcrText() {
    const auto text = ocr_selection_.SelectedText();
    if (text.empty()) return;
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* data = allocation ? GlobalLock(allocation) : nullptr;
    if (!data) {
        if (allocation) GlobalFree(allocation);
        ocr_status_ = L"复制失败，请重试。";
    } else {
        std::memcpy(data, text.c_str(), bytes);
        GlobalUnlock(allocation);
        bool copied = false;
        if (OpenClipboard(window_)) {
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, allocation)) copied = true;
            CloseClipboard();
        }
        if (!copied) GlobalFree(allocation);
        ocr_status_ = copied ? L"已复制选中文字 · 可以继续拖动选择" : L"剪贴板暂时不可用，请重试。";
    }
    RECT client{};
    GetClientRect(window_, &client);
    RefreshDirtyRect(SelectionLabelRect(session_.selection, client));
}
void RegionSelectionOverlay::ShowOcrContextMenu(POINT point) {
    const UINT dpi = UiDpi(window_);
    if (!ocr_menu_font_ || ocr_menu_dpi_ != dpi) {
        common::UniqueGdiObject<HFONT> font(CreateUiFont(dpi));
        if (!font) return;
        ocr_menu_font_ = std::move(font);
        ocr_menu_dpi_ = dpi;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (ocr_selection_.HasSelection() ? MF_ENABLED : MF_GRAYED), 1, L"复制选中文字\tCtrl+C");
    AppendMenuW(menu, MF_STRING | (ocr_selection_.Empty() ? MF_GRAYED : MF_ENABLED), 2, L"全选\tCtrl+A");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"返回编辑");
    static constexpr const wchar_t* labels[] = {
        L"复制选中文字\tCtrl+C", L"全选\tCtrl+A", nullptr, L"返回编辑"};
    for (UINT index = 0; index < std::size(labels); ++index) {
        if (!labels[index]) continue;
        // Keep the native text for accessibility while using the same font for
        // measuring and drawing both the label and its shortcut.
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.dwItemData = reinterpret_cast<ULONG_PTR>(labels[index]);
        item.dwTypeData = const_cast<wchar_t*>(labels[index]);
        SetMenuItemInfoW(menu, index, TRUE, &item);
    }
    // A popup's cursor messages can reach its owner. Do not hit-test OCR text
    // underneath the menu, and replace any I-beam before its first mouse move.
    ocr_menu_open_ = true;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    ocr_menu_open_ = false;
    DestroyMenu(menu);
    if (command == 1) CopyOcrText();
    if (command == 2) { ocr_selection_.SelectAll(); RefreshOcrSelection(); }
    if (command == 3) LeaveOcr();
    if (window_) HandleSetCursor();
}
bool RegionSelectionOverlay::MeasureOcrMenuItem(MEASUREITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || !item.itemData || !ocr_menu_font_) return false;
    const auto [label, shortcut] = MenuText(reinterpret_cast<const wchar_t*>(item.itemData));
    common::WindowDcHandle dc(window_, GetDC(window_));
    if (!dc.Get()) return false;
    common::ScopedSelectObject font(dc.Get(), ocr_menu_font_.Get());
    SIZE label_size{}, shortcut_size{};
    GetTextExtentPoint32W(dc.Get(), label.data(), static_cast<int>(label.size()), &label_size);
    if (!shortcut.empty()) GetTextExtentPoint32W(dc.Get(), shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size);
    const auto scale = [this](int value) { return MulDiv(value, ocr_menu_dpi_, 96); };
    item.itemWidth = label_size.cx + shortcut_size.cx + scale(shortcut.empty() ? 40 : 72);
    item.itemHeight = std::max(label_size.cy + scale(10), static_cast<LONG>(scale(32)));
    return true;
}
bool RegionSelectionOverlay::DrawOcrMenuItem(const DRAWITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || !item.itemData || !ocr_menu_font_) return false;
    const int saved = SaveDC(item.hDC);
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
    {
        common::ScopedSelectObject font(item.hDC, ocr_menu_font_.Get());
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        RECT bounds = item.rcItem;
        bounds.left += MulDiv(18, ocr_menu_dpi_, 96);
        bounds.right -= MulDiv(12, ocr_menu_dpi_, 96);
        const auto [label, shortcut] = MenuText(reinterpret_cast<const wchar_t*>(item.itemData));
        DrawTextW(item.hDC, label.data(), static_cast<int>(label.size()), &bounds,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (!shortcut.empty()) DrawTextW(item.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &bounds,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (saved) RestoreDC(item.hDC, saved);
    return true;
}
void RegionSelectionOverlay::PaintOcrSelection(HDC hdc, const RECT& client_rect) const {
    const auto quads = ocr_selection_.SelectedQuads();
    if (quads.empty()) return;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1DCRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)))) return;
    auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(factory->CreateDCRenderTarget(&properties, &target)) ||
        FAILED(target->BindDC(hdc, &client_rect)) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.45f, 0.94f, 0.38f), &brush)) ||
        FAILED(factory->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) return;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    for (const auto& quad : quads) {
        auto point = [this](ocr::Point value) {
            return D2D1::Point2F(value.x + session_.selection.left, value.y + session_.selection.top);
        };
        sink->BeginFigure(point(quad[0]), D2D1_FIGURE_BEGIN_FILLED);
        for (std::size_t i = 1; i < 4; ++i) sink->AddLine(point(quad[i]));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    if (FAILED(sink->Close())) return;
    target->BeginDraw();
    const auto& selection = session_.selection;
    target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(selection.left), static_cast<float>(selection.top),
        static_cast<float>(selection.right), static_cast<float>(selection.bottom)), D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillGeometry(geometry.Get(), brush.Get());
    target->PopAxisAlignedClip();
    target->EndDraw();
}
} // namespace ui
