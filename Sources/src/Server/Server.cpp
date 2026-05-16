#include "Server.h"
#include "Conversation/ConversationManager.h"
#include "Json/Json.h"
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
    ConversationManager* conversationMgr = nullptr;
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

std::string buildJsonResponse(int code, const char* status, const std::string& json)
{
    return buildTextResponse(code, status, "application/json; charset=utf-8", json);
}

std::string jsonError(const std::string& msg)
{
    return R"({"result":"error","message":")" + msg + R"("})";
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

enum class HttpMethod { Get, Post, Other };

struct ParsedRequest {
    HttpMethod method;
    std::string path;
    std::string body;
};

ParsedRequest parseRequest(std::string_view data)
{
    ParsedRequest req;
    auto lf = data.find('\n');
    if (lf == std::string_view::npos) {
        req.method = HttpMethod::Other;
        return req;
    }

    std::string line(data.substr(0, lf));
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string methodStr;
    {
        std::istringstream iss(line);
        iss >> methodStr >> req.path;
    }

    if (methodStr == "GET") req.method = HttpMethod::Get;
    else if (methodStr == "POST") req.method = HttpMethod::Post;
    else req.method = HttpMethod::Other;

    for (char& c : req.path) {
        if (c == '\\') c = '/';
    }

    size_t headerEnd = data.find("\r\n\r\n");
    if (headerEnd != std::string_view::npos) {
        req.body = std::string(data.substr(headerEnd + 4));
    }

    return req;
}

bool ensurePost(const ParsedRequest& req, std::string& out)
{
    if (req.method == HttpMethod::Post) return true;
    out = buildJsonResponse(405, "Method Not Allowed", R"({"result":"error","message":"仅支持POST"})");
    return false;
}

bool parseJsonBody(const ParsedRequest& req, Json& outBody, std::string& outResp)
{
    std::string err;
    Json body = Json::parse(req.body, err);
    if (!err.empty() || !body.isObject()) {
        outResp = buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"请求体不是有效JSON"})");
        return false;
    }
    outBody = std::move(body);
    return true;
}

