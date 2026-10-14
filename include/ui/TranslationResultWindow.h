#pragma once

#include <Windows.h>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include "common/GdiResources.h"
#include "translation/GoogleTranslationService.h"

namespace ui {
class TranslationResultWindow {
public:
    using TranslateFunction = std::function<common::Result<std::wstring>(std::wstring_view, std::stop_token)>;
    ~TranslationResultWindow() { Close(); }
    bool Show(HWND owner, std::wstring text,
              TranslateFunction translate = translation::TranslateEnglishToChinese);
    void Close();
    HWND Window() const { return window_; }
    bool ProcessMessage(MSG& message);
private:
    struct Job;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    void Layout();
    void Poll();
    void CopyTranslation();
    void UpdateFont();
    HWND window_ = nullptr;
    HWND source_label_ = nullptr, source_edit_ = nullptr, target_label_ = nullptr;
    HWND target_edit_ = nullptr, status_ = nullptr, copy_button_ = nullptr, close_button_ = nullptr;
    common::UniqueGdiObject<HFONT> font_;
    std::wstring translated_;
    std::shared_ptr<Job> job_;
};
} // namespace ui
