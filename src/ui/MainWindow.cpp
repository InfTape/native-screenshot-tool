#include "ui/MainWindow.h"

#include <Windows.h>
#include <shobjidl.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "common/Win32Error.h"
#include "hotkey/HotkeyDefinition.h"

namespace {

constexpr int kFullCaptureButtonId = 1001;
constexpr int kRegionCaptureButtonId = 1002;
constexpr int kWindowCaptureButtonId = 1003;
constexpr int kSaveDirectoryButtonId = 1004;
constexpr int kSaveFormatComboId = 1005;
constexpr int kSaveButtonId = 1006;
constexpr int kClearButtonId = 1007;
constexpr int kFullHotkeyButtonId = 1011;
constexpr int kRegionHotkeyButtonId = 1012;
constexpr int kWindowHotkeyButtonId = 1013;
constexpr int kFullHotkeyId = 2001;
constexpr int kRegionHotkeyId = 2002;
constexpr int kWindowHotkeyId = 2003;
constexpr UINT kTrayIconMessage = WM_APP + 1;

constexpr int kWindowWidth = 1240;
constexpr int kWindowHeight = 820;
constexpr int kMinWindowWidth = 980;
constexpr int kMinWindowHeight = 620;
constexpr int kMargin = 16;
constexpr int kButtonWidth = 132;
constexpr int kDirectoryButtonWidth = 148;
constexpr int kSmallButtonWidth = 96;
constexpr int kHotkeyButtonWidth = 168;
constexpr int kButtonHeight = 32;
constexpr int kComboBoxHeight = 220;
constexpr int kGap = 10;
constexpr int kStatusHeight = 24;
constexpr int kHotkeyLabelHeight = 24;
constexpr int kSaveFormatComboWidth = 110;

struct ScopedBooleanReset {
    explicit ScopedBooleanReset(bool& value) : value_(value) {
        value_ = true;
    }

    ~ScopedBooleanReset() {
        value_ = false;
    }

    bool& value_;
};

struct ScopedComInitialization {
    ScopedComInitialization() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ScopedComInitialization() {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    bool IsAvailable() const {
        return result_ == S_OK || result_ == S_FALSE || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
};

constexpr std::array<ui::HotkeySlot, 3> kConfigurableHotkeySlots = {
    ui::HotkeySlot::FullCapture,
    ui::HotkeySlot::RegionCapture,
    ui::HotkeySlot::WindowCapture,
};

constexpr std::size_t HotkeySlotIndex(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return 0;
    case ui::HotkeySlot::RegionCapture:
        return 1;
    case ui::HotkeySlot::WindowCapture:
        return 2;
    case ui::HotkeySlot::None:
    default:
        return 0;
    }
}

int GetHotkeyCommandId(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return kFullHotkeyButtonId;
    case ui::HotkeySlot::RegionCapture:
        return kRegionHotkeyButtonId;
    case ui::HotkeySlot::WindowCapture:
        return kWindowHotkeyButtonId;
    case ui::HotkeySlot::None:
    default:
        return 0;
    }
}

int GetHotkeyIdentifier(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return kFullHotkeyId;
    case ui::HotkeySlot::RegionCapture:
        return kRegionHotkeyId;
    case ui::HotkeySlot::WindowCapture:
        return kWindowHotkeyId;
    case ui::HotkeySlot::None:
    default:
        return 0;
    }
}

const wchar_t* GetHotkeyButtonText(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return L"设置全屏快捷键";
    case ui::HotkeySlot::RegionCapture:
        return L"设置选区快捷键";
    case ui::HotkeySlot::WindowCapture:
        return L"设置窗口快捷键";
    case ui::HotkeySlot::None:
    default:
        return L"设置快捷键";
    }
}

const wchar_t* GetHotkeyLabelTitle(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return L"全屏截图快捷键：";
    case ui::HotkeySlot::RegionCapture:
        return L"选区截图快捷键：";
    case ui::HotkeySlot::WindowCapture:
        return L"窗口截图快捷键：";
    case ui::HotkeySlot::None:
    default:
        return L"快捷键：";
    }
}

const wchar_t* GetCaptureModeName(ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return L"全屏截图";
    case ui::HotkeySlot::RegionCapture:
        return L"选区截图";
    case ui::HotkeySlot::WindowCapture:
        return L"窗口截图";
    case ui::HotkeySlot::None:
    default:
        return L"截图";
    }
}

hotkey::HotkeyDefinition& AccessHotkeyDefinition(settings::AppSettings& settings,
                                                 ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return settings.full_capture_hotkey;
    case ui::HotkeySlot::RegionCapture:
        return settings.region_capture_hotkey;
    case ui::HotkeySlot::WindowCapture:
        return settings.window_capture_hotkey;
    case ui::HotkeySlot::None:
    default:
        return settings.region_capture_hotkey;
    }
}

const hotkey::HotkeyDefinition& AccessHotkeyDefinition(const settings::AppSettings& settings,
                                                       ui::HotkeySlot slot) {
    switch (slot) {
    case ui::HotkeySlot::FullCapture:
        return settings.full_capture_hotkey;
    case ui::HotkeySlot::RegionCapture:
        return settings.region_capture_hotkey;
    case ui::HotkeySlot::WindowCapture:
        return settings.window_capture_hotkey;
    case ui::HotkeySlot::None:
    default:
        return settings.region_capture_hotkey;
    }
}

