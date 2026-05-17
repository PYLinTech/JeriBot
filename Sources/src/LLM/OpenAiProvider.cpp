#include "OpenAiProvider.h"
#include "Json/Json.h"

#include <string>

namespace JeriBot {

static const char* roleToString(LlmRole role)
{
    switch (role) {
    case LlmRole::System: return "system";
    case LlmRole::User: return "user";
    case LlmRole::Assistant: return "assistant";
    }
    return "user";
}

OpenAiProvider::OpenAiProvider(const LlmConfig& config, HttpClient* http)
    : config_(config), http_(http)
{
}

std::vector<HttpClient::Header> OpenAiProvider::buildHeaders()
{
    std::vector<HttpClient::Header> headers;
    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({"Authorization", "Bearer " + config_.apiKey});
    return headers;
}

std::string OpenAiProvider::buildRequestBody(const std::vector<LlmMessage>& messages, bool stream)
{
    Json body = Json::Object{};
    body["model"] = config_.modelId;
    body["stream"] = stream;

    if (config_.maxTokens.has_value()) body["max_tokens"] = config_.maxTokens.value();
    if (config_.temperature.has_value()) body["temperature"] = config_.temperature.value();
    if (config_.topP.has_value()) body["top_p"] = config_.topP.value();

    Json msgs = Json::Array{};
    for (const auto& m : messages) {
        Json msg = Json::Object{};
        msg["role"] = roleToString(m.role);
        msg["content"] = m.content;
        msgs.asArray().push_back(std::move(msg));
    }
    body["messages"] = std::move(msgs);

    return body.dump();
}

bool OpenAiProvider::parseChatResponse(const std::string& body, std::string& response, std::string& error)
{
    std::string parseErr;
    Json doc = Json::parse(body, parseErr);
    if (!parseErr.empty() || !doc.isObject()) {
        error = "解析响应 JSON 失败: " + parseErr;
        return false;
    }

    const Json& choices = doc["choices"];
    if (!choices.isArray() || choices.size() == 0) {
        // Check for error in response
        if (doc.contains("error") && doc["error"].isObject()) {
            const Json& err = doc["error"];
            if (err.contains("message") && err["message"].isString()) {
                error = err["message"].asString();
            } else {
                error = "API 返回错误";
            }
            return false;
        }
        error = "响应中没有 choices";
        return false;
    }

    const Json& first = choices[0];
    if (!first.isObject()) {
        error = "choice 不是对象";
        return false;
    }

    const Json& message = first["message"];
    if (!message.isObject() || !message.contains("content")) {
        error = "choice 中没有 message.content";
        return false;
    }

    response = message["content"].asString();
    return true;
}

bool OpenAiProvider::chat(const std::vector<LlmMessage>& messages,
                           std::string& response,
                           std::string& error)
{
    std::string body = buildRequestBody(messages, false);
    std::string url = config_.baseUrl + "/chat/completions";

    auto resp = http_->post(url, body, buildHeaders(), error);
    if (!error.empty()) return false;

    if (resp.statusCode != 200) {
        error = "HTTP " + std::to_string(resp.statusCode);
        // Try to extract error message from body
        std::string parseErr;
        Json doc = Json::parse(resp.body, parseErr);
        if (parseErr.empty() && doc.isObject() && doc.contains("error") && doc["error"].isObject()) {
            const Json& err = doc["error"];
            if (err.contains("message") && err["message"].isString()) {
                error += ": " + err["message"].asString();
            }
        } else {
            error += ": " + resp.body;
        }
        return false;
    }

    return parseChatResponse(resp.body, response, error);
}

bool OpenAiProvider::chatStream(const std::vector<LlmMessage>& messages,
                                 std::function<bool(const std::string& delta)> onChunk,
                                 std::string& error)
{
    std::string body = buildRequestBody(messages, true);
    std::string url = config_.baseUrl + "/chat/completions";

    std::string buffer;
    bool success = true;

    auto onData = [&](const char* data, size_t len) -> bool {
        buffer.append(data, len);

        // Split on "\n\n" (SSE message boundaries)
        size_t pos;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
            std::string eventBlock = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);

            // Parse "data: <json>" lines
            std::string jsonLine;
            size_t lineStart = 0;
            while (lineStart < eventBlock.size()) {
                size_t lineEnd = eventBlock.find('\n', lineStart);
                if (lineEnd == std::string::npos) lineEnd = eventBlock.size();
                std::string line = eventBlock.substr(lineStart, lineEnd - lineStart);

                // Trim trailing \r
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.compare(0, 6, "data: ") == 0) {
                    jsonLine = line.substr(6);
                }

                lineStart = lineEnd + 1;
                if (lineStart > eventBlock.size()) break;
            }

            if (jsonLine.empty()) continue;

            // Check for [DONE] sentinel
            if (jsonLine == "[DONE]") {
                return false; // stop reading
            }

            // Parse the JSON chunk
            std::string parseErr;
            Json chunk = Json::parse(jsonLine, parseErr);
            if (!parseErr.empty() || !chunk.isObject()) continue;

            const Json& choices = chunk["choices"];
            if (!choices.isArray() || choices.size() == 0) continue;

            const Json& first = choices[0];
            if (!first.isObject()) continue;

            const Json& delta = first["delta"];
            if (!delta.isObject() || !delta.contains("content")) continue;

            std::string text = delta["content"].asString();
            if (!text.empty()) {
                if (!onChunk(text)) {
                    return false; // user abort
                }
            }
        }
        return true; // continue reading
    };

    if (!http_->postStream(url, body, buildHeaders(), onData, error)) {
        return false;
    }

    return success;
}

} // namespace JeriBot
