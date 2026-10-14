#include "ui/TranslationResultWindow.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include "ui/UiTypography.h"

namespace ui {
namespace {
constexpr wchar_t kClassName[] = L"NativeScreenshot.TranslationResultWindow";
constexpr UINT_PTR kPollTimer = 1;
constexpr int kCopyId = 1, kCloseId = 2;
}
struct TranslationResultWindow::Job {
    std::stop_source stop;
    std::mutex mutex;
    std::optional<common::Result<std::wstring>> result;
};

bool TranslationResultWindow::Show(HWND owner, std::wstring text, TranslateFunction translate) {
    Close();
    translated_.clear();
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window_class.lpszClassName = kClassName;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    const auto dpi = UiDpi(owner);
    const int width = std::min(MulDiv(560, dpi, 96), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = std::min(MulDiv(450, dpi, 96), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"OCR 翻译 · 英文 → 简体中文",
        WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
        width, height, owner, nullptr, instance, this);
    if (!window_) return false;
    auto control = [&](DWORD ex, const wchar_t* type, const wchar_t* label, DWORD style, int id = 0) {
        return CreateWindowExW(ex, type, label, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    source_label_ = control(0, L"STATIC", L"原文（英文）", 0);
    source_edit_ = control(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 100);
    target_label_ = control(0, L"STATIC", L"译文（简体中文）", 0);
    target_edit_ = control(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 101);
    status_ = control(0, L"STATIC", L"正在翻译…", SS_LEFT, 102);
    copy_button_ = control(0, L"BUTTON", L"复制译文", WS_TABSTOP | BS_PUSHBUTTON, kCopyId);
    close_button_ = control(0, L"BUTTON", L"关闭", WS_TABSTOP | BS_PUSHBUTTON, kCloseId);
    if (!source_label_ || !source_edit_ || !target_label_ || !target_edit_ || !status_ || !copy_button_ || !close_button_) {
        Close(); return false;
    }
    SendMessageW(source_edit_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    SendMessageW(target_edit_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    SetWindowTextW(source_edit_, text.c_str());
    EnableWindow(copy_button_, FALSE);
    UpdateFont();
    Layout();
    job_ = std::make_shared<Job>();
    if (!SetTimer(window_, kPollTimer, 40, nullptr)) { Close(); return false; }
    try {
        // Only shared job state crosses threads. Closing the window cancels the
        // request; completion cannot access a destroyed/reused HWND or UI object.
        std::thread([job = job_, text = std::move(text), translate = std::move(translate)] {
            auto result = common::Result<std::wstring>::Failure(L"翻译失败，请稍后重试。");
            try { result = translate(text, job->stop.get_token()); }
            catch (...) { /* Surface errors in the result window. */ }
            std::lock_guard lock(job->mutex);
            job->result = std::move(result);
        }).detach();
    } catch (const std::exception&) {
        KillTimer(window_, kPollTimer);
        SetWindowTextW(status_, L"无法启动翻译，请关闭后重试。");
    }
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(source_edit_);
    return true;
}
void TranslationResultWindow::Close() {
    if (job_) job_->stop.request_stop();
    if (window_) DestroyWindow(window_);
    job_.reset();
}
bool TranslationResultWindow::ProcessMessage(MSG& message) {
    if (!window_) return false;
    if (message.message == WM_KEYDOWN && message.wParam == 'A' && GetKeyState(VK_CONTROL) < 0 &&
        (message.hwnd == source_edit_ || message.hwnd == target_edit_)) {
        SendMessageW(message.hwnd, EM_SETSEL, 0, -1);
        return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
void TranslationResultWindow::UpdateFont() {
    auto new_font = common::UniqueGdiObject<HFONT>(CreateUiFont(UiDpi(window_)));
    if (!new_font) return;
    for (const auto control : {source_label_, source_edit_, target_label_, target_edit_, status_, copy_button_, close_button_})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(new_font.Get()), TRUE);
    font_ = std::move(new_font);
}
void TranslationResultWindow::Layout() {
    if (!source_edit_) return;
    RECT client{}; GetClientRect(window_, &client);
    const auto scale = [this](int value) { return MulDiv(value, UiDpi(window_), 96); };
    const int margin = scale(12), label = scale(22), gap = scale(10), button_height = scale(30);
    const int width = std::max(1, static_cast<int>(client.right) - 2 * margin);
    const int edit_height = std::max(1, (static_cast<int>(client.bottom) - 2 * margin - 2 * label - 2 * gap - button_height) / 2);
    int y = margin;
    MoveWindow(source_label_, margin, y, width, label, TRUE); y += label;
    MoveWindow(source_edit_, margin, y, width, edit_height, TRUE); y += edit_height + gap;
    MoveWindow(target_label_, margin, y, width, label, TRUE); y += label;
    MoveWindow(target_edit_, margin, y, width, edit_height, TRUE); y += edit_height + gap;
    const int button_width = scale(84);
    MoveWindow(status_, margin, y + scale(5), std::max(1, width - 2 * button_width - 2 * gap), button_height, TRUE);
    MoveWindow(copy_button_, client.right - margin - 2 * button_width - gap, y, button_width, button_height, TRUE);
    MoveWindow(close_button_, client.right - margin - button_width, y, button_width, button_height, TRUE);
}
void TranslationResultWindow::Poll() {
    if (!job_) return;
    std::optional<common::Result<std::wstring>> result;
    {
        std::lock_guard lock(job_->mutex);
        if (!job_->result) return;
        result = std::move(job_->result);
    }
    KillTimer(window_, kPollTimer);
    if (*result) {
        translated_ = result->Value();
        SetWindowTextW(target_edit_, translated_.c_str());
        EnableWindow(copy_button_, !translated_.empty());
        SetWindowTextW(status_, L"翻译完成");
    } else {
        SetWindowTextW(target_edit_, result->Error().c_str());
        SetWindowTextW(status_, L"翻译失败，可关闭后重试");
    }
}
void TranslationResultWindow::CopyTranslation() {
    if (translated_.empty()) return;
    const auto bytes = (translated_.size() + 1) * sizeof(wchar_t);
    HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* data = allocation ? GlobalLock(allocation) : nullptr;
    if (!data) { if (allocation) GlobalFree(allocation); return; }
    std::memcpy(data, translated_.c_str(), bytes);
    GlobalUnlock(allocation);
    bool copied = false;
    if (OpenClipboard(window_)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, allocation)) copied = true;
        CloseClipboard();
    }
    if (!copied) GlobalFree(allocation);
    SetWindowTextW(status_, copied ? L"已复制译文" : L"剪贴板暂时不可用，请重试");
}
LRESULT CALLBACK TranslationResultWindow::WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<TranslationResultWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<TranslationResultWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_TIMER: self->Poll(); return 0;
    case WM_SIZE: self->Layout(); return 0;
    case WM_GETMINMAXINFO: {
        const auto dpi = UiDpi(hwnd);
        const auto info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = {MulDiv(360, dpi, 96), MulDiv(300, dpi, 96)};
        return 0;
    }
    case WM_DPICHANGED: {
        self->UpdateFont();
        const auto rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == kCopyId) { self->CopyTranslation(); return 0; }
        if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == kCloseId) { self->Close(); return 0; }
        break;
    case WM_CLOSE: self->Close(); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kPollTimer);
        if (self->job_) self->job_->stop.request_stop();
        return 0;
    case WM_NCDESTROY:
        self->window_ = nullptr;
        self->source_label_ = self->source_edit_ = self->target_label_ = self->target_edit_ = nullptr;
        self->status_ = self->copy_button_ = self->close_button_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace ui