bool SameHotkey(const hotkey::HotkeyDefinition& left, const hotkey::HotkeyDefinition& right) {
    return left.modifiers == right.modifiers && left.virtual_key == right.virtual_key;
}

const wchar_t* FormatDisplayName(capture::ImageFileFormat format) {
    switch (format) {
    case capture::ImageFileFormat::Bmp:
        return L"BMP";
    case capture::ImageFileFormat::Png:
    default:
        return L"PNG";
    }
}

int SaveFormatComboIndex(capture::ImageFileFormat format) {
    return format == capture::ImageFileFormat::Bmp ? 1 : 0;
}

capture::ImageFileFormat SaveFormatFromComboIndex(int combo_index) {
    return combo_index == 1 ? capture::ImageFileFormat::Bmp : capture::ImageFileFormat::Png;
}

}  // namespace

namespace ui {

MainWindow::MainWindow(HINSTANCE instance)
    : instance_(instance),
      region_selection_overlay_(instance),
      window_selection_overlay_(instance),
      tray_icon_controller_(instance) {}

bool MainWindow::Create() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &MainWindow::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kClassName;

    if (RegisterClassExW(&window_class) == 0) {
        const DWORD register_error = GetLastError();
        if (register_error != ERROR_CLASS_ALREADY_EXISTS) {
            SetLastError(register_error);
            return false;
        }
    }

    window_ = CreateWindowExW(0,
                              kClassName,
                              L"原生 Win32 截图工具",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              kWindowWidth,
                              kWindowHeight,
                              nullptr,
                              nullptr,
                              instance_,
                              this);

    return window_ != nullptr;
}

