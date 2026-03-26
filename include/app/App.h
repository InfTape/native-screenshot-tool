#pragma once

#include <Windows.h>

namespace app {

class App {
public:
    explicit App(HINSTANCE instance);

    int Run(int nCmdShow);

private:
    HINSTANCE instance_;
};

}  // namespace app
