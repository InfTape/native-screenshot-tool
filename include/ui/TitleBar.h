#pragma once

#include <Windows.h>
#include <array>

namespace ui {

class TitleBar {
public:
    static constexpr int kHeight = 36;
    bool Create(HWND parent, HINSTANCE instance);
    void SetFont(HFONT font);
    void Layout(int client_width);
    void Paint(HDC dc) const;
    bool DrawButton(const DRAWITEMSTRUCT& item) const;
    bool HandleCommand(UINT identifier) const;
    static LRESULT CalculateClient(HWND window, WPARAM w_param, LPARAM l_param);
    static LRESULT HitTest(HWND window, LPARAM l_param);

private:
    static LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM w_param,
        LPARAM l_param, UINT_PTR subclass_id, DWORD_PTR reference_data);
    HWND parent_ = nullptr;
    HFONT font_ = nullptr; // Owned by MainWindow.
    std::array<HWND, 3> buttons_{};
};

}  // namespace ui