void MainWindow::Show(int nCmdShow) const {
    ShowWindow(window_, nCmdShow);
    UpdateWindow(window_);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<MainWindow*>(create_struct->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

bool MainWindow::CreateChildControls() {
    full_capture_button_ = CreateWindowExW(0,
                                           L"BUTTON",
                                           L"全屏截图",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                           0,
                                           0,
                                           0,
                                           0,
                                           window_,
                                           reinterpret_cast<HMENU>(
                                               static_cast<INT_PTR>(kFullCaptureButtonId)),
                                           instance_,
                                           nullptr);

    region_capture_button_ = CreateWindowExW(0,
                                             L"BUTTON",
                                             L"选区截图",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             0,
                                             0,
                                             0,
                                             0,
                                             window_,
                                             reinterpret_cast<HMENU>(
                                                 static_cast<INT_PTR>(kRegionCaptureButtonId)),
                                             instance_,
                                             nullptr);

    window_capture_button_ = CreateWindowExW(0,
                                             L"BUTTON",
                                             L"窗口截图",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             0,
                                             0,
                                             0,
                                             0,
                                             window_,
                                             reinterpret_cast<HMENU>(
                                                 static_cast<INT_PTR>(kWindowCaptureButtonId)),
                                              instance_,
                                              nullptr);

    save_directory_button_ = CreateWindowExW(0,
                                             L"BUTTON",
                                             L"设置保存目录",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             0,
                                             0,
                                             0,
                                             0,
                                             window_,
                                             reinterpret_cast<HMENU>(
                                                 static_cast<INT_PTR>(kSaveDirectoryButtonId)),
                                             instance_,
                                             nullptr);

    save_directory_label_ = CreateWindowExW(0,
                                            L"STATIC",
                                            L"保存目录：",
                                            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                            0,
                                            0,
                                            0,
                                            0,
                                            window_,
                                            nullptr,
                                            instance_,
                                            nullptr);

    save_format_combo_ = CreateWindowExW(0,
                                         L"COMBOBOX",
                                         L"",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         0,
                                         0,
                                         0,
                                         0,
                                         window_,
                                         reinterpret_cast<HMENU>(
                                             static_cast<INT_PTR>(kSaveFormatComboId)),
                                         instance_,
                                         nullptr);

    save_button_ = CreateWindowExW(0,
                                   L"BUTTON",
                                   L"立即保存",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    0,
                                    0,
                                   0,
                                   0,
                                   window_,
                                   reinterpret_cast<HMENU>(
                                       static_cast<INT_PTR>(kSaveButtonId)),
                                   instance_,
                                   nullptr);

    clear_button_ = CreateWindowExW(0,
                                    L"BUTTON",
                                    L"清空",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    0,
                                    0,
                                    0,
                                    0,
                                    window_,
                                    reinterpret_cast<HMENU>(
                                        static_cast<INT_PTR>(kClearButtonId)),
                                    instance_,
                                    nullptr);

    for (const HotkeySlot slot : kConfigurableHotkeySlots) {
        const std::size_t index = HotkeySlotIndex(slot);
        hotkey_buttons_[index] = CreateWindowExW(
            0,
            L"BUTTON",
            GetHotkeyButtonText(slot),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(GetHotkeyCommandId(slot))),
            instance_,
            nullptr);

        hotkey_labels_[index] = CreateWindowExW(0,
                                                L"STATIC",
                                                L"",
                                                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                                0,
                                                0,
                                                0,
                                                0,
                                                window_,
                                                nullptr,
                                                instance_,
                                                nullptr);
    }

    status_label_ = CreateWindowExW(0,
                                    L"STATIC",
                                    L"支持全屏截图、选区截图、窗口截图，并可分别设置三组全局快捷键。",
                                    WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                    0,
                                    0,
                                    0,
                                    0,
                                    window_,
                                    nullptr,
                                    instance_,
                                    nullptr);

    if (full_capture_button_ == nullptr || region_capture_button_ == nullptr ||
        window_capture_button_ == nullptr || save_directory_button_ == nullptr ||
        save_directory_label_ == nullptr || save_format_combo_ == nullptr ||
        save_button_ == nullptr || clear_button_ == nullptr || status_label_ == nullptr) {
        return false;
    }

    SendMessageW(save_format_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PNG"));
    SendMessageW(save_format_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"BMP"));

    for (const HWND button : hotkey_buttons_) {
        if (button == nullptr) {
            return false;
        }
    }

    for (const HWND label : hotkey_labels_) {
        if (label == nullptr) {
            return false;
        }
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);
    LayoutControls(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
    UpdateActionState();
    UpdateHotkeyLabels();
    return true;
}

bool MainWindow::InitializeHotkeySettings() {
    std::wstring error_message;
    if (!settings_repository_.Load(settings_, error_message)) {
        MessageBoxW(window_, error_message.c_str(), L"读取设置失败", MB_OK | MB_ICONWARNING);
    }

    EnsureSaveDirectoryConfigured();
    UpdateSaveSettingsDisplay();
    UpdateSaveButtonLabel();

    if (!RegisterConfiguredHotkeys(true)) {
        UpdateStatus(L"部分快捷键注册失败，请重新设置未被占用的组合键。");
        return false;
    }

    return true;
}

bool MainWindow::InitializeTrayIcon() {
    std::wstring error_message;
    if (!tray_icon_controller_.AddIcon(window_, kTrayIconMessage, error_message)) {
        MessageBoxW(window_, error_message.c_str(), L"托盘初始化失败", MB_OK | MB_ICONWARNING);
        return false;
    }

    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    return true;
}

bool MainWindow::SaveImageToConfiguredDirectory(const capture::CapturedImage& image,
                                                std::wstring& saved_path,
                                                std::wstring& error_message,
                                                bool& clipboard_failed) const {
    saved_path.clear();
    error_message.clear();
    clipboard_failed = false;

    if (image.IsEmpty()) {
        error_message = L"当前没有可保存的截图。";
        return false;
    }

    capture::AutoSaveOptions options;
    options.directory = settings_.save_directory;
    options.format = settings_.save_format;

    const auto result = auto_save_service_.SaveImage(window_, image, options, error_message);
    saved_path = result.saved_path;
    if (result.status == capture::AutoSaveStatus::SavedAndCopied) {
        return true;
    }

    if (result.status == capture::AutoSaveStatus::ClipboardFailed) {
        clipboard_failed = true;
        return true;
    }

    return false;
}

void MainWindow::UpdateSaveSettingsDisplay() const {
    if (save_directory_label_ != nullptr) {
        const std::wstring text = settings_.save_directory.empty()
                                      ? L"保存目录：未设置"
                                      : L"保存目录：" + settings_.save_directory;
        SetWindowTextW(save_directory_label_, text.c_str());
    }

    if (save_format_combo_ != nullptr) {
        SendMessageW(
            save_format_combo_, CB_SETCURSEL, SaveFormatComboIndex(settings_.save_format), 0);
    }
}

void MainWindow::UpdateSaveButtonLabel() const {
    if (save_button_ != nullptr) {
        const std::wstring text = std::wstring(L"立即保存 ") + FormatDisplayName(settings_.save_format);
        SetWindowTextW(save_button_, text.c_str());
    }
}

bool MainWindow::ResolveDefaultSaveDirectory(std::wstring& directory,
                                             std::wstring& error_message) const {
    directory.clear();
    error_message.clear();

    PWSTR known_folder_path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &known_folder_path);
    if (SUCCEEDED(hr) && known_folder_path != nullptr) {
        std::filesystem::path pictures_path(known_folder_path);
        CoTaskMemFree(known_folder_path);
        directory = (pictures_path / L"NativeScreenshot").wstring();
        return true;
    }

    if (known_folder_path != nullptr) {
        CoTaskMemFree(known_folder_path);
    }

    DWORD required_size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required_size > 0) {
        std::vector<wchar_t> buffer(required_size);
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required_size) > 0) {
            directory = (std::filesystem::path(buffer.data()) / L"NativeScreenshot" / L"Captures").wstring();
            return true;
        }
    }

    error_message = L"无法解析默认保存目录。";
    return false;
}

bool MainWindow::EnsureSaveDirectoryConfigured() {
    if (!settings_.save_directory.empty()) {
        return true;
    }

    std::wstring default_directory;
    std::wstring error_message;
    if (!ResolveDefaultSaveDirectory(default_directory, error_message)) {
        MessageBoxW(window_, error_message.c_str(), L"设置保存目录失败", MB_OK | MB_ICONWARNING);
        return false;
    }

    settings_.save_directory = default_directory;
    if (!settings_repository_.Save(settings_, error_message)) {
        MessageBoxW(window_, error_message.c_str(), L"保存设置失败", MB_OK | MB_ICONWARNING);
    }
    return true;
}

