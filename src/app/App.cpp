#include "app/App.h"

#include <commctrl.h>

#include "common/DpiAwareness.h"
#include "common/Win32Error.h"
#include "ui/MainWindow.h"

namespace app {

App::App(HINSTANCE instance) : instance_(instance) {}

int App::Run(int nCmdShow) {
    common::EnableDpiAwareness();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    ui::MainWindow main_window(instance_);
    if (!main_window.Create()) {
        const std::wstring error_message =
            L"创建主窗口失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        MessageBoxW(nullptr, error_message.c_str(), L"启动失败", MB_OK | MB_ICONERROR);
        return -1;
    }

    main_window.Show(nCmdShow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

}  // namespace app
