#include <Windows.h>

#include "app/App.h"
#include "ocr/OcrWorker.h"

#if defined(_MSC_VER)
// Use themed controls instead of the legacy common-control renderer.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow) {
    if (const auto worker = ocr::RunWorkerIfRequested()) return *worker;
    app::App application(instance);
    return application.Run(nCmdShow);
}
