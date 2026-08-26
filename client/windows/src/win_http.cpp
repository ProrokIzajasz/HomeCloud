#include "win_http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <stdexcept>
#include <vector>

using namespace std;
using namespace homecloud_client;

namespace homecloud_windows {
namespace {

wstring widen(const string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
};

[[noreturn]] void fail(const char* message) {
    throw runtime_error(string(message) + " (Windows: " + to_string(GetLastError()) + ")");
}

} // namespace

WinHttpTransport::WinHttpTransport(wstring base_url) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(base_url.c_str(), 0, 0, &parts)) fail("Invalid server address");
    host_.assign(parts.lpszHostName, parts.dwHostNameLength);
    base_path_.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (base_path_ == L"/") base_path_.clear();
    port_ = parts.nPort;
    secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;
}

HttpResponse WinHttpTransport::send(const HttpRequest& request) const {
    InternetHandle session{WinHttpOpen(L"HomeCloud/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) fail("Cannot start network connection");
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
    InternetHandle connection{WinHttpConnect(session, host_.c_str(), port_, 0)};
    if (!connection.value) fail("Cannot connect to HomeCloud");

    const wchar_t* verb = request.method == HttpMethod::get ? L"GET" :
                          request.method == HttpMethod::post ? L"POST" : L"DELETE";
    const wstring path = base_path_ + widen(request.path);
    InternetHandle handle{WinHttpOpenRequest(connection, verb, path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              secure_ ? WINHTTP_FLAG_SECURE : 0)};
    if (!handle.value) fail("Cannot prepare request");

    wstring headers;
    for (const auto& [name, value] : request.headers)
        headers += widen(name) + L": " + widen(value) + L"\r\n";
    const void* body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : request.body.data();
    if (!WinHttpSendRequest(handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1L),
                            const_cast<void*>(body), static_cast<DWORD>(request.body.size()),
                            static_cast<DWORD>(request.body.size()), 0) ||
        !WinHttpReceiveResponse(handle, nullptr)) fail("HomeCloud did not answer");

    DWORD status{};
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    HttpResponse response;
    response.status = static_cast<int>(status);
    for (;;) {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(handle, &available)) fail("Cannot read response");
        if (!available) break;
        const auto offset = response.body.size();
        response.body.resize(offset + available);
        DWORD read{};
        if (!WinHttpReadData(handle, response.body.data() + offset, available, &read))
            fail("Cannot read response");
        response.body.resize(offset + read);
    }
    return response;
}

} // namespace homecloud_windows
