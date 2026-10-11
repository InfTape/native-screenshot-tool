#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "common/DpiAwareness.h"
#include "ocr/OneOcrService.h"
#include "ui/RegionSelectionOverlay.h"
#include "ui/UiTypography.h"

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A separate window station gives the UI test its own clipboard and keeps test
// dialogs off the user's desktop. Restore the original desktop on every exit.
struct IsolatedDesktop {
    HWINSTA original_station = GetProcessWindowStation(), station = nullptr;
    HDESK original_desktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
    IsolatedDesktop() {
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station || !SetProcessWindowStation(station)) throw std::runtime_error("Cannot isolate OCR clipboard");
        desktop = CreateDesktopW(L"OcrTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop || !SetThreadDesktop(desktop)) throw std::runtime_error("Cannot isolate OCR desktop");
    }
    ~IsolatedDesktop() {
        SetThreadDesktop(original_desktop);
        SetProcessWindowStation(original_station);
        if (desktop) CloseDesktop(desktop);
        if (station) CloseWindowStation(station);
    }
};

capture::CapturedImage TextImage() {
    constexpr int width = 720, height = 180;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    HDC dc = CreateCompatibleDC(nullptr);
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Require(dc && bitmap && pixels, "Cannot create OCR image");
    const auto old_bitmap = SelectObject(dc, bitmap);
    std::memset(pixels, 255, width * height * 4);
    HFONT font = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    const wchar_t first[] = L"OCR VERIFY 12345";
    const wchar_t second[] = L"中文文字识别测试";
    TextOutW(dc, 24, 24, first, static_cast<int>(std::size(first) - 1));
    TextOutW(dc, 24, 82, second, static_cast<int>(std::size(second) - 1));
    GdiFlush();
    std::vector<std::uint8_t> copy(width * height * 4);
    std::memcpy(copy.data(), pixels, copy.size());
    for (std::size_t i = 3; i < copy.size(); i += 4) copy[i] = 255;
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return {width, height, std::move(copy)};
}


ocr::Quad Box(float x, float y, float width, float height) {
    return {ocr::Point{x,y}, {x+width,y}, {x+width,y+height}, {x,y+height}};
}
void CheckSelectionModel() {
    ocr::Document document;
    document.lines = {{L"AB CD", Box(10,20,100,20),
        {{L"AB",Box(10,20,40,20)}, {L"CD",Box(70,20,40,20)}}},
        {L"中文测试",Box(10,60,80,20),{{L"中文测试",Box(10,60,80,20)}}}};
    ui::OcrTextSelection model;
    model.SetDocument(document);
    Require(!model.HitText({300,200}), "Blank area is selectable text");
    model.BeginSelection({11,30}); model.ExtendSelection({109,30});
    Require(model.SelectedText() == L"AB CD", "OCR spaces were lost");
    model.BeginSelection({31,30}); model.ExtendSelection({69,70});
    Require(model.SelectedText() == L"B CD\r\n中文测", "Cross-line partial selection incorrect");
    model.BeginSelection({69,70}); model.ExtendSelection({31,30});
    Require(model.SelectedText() == L"B CD\r\n中文测", "Reverse selection incorrect");
    Require(model.SelectedQuads().size() == 7, "Partial highlight does not match copied characters");
    model.SelectWord({80,30});
    Require(model.SelectedText() == L"CD", "Double-click word selection incorrect");
    model.SelectAll();
    Require(model.SelectedText() == L"AB CD\r\n中文测试", "Select-all incorrect");
    model.BeginSelection({300,200});
    Require(!model.HasSelection(), "Clicking blank area did not clear selection");
    document.lines = {{L"A\U0001F600B", Box(10,20,60,20), {{L"A\U0001F600B",Box(10,20,60,20)}}}};
    model.SetDocument(document);
    model.BeginSelection({31,30}); model.ExtendSelection({49,30});
    Require(model.SelectedText() == L"\U0001F600", "Surrogate pair was split");
    document.lines = {{L"AB", {ocr::Point{10,20},{50,30},{45,50},{5,40}},
        {{L"AB",{ocr::Point{10,20},{50,30},{45,50},{5,40}}}}}};
    model.SetDocument(document);
    model.BeginSelection({8,30}); model.ExtendSelection({47,40});
    Require(model.SelectedText() == L"AB", "Rotated geometry hit test incorrect");
}