bool MainWindow::ChooseSaveDirectory(std::wstring& selected_directory) const {
    selected_directory.clear();

    ScopedComInitialization com_initialization;
    if (!com_initialization.IsAvailable()) {
        MessageBoxW(window_, L"初始化目录选择器失败。", L"选择目录失败", MB_OK | MB_ICONERROR);
        return false;
    }

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        MessageBoxW(window_, L"创建目录选择器失败。", L"选择目录失败", MB_OK | MB_ICONERROR);
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择截图保存目录");

    if (!settings_.save_directory.empty()) {
        IShellItem* default_folder = nullptr;
        hr = SHCreateItemFromParsingName(
            settings_.save_directory.c_str(), nullptr, IID_PPV_ARGS(&default_folder));
        if (SUCCEEDED(hr) && default_folder != nullptr) {
            dialog->SetDefaultFolder(default_folder);
            dialog->SetFolder(default_folder);
            default_folder->Release();
        }
    }

    hr = dialog->Show(window_);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return false;
    }

    if (FAILED(hr)) {
        dialog->Release();
        MessageBoxW(window_, L"打开目录选择器失败。", L"选择目录失败", MB_OK | MB_ICONERROR);
        return false;
    }

    IShellItem* folder = nullptr;
    hr = dialog->GetResult(&folder);
    dialog->Release();
    if (FAILED(hr) || folder == nullptr) {
        MessageBoxW(window_, L"读取目录选择结果失败。", L"选择目录失败", MB_OK | MB_ICONERROR);
        return false;
    }

    PWSTR folder_path = nullptr;
    hr = folder->GetDisplayName(SIGDN_FILESYSPATH, &folder_path);
    folder->Release();
    if (FAILED(hr) || folder_path == nullptr) {
        MessageBoxW(window_, L"解析目录路径失败。", L"选择目录失败", MB_OK | MB_ICONERROR);
        return false;
    }

    selected_directory = folder_path;
    CoTaskMemFree(folder_path);
    return true;
}

void MainWindow::ApplySaveFormatSelection() {
    const int selected_index = static_cast<int>(SendMessageW(save_format_combo_, CB_GETCURSEL, 0, 0));
    if (selected_index == CB_ERR) {
        return;
    }

    const auto new_format = SaveFormatFromComboIndex(selected_index);
    if (new_format == settings_.save_format) {
        return;
    }

    const auto previous_format = settings_.save_format;
    settings_.save_format = new_format;

    std::wstring error_message;
    if (!settings_repository_.Save(settings_, error_message)) {
        settings_.save_format = previous_format;
        UpdateSaveSettingsDisplay();
        MessageBoxW(window_, error_message.c_str(), L"保存设置失败", MB_OK | MB_ICONWARNING);
        return;
    }

    UpdateSaveSettingsDisplay();
    UpdateSaveButtonLabel();
    UpdateStatus(std::wstring(L"默认保存格式已切换为 ") + FormatDisplayName(settings_.save_format) + L"。");
}

RECT MainWindow::CalculatePreviewRect(int client_width, int client_height) const {
    RECT preview{};
    preview.left = kMargin;
    preview.top = kMargin + kButtonHeight + kGap + ((kButtonHeight + kGap) * 3) + kStatusHeight +
                  kGap;
    preview.right = std::max<LONG>(preview.left, static_cast<LONG>(client_width - kMargin));
    preview.bottom = std::max<LONG>(preview.top, static_cast<LONG>(client_height - kMargin));
    return preview;
}

void MainWindow::LayoutControls(int client_width, int client_height) {
    int y = kMargin;
    int x = kMargin;

    MoveWindow(full_capture_button_, x, y, kButtonWidth, kButtonHeight, TRUE);
    x += kButtonWidth + kGap;

    MoveWindow(region_capture_button_, x, y, kButtonWidth, kButtonHeight, TRUE);
    x += kButtonWidth + kGap;

    MoveWindow(window_capture_button_, x, y, kButtonWidth, kButtonHeight, TRUE);
    x += kButtonWidth + kGap;

    MoveWindow(save_button_, x, y, kButtonWidth, kButtonHeight, TRUE);
    x += kButtonWidth + kGap;

    MoveWindow(clear_button_, x, y, kSmallButtonWidth, kButtonHeight, TRUE);

    y += kButtonHeight + kGap;

    x = kMargin;
    MoveWindow(save_directory_button_, x, y, kDirectoryButtonWidth, kButtonHeight, TRUE);
    x += kDirectoryButtonWidth + kGap;

    MoveWindow(save_format_combo_, x, y, kSaveFormatComboWidth, kComboBoxHeight, TRUE);
    x += kSaveFormatComboWidth + kGap;

    MoveWindow(save_directory_label_,
               x,
               y + (kButtonHeight - kHotkeyLabelHeight) / 2,
               std::max(220, client_width - (x + kMargin)),
               kHotkeyLabelHeight,
               TRUE);

    y += kButtonHeight + kGap;

    for (const HotkeySlot slot : kConfigurableHotkeySlots) {
        const std::size_t index = HotkeySlotIndex(slot);
        MoveWindow(hotkey_buttons_[index], kMargin, y, kHotkeyButtonWidth, kButtonHeight, TRUE);
        MoveWindow(hotkey_labels_[index],
                   kMargin + kHotkeyButtonWidth + kGap,
                   y + (kButtonHeight - kHotkeyLabelHeight) / 2,
                   std::max(220, client_width - ((kMargin * 2) + kHotkeyButtonWidth + kGap)),
                   kHotkeyLabelHeight,
                   TRUE);
        y += kButtonHeight + kGap;
    }

    MoveWindow(status_label_,
               kMargin,
               y,
               std::max(240, client_width - (kMargin * 2)),
               kStatusHeight,
               TRUE);

    preview_rect_ = CalculatePreviewRect(client_width, client_height);
    preview_rect_.top = y + kStatusHeight + kGap;
}

