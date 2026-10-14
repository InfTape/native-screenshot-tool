#include <Windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "common/DpiAwareness.h"
#include "translation/GoogleTranslationService.h"
#include "ui/TranslationResultWindow.h"

namespace {
using namespace winrt::Windows::Data::Json;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::wstring Text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    const auto length = GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.resize(length); return value;
}
std::string Fixture(std::wstring_view translation, bool chunks = false) {
    JsonArray rows;
    if (chunks) rows = JsonArray::Parse(L"[[\"第一句。\",\"First sentence.\"],[\"第二句。\",\"Second sentence.\"],[null,null,null,\"phonetic\"]]");
    else {
        JsonArray entry; entry.Append(JsonValue::CreateStringValue(winrt::hstring(translation)));
        entry.Append(JsonValue::CreateStringValue(L"Original source, not translation"));
        rows.Append(entry);
    }
    JsonArray root;
    root.Append(rows);
    // Dictionary and language metadata must never become translation text.
    root.Append(JsonArray::Parse(L"[[\"noun\",[\"dictionary entry\"]]]"));
    root.Append(JsonValue::CreateStringValue(L"en"));
    return winrt::to_string(root.Stringify());
}
void CheckProtocol() {
    const std::wstring input = L"\"Hello\" & 100% + ? #\r\n中文 😀";
    const auto path = translation::BuildGoogleTranslationPath(input);
    Require(path.starts_with(L"/translate_a/single?"), "Wrong legacy endpoint");
    Require(path.find(L"&client=gtx&sl=en&tl=zh-CN&") != std::wstring::npos, "Wrong client or language pair");
    const auto query = path.find(L"&q=");
    Require(query != std::wstring::npos, "Missing source text query");
    const auto encoded = winrt::to_string(winrt::hstring(path.substr(query + 3)));
    Require(encoded.find_first_of("&+?#\r\n") == std::string::npos, "Unescaped characters alter the request query");
    std::string decoded;
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '%') {
            decoded += static_cast<char>(std::stoi(encoded.substr(index + 1, 2), nullptr, 16)); index += 2;
        } else decoded += encoded[index];
    }
    Require(winrt::to_hstring(decoded) == input, "Quotes, newlines or Unicode corrupted in request");
    auto result = translation::ParseGoogleTranslationResponse(Fixture(L"窗口化游戏优化\r\n你好 😀"));
    Require(result && result.Value() == L"窗口化游戏优化\r\n你好 😀", "Unicode/multiline response corrupted");
    result = translation::ParseGoogleTranslationResponse(Fixture(L"unused", true));
    Require(result && result.Value() == L"第一句。第二句。", "Segments, pronunciation or dictionary metadata parsed incorrectly");
    result = translation::ParseGoogleTranslationResponse("\n  " + Fixture(L" \t你好\r\n ") + "\n");
    Require(result && result.Value() == L"你好", "Response whitespace not trimmed");
    Require(!translation::ParseGoogleTranslationResponse("<html>captcha</html>"), "HTML response reported as success");
    Require(!translation::ParseGoogleTranslationResponse("[[\"wrb.fr\",\"MkEWBc\",\"[]\"]]"), "RPC response reported as legacy success");
    Require(!translation::ParseGoogleTranslationResponse("[null]"), "Null result reported as success");
    Require(!translation::ParseGoogleTranslationResponse("[[[\"incomplete\"]]"), "Malformed JSON reported as success");
    Require(!translation::ParseGoogleTranslationResponse(Fixture(L"")), "Empty translation reported as success");
    Require(!translation::ParseGoogleTranslationResponse(Fixture(L" \r\n")), "Blank translation reported as success");
    std::wstring long_text(4999, L'x'); long_text += L"😀Z\n"; long_text += std::wstring(6000, L'y');
    const auto chunks = translation::SplitTranslationText(long_text);
    std::wstring rejoined;
    for (const auto& chunk : chunks) {
        Require(!chunk.empty() && chunk.size() <= 5000, "Chunk exceeds Google limit");
        Require(chunk.back() < 0xD800 || chunk.back() > 0xDBFF, "Surrogate pair split");
        rejoined += chunk;
    }
    Require(rejoined == long_text, "Chunking lost text");
    std::stop_source cancelled; cancelled.request_stop();
    Require(!translation::TranslateEnglishToChinese(L"Hello", cancelled.get_token()), "Pre-cancelled request ran");
    Require(!translation::TranslateEnglishToChinese(L" \r\n"), "Blank request ran");
}
struct IsolatedDesktop {
    HWINSTA original_station = GetProcessWindowStation(), station = nullptr;
    HDESK original_desktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
    IsolatedDesktop() {
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        Require(station && SetProcessWindowStation(station), "Cannot isolate translation clipboard");
        desktop = CreateDesktopW(L"TranslationTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        Require(desktop && SetThreadDesktop(desktop), "Cannot isolate translation desktop");
    }
    ~IsolatedDesktop() {
        SetThreadDesktop(original_desktop); SetProcessWindowStation(original_station);
        if (desktop) CloseDesktop(desktop);
        if (station) CloseWindowStation(station);
    }
};
void PumpUntil(ui::TranslationResultWindow& window, const std::function<bool()>& ready) {
    const auto deadline = GetTickCount64() + 3000;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (!window.ProcessMessage(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if (ready()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (GetTickCount64() < deadline);
    throw std::runtime_error("Translation window timed out");
}
void SavePreview(HWND window) {
    RECT bounds{}; GetClientRect(window, &bounds);
    BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), bounds.right, -bounds.bottom, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Require(dc && bitmap && pixels, "Cannot render translation preview");
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + bounds.right * bounds.bottom * 4;
    std::ofstream file("TranslationPreview.bmp", std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(pixels), bounds.right * bounds.bottom * 4);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
}
void CheckWindow() {
    IsolatedDesktop isolated;
    HWND owner = CreateWindowExW(0, L"STATIC", L"Test owner", WS_OVERLAPPEDWINDOW,
        0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(owner != nullptr, "Cannot create translation owner");
    ui::TranslationResultWindow window;
    const std::wstring input = L"Optimizations for windowed games\r\nReduce latency and use advanced features in compatible games by using flip presentation model.";
    const std::wstring output = L"窗口化游戏优化\r\n通过使用翻转呈现模型，降低兼容游戏的延迟，并启用高级功能。";
    Require(window.Show(owner, input, [output](std::wstring_view, std::stop_token) {
        return common::Result<std::wstring>::Success(output);
    }), "Cannot show translation window");
    PumpUntil(window, [&] { return IsWindowEnabled(GetDlgItem(window.Window(), 1)) != FALSE; });
    Require(Text(GetDlgItem(window.Window(), 100)) == input, "Source text changed");
    Require(Text(GetDlgItem(window.Window(), 101)) == output, "Translation not displayed");
    SendMessageW(window.Window(), WM_COMMAND, MAKEWPARAM(1, BN_CLICKED), 0);
    Require(OpenClipboard(window.Window()), "Cannot read isolated clipboard");
    const auto clipboard = GetClipboardData(CF_UNICODETEXT);
    const auto data = static_cast<const wchar_t*>(GlobalLock(clipboard));
    const bool copied = data && output == data;
    if (data) GlobalUnlock(clipboard); CloseClipboard();
    Require(copied, "Copy translation lost Unicode text");
    SavePreview(window.Window());
    SetWindowPos(window.Window(), nullptr, 0, 0, 370, 310, SWP_NOMOVE | SWP_NOZORDER);
    RECT edit{}; GetWindowRect(GetDlgItem(window.Window(), 101), &edit);
    Require(edit.right > edit.left && edit.bottom > edit.top, "Resize hid translation");

    auto cancelled = std::make_shared<std::promise<void>>();
    auto finished = cancelled->get_future();
    Require(window.Show(owner, L"old request", [cancelled](std::wstring_view, std::stop_token stop) {
        while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cancelled->set_value();
        return common::Result<std::wstring>::Success(L"stale result");
    }), "Cannot start cancellation fixture");
    const auto start = GetTickCount64(); window.Close();
    Require(GetTickCount64() - start < 250, "Closing translation blocked UI");
    Require(finished.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "Closing did not cancel worker");
    Require(window.Show(owner, L"new request", [](std::wstring_view, std::stop_token) {
        return common::Result<std::wstring>::Failure(L"网络错误测试");
    }), "Cannot reopen translation");
    PumpUntil(window, [&] { return Text(GetDlgItem(window.Window(), 101)) == L"网络错误测试"; });
    Require(!IsWindowEnabled(GetDlgItem(window.Window(), 1)), "Failed translation can be copied");
    DestroyWindow(owner);
    Require(!window.Window(), "Translation survived owner destruction");
}
}
int main(int argc, char** argv) {
    try {
        common::EnableDpiAwareness();
        // No ambient COM apartment: each worker fully tears down its MTA.
        // This catches cached activation factories left by a previous request.
        for (int index = 0; index < 3; ++index) {
            auto worker = std::async(std::launch::async, [] {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                const auto path = translation::BuildGoogleTranslationPath(L"Hello");
                const auto result = translation::ParseGoogleTranslationResponse(R"([[["你好","Hello"]],null,"en"])");
                winrt::uninit_apartment();
                return !path.empty() && result && result.Value() == L"你好";
            });
            Require(worker.get(), "Repeated isolated translation workers failed");
        }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        CheckProtocol(); CheckWindow();
        if (argc > 1 && std::string_view(argv[1]) == "--live") {
            const auto result = translation::TranslateEnglishToChinese(L"Optimizations for windowed games");
            Require(static_cast<bool>(result), result ? "" : winrt::to_string(result.Error()).c_str());
            Require(result.Value().find_first_of(L"窗口游戏优化") != std::wstring::npos, "Live response is not Chinese");
            std::cout << "Live Google legacy endpoint: " << winrt::to_string(result.Value()) << '\n';
            std::stop_source stop;
            auto pending = std::async(std::launch::async, [&] {
                return translation::TranslateEnglishToChinese(L"Cancel this translation request", stop.get_token());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            stop.request_stop();
            Require(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                "Live WinHTTP cancellation did not promptly stop request");
            const auto cancelled = pending.get();
            Require(!cancelled && cancelled.Error() == L"已取消翻译。", "Cancelled live request returned a result");
        }
        winrt::uninit_apartment();
        std::cout << "Google translation: protocol, Unicode, chunking, cancellation, result/copy/error UI and owner lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
