#include <Windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "common/DpiAwareness.h"
#include "common/GdiResources.h"
#include "ui/OverlayPrimitives.h"
#include "ui/RegionSelectionBuffers.h"
#include "ui/RegionSelectionOverlay.h"
#include "ui/WindowEditingCanvas.h"
#include "ui/TitleBar.h"
#include "ui/TrayIconController.h"
#include "ui/UiTypography.h"
#include "ui/RecordingOverlay.h"

#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckFont(HFONT font, UINT dpi) {
    LOGFONTW description{};
    Require(font && GetObjectW(font, sizeof(description), &description), "Missing font");
    Require(description.lfWeight == FW_NORMAL, "Unexpected font weight");
    Require(wcscmp(description.lfFaceName, L"Microsoft YaHei UI") == 0, "Unexpected font family");
    Require(description.lfHeight == -MulDiv(9, dpi, 72), "Incorrect DPI font size");
}

std::vector<DWORD> RenderOverlayHint(UINT dpi, bool shared_renderer) {
    constexpr int width = 1024;
    constexpr int height = 240;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    common::UniqueBitmap bitmap(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    common::UniqueDc dc(CreateCompatibleDC(nullptr));
    Require(bitmap && dc && bits, "Cannot create typography bitmap");
    common::ScopedSelectObject selected_bitmap(dc.Get(), bitmap.Get());
    RECT bounds{0, 0, width, height};
    FillRect(dc.Get(), &bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    constexpr wchar_t text[] = L"窗口截图 · 1234 x 567 · Esc & A";
    constexpr UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    common::UniqueGdiObject<HFONT> expected_font(ui::CreateUiFont(dpi));
    CheckFont(expected_font.Get(), dpi);
    // Reproduce the original bug: an overlay memory DC inherits the stock bold font.
    const HGDIOBJ incoming_font = shared_renderer ? GetStockObject(SYSTEM_FONT) : expected_font.Get();
    common::ScopedSelectObject selected_font(dc.Get(), incoming_font);
    if (shared_renderer) {
        ui::DrawTextBlock(dc.Get(), bounds, text, flags, RGB(0, 0, 0), dpi);
        Require(GetCurrentObject(dc.Get(), OBJ_FONT) == incoming_font, "Overlay renderer leaks its font selection");
    } else {
        SetBkMode(dc.Get(), TRANSPARENT);
        SetTextColor(dc.Get(), RGB(0, 0, 0));
        DrawTextW(dc.Get(), text, -1, &bounds, flags);
    }
    GdiFlush();
    const auto* pixels = static_cast<const DWORD*>(bits);
    return {pixels, pixels + width * height};
}

capture::CapturedImage SolidImage(int width, int height, BYTE blue, BYTE green, BYTE red) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = blue;
        pixels[i + 1] = green;
        pixels[i + 2] = red;
        pixels[i + 3] = 255;
    }
    return capture::CapturedImage(width, height, std::move(pixels));
}