void MainWindow::UpdateStatus(const std::wstring& text) const {
    if (status_label_ != nullptr) {
        SetWindowTextW(status_label_, text.c_str());
    }
}

void MainWindow::UpdateHotkeyLabels() const {
    for (const HotkeySlot slot : kConfigurableHotkeySlots) {
        const std::size_t index = HotkeySlotIndex(slot);
        if (hotkey_labels_[index] == nullptr) {
            continue;
        }

        std::wstring text;
        if (recording_hotkey_slot_ == slot) {
            text = std::wstring(GetHotkeyLabelTitle(slot)) + L"录制中，按下组合键，Esc 取消";
        } else {
            text = std::wstring(GetHotkeyLabelTitle(slot)) +
                   hotkey::FormatHotkey(AccessHotkeyDefinition(settings_, slot));
            if (recording_hotkey_slot_ != HotkeySlot::None) {
                text += L"（录制期间临时暂停）";
            } else {
                text += hotkey_registered_[index] ? L"（全局已注册）" : L"（注册失败）";
            }
        }

        SetWindowTextW(hotkey_labels_[index], text.c_str());
    }
}

void MainWindow::UpdateActionState() const {
    const BOOL has_image = image_.IsEmpty() ? FALSE : TRUE;
    EnableWindow(save_button_, has_image);
    EnableWindow(clear_button_, has_image);
}

std::optional<capture::DesktopSnapshot> MainWindow::CaptureSnapshot(
    std::wstring& error_message) const {
    return capture_service_.CaptureDesktopSnapshot(error_message);
}

bool MainWindow::CaptureDesktop() {
    if (capture_in_progress_) {
        return false;
    }

    if (recording_hotkey_slot_ != HotkeySlot::None) {
        recording_hotkey_slot_ = HotkeySlot::None;
        RegisterConfiguredHotkeys(false);
    }

    ScopedBooleanReset capturing(capture_in_progress_);
    std::wstring error_message;
    auto snapshot = CaptureSnapshot(error_message);
    if (!snapshot.has_value()) {
        UpdateStatus(L"全屏截图失败。");
        MessageBoxW(window_, error_message.c_str(), L"截图失败", MB_OK | MB_ICONERROR);
        return false;
    }

    image_ = std::move(snapshot->image);
    UpdateHotkeyLabels();
    UpdateActionState();
    UpdateStatus(L"全屏截图完成：" + std::to_wstring(image_.Width()) + L" x " +
                 std::to_wstring(image_.Height()));
    InvalidateRect(window_, &preview_rect_, TRUE);
    return true;
}

bool MainWindow::CaptureRegion() {
    if (capture_in_progress_) {
        return false;
    }

    if (recording_hotkey_slot_ != HotkeySlot::None) {
        recording_hotkey_slot_ = HotkeySlot::None;
        RegisterConfiguredHotkeys(false);
    }

    ScopedBooleanReset capturing(capture_in_progress_);
    std::wstring error_message;
    auto snapshot = CaptureSnapshot(error_message);
    if (!snapshot.has_value()) {
        UpdateStatus(L"选区截图失败。");
        MessageBoxW(window_, error_message.c_str(), L"截图失败", MB_OK | MB_ICONERROR);
        return false;
    }

    auto selected_result = region_selection_overlay_.SelectRegion(*snapshot, error_message);
    if (!selected_result.has_value()) {
        if (!error_message.empty()) {
            UpdateStatus(L"选区截图失败。");
            MessageBoxW(window_, error_message.c_str(), L"选区失败", MB_OK | MB_ICONERROR);
        } else {
            UpdateStatus(L"已取消选区截图。");
        }
        return false;
    }

    image_ = std::move(selected_result->image);
    UpdateHotkeyLabels();
    UpdateActionState();
    InvalidateRect(window_, &preview_rect_, TRUE);

    std::wstring save_error_message;
    std::wstring saved_path;
    bool clipboard_failed = false;
    if (!SaveImageToConfiguredDirectory(image_, saved_path, save_error_message, clipboard_failed)) {
        UpdateStatus(L"选区截图完成，但自动保存失败。");
        MessageBoxW(window_, save_error_message.c_str(), L"保存失败", MB_OK | MB_ICONERROR);
        return true;
    }

    if (clipboard_failed) {
        UpdateStatus(L"选区截图已自动保存到：" + saved_path + L"，但复制到剪贴板失败。");
        MessageBoxW(window_,
                    save_error_message.c_str(),
                    L"复制到剪贴板失败",
                    MB_OK | MB_ICONWARNING);
        return true;
    }

    UpdateStatus(std::wstring(L"选区截图已自动保存为 ") +
                 FormatDisplayName(settings_.save_format) + L"，并复制到剪贴板：" + saved_path);

    return true;
}

