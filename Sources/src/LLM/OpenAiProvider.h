#pragma once

#include "HttpClient.h"
#include "LlmProvider.h"

namespace JeriBot {

class OpenAiProvider : public LlmProvider {
public:
    explicit OpenAiProvider(const LlmConfig& config, HttpClient* http);

    bool chat(const std::vector<LlmMessage>& messages,
              std::string& response,
              std::string& error) override;

    bool chatStream(const std::vector<LlmMessage>& messages,
                    std::function<bool(const std::string& delta)> onChunk,
                    std::string& error) override;

private:
    LlmConfig config_;
    HttpClient* http_;

    std::string buildRequestBody(const std::vector<LlmMessage>& messages, bool stream);
    bool parseChatResponse(const std::string& body, std::string& response, std::string& error);
    std::vector<HttpClient::Header> buildHeaders();
};

} // namespace JeriBot