void CheckWindowEditing(HWND window) {
    capture::DesktopSnapshot desktop;
    desktop.origin_x = -160;
    desktop.origin_y = -90;
    desktop.image = SolidImage(800, 600, 200, 200, 200);
    const auto captured = SolidImage(150, 100, 120, 80, 40);
    for (const POINT origin : {POINT{60, 80}, POINT{-230, -130}, POINT{610, 450}}) {
        const auto composed = ui::CreateWindowEditingCanvas(desktop, captured, origin);
        Require(static_cast<bool>(composed), "Cannot compose window editing canvas");
        const auto& canvas = composed.Value();
        Require(canvas.snapshot.image.Width() >= desktop.image.Width() &&
                canvas.snapshot.image.Height() >= desktop.image.Height(), "Editing canvas omits desktop");
        Require(canvas.selection.left + canvas.snapshot.origin_x == origin.x &&
                canvas.selection.top + canvas.snapshot.origin_y == origin.y, "Window selection offset changed");
        const auto exported = canvas.snapshot.image.Crop(canvas.selection);
        Require(exported && exported.Value().Width() == captured.Width() &&
                exported.Value().Height() == captured.Height() &&
                exported.Value().Pixels() == captured.Pixels(), "Window export includes desktop or loses off-screen pixels");
        Require(desktop.image.Pixels()[0] == 200, "Canvas changed frozen desktop pixels");
        for (const UINT dpi : {96U, 144U, 192U, 288U}) {
            const RECT area = ui::WindowEditingInstructionArea(canvas.desktop_bounds, canvas.selection, dpi);
            Require(area.left >= canvas.desktop_bounds.left && area.top >= canvas.desktop_bounds.top &&
                    area.right <= canvas.desktop_bounds.right && area.bottom <= canvas.desktop_bounds.bottom,
                    "Window instructions placed outside desktop");
        }
    }
    const auto composed = ui::CreateWindowEditingCanvas(desktop, captured, POINT{60, 80});
    const auto& canvas = composed.Value();
    ui::RegionSelectionBuffers buffers;
    const SIZE size{canvas.snapshot.image.Width(), canvas.snapshot.image.Height()};
    Require(static_cast<bool>(buffers.Initialize(window, size, 120,
        [&](HDC dc) {
            const BITMAPINFO info = canvas.snapshot.image.CreateBitmapInfo();
            SetDIBitsToDevice(dc, 0, 0, size.cx, size.cy, 0, 0, 0, size.cy,
                canvas.snapshot.image.Pixels().data(), &info, DIB_RGB_COLORS);
        }, [](HDC, const RECT&) {})), "Cannot create window editing buffers");
    const RECT full{0, 0, size.cx, size.cy};
    const RECT hint{16, 16, 160, 70};
    const auto paint_hint = [&](HDC dc, const RECT&) {
        ui::FillSolidRect(dc, hint, RGB(5, 7, 9));
    };
    buffers.UpdateBackBuffer(full, &canvas.selection, paint_hint);
    Require(GetPixel(buffers.BackBufferDc(), 400, 400) != RGB(200, 200, 200),
            "Desktop outside selected window has no dimming");
    Require(GetPixel(buffers.BackBufferDc(), 240, 200) == RGB(40, 80, 120),
            "Selected window is dimmed or rescaled");
    Require(GetPixel(buffers.BackBufferDc(), 30, 30) == RGB(5, 7, 9), "Window instructions disappeared");
    buffers.UpdateBackBuffer(full, &full, paint_hint);
    Require(GetPixel(buffers.BackBufferDc(), 30, 30) == RGB(5, 7, 9),
            "Full-window selection erased instructions");
    const auto exported = canvas.snapshot.image.Crop(canvas.selection);
    Require(exported && exported.Value().Pixels() == captured.Pixels(), "Mask or hints entered exported image");
}

struct EditingProbe {
    bool found = false;
    bool desktop_covered = false;
    bool outside_dimmed = false;
    bool target_clear = false;
    bool hint_visible = false;
    bool full_selection = false;
    ULONGLONG started = GetTickCount64();
};
EditingProbe* editing_probe = nullptr;

BOOL CALLBACK InspectEditor(HWND window, LPARAM parameter) {
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (wcscmp(class_name, L"NativeScreenshot.RegionSelectionOverlay") != 0) return TRUE;
    auto& probe = *reinterpret_cast<EditingProbe*>(parameter);
    probe.found = true;
    RECT rect{};
    GetWindowRect(window, &rect);
    probe.desktop_covered = rect.left == 40 && rect.top == 40 &&
                             rect.right == 840 && rect.bottom == 640;
    common::WindowDcHandle dc(window, GetDC(window));
    probe.outside_dimmed = GetPixel(dc.Get(), 600, 400) != RGB(200, 200, 200);
    probe.target_clear = GetPixel(dc.Get(), 240, 200) == RGB(40, 80, 120);
    const int margin = MulDiv(16, ui::UiDpi(window), 96);
    const RECT hint_area = ui::WindowEditingInstructionArea(RECT{0, 0, 800, 600},
        probe.full_selection ? RECT{0, 0, 800, 600} : RECT{220, 170, 370, 270}, ui::UiDpi(window));
    probe.hint_visible = GetPixel(dc.Get(), hint_area.left + margin + 2,
                                hint_area.top + margin + 2) == RGB(24, 24, 24);
    PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
    return FALSE;
}

void CALLBACK InspectEditingTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!editing_probe) return;
    EnumThreadWindows(GetCurrentThreadId(), InspectEditor, reinterpret_cast<LPARAM>(editing_probe));
    if (editing_probe->found) KillTimer(nullptr, timer);
    else if (GetTickCount64() - editing_probe->started > 2000) PostQuitMessage(1);
}