std::string handleApi(const ParsedRequest& req, ConversationManager* mgr)
{
    const std::string& path = req.path;

    if (path == "/api/conversation/add-group") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        if (!body.contains("name") || !body["name"].isString()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"缺少name字段"})");
        }
        const std::string& name = body["name"].asString();
        if (name.empty()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"分组名称不能为空"})");
        }
        std::string error;
        if (mgr->addGroup(name, error)) {
            return buildJsonResponse(200, "OK", R"({"result":"success"})");
        }
        return buildJsonResponse(400, "Bad Request", jsonError(error));
    }

    if (path == "/api/conversation/rename-group") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        if (!body.contains("old_name") || !body["old_name"].isString() ||
            !body.contains("new_name") || !body["new_name"].isString()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"缺少old_name或new_name字段"})");
        }
        const std::string& oldName = body["old_name"].asString();
        const std::string& newName = body["new_name"].asString();
        if (oldName.empty() || newName.empty()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"分组名称不能为空"})");
        }
        std::string error;
        if (mgr->renameGroup(oldName, newName, error)) {
            return buildJsonResponse(200, "OK", R"({"result":"success"})");
        }
        return buildJsonResponse(400, "Bad Request", jsonError(error));
    }

    if (path == "/api/conversation/delete-group") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        if (!body.contains("name") || !body["name"].isString()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"缺少name字段"})");
        }
        const std::string& name = body["name"].asString();
        if (name.empty()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"分组名称不能为空"})");
        }
        std::string error;
        if (mgr->deleteGroup(name, error)) {
            return buildJsonResponse(200, "OK", R"({"result":"success"})");
        }
        return buildJsonResponse(400, "Bad Request", jsonError(error));
    }

    if (path == "/api/conversation/new") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        std::string group;
        if (body.contains("group") && body["group"].isString()) {
            group = body["group"].asString();
        }
        std::string outId;
        std::string error;
        std::string resolvedGroup = mgr->newConversation(std::move(group), outId, error);
        if (!error.empty()) {
            return buildJsonResponse(400, "Bad Request", jsonError(error));
        }
        std::string json = R"({"result":"success","group":")" + resolvedGroup + R"(","id":")" + outId + R"("})";
        return buildJsonResponse(200, "OK", json);
    }

    if (path == "/api/conversation/delete") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        if (!body.contains("group") || !body["group"].isString() ||
            !body.contains("id") || !body["id"].isString()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"缺少group或id字段"})");
        }
        const std::string& group = body["group"].asString();
        const std::string& id = body["id"].asString();
        if (group.empty() || id.empty()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"group和id不能为空"})");
        }
        std::string error;
        if (mgr->deleteConversation(group, id, error)) {
            return buildJsonResponse(200, "OK", R"({"result":"success"})");
        }
        return buildJsonResponse(400, "Bad Request", jsonError(error));
    }

    if (path == "/api/conversation/rename") {
        std::string resp;
        if (!ensurePost(req, resp)) return resp;
        Json body;
        if (!parseJsonBody(req, body, resp)) return resp;
        if (!body.contains("group") || !body["group"].isString() ||
            !body.contains("id") || !body["id"].isString() ||
            !body.contains("name") || !body["name"].isString()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"缺少group、id或name字段"})");
        }
        const std::string& group = body["group"].asString();
        const std::string& id = body["id"].asString();
        const std::string& name = body["name"].asString();
        if (group.empty() || id.empty()) {
            return buildJsonResponse(400, "Bad Request", R"({"result":"error","message":"group和id不能为空"})");
        }
        std::string error;
        if (mgr->renameConversation(group, id, name, error)) {
            return buildJsonResponse(200, "OK", R"({"result":"success"})");
        }
        return buildJsonResponse(400, "Bad Request", jsonError(error));
    }

    if (path == "/api/conversation/list") {
        if (req.method != HttpMethod::Get) {
            return buildJsonResponse(405, "Method Not Allowed", R"({"result":"error","message":"仅支持GET"})");
        }
        std::string error;
        auto groups = mgr->listGroups(error);
        if (!error.empty()) {
            return buildJsonResponse(400, "Bad Request", jsonError(error));
        }
        std::string json = R"({"result":"success","data":[)";
        for (size_t i = 0; i < groups.size(); ++i) {
            const auto& g = groups[i];
            if (i > 0) json += ",";
            json += R"({"group":")" + g.name + R"(","conversations":[)";
            for (size_t j = 0; j < g.conversations.size(); ++j) {
                const auto& c = g.conversations[j];
                if (j > 0) json += ",";
                json += R"({"id":")" + c.id + R"(","name":")" + c.name + R"(","updateTime":)" + std::to_string(c.updateTime) + "}";
            }
            json += "]}";
        }
        json += "]}";
        return buildJsonResponse(200, "OK", json);
    }

    return buildJsonResponse(404, "Not Found", R"({"result":"error","message":"未知的API路径"})");
}

} // namespace

Server::Server(uint16_t port, ConversationManager* conversationMgr)
    : p_(new Impl{})
{
    p_->port = port;
    p_->conversationMgr = conversationMgr;
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

        std::string received;
        received.resize(8192);
        int len = recv(client, &received[0], static_cast<int>(received.size()) - 1, 0);
        if (len > 0) {
            received.resize(static_cast<size_t>(len));
            ParsedRequest req = parseRequest(received);

            std::string response;
            if (req.path.compare(0, 5, "/api/") == 0) {
                response = handleApi(req, p_->conversationMgr);
            } else if (req.method == HttpMethod::Get) {
                response = handleGet(req.path);
            } else {
                response = buildTextResponse(405, "Method Not Allowed", "text/plain; charset=utf-8", "405 Method Not Allowed");
            }
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