const RECT kRegion{80,200,800,380};
struct Probe {
    ocr::Document document;
    std::wstring expected, partial;
    bool blank = false, early_close = false, return_editing = false;
    bool passed = false, found = false, closed = false;
    bool highlighted = false;
    int phase = 0;
    ULONGLONG started = GetTickCount64();
    std::string error;
};
Probe* active_probe = nullptr;
void ClickCommand(HWND window, ui::SelectionToolbarCommand command, bool ocr_mode) {
    ui::SelectionEditToolbar toolbar;
    RECT client{};
    GetClientRect(window, &client);
    toolbar.UpdateLayout(kRegion, client, false, ui::UiDpi(window), ocr_mode);
    const auto bounds = toolbar.Bounds();
    for (LONG x = bounds.left; x < bounds.right; ++x) {
        POINT point{x, (bounds.top + bounds.bottom) / 2};
        if (toolbar.HitTest(point).IsCommand(command)) {
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x,point.y));
            SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(point.x,point.y));
            return;
        }
    }
    throw std::runtime_error("OCR toolbar command missing");
}
void CtrlKey(HWND window, WPARAM key) {
    BYTE original[256]{}, state[256]{};
    GetKeyboardState(original);
    std::memcpy(state,original,sizeof(state));
    state[VK_CONTROL] = 0x80;
    SetKeyboardState(state);
    SendMessageW(window,WM_KEYDOWN,key,0);
    SetKeyboardState(original);
}
std::wstring ClipboardText(HWND window) {
    if (!OpenClipboard(window)) return {};
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const auto* text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
    std::wstring copy = text ? text : L"";
    if (text) GlobalUnlock(data);
    CloseClipboard();
    return copy;
}
ocr::Point Middle(ocr::Point a,ocr::Point b) { return {(a.x+b.x)/2,(a.y+b.y)/2}; }
POINT ClientPoint(ocr::Point point) {
    return {static_cast<LONG>(point.x+0.5f)+kRegion.left,static_cast<LONG>(point.y+0.5f)+kRegion.top};
}
void Drag(HWND window, ocr::Point begin, ocr::Point end) {
    const auto first = ClientPoint(begin),last = ClientPoint(end);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.x,first.y));
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(last.x,last.y));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(last.x,last.y));
}
COLORREF SavePreview(HWND window, POINT sample) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER),900,-600,1,32,BI_RGB};
    void* pixels = nullptr;
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto old=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType=0x4D42; header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
    header.bfSize=header.bfOffBits+900*600*4;
    std::ofstream output("OcrInlinePreview.bmp",std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header),sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    output.write(static_cast<const char*>(pixels),900*600*4);
    const auto color=GetPixel(dc,sample.x,sample.y);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return color;
}
BOOL InspectEditorImpl(HWND window, LPARAM parameter) {
    wchar_t class_name[128]{};
    GetClassNameW(window,class_name,128);
    if (wcscmp(class_name,L"NativeScreenshot.OcrResultWindow") == 0)
        throw std::runtime_error("Unexpected separate OCR result window");
    if (wcscmp(class_name,L"NativeScreenshot.RegionSelectionOverlay") != 0) return TRUE;
    auto& probe=*reinterpret_cast<Probe*>(parameter);
    probe.found=true;
    if (probe.phase==0) {
        ClickCommand(window,ui::SelectionToolbarCommand::Ocr,false);
        probe.phase=1;
        if (probe.early_close) {
            probe.passed=true;probe.closed=true;
            SendMessageW(window,WM_CLOSE,0,0);
        }
        return FALSE;
    }
    wchar_t status[1024]{};
    GetWindowTextW(window,status,1024);
    if (wcsstr(status,L"正在识别") && GetTickCount64()-probe.started<15000) return FALSE;
    if (probe.blank) {
        Require(wcsstr(status,L"未识别到文字")!=nullptr,"Blank OCR did not finish with an empty result");
        CtrlKey(window,'A');ClickCommand(window,ui::SelectionToolbarCommand::CopyText,true);
        probe.passed=ClipboardText(window)==L"blank-test-sentinel";
        probe.closed=true;ClickCommand(window,ui::SelectionToolbarCommand::Confirm,true);
        return FALSE;
    }
    CtrlKey(window,'A');ClickCommand(window,ui::SelectionToolbarCommand::CopyText,true);
    if (ClipboardText(window)!=probe.expected && GetTickCount64()-probe.started<15000) return FALSE;
    if (ClipboardText(window)!=probe.expected) {
        std::wcerr << L"clipboard was: " << ClipboardText(window) << L"\n";
        throw std::runtime_error("Inline select-all/Unicode clipboard incorrect");
    }
    const auto first=probe.document.lines.front().bounds,last=probe.document.lines.back().bounds;
    auto begin=Middle(first[0],first[3]);begin.x+=2;
    auto end=Middle(last[1],last[2]);end.x-=2;
    Drag(window,begin,end);
    ClickCommand(window,ui::SelectionToolbarCommand::CopyText,true);
    Require(ClipboardText(window)==probe.expected,"Inline cross-line drag or region origin incorrect");
    UpdateWindow(window);
    const auto center=ClientPoint(Middle(first[0],first[2]));
    const auto color=SavePreview(window,center);
    probe.highlighted=color!=CLR_INVALID && GetBValue(color)>GetRValue(color)+20;
    Require(probe.highlighted,"Selection highlight missing or misaligned");
    // Drag only part of the first line, then reverse it; derive the expected text
    // with image-coordinate hit testing and compare the actual UI clipboard.
    const auto& word=probe.document.lines.front().words.front();
    begin=Middle(word.bounds[0],word.bounds[3]);begin.x+=2;
    end=Middle(word.bounds[1],word.bounds[2]);end.x-=2;
    ui::OcrTextSelection selection;
    selection.SetDocument(probe.document);
    selection.BeginSelection({static_cast<float>(ClientPoint(begin).x-kRegion.left),static_cast<float>(ClientPoint(begin).y-kRegion.top)});
    selection.ExtendSelection({static_cast<float>(ClientPoint(end).x-kRegion.left),static_cast<float>(ClientPoint(end).y-kRegion.top)});
    probe.partial=selection.SelectedText();
    Require(!probe.partial.empty() && probe.partial!=probe.expected,"Invalid partial selection fixture");
    Drag(window,end,begin);CtrlKey(window,'C');
    Require(ClipboardText(window)==probe.partial,"Reverse partial selection/Ctrl+C incorrect");
    const auto word_center=ClientPoint(Middle(word.bounds[0],word.bounds[2]));
    SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(word_center.x,word_center.y));
    CtrlKey(window,'C');
    Require(ClipboardText(window)==word.text,"Inline double-click word copy incorrect");
    if (!probe.return_editing) {
        probe.passed=true;probe.closed=true;
        ClickCommand(window,ui::SelectionToolbarCommand::Confirm,true);
        Require(ClipboardText(nullptr)==word.text,"Text-only exit changed the clipboard");
        return FALSE;
    }
    ClickCommand(window,ui::SelectionToolbarCommand::Ocr,true);
    // Copying and returning to editing must leave the screenshot intact.
    probe.passed=true;probe.closed=true;
    SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
    return FALSE;
}
BOOL CALLBACK InspectEditor(HWND window, LPARAM parameter) {
    try { return InspectEditorImpl(window,parameter); }
    catch (const std::exception& error) {
        auto& probe=*reinterpret_cast<Probe*>(parameter);
        probe.error=error.what();probe.closed=true;
        SendMessageW(window,WM_CLOSE,0,0);
        return FALSE;
    }
}
void CALLBACK InspectTimer(HWND,UINT,UINT_PTR timer,DWORD) {
    if (!active_probe) return;
    try {
        EnumThreadWindows(GetCurrentThreadId(),InspectEditor,reinterpret_cast<LPARAM>(active_probe));
        if (active_probe->closed) KillTimer(nullptr,timer);
        if (GetTickCount64()-active_probe->started>18000) throw std::runtime_error("Inline OCR test timed out");
    } catch (const std::exception& error) {
        active_probe->error=error.what();
        HWND window=FindWindowW(L"NativeScreenshot.RegionSelectionOverlay",nullptr);
        if (window) SendMessageW(window,WM_CLOSE,0,0);
        KillTimer(nullptr,timer);
    }
}
void CheckOverlay(const capture::CapturedImage& image,const ocr::Document& document) {
    capture::DesktopSnapshot desktop;
    desktop.origin_x=40;desktop.origin_y=40;
    desktop.image={900,600,std::vector<std::uint8_t>(900*600*4,200)};
    ui::RegionSelectionOverlay overlay(GetModuleHandleW(nullptr));
    // Early exit followed immediately by another OCR session exercises stale
    // asynchronous completion on the same persistent overlay/service instance.
    for (int scenario=0;scenario<4;++scenario) {
        Probe probe;
        probe.document=document;probe.expected=document.text;
        probe.early_close=scenario==0;probe.blank=scenario==2;probe.return_editing=scenario==1;
        {
            Require(OpenClipboard(nullptr)!=FALSE,"Cannot prepare isolated clipboard");
            EmptyClipboard();
            constexpr wchar_t sentinel[]=L"blank-test-sentinel";
            HANDLE data=GlobalAlloc(GMEM_MOVEABLE,sizeof(sentinel));
            void* value=GlobalLock(data);std::memcpy(value,sentinel,sizeof(sentinel));GlobalUnlock(data);
            SetClipboardData(CF_UNICODETEXT,data);CloseClipboard();
        }
        active_probe=&probe;
        auto timer=SetTimer(nullptr,0,50,InspectTimer);
        Require(timer!=0,"Cannot start inline OCR inspection");
        const auto input=probe.blank ? capture::CapturedImage(720,180,std::vector<std::uint8_t>(720*180*4,255)) : image;
        const auto started=std::chrono::steady_clock::now();
        const auto result=overlay.EditWindow(desktop,input,POINT{120,240});
        const auto elapsed=std::chrono::steady_clock::now()-started;
        KillTimer(nullptr,timer);active_probe=nullptr;
        if (!probe.error.empty()) throw std::runtime_error(probe.error);
        Require(probe.found && probe.passed,"Inline OCR interaction failed");
        if (probe.early_close) {
            Require(result && !result.Value(),"Early close did not cancel");
            Require(elapsed<std::chrono::seconds(1),"Closing overlay blocked on OCR worker");
        } else {
            Require(result && result.Value(),"Inline OCR result missing");
            Require(result.Value()->text_only==!probe.return_editing,"Text-only exit could overwrite text clipboard with an image");
            Require(result.Value()->image.Pixels()==input.Pixels(),"OCR highlight entered saved pixels");
        }
    }
}
}

