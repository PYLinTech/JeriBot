#pragma once

#include "HttpClient.h"
#include "LlmProvider.h"

#include <memory>

namespace JeriBot {

// High-level interface: auto-reads LLM config from JeriBot.json,
// creates the appropriate provider, and provides chat/chatStream.
class LlmClient {
public:
    LlmClient();
    ~LlmClient();

    // Non-streaming chat.
    bool chat(const std::vector<LlmMessage>& messages,
              std::string& response,
              std::string& error);

    // Streaming chat.
    bool chatStream(const std::vector<LlmMessage>& messages,
                    std::function<bool(const std::string& delta)> onChunk,
                    std::string& error);

    const LlmConfig& config() const { return config_; }

private:
    LlmConfig config_;
    HttpClient http_;
    std::unique_ptr<LlmProvider> provider_;
    bool initialized_ = false;

    bool ensureInitialized(std::string& error);
};

} // namespace JeriBot