void CheckWindowEditingSession() {
    capture::DesktopSnapshot desktop;
    desktop.origin_x = 40;
    desktop.origin_y = 40;
    desktop.image = SolidImage(800, 600, 200, 200, 200);
    ui::RegionSelectionOverlay editor(GetModuleHandleW(nullptr));
    for (const bool full : {false, true}) {
        const auto captured = SolidImage(full ? 800 : 150, full ? 600 : 100, 120, 80, 40);
        EditingProbe probe;
        probe.full_selection = full;
        editing_probe = &probe;
        const UINT_PTR timer = SetTimer(nullptr, 0, 150, InspectEditingTimer);
        Require(timer != 0, "Cannot create editor inspection timer");
        const auto edited = editor.EditWindow(desktop, captured, full ? POINT{40, 40} : POINT{260, 210});
        KillTimer(nullptr, timer);
        editing_probe = nullptr;
        Require(probe.found && probe.desktop_covered, "Actual window editor does not cover frozen desktop");
        Require(full || probe.outside_dimmed, "Actual editor has no outside-window mask");
        Require(probe.target_clear, "Actual editor dims the selected window");
        Require(probe.hint_visible, "Actual editor hides operation instructions");
        Require(edited && edited.Value() && edited.Value()->image.Width() == captured.Width() &&
                edited.Value()->image.Height() == captured.Height() &&
                edited.Value()->image.Pixels() == captured.Pixels(),
                "Actual window editor exports mask, hints or desktop pixels");
    }
}

struct Fixture {
    ui::TitleBar caption;
    ui::TrayIconController tray{GetModuleHandleW(nullptr)};
    unsigned measured = 0;
    unsigned drawn = 0;
    bool stop_requested = false;
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(l_param)->lpCreateParams));
    }
    auto* fixture = reinterpret_cast<Fixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (fixture) {
        if (message == WM_APP + 5) { fixture->stop_requested = true; return 0; }
        if (message == WM_NCCALCSIZE) return ui::TitleBar::CalculateClient(window, w_param, l_param);
        if (message == WM_MEASUREITEM && fixture->tray.MeasureMenuItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(l_param))) {
            ++fixture->measured;
            return TRUE;
        }
        if (message == WM_DRAWITEM) {
            const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
            if (fixture->caption.DrawButton(item)) return TRUE;
            if (fixture->tray.DrawMenuItem(item)) {
                ++fixture->drawn;
                // Owner drawing must keep native menu text available to assistive tools.
                wchar_t label[128]{};
                GetMenuStringW(reinterpret_cast<HMENU>(item.hwndItem), item.itemID,
                    label, 128, MF_BYCOMMAND);
                if (!label[0]) fixture->drawn = 0;
                return TRUE;
            }
        }
        if (message == WM_TIMER) { EndMenu(); return 0; }
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

BOOL CALLBACK FindTooltip(HWND window, LPARAM parameter) {
    auto* result = reinterpret_cast<HWND*>(parameter);
    wchar_t class_name[64]{};
    GetClassNameW(window, class_name, 64);
    if (wcscmp(class_name, TOOLTIPS_CLASSW) == 0) *result = window;
    return TRUE;
}

struct OverlayWindows { HWND mask = nullptr; HWND border = nullptr; HWND bar = nullptr; };
BOOL CALLBACK FindOverlay(HWND window, LPARAM parameter) {
    auto& result = *reinterpret_cast<OverlayWindows*>(parameter);
    wchar_t title[128]{};
    GetWindowTextW(window, title, 128);
    if (wcscmp(title, L"录制范围遮罩") == 0) result.mask = window;
    if (wcscmp(title, L"录制范围边框") == 0) result.border = window;
    if (wcscmp(title, L"录制状态 - 原生 Win32 截图工具") == 0) result.bar = window;
    return TRUE;
}
}  // namespace