bool MainWindow::CaptureWindow() {
    if (capture_in_progress_) {
        return false;
    }

    if (recording_hotkey_slot_ != HotkeySlot::None) {
        recording_hotkey_slot_ = HotkeySlot::None;
        RegisterConfiguredHotkeys(false);
    }

    ScopedBooleanReset capturing(capture_in_progress_);
    std::wstring error_message;
    auto snapshot = CaptureSnapshot(error_message);
    if (!snapshot.has_value()) {
        UpdateStatus(L"窗口截图失败。");
        MessageBoxW(window_, error_message.c_str(), L"截图失败", MB_OK | MB_ICONERROR);
        return false;
    }

    auto selected_window = window_selection_overlay_.SelectWindow(*snapshot, {}, error_message);
    if (!selected_window.has_value()) {
        if (!error_message.empty()) {
            UpdateStatus(L"窗口截图失败。");
            MessageBoxW(window_, error_message.c_str(), L"窗口选择失败", MB_OK | MB_ICONERROR);
        } else {
            UpdateStatus(L"已取消窗口截图。");
        }
        return false;
    }

    auto captured_window =
        window_capture_service_.CaptureWindow(*selected_window, &(*snapshot), error_message);
    if (!captured_window.has_value()) {
        UpdateStatus(L"窗口截图失败。");
        MessageBoxW(window_, error_message.c_str(), L"窗口采集失败", MB_OK | MB_ICONERROR);
        return false;
    }

    image_ = std::move(*captured_window);
    UpdateHotkeyLabels();
    UpdateActionState();
    UpdateStatus(L"窗口截图完成：" + selected_window->title + L" (" +
                 std::to_wstring(image_.Width()) + L" x " +
                 std::to_wstring(image_.Height()) + L")");
    InvalidateRect(window_, &preview_rect_, TRUE);
    return true;
}

void MainWindow::SaveCapture() {
    if (image_.IsEmpty()) {
        return;
    }

    std::wstring error_message;
    std::wstring saved_path;
    bool clipboard_failed = false;
    if (!SaveImageToConfiguredDirectory(image_, saved_path, error_message, clipboard_failed)) {
        MessageBoxW(window_, error_message.c_str(), L"保存失败", MB_OK | MB_ICONERROR);
        return;
    }

    if (clipboard_failed) {
        UpdateStatus(L"已保存到：" + saved_path + L"，但复制到剪贴板失败。");
        MessageBoxW(window_, error_message.c_str(), L"复制到剪贴板失败", MB_OK | MB_ICONWARNING);
        return;
    }

    UpdateStatus(std::wstring(L"已保存为 ") + FormatDisplayName(settings_.save_format) +
                 L"，并复制到剪贴板：" + saved_path);
}

void MainWindow::ClearCapture() {
    image_ = capture::CapturedImage{};
    UpdateActionState();
    UpdateStatus(L"截图已清空。");
    InvalidateRect(window_, &preview_rect_, TRUE);
}

void MainWindow::BeginHotkeyRecording(HotkeySlot slot) {
    recording_hotkey_slot_ = slot;
    UnregisterAllHotkeys();
    UpdateHotkeyLabels();
    UpdateStatus(std::wstring(L"请按下新的") + GetCaptureModeName(slot) +
                 L"快捷键组合，至少包含 Ctrl / Shift / Alt / Win 中的一种。");
    SetFocus(window_);
}

void MainWindow::CancelHotkeyRecording() {
    if (recording_hotkey_slot_ == HotkeySlot::None) {
        return;
    }

    recording_hotkey_slot_ = HotkeySlot::None;
    RegisterConfiguredHotkeys(true);
    UpdateStatus(L"已取消快捷键修改。");
}

