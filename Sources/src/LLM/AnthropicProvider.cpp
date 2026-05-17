#include "AnthropicProvider.h"
#include "Json/Json.h"

#include <string>

namespace JeriBot {

AnthropicProvider::AnthropicProvider(const LlmConfig& config, HttpClient* http)
    : config_(config), http_(http)
{
}

std::vector<HttpClient::Header> AnthropicProvider::buildHeaders()
{
    std::vector<HttpClient::Header> headers;
    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({"x-api-key", config_.apiKey});
    headers.push_back({"anthropic-version", "2023-06-01"});
    return headers;
}

std::string AnthropicProvider::buildRequestBody(const std::vector<LlmMessage>& messages, bool stream)
{
    Json body = Json::Object{};
    body["model"] = config_.modelId;
    body["stream"] = stream;

    // max_tokens is required by Anthropic
    body["max_tokens"] = config_.maxTokens.value_or(4096);

    if (config_.temperature.has_value()) body["temperature"] = config_.temperature.value();
    if (config_.topP.has_value()) body["top_p"] = config_.topP.value();

    // Extract system message(s) from the messages array.
    // Anthropic expects system as a top-level field (string or array of text blocks).
    Json systemContent = Json::Array{};
    Json msgArray = Json::Array{};

    for (const auto& m : messages) {
        if (m.role == LlmRole::System) {
            Json textBlock = Json::Object{};
            textBlock["type"] = "text";
            textBlock["text"] = m.content;
            systemContent.asArray().push_back(std::move(textBlock));
        } else {
            Json msg = Json::Object{};
            msg["role"] = (m.role == LlmRole::Assistant) ? "assistant" : "user";
            msg["content"] = m.content;
            msgArray.asArray().push_back(std::move(msg));
        }
    }

    if (systemContent.size() > 0) {
        if (systemContent.size() == 1) {
            // Single system message: send as string for simplicity
            body["system"] = systemContent[0]["text"].asString();
        } else {
            body["system"] = std::move(systemContent);
        }
    }

    body["messages"] = std::move(msgArray);

    return body.dump();
}

bool AnthropicProvider::parseChatResponse(const std::string& body, std::string& response, std::string& error)
{
    std::string parseErr;
    Json doc = Json::parse(body, parseErr);
    if (!parseErr.empty() || !doc.isObject()) {
        error = "解析响应 JSON 失败: " + parseErr;
        return false;
    }

    // Check for error response
    if (doc.contains("error") && doc["error"].isObject()) {
        const Json& err = doc["error"];
        if (err.contains("message") && err["message"].isString()) {
            error = err["message"].asString();
        } else {
            error = "Anthropic API 返回错误";
        }
        return false;
    }

    const Json& content = doc["content"];
    if (!content.isArray() || content.size() == 0) {
        error = "响应中没有 content 数组";
        return false;
    }

    // Concatenate all text blocks
    std::string result;
    for (size_t i = 0; i < content.size(); ++i) {
        const Json& block = content[i];
        if (block.isObject() && block.contains("type") && block["type"].isString()) {
            const std::string& type = block["type"].asString();
            if (type == "text" && block.contains("text") && block["text"].isString()) {
                result += block["text"].asString();
            }
        }
    }

    response = std::move(result);
    return true;
}

bool AnthropicProvider::chat(const std::vector<LlmMessage>& messages,
                              std::string& response,
                              std::string& error)
{
    std::string body = buildRequestBody(messages, false);
    std::string url = config_.baseUrl + "/messages";

    auto resp = http_->post(url, body, buildHeaders(), error);
    if (!error.empty()) return false;

    if (resp.statusCode != 200) {
        error = "HTTP " + std::to_string(resp.statusCode);
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

bool AnthropicProvider::chatStream(const std::vector<LlmMessage>& messages,
                                    std::function<bool(const std::string& delta)> onChunk,
                                    std::string& error)
{
    std::string body = buildRequestBody(messages, true);
    std::string url = config_.baseUrl + "/messages";

    std::string buffer;

    auto onData = [&](const char* data, size_t len) -> bool {
        buffer.append(data, len);

        // Split on "\n\n" (SSE message boundaries)
        size_t pos;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
            std::string eventBlock = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);

            // Parse SSE event lines
            std::string eventType;
            std::string dataLine;
            size_t lineStart = 0;
            while (lineStart < eventBlock.size()) {
                size_t lineEnd = eventBlock.find('\n', lineStart);
                if (lineEnd == std::string::npos) lineEnd = eventBlock.size();
                std::string line = eventBlock.substr(lineStart, lineEnd - lineStart);
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.compare(0, 6, "event:") == 0) {
                    eventType = line.substr(6);
                    // Trim leading space
                    if (!eventType.empty() && eventType[0] == ' ') eventType.erase(0, 1);
                } else if (line.compare(0, 5, "data:") == 0) {
                    dataLine = line.substr(5);
                    if (!dataLine.empty() && dataLine[0] == ' ') dataLine.erase(0, 1);
                }

                lineStart = lineEnd + 1;
            }

            if (eventType == "message_stop") {
                return false; // stream complete
            }

            if (eventType != "content_block_delta" || dataLine.empty()) continue;

            // Parse the delta JSON
            std::string parseErr;
            Json delta = Json::parse(dataLine, parseErr);
            if (!parseErr.empty() || !delta.isObject()) continue;

            if (!delta.contains("type") || !delta["type"].isString()) continue;
            if (delta["type"].asString() != "content_block_delta") continue;

            if (!delta.contains("delta") || !delta["delta"].isObject()) continue;
            const Json& inner = delta["delta"];

            if (!inner.contains("type") || !inner["type"].isString()) continue;
            if (inner["type"].asString() != "text_delta") continue;

            if (!inner.contains("text") || !inner["text"].isString()) continue;

            std::string text = inner["text"].asString();
            if (!text.empty()) {
                if (!onChunk(text)) {
                    return false; // user abort
                }
            }
        }
        return true;
    };

    return http_->postStream(url, body, buildHeaders(), onData, error);
}

} // namespace JeriBot