int main() {
    common::EnableDpiAwareness();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    Fixture fixture;
    HFONT font = nullptr;
    HWND window = nullptr;
    int result = 0;
    try {
        for (const UINT dpi : {96U, 120U, 144U, 192U, 288U}) {
            font = ui::CreateUiFont(dpi);
            CheckFont(font, dpi);
            DeleteObject(font);
            font = nullptr;
            const auto expected = RenderOverlayHint(dpi, false);
            Require(std::any_of(expected.begin(), expected.end(), [](DWORD pixel) {
                return (pixel & 0x00FFFFFF) != 0x00FFFFFF;
            }), "Font regression check rendered no text");
            Require(RenderOverlayHint(dpi, true) == expected,
                "Overlay text differs from the shared UI font at this DPI");
        }
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = WindowProc;
        window_class.lpszClassName = L"NativeScreenshot.UiTypographyTest";
        RegisterClassW(&window_class);
        window = CreateWindowExW(0, window_class.lpszClassName, L"字体与窗口交互测试",
            WS_OVERLAPPEDWINDOW, 100, 100, 700, 300, nullptr, nullptr,
            window_class.hInstance, &fixture);
        Require(window && fixture.caption.Create(window, window_class.hInstance), "Cannot create caption fixture");
        CheckWindowEditing(window);
        CheckWindowEditingSession();
        font = ui::CreateUiFont(ui::UiDpi(window));
        fixture.caption.SetFont(font);
        RECT client{};
        GetClientRect(window, &client);
        fixture.caption.Layout(client.right);
        for (const int id : {0x7001, 0x7002, 0x7003}) {
            CheckFont(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(window, id), WM_GETFONT, 0, 0)), ui::UiDpi(window));
        }
        const auto hit = [window](int x, int y) {
            POINT point{x, y};
            ClientToScreen(window, &point);
            return ui::TitleBar::HitTest(window, MAKELPARAM(point.x, point.y));
        };
        Require(hit(100, 10) == HTCAPTION, "Caption no longer supports dragging");
        Require(hit(client.right - 20, 10) == HTCLIENT, "Caption buttons cannot receive clicks");
        Require(hit(100, MulDiv(ui::TitleBar::kHeight + 10, ui::UiDpi(window), 96)) == HTCLIENT,
            "Client controls intercepted by caption");
        RECT bounds{};
        GetWindowRect(window, &bounds);
        POINT client_origin{};
        ClientToScreen(window, &client_origin);
        Require(client_origin.y - bounds.top == 1, "Visible resize strip left above caption");
        Require(ui::TitleBar::HitTest(window, MAKELPARAM(bounds.left + 1, bounds.top + 1)) == HTTOPLEFT,
            "Top-left resize border broken");
        Require(static_cast<bool>(fixture.tray.AddIcon(window, WM_APP + 1)), "Cannot install tray icon");
        Require(fixture.tray.UsesVersion4(), "Shell custom-popup callbacks not enabled");
        POINT anchor{200, 200};
        fixture.tray.ShowTooltip(anchor);
        HWND tooltip = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), FindTooltip, reinterpret_cast<LPARAM>(&tooltip));
        Require(tooltip && IsWindowVisible(tooltip), "Tracking tooltip not shown");
        CheckFont(reinterpret_cast<HFONT>(SendMessageW(tooltip, WM_GETFONT, 0, 0)), ui::UiDpi(tooltip));
        fixture.tray.HideTooltip();
        Require(!IsWindowVisible(tooltip), "Tracking tooltip not hidden");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        SetTimer(window, 1, 200, nullptr);
        fixture.tray.ShowContextMenu(window, anchor, false, false);
        KillTimer(window, 1);
        Require(fixture.measured >= 8 && fixture.drawn >= 8, "Menu drawing or accessible labels missing");
        ui::RecordingOverlay overlay(window_class.hInstance);
        Require(static_cast<bool>(overlay.Show(window, WM_APP + 5, bounds, L"窗口录制")), "Cannot show recording overlay");
        OverlayWindows overlay_windows;
        EnumThreadWindows(GetCurrentThreadId(), FindOverlay, reinterpret_cast<LPARAM>(&overlay_windows));
        Require(overlay_windows.mask && overlay_windows.border && overlay_windows.bar, "Missing live recording prompts");
        for (HWND popup : {overlay_windows.mask, overlay_windows.border, overlay_windows.bar}) {
            DWORD affinity = 0;
            Require(GetWindowDisplayAffinity(popup, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE,
                "Recording prompts must be excluded from video");
        }
        Require(GetWindowLongPtrW(overlay_windows.mask, GWL_EXSTYLE) & WS_EX_TRANSPARENT,
            "Recording mask blocks pointer input");
        CheckFont(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(overlay_windows.bar, 2), WM_GETFONT, 0, 0)),
            ui::UiDpi(overlay_windows.bar));
        RECT moved = bounds;
        OffsetRect(&moved, 20, 20);
        overlay.Update(moved, 65, false);
        wchar_t counter[128]{};
        GetWindowTextW(GetDlgItem(overlay_windows.bar, 2), counter, 128);
        Require(wcsstr(counter, L"00:01:05") != nullptr, "Elapsed time not updated");
        SendMessageW(GetDlgItem(overlay_windows.bar, 1), BM_CLICK, 0, 0);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        Require(fixture.stop_requested, "Overlay stop button failed to notify owner");
        overlay.Update(moved, 65, true);
        Require(!IsWindowEnabled(GetDlgItem(overlay_windows.bar, 1)), "Stop button still enabled while saving");
        overlay.Close();
        Require(!IsWindow(overlay_windows.bar) && !IsWindow(overlay_windows.mask), "Overlay survives recording end");
        std::cout << "Typography DPI, caption hit tests, tray tooltip and owner-drawn menu passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fixture.tray.RemoveIcon();
    if (window) DestroyWindow(window);
    if (font) DeleteObject(font);
    return result;
}
