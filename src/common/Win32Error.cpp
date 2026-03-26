#include "common/Win32Error.h"

#include <string>

namespace common {

std::wstring GetLastErrorMessage(DWORD error_code) {
    if (error_code == 0) {
        return L"未提供错误码。";
    }

    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        error_code,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);

    if (length == 0 || buffer == nullptr) {
        return L"Win32 错误码: " + std::to_wstring(error_code);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    return L"Win32 错误码 " + std::to_wstring(error_code) + L": " + message;
}

}  // namespace common
