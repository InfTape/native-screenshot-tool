#include <Windows.h>

#include "app/App.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow) {
    app::App application(instance);
    return application.Run(nCmdShow);
}
