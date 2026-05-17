#include "HttpClient.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace JeriBot {

bool HttpClient::parseUrl(const std::string& url,
                          std::wstring& host,
                          uint16_t& port,
                          std::wstring& path,
                          bool& secure,
                          std::string& error)
{
    std::string remaining = url;

    // Scheme
    if (remaining.size() >= 8 && _strnicmp(remaining.c_str(), "https://", 8) == 0) {
        secure = true;
        port = 443;
        remaining = remaining.substr(8);
    } else if (remaining.size() >= 7 && _strnicmp(remaining.c_str(), "http://", 7) == 0) {
        secure = false;
        port = 80;
        remaining = remaining.substr(7);
    } else {
        error = "URL 缺少协议前缀 (http:// 或 https://)";
        return false;
    }

    // Host + optional port
    size_t slashPos = remaining.find('/');
    std::string hostPart;
    std::string pathPart;
    if (slashPos != std::string::npos) {
        hostPart = remaining.substr(0, slashPos);
        pathPart = remaining.substr(slashPos);
    } else {
        hostPart = remaining;
        pathPart = "/";
    }

    // Extract port from host if present
    size_t colonPos = hostPart.find(':');
    if (colonPos != std::string::npos) {
        std::string portStr = hostPart.substr(colonPos + 1);
        hostPart = hostPart.substr(0, colonPos);
        try {
            int p = std::stoi(portStr);
            if (p < 1 || p > 65535) {
                error = "端口号超出范围";
                return false;
            }
            port = static_cast<uint16_t>(p);
        } catch (...) {
            error = "端口号格式无效";
            return false;
        }
    }

    if (hostPart.empty()) {
        error = "URL 缺少主机名";
        return false;
    }

    // Convert to wide strings
    int hostLen = MultiByteToWideChar(CP_UTF8, 0, hostPart.c_str(), -1, nullptr, 0);
    host.resize(static_cast<size_t>(hostLen - 1));
    MultiByteToWideChar(CP_UTF8, 0, hostPart.c_str(), -1, &host[0], hostLen);

    int pathLen = MultiByteToWideChar(CP_UTF8, 0, pathPart.c_str(), -1, nullptr, 0);
    path.resize(static_cast<size_t>(pathLen - 1));
    MultiByteToWideChar(CP_UTF8, 0, pathPart.c_str(), -1, &path[0], pathLen);

    return true;
}

static std::wstring buildHeadersString(const std::vector<HttpClient::Header>& headers)
{
    std::wstring result;
    for (const auto& h : headers) {
        int nameLen = MultiByteToWideChar(CP_UTF8, 0, h.name.c_str(), -1, nullptr, 0);
        std::wstring wname(static_cast<size_t>(nameLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, h.name.c_str(), -1, &wname[0], nameLen);

        int valLen = MultiByteToWideChar(CP_UTF8, 0, h.value.c_str(), -1, nullptr, 0);
        std::wstring wval(static_cast<size_t>(valLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, h.value.c_str(), -1, &wval[0], valLen);

        result += wname + L": " + wval + L"\r\n";
    }
    return result;
}

HttpClient::Response HttpClient::post(const std::string& url,
                                       const std::string& body,
                                       const std::vector<Header>& headers,
                                       std::string& error)
{
    Response resp;

    std::wstring host, path;
    uint16_t port = 0;
    bool secure = false;
    if (!parseUrl(url, host, port, path, secure, error)) return resp;

    HINTERNET hSession = WinHttpOpen(L"JeriBot/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "WinHttpOpen 失败";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        error = "WinHttpConnect 失败";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        error = "WinHttpOpenRequest 失败";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    std::wstring wideHeaders = buildHeadersString(headers);
    BOOL ok = WinHttpSendRequest(hRequest,
                                  wideHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.c_str(),
                                  static_cast<DWORD>(wideHeaders.size()),
                                  const_cast<char*>(body.data()),
                                  static_cast<DWORD>(body.size()),
                                  static_cast<DWORD>(body.size()), 0);
    if (!ok) {
        error = "WinHttpSendRequest 失败";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        error = "WinHttpReceiveResponse 失败";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Read status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    resp.statusCode = static_cast<int>(statusCode);

    // Read body
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::string chunk(static_cast<size_t>(bytesAvailable), '\0');
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead)) {
            resp.body.append(chunk.data(), bytesRead);
        } else {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
}

bool HttpClient::postStream(const std::string& url,
                             const std::string& body,
                             const std::vector<Header>& headers,
                             std::function<bool(const char* data, size_t len)> onData,
                             std::string& error)
{
    std::wstring host, path;
    uint16_t port = 0;
    bool secure = false;
    if (!parseUrl(url, host, port, path, secure, error)) return false;

    HINTERNET hSession = WinHttpOpen(L"JeriBot/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "WinHttpOpen 失败";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        error = "WinHttpConnect 失败";
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        error = "WinHttpOpenRequest 失败";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring wideHeaders = buildHeadersString(headers);
    BOOL ok = WinHttpSendRequest(hRequest,
                                  wideHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.c_str(),
                                  static_cast<DWORD>(wideHeaders.size()),
                                  const_cast<char*>(body.data()),
                                  static_cast<DWORD>(body.size()),
                                  static_cast<DWORD>(body.size()), 0);
    if (!ok) {
        error = "WinHttpSendRequest 失败";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        error = "WinHttpReceiveResponse 失败";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Read status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode >= 400) {
        // Read error body non-streaming
        std::string errorBody;
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::string chunk(static_cast<size_t>(bytesAvailable), '\0');
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead)) {
                errorBody.append(chunk.data(), bytesRead);
            } else break;
        }
        error = "HTTP " + std::to_string(statusCode) + ": " + errorBody;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Streaming read loop
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
        if (bytesAvailable == 0) break;

        std::string chunk(static_cast<size_t>(bytesAvailable), '\0');
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead) || bytesRead == 0) {
            break;
        }

        if (!onData(chunk.data(), bytesRead)) {
            break; // caller requested abort
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}

} // namespace JeriBot
