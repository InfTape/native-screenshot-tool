#include "translation/GoogleTranslationService.h"

#include <Windows.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <mutex>
#include <utility>

namespace translation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr std::size_t kMaxTextLength = 5000;
constexpr std::size_t kMaxResponseBytes = 4 * 1024 * 1024;

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
struct CancellableRequest {
    HINTERNET value = nullptr;
    std::mutex mutex;
    void Close() {
        std::lock_guard lock(mutex);
        if (const auto handle = std::exchange(value, nullptr)) WinHttpCloseHandle(handle);
    }
    ~CancellableRequest() { Close(); }
};
std::string EncodeFormValue(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%'; encoded += hex[ch >> 4]; encoded += hex[ch & 15];
        }
    }
    return encoded;
}
common::Result<std::wstring> NetworkFailure(std::stop_token stop, DWORD error = GetLastError()) {
    if (stop.stop_requested()) return common::Result<std::wstring>::Failure(L"已取消翻译。");
    return common::Result<std::wstring>::Failure(
        L"无法连接谷歌翻译，请检查网络后重试。（错误 " + std::to_wstring(error) + L"）");
}
common::Result<std::wstring> TranslateChunk(std::wstring_view text, std::stop_token stop) {
    if (stop.stop_requested()) return NetworkFailure(stop);
    const auto path = BuildGoogleTranslationPath(text);
    InternetHandle session{WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return NetworkFailure(stop);
    if (!WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000)) return NetworkFailure(stop);
    InternetHandle connection{WinHttpConnect(session.value, L"translate.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return NetworkFailure(stop);
    CancellableRequest request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.value) return NetworkFailure(stop);
    // Keep a local value for WinHTTP operations; the stop callback can close it
    // to interrupt an in-flight synchronous request without blocking the UI.
    const auto handle = request.value;
    std::stop_callback cancellation(stop, [&request] { request.Close(); });
    if (stop.stop_requested()) return NetworkFailure(stop);
    if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(handle, nullptr)) return NetworkFailure(stop);
    DWORD status = 0, status_size = sizeof(status);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) return NetworkFailure(stop);
    if (status != 200) return common::Result<std::wstring>::Failure(status == 429 ?
        L"谷歌翻译请求过于频繁，请稍后重试。" : L"谷歌翻译暂时不可用。（HTTP " + std::to_wstring(status) + L"）");
    std::string response;
    char buffer[8192];
    for (;;) {
        if (stop.stop_requested()) return NetworkFailure(stop);
        DWORD read = 0;
        if (!WinHttpReadData(handle, buffer, sizeof(buffer), &read)) return NetworkFailure(stop);
        if (!read) break;
        if (response.size() + read > kMaxResponseBytes)
            return common::Result<std::wstring>::Failure(L"谷歌翻译返回的数据过大。");
        response.append(buffer, read);
    }
    if (stop.stop_requested()) return NetworkFailure(stop);
    return ParseGoogleTranslationResponse(response);
}
}

std::wstring BuildGoogleTranslationPath(std::wstring_view text) {
    // Encode the UTF-8 bytes of q, including literal &, +, %, # and newlines.
    // Use Pot's legacy endpoint parameters; both languages stay explicit.
    return L"/translate_a/single?dt=at&dt=bd&dt=ex&dt=ld&dt=md&dt=qca&dt=rw&dt=rm&dt=ss&dt=t"
        L"&client=gtx&sl=en&tl=zh-CN&hl=zh-CN&ie=UTF-8&oe=UTF-8&otf=1&ssel=0&tsel=0&kc=7&q=" +
        std::wstring(winrt::to_hstring(EncodeFormValue(winrt::to_string(winrt::hstring(text)))));
}

common::Result<std::wstring> ParseGoogleTranslationResponse(std::string_view response) {
    try {
        // Name-based activation avoids cached factories surviving the last MTA
        // worker and pointing into freed memory when the next request starts.
        const auto arrays = winrt::get_activation_factory<IJsonArrayStatics>(L"Windows.Data.Json.JsonArray");
        JsonArray root{nullptr};
        if (arrays.TryParse(winrt::to_hstring(response), root) && root.Size() > 0 &&
                root.GetAt(0).ValueType() == JsonValueType::Array) {
            std::wstring translated;
            for (const auto& value : root.GetArrayAt(0)) {
                if (value.ValueType() != JsonValueType::Array) continue;
                const auto row = value.GetArray();
                // Other columns contain source text, pronunciation and metadata.
                if (row.Size() > 0 && row.GetAt(0).ValueType() == JsonValueType::String)
                    translated += row.GetStringAt(0).c_str();
            }
            const auto first = translated.find_first_not_of(L" \t\r\n");
            if (first != std::wstring::npos) {
                const auto last = translated.find_last_not_of(L" \t\r\n");
                return common::Result<std::wstring>::Success(translated.substr(first, last - first + 1));
            }
        }
    } catch (const winrt::hresult_error&) {
        // An endpoint change must produce a visible error, not an empty result.
    }
    return common::Result<std::wstring>::Failure(L"无法读取谷歌译文，接口可能发生变化，请稍后重试。");
}

std::vector<std::wstring> SplitTranslationText(std::wstring_view text) {
    std::vector<std::wstring> chunks;
    while (!text.empty()) {
        auto count = std::min(text.size(), kMaxTextLength);
        if (count < text.size()) {
            if (text[count - 1] >= 0xD800 && text[count - 1] <= 0xDBFF) --count;
            const auto boundary = text.substr(0, count).find_last_of(L"\n \t");
            if (boundary != std::wstring_view::npos && boundary > count / 2) count = boundary + 1;
        }
        chunks.emplace_back(text.substr(0, count));
        text.remove_prefix(count);
    }
    return chunks;
}

common::Result<std::wstring> TranslateEnglishToChinese(std::wstring_view text, std::stop_token stop) {
    if (stop.stop_requested()) return NetworkFailure(stop);
    if (text.find_first_not_of(L" \t\r\n") == std::wstring_view::npos)
        return common::Result<std::wstring>::Failure(L"没有可翻译的文字。");
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        std::wstring translated;
        for (const auto& chunk : SplitTranslationText(text)) {
            if (chunk.find_first_not_of(L" \t\r\n") == std::wstring::npos) continue;
            auto result = TranslateChunk(chunk, stop);
            if (!result) return result;
            if (!translated.empty()) translated += L"\r\n";
            translated += result.Value();
        }
        return common::Result<std::wstring>::Success(std::move(translated));
    } catch (const winrt::hresult_error&) {
        return common::Result<std::wstring>::Failure(L"翻译失败，请稍后重试。");
    }
}
} // namespace translation
