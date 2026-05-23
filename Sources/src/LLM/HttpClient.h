#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace JeriBot {

class HttpClient {
public:
    struct Header {
        std::string name;
        std::string value;
    };

    struct Response {
        int statusCode = 0;
        std::string body;
    };

    // Non-streaming GET: retrieve a resource, return full response.
    Response get(const std::string& url,
                 const std::vector<Header>& headers,
                 std::string& error);

    // Non-streaming POST: send JSON body, return full response.
    Response post(const std::string& url,
                  const std::string& body,
                  const std::vector<Header>& headers,
                  std::string& error);

    // Streaming POST: onData callback receives raw bytes as they arrive.
    // Return false from onData to abort early.
    bool postStream(const std::string& url,
                    const std::string& body,
                    const std::vector<Header>& headers,
                    std::function<bool(const char* data, size_t len)> onData,
                    std::string& error);

private:
    bool parseUrl(const std::string& url,
                  std::wstring& host,
                  uint16_t& port,
                  std::wstring& path,
                  bool& secure,
                  std::string& error);

    Response sendRequest(const wchar_t* method,
                         const std::string& url,
                         const std::string* body,
                         const std::vector<Header>& headers,
                         std::string& error);
};

} // namespace JeriBot
