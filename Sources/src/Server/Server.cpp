#include "Server.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
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

std::string contentTypeFor(const std::string& path)
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    return "application/octet-stream";
}

std::string buildResponse(int code, const std::string& status,
                          const std::string& ctype, const std::string& body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << status << "\r\n"
        << "Content-Type: " << ctype << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::wstring toWide(const std::string& narrow)
{
    if (narrow.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, &wide[0], len);
    wide.pop_back();
    return wide;
}

bool loadEmbeddedResource(const std::string& name, std::string& out)
{
    HMODULE hMod = GetModuleHandleW(nullptr);
    std::wstring wideName = toWide(name);
    HRSRC hRes = FindResourceW(hMod, wideName.c_str(), L"ASSETS");
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(hMod, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(hMod, hRes);
    if (size == 0) return false;

    void* ptr = LockResource(hData);
    if (!ptr) return false;

    out.assign(static_cast<const char*>(ptr), size);
    return true;
}

std::string handleGet(const std::string& requestPath)
{
    std::string path = requestPath;
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    if (path.find("..") != std::string::npos) {
        return buildResponse(403, "Forbidden", "text/plain; charset=utf-8", "403 Forbidden");
    }
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    if (path.empty()) path = "index.html";

    std::string content;
    if (!loadEmbeddedResource(path, content)) {
        return buildResponse(404, "Not Found", "text/plain; charset=utf-8", "404 Not Found");
    }

    std::string ctype = contentTypeFor(path);
    return buildResponse(200, "OK", ctype, content);
}

std::string processRequest(std::string_view data)
{
    auto lf = data.find('\n');
    if (lf == std::string_view::npos) {
        return buildResponse(400, "Bad Request", "text/plain; charset=utf-8", "400 Bad Request");
    }

    std::string line(data.substr(0, lf));
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string method, path;
    {
        std::istringstream iss(line);
        iss >> method >> path;
    }

    if (method != "GET") {
        return buildResponse(405, "Method Not Allowed", "text/plain; charset=utf-8", "405 Method Not Allowed");
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