int main() {
    try {
        common::EnableDpiAwareness();
        IsolatedDesktop isolated;
        const auto image = TextImage();
        const auto original_pixels = image.Pixels();
        ocr::OneOcrService missing(std::filesystem::temp_directory_path() /
            (L"MissingOneOCR_" + std::to_wstring(GetCurrentProcessId())));
        Require(!missing.Recognize(image), "Missing OCR runtime must report a failure");
        Require(!missing.Recognize({}), "Empty image must report a failure");
        Require(!missing.Recognize({4, 4, {1, 2, 3}}), "Truncated pixels must be rejected");
        Require(!missing.Recognize({10001, 1, {1, 2, 3, 4}}), "Oversized image must be rejected");
        ocr::OneOcrService service;
        const auto result = service.Recognize(image);
        if (!result) {
            std::wcerr << result.Error() << L'\n';
            throw std::runtime_error("Cannot initialize installed Photos/Snipping Tool OCR");
        }
        Require(result.Value().find(L"OCR VERIFY 12345") != std::wstring::npos, "English OCR result incorrect");
        Require(result.Value().find(L"中文文字识别测试") != std::wstring::npos, "Chinese OCR result incorrect");
        const auto repeated = service.Recognize(image);
        Require(repeated && repeated.Value() == result.Value(), "Repeated recognition must be stable");
        Require(image.Pixels() == original_pixels, "Recognition changed source pixels");
        const capture::CapturedImage tiny(8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 0));
        const auto empty = service.Recognize(tiny);
        Require(empty && empty.Value().empty(), "Tiny transparent selection must produce empty text");
        CheckSelectionModel();
        const auto document=service.RecognizeDocument(image);
        Require(document && document.Value().text==result.Value(),"Document text differs from plain text");
        Require(document.Value().lines.size()>=2 && !document.Value().lines.front().words.empty(),"Missing line/word geometry");
        const auto bounds=document.Value().lines.front().bounds;
        Require(bounds[0].x>15 && bounds[0].y>15 && bounds[2].x<720 && bounds[2].y<80,"OCR bounds outside source text");
        CheckOverlay(image,document.Value());
        std::cout << "OneOCR: Chinese/English, geometry, engine reuse, invalid inputs, pixel preservation,\n"
                     "cross-line/reverse/partial/rotated/surrogate selection, inline highlight, Unicode clipboard,\n"
                     "blank result and nonblocking early-close/reopen passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
