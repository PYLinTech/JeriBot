#include "Server.h"
#include "resource_ids.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <sstream>
#include <string>
#include <string_view>

namespace JeriBot {

struct Server::Impl {
    uint16_t port = 0;
    SOCKET listenSocket = INVALID_SOCKET;
    bool running = false;
    bool wsaInitialized = false;
};

namespace {

struct ResourceEntry {
    const char* urlPath;
    int resourceId;
    const char* contentType;
};

const ResourceEntry resourceMap[] = {
    {"/",             RES_INDEX_HTML,      "text/html; charset=utf-8"},
    {"/index.html",   RES_INDEX_HTML,      "text/html; charset=utf-8"},
    {"/style.css",    RES_STYLE_CSS,       "text/css; charset=utf-8"},
    {"/app.js",       RES_APP_JS,          "application/javascript; charset=utf-8"},
    {"/remixicon.css", RES_REMIXICON_CSS,  "text/css; charset=utf-8"},
    {"/remixicon.woff2", RES_REMIXICON_WOFF2, "font/woff2"},
    {"/favicon.ico",  RES_FAVICON_ICO,     "image/x-icon"},
};

constexpr size_t resourceMapSize = sizeof(resourceMap) / sizeof(resourceMap[0]);

std::string buildResponse(int code, const char* status,
                          const char* ctype, const void* body, size_t bodySize)
{
    std::string header;
    header.reserve(256 + bodySize);
    header += "HTTP/1.1 ";
    header += std::to_string(code);
    header += ' ';
    header += status;
    header += "\r\nContent-Type: ";
    header += ctype;
    header += "\r\nContent-Length: ";
    header += std::to_string(bodySize);
    header += "\r\nConnection: close\r\n\r\n";
    header.append(static_cast<const char*>(body), bodySize);
    return header;
}

std::string buildTextResponse(int code, const char* status,
                               const char* ctype, const std::string& text)
{
    return buildResponse(code, status, ctype, text.c_str(), text.size());
}

bool loadResource(int id, const void*& outPtr, size_t& outSize)
{
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(hMod, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(hMod, hRes);
    if (size == 0) return false;

    void* ptr = LockResource(hData);
    if (!ptr) return false;

    outPtr = ptr;
    outSize = static_cast<size_t>(size);
    return true;
}

std::string handleGet(const std::string& requestPath)
{
    std::string path = requestPath;
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    if (path.find("..") != std::string::npos) {
        return buildTextResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden");
    }

    for (size_t i = 0; i < resourceMapSize; ++i) {
        if (path == resourceMap[i].urlPath) {
            const void* data = nullptr;
            size_t size = 0;
            if (loadResource(resourceMap[i].resourceId, data, size)) {
                return buildResponse(200, "OK", resourceMap[i].contentType, data, size);
            }
            break;
        }
    }

    return buildTextResponse(404, "Not Found", "text/plain; charset=utf-8", "404 Not Found");
}

std::string processRequest(std::string_view data)
{
    auto lf = data.find('\n');
    if (lf == std::string_view::npos) {
        return buildTextResponse(400, "Bad Request", "text/plain; charset=utf-8", "400 Bad Request");
    }

    std::string line(data.substr(0, lf));
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string method, path;
    {
        std::istringstream iss(line);
        iss >> method >> path;
    }

    if (method != "GET") {
        return buildTextResponse(405, "Method Not Allowed", "text/plain; charset=utf-8", "405 Method Not Allowed");
    }

    auto qpos = path.find('?');
    if (qpos != std::string::npos) path.resize(qpos);

    return handleGet(path);
}

constexpr size_t RECV_BUF_SIZE = 8192;

} // namespace

Server::Server(uint16_t port)
    : p_(new Impl{})
{
    p_->port = port;
}

Server::~Server()
{
    stop();
    delete p_;
}

bool Server::start(std::string& error)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "WSA 初始化失败";
        return false;
    }
    p_->wsaInitialized = true;

    p_->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (p_->listenSocket == INVALID_SOCKET) {
        error = "创建套接字失败";
        return false;
    }

    BOOL optval = TRUE;
    setsockopt(p_->listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(p_->port);

    if (bind(p_->listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        error = "绑定端口失败，端口可能已被占用";
        return false;
    }

    if (listen(p_->listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        error = "监听失败";
        return false;
    }

    p_->running = true;
    return true;
}

void Server::run()
{
    while (p_->running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(p_->listenSocket, &readfds);

        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(0, &readfds, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR) break;
        if (sel == 0) continue;

        if (!FD_ISSET(p_->listenSocket, &readfds)) continue;

        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(p_->listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) continue;

        char buf[RECV_BUF_SIZE];
        int received = recv(client, buf, static_cast<int>(sizeof(buf)) - 1, 0);
        if (received > 0) {
            buf[received] = '\0';
            std::string response = processRequest(std::string_view(buf, static_cast<size_t>(received)));
            send(client, response.c_str(), static_cast<int>(response.size()), 0);
        }

        closesocket(client);
    }
}

void Server::stop()
{
    if (!p_) return;
    p_->running = false;
    if (p_->listenSocket != INVALID_SOCKET) {
        closesocket(p_->listenSocket);
        p_->listenSocket = INVALID_SOCKET;
    }
    if (p_->wsaInitialized) {
        WSACleanup();
        p_->wsaInitialized = false;
    }
}

void openBrowser(uint16_t port)
{
    std::wstring url = L"http://localhost:" + std::to_wstring(port);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace JeriBot