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
    const auto body = BuildGoogleRpcBody(text);
    InternetHandle session{WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return NetworkFailure(stop);
    if (!WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000)) return NetworkFailure(stop);
    InternetHandle connection{WinHttpConnect(session.value, L"translate.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return NetworkFailure(stop);
    CancellableRequest request;
    request.value = WinHttpOpenRequest(connection.value, L"POST",
        L"/_/TranslateWebserverUi/data/batchexecute?rpcids=MkEWBc", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.value) return NetworkFailure(stop);
    // Keep a local value for WinHTTP operations; the stop callback can close it
    // to interrupt an in-flight synchronous request without blocking the UI.
    const auto handle = request.value;
    std::stop_callback cancellation(stop, [&request] { request.Close(); });
    if (stop.stop_requested()) return NetworkFailure(stop);
    if (!WinHttpSendRequest(handle, L"Content-Type: application/x-www-form-urlencoded\r\n", static_cast<DWORD>(-1),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
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
    return ParseGoogleRpcResponse(response);
}
}

std::string BuildGoogleRpcBody(std::wstring_view text) {
    // Name-based activation avoids C++/WinRT's process-wide factory cache.
    // A request may be the last MTA thread: cached factories become invalid
    // when that apartment shuts down, and cannot be reused by the next worker.
    const auto arrays = winrt::get_activation_factory<>(L"Windows.Data.Json.JsonArray");
    const auto values = winrt::get_activation_factory<IJsonValueStatics>(L"Windows.Data.Json.JsonValue");
    const auto nulls = winrt::get_activation_factory<IJsonValueStatics2>(L"Windows.Data.Json.JsonValue");
    auto arguments = arrays.ActivateInstance<JsonArray>();
    arguments.Append(values.CreateStringValue(winrt::hstring(text)));
    arguments.Append(values.CreateStringValue(L"en"));
    arguments.Append(values.CreateStringValue(L"zh-CN"));
    arguments.Append(values.CreateNumberValue(1));
    auto payload = arrays.ActivateInstance<JsonArray>();
    payload.Append(arguments); payload.Append(arrays.ActivateInstance<JsonArray>());
    auto rpc = arrays.ActivateInstance<JsonArray>();
    rpc.Append(values.CreateStringValue(L"MkEWBc"));
    rpc.Append(values.CreateStringValue(payload.Stringify()));
    rpc.Append(nulls.CreateNullValue());
    rpc.Append(values.CreateStringValue(L"generic"));
    auto batch = arrays.ActivateInstance<JsonArray>();
    auto envelope = arrays.ActivateInstance<JsonArray>();
    batch.Append(rpc); envelope.Append(batch);
    return "f.req=" + EncodeFormValue(winrt::to_string(envelope.Stringify()));
}

common::Result<std::wstring> ParseGoogleRpcResponse(std::string_view response) {
    try {
        const auto arrays = winrt::get_activation_factory<IJsonArrayStatics>(L"Windows.Data.Json.JsonArray");
        // Accept the web RPC's anti-XSSI prefix and optional numeric frame sizes.
        while (!response.empty()) {
            const auto newline = response.find('\n');
            const auto line = response.substr(0, newline);
            const auto first = line.find_first_not_of(" \r\t");
            if (first != std::string_view::npos && line[first] == '[') {
                JsonArray records{nullptr};
                if (arrays.TryParse(winrt::to_hstring(line.substr(first)), records)) {
                    for (const auto& record_value : records) {
                        if (record_value.ValueType() != JsonValueType::Array) continue;
                        const auto record = record_value.GetArray();
                        if (record.Size() < 3 || record.GetAt(1).ValueType() != JsonValueType::String ||
                            record.GetStringAt(1) != L"MkEWBc" || record.GetAt(2).ValueType() != JsonValueType::String) continue;
                        const auto root = arrays.Parse(record.GetStringAt(2));
                        const auto entry = root.GetArrayAt(1).GetArrayAt(0).GetArrayAt(0);
                        std::wstring translated;
                        if (entry.Size() > 5 && entry.GetAt(5).ValueType() == JsonValueType::Array) {
                            for (const auto& part : entry.GetArrayAt(5)) {
                                if (!translated.empty()) translated += L' ';
                                translated += part.GetArray().GetStringAt(0).c_str();
                            }
                        } else {
                            translated = entry.GetStringAt(0).c_str();
                        }
                        if (!translated.empty()) return common::Result<std::wstring>::Success(std::move(translated));
                    }
                }
            }
            if (newline == std::string_view::npos) break;
            response.remove_prefix(newline + 1);
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
