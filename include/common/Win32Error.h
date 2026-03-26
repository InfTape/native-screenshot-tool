#pragma once

#include <Windows.h>

#include <string>

namespace common {

std::wstring GetLastErrorMessage(DWORD error_code);

}  // namespace common