void MainWindow::CommitRecordedHotkey(UINT virtual_key) {
    const HotkeySlot slot = recording_hotkey_slot_;
    if (slot == HotkeySlot::None) {
        return;
    }

    if (virtual_key == VK_ESCAPE) {
        CancelHotkeyRecording();
        return;
    }

    if (hotkey::IsModifierKey(virtual_key)) {
        return;
    }

    const hotkey::HotkeyDefinition definition = hotkey::BuildFromKeyboardState(virtual_key);
    if (!hotkey::IsValid(definition)) {
        MessageBoxW(window_,
                    L"快捷键必须至少包含一个修饰键，且最后一个按键不能只是 Ctrl / Shift / Alt / Win。",
                    L"快捷键无效",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (!ApplyRecordedHotkey(slot, definition)) {
        UpdateHotkeyLabels();
        return;
    }

    UpdateStatus(std::wstring(L"新的") + GetCaptureModeName(slot) +
                 L"快捷键已生效：" + hotkey::FormatHotkey(definition));
}

bool MainWindow::ApplyRecordedHotkey(HotkeySlot slot,
                                     const hotkey::HotkeyDefinition& definition) {
    for (const HotkeySlot other_slot : kConfigurableHotkeySlots) {
        if (other_slot == slot) {
            continue;
        }

        if (SameHotkey(definition, AccessHotkeyDefinition(settings_, other_slot))) {
            MessageBoxW(window_,
                        (std::wstring(L"该组合键已经分配给") + GetCaptureModeName(other_slot) +
                         L"，请使用其他组合键。")
                            .c_str(),
                        L"快捷键冲突",
                        MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    const settings::AppSettings previous_settings = settings_;
    AccessHotkeyDefinition(settings_, slot) = definition;
    recording_hotkey_slot_ = HotkeySlot::None;

    std::wstring error_summary;
    if (!RegisterConfiguredHotkeys(false, &error_summary)) {
        settings_ = previous_settings;
        RegisterConfiguredHotkeys(false);
        UpdateStatus(std::wstring(L"新的") + GetCaptureModeName(slot) +
                     L"快捷键注册失败，已恢复原设置。");
        MessageBoxW(window_,
                    (std::wstring(L"无法注册新的") + GetCaptureModeName(slot) +
                     L"快捷键，可能已被其他程序占用。\n\n" + error_summary)
                        .c_str(),
                    L"快捷键注册失败",
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    std::wstring error_message;
    if (!settings_repository_.Save(settings_, error_message)) {
        MessageBoxW(window_, error_message.c_str(), L"保存设置失败", MB_OK | MB_ICONWARNING);
    }

    return true;
}

bool MainWindow::RegisterConfiguredHotkeys(bool show_error_messages,
                                           std::wstring* error_summary) {
    UnregisterAllHotkeys();

    bool all_registered = true;
    std::vector<std::wstring> failed_messages;
    for (const HotkeySlot slot : kConfigurableHotkeySlots) {
        std::wstring error_message;
        if (hotkey_manager_.RegisterHotkey(window_,
                                           GetHotkeyIdentifier(slot),
                                           AccessHotkeyDefinition(settings_, slot),
                                           error_message)) {
            hotkey_registered_[HotkeySlotIndex(slot)] = true;
            continue;
        }

        all_registered = false;
        hotkey_registered_[HotkeySlotIndex(slot)] = false;
        failed_messages.push_back(std::wstring(GetCaptureModeName(slot)) + L"：" + error_message);
    }

    UpdateHotkeyLabels();

    if (error_summary != nullptr) {
        error_summary->clear();
        for (std::size_t index = 0; index < failed_messages.size(); ++index) {
            if (index > 0) {
                *error_summary += L"\n";
            }
            *error_summary += failed_messages[index];
        }
    }

    if (!all_registered && show_error_messages) {
        std::wstring message = L"以下快捷键未能注册：\n\n";
        for (const auto& failed_message : failed_messages) {
            message += L"- " + failed_message + L"\n";
        }
        MessageBoxW(window_, message.c_str(), L"快捷键注册失败", MB_OK | MB_ICONWARNING);
    }

    return all_registered;
}

void MainWindow::UnregisterAllHotkeys() {
    for (const HotkeySlot slot : kConfigurableHotkeySlots) {
        hotkey_manager_.UnregisterHotkey(window_, GetHotkeyIdentifier(slot));
        hotkey_registered_[HotkeySlotIndex(slot)] = false;
    }
}

bool MainWindow::TriggerCaptureByHotkey(int hotkey_identifier) {
    if (recording_hotkey_slot_ != HotkeySlot::None) {
        return false;
    }

    const bool should_restore = hidden_to_tray_;
    bool captured = false;
    switch (hotkey_identifier) {
    case kFullHotkeyId:
        captured = CaptureDesktop();
        break;
    case kRegionHotkeyId:
        captured = CaptureRegion();
        break;
    case kWindowHotkeyId:
        captured = CaptureWindow();
        break;
    default:
        return false;
    }

    if (captured && should_restore) {
        ShowFromTray();
    }

    return captured;
}

void MainWindow::HideToTray() {
    if (hidden_to_tray_ || window_ == nullptr) {
        return;
    }

    hidden_to_tray_ = true;
    ShowWindow(window_, SW_HIDE);
    if (!tray_hint_shown_) {
        tray_icon_controller_.ShowBalloon(L"原生 Win32 截图工具",
                                          L"程序已隐藏到托盘，双击托盘图标可恢复主窗口。");
        tray_hint_shown_ = true;
    }
}

void MainWindow::ShowFromTray() {
    hidden_to_tray_ = false;
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
}

void MainWindow::ExitApplication() {
    exiting_ = true;
    hidden_to_tray_ = false;
    DestroyWindow(window_);
}

void MainWindow::HandleTrayCommand(TrayMenuCommand command) {
    switch (command) {
    case TrayMenuCommand::ShowWindow:
        ShowFromTray();
        break;
    case TrayMenuCommand::CaptureRegion: {
        const bool should_restore = hidden_to_tray_;
        if (CaptureRegion() && should_restore) {
            ShowFromTray();
        }
        break;
    }
    case TrayMenuCommand::CaptureFull: {
        const bool should_restore = hidden_to_tray_;
        if (CaptureDesktop() && should_restore) {
            ShowFromTray();
        }
        break;
    }
    case TrayMenuCommand::CaptureWindow: {
        const bool should_restore = hidden_to_tray_;
        if (CaptureWindow() && should_restore) {
            ShowFromTray();
        }
        break;
    }
    case TrayMenuCommand::ExitApplication:
        ExitApplication();
        break;
    case TrayMenuCommand::None:
    default:
        break;
    }
}

void MainWindow::DrawPreview(HDC hdc, const RECT& bounds) const {
    RECT content = bounds;
    InflateRect(&content, -1, -1);

    const int available_width = content.right - content.left;
    const int available_height = content.bottom - content.top;
    if (available_width <= 0 || available_height <= 0) {
        return;
    }

    const double width_ratio =
        static_cast<double>(available_width) / static_cast<double>(image_.Width());
    const double height_ratio =
        static_cast<double>(available_height) / static_cast<double>(image_.Height());
    const double scale = std::min(width_ratio, height_ratio);

    const int draw_width = std::max(1, static_cast<int>(std::lround(image_.Width() * scale)));
    const int draw_height = std::max(1, static_cast<int>(std::lround(image_.Height() * scale)));
    const int draw_left = content.left + (available_width - draw_width) / 2;
    const int draw_top = content.top + (available_height - draw_height) / 2;

    const BITMAPINFO bitmap_info = image_.CreateBitmapInfo();

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(hdc,
                  draw_left,
                  draw_top,
                  draw_width,
                  draw_height,
                  0,
                  0,
                  image_.Width(),
                  image_.Height(),
                  image_.Pixels().data(),
                  &bitmap_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

void MainWindow::DrawEmptyState(HDC hdc, const RECT& bounds) const {
    RECT text_rect = bounds;
    InflateRect(&text_rect, -24, -24);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(90, 90, 90));
    DrawTextW(hdc,
              L"当前没有截图。\n\n可使用“全屏截图”“选区截图”“窗口截图”，\n或直接按对应的全局快捷键。",
              -1,
              &text_rect,
              DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE:
        if (!CreateChildControls()) {
            return -1;
        }
        InitializeHotkeySettings();
        InitializeTrayIcon();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* min_max_info = reinterpret_cast<MINMAXINFO*>(l_param);
        min_max_info->ptMinTrackSize.x = kMinWindowWidth;
        min_max_info->ptMinTrackSize.y = kMinWindowHeight;
        return 0;
    }

    case WM_SIZE:
        if (w_param == SIZE_MINIMIZED) {
            HideToTray();
            return 0;
        }
        LayoutControls(LOWORD(l_param), HIWORD(l_param));
        return 0;

    case WM_HOTKEY:
        if (TriggerCaptureByHotkey(static_cast<int>(w_param))) {
            return 0;
        }
        break;

    case kTrayIconMessage:
        switch (l_param) {
        case WM_LBUTTONDBLCLK:
            ShowFromTray();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT point{};
            GetCursorPos(&point);
            if (auto command = tray_icon_controller_.ShowContextMenu(window_, point);
                command.has_value()) {
                HandleTrayCommand(*command);
            }
            return 0;
        }
        default:
            break;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (recording_hotkey_slot_ != HotkeySlot::None) {
            CommitRecordedHotkey(static_cast<UINT>(w_param));
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(w_param) == kSaveFormatComboId && HIWORD(w_param) == CBN_SELCHANGE) {
            ApplySaveFormatSelection();
            return 0;
        }

        switch (LOWORD(w_param)) {
        case kFullCaptureButtonId:
            CaptureDesktop();
            return 0;
        case kRegionCaptureButtonId:
            CaptureRegion();
            return 0;
        case kWindowCaptureButtonId:
            CaptureWindow();
            return 0;
        case kSaveDirectoryButtonId: {
            std::wstring selected_directory;
            if (!ChooseSaveDirectory(selected_directory)) {
                return 0;
            }

            const std::wstring previous_directory = settings_.save_directory;
            settings_.save_directory = selected_directory;

            std::wstring error_message;
            if (!settings_repository_.Save(settings_, error_message)) {
                settings_.save_directory = previous_directory;
                MessageBoxW(window_, error_message.c_str(), L"保存设置失败", MB_OK | MB_ICONWARNING);
                return 0;
            }

            UpdateSaveSettingsDisplay();
            UpdateStatus(L"保存目录已更新为：" + settings_.save_directory);
            return 0;
        }
        case kSaveButtonId:
            SaveCapture();
            return 0;
        case kClearButtonId:
            ClearCapture();
            return 0;
        case kFullHotkeyButtonId:
            BeginHotkeyRecording(HotkeySlot::FullCapture);
            return 0;
        case kRegionHotkeyButtonId:
            BeginHotkeyRecording(HotkeySlot::RegionCapture);
            return 0;
        case kWindowHotkeyButtonId:
            BeginHotkeyRecording(HotkeySlot::WindowCapture);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        if (!exiting_) {
            HideToTray();
            return 0;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT paint_struct{};
        HDC hdc = BeginPaint(window_, &paint_struct);

        RECT client_rect{};
        GetClientRect(window_, &client_rect);
        FillRect(hdc, &client_rect, GetSysColorBrush(COLOR_WINDOW));

        HBRUSH preview_brush = CreateSolidBrush(RGB(245, 247, 250));
        FillRect(hdc, &preview_rect_, preview_brush);
        DeleteObject(preview_brush);
        FrameRect(hdc, &preview_rect_, GetSysColorBrush(COLOR_3DSHADOW));

        if (image_.IsEmpty()) {
            DrawEmptyState(hdc, preview_rect_);
        } else {
            DrawPreview(hdc, preview_rect_);
        }

        EndPaint(window_, &paint_struct);
        return 0;
    }

    case WM_DESTROY:
        tray_icon_controller_.RemoveIcon();
        UnregisterAllHotkeys();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        std::wstring error_message;
        tray_icon_controller_.ReAddIcon(window_, kTrayIconMessage, error_message);
        return 0;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

}  // namespace ui
