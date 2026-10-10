#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "capture/FileClipboardWriter.h"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Each window station has its own clipboard: tests never replace the user's clipboard.
class IsolatedClipboard {
public:
    bool Initialize() {
        original_station_ = GetProcessWindowStation();
        original_desktop_ = GetThreadDesktop(GetCurrentThreadId());
        // Let Windows choose the name so this also works without elevation.
        station_ = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station_) return InitializationFailure("CreateWindowStationW");
        if (!SetProcessWindowStation(station_)) return InitializationFailure("SetProcessWindowStation");
        desktop_ = CreateDesktopW(L"ClipboardTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop_) return InitializationFailure("CreateDesktopW");
        if (!SetThreadDesktop(desktop_)) return InitializationFailure("SetThreadDesktop");
        owner_ = CreateWindowExW(0, L"STATIC", L"Clipboard test", 0, 0, 0, 1, 1,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        return owner_ != nullptr || InitializationFailure("CreateWindowExW");
    }
    ~IsolatedClipboard() {
        DestroyOwner();
        if (original_desktop_) SetThreadDesktop(original_desktop_);
        if (original_station_) SetProcessWindowStation(original_station_);
        if (desktop_) CloseDesktop(desktop_);
        if (station_) CloseWindowStation(station_);
    }
    void DestroyOwner() {
        if (owner_) DestroyWindow(owner_);
        owner_ = nullptr;
    }
    HWND Owner() const { return owner_; }
    HDESK Desktop() const { return desktop_; }

private:
    static bool InitializationFailure(const char* operation) {
        std::cerr << operation << " failed: " << GetLastError() << '\n';
        return false;
    }
    HWINSTA original_station_ = nullptr;
    HDESK original_desktop_ = nullptr;
    HWINSTA station_ = nullptr;
    HDESK desktop_ = nullptr;
    HWND owner_ = nullptr;
};

struct ClipboardCloser {
    ~ClipboardCloser() { CloseClipboard(); }
};

void CheckFile(const std::filesystem::path& path) {
    Require(OpenClipboard(nullptr) != FALSE, "open clipboard to read");
    ClipboardCloser closer;
    auto files = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    Require(files != nullptr, "CF_HDROP is available");
    Require(DragQueryFileW(files, 0xFFFFFFFF, nullptr, 0) == 1, "exactly one file");
    const UINT length = DragQueryFileW(files, 0, nullptr, 0);
    std::vector<wchar_t> value(static_cast<size_t>(length) + 1);
    Require(DragQueryFileW(files, 0, value.data(), length + 1) == length, "read Unicode path");
    Require(std::wstring(value.data()) == path.wstring(), "clipboard holds the complete absolute file path");
    auto* drop = static_cast<DROPFILES*>(GlobalLock(files));
    Require(drop != nullptr, "lock CF_HDROP");
    const auto* list = reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(drop) + drop->pFiles);
    const bool valid = drop->fWide && list[length] == L'\0' && list[length + 1] == L'\0';
    GlobalUnlock(files);
    Require(valid, "Unicode list has two terminating NULs");
    const UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HANDLE effect = GetClipboardData(format);
    Require(effect != nullptr, "preferred drop effect is available");
    auto* effect_value = static_cast<DWORD*>(GlobalLock(effect));
    Require(effect_value != nullptr, "lock preferred drop effect");
    const DWORD actual_effect = *effect_value;
    GlobalUnlock(effect);
    Require(actual_effect == DROPEFFECT_COPY, "paste copies rather than moves the saved recording");
}

class ClipboardBlocker {
public:
    explicit ClipboardBlocker(HDESK desktop) {
        auto ready = ready_.get_future();
        worker_ = std::thread([this, desktop] {
            if (!SetThreadDesktop(desktop)) {
                ready_.set_value(false);
                return;
            }
            HWND owner = CreateWindowExW(0, L"STATIC", L"Clipboard blocker", 0, 0, 0, 1, 1,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            const bool opened = owner && OpenClipboard(owner);
            ready_.set_value(opened);
            if (opened) {
                while (!release_.load()) Sleep(1);
                CloseClipboard();
            }
            if (owner) DestroyWindow(owner);
        });
        opened_ = ready.get();
    }
    ~ClipboardBlocker() {
        release_.store(true);
        worker_.join();
    }
    bool Opened() const { return opened_; }

private:
    std::atomic<bool> release_{false};
    std::promise<bool> ready_;
    std::thread worker_;
    bool opened_ = false;
};

struct TestFiles {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        (L"NativeScreenshot Clipboard 测试 " + std::to_wstring(GetCurrentProcessId()));
    bool owned = false;
    ~TestFiles() {
        if (!owned) return;
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

}  // namespace

int main() {
    try {
        IsolatedClipboard clipboard;
        Require(clipboard.Initialize(), "create isolated clipboard station and desktop");
        TestFiles fixtures;
        fixtures.owned = std::filesystem::create_directory(fixtures.directory);
        Require(fixtures.owned, "create test directory");
        capture::FileClipboardWriter writer;
        for (const wchar_t* extension : {L".mp4", L".gif"}) {
            const auto path = fixtures.directory / (std::wstring(L"录屏 with spaces") + extension);
            std::ofstream(path, std::ios::binary) << "clipboard file fixture";
            Require(static_cast<bool>(writer.CopyToClipboard(clipboard.Owner(), path.wstring())), "copy saved MP4/GIF file");
            CheckFile(path);
            const auto relative_path = path.lexically_relative(std::filesystem::current_path());
            Require(static_cast<bool>(writer.CopyToClipboard(clipboard.Owner(), relative_path.wstring())), "relative path is resolved before copy");
            CheckFile(path);
            Require(!writer.CopyToClipboard(clipboard.Owner(), L""), "empty path rejected");
            Require(!writer.CopyToClipboard(clipboard.Owner(), (fixtures.directory / L"missing.mp4").wstring()), "missing file rejected");
            Require(!writer.CopyToClipboard(clipboard.Owner(), fixtures.directory.wstring()), "directory rejected");
            Require(!writer.CopyToClipboard(nullptr, path.wstring()), "invalid owner rejected");
            std::wstring invalid_path = path.wstring();
            invalid_path.push_back(L'\0');
            invalid_path += L"truncated";
            Require(!writer.CopyToClipboard(clipboard.Owner(), invalid_path), "embedded NUL rejected");
            CheckFile(path);  // Invalid inputs preserve the previous clipboard.
            {
                ClipboardBlocker blocker(clipboard.Desktop());
                Require(blocker.Opened(), "another thread holds the clipboard");
                const auto started = std::chrono::steady_clock::now();
                Require(!writer.CopyToClipboard(clipboard.Owner(), path.wstring()), "clipboard contention reports failure");
                Require(std::chrono::steady_clock::now() - started < std::chrono::seconds(1), "clipboard retries are bounded");
            }
            Require(std::filesystem::is_regular_file(path), "clipboard failure leaves saved recording intact");
            CheckFile(path);
        }
        const auto gif = fixtures.directory / L"录屏 with spaces.gif";
        clipboard.DestroyOwner();
        CheckFile(gif);  // Clipboard data still works after exiting its owner window.
        std::cout << "MP4/GIF file copy, Unicode paths, exit persistence, invalid paths and clipboard contention passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
