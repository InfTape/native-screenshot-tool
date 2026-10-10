#include <Windows.h>

#include "app/App.h"

#if defined(_MSC_VER)
// Use themed controls instead of the legacy common-control renderer.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow) {
    app::App application(instance);
    return application.Run(nCmdShow);
}
