#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace JeriBot {

enum class LlmRole {
    System,
    User,
    Assistant,
};

struct LlmMessage {
    LlmRole role = LlmRole::User;
    std::string content;
};

struct LlmConfig {
    std::string providerCompatible;  // "OpenAI" or "Anthropic"
    std::string baseUrl;
    std::string apiKey;
    std::string modelId;
    std::optional<int> maxTokens;
    std::optional<double> temperature;
    std::optional<double> topP;
};

// Abstract base for LLM API providers.
class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    // Non-streaming chat: send messages, return full assistant response.
    virtual bool chat(const std::vector<LlmMessage>& messages,
                      std::string& response,
                      std::string& error) = 0;

    // Streaming chat: invoke onChunk with each text delta.
    // Return false from onChunk to abort early.
    virtual bool chatStream(const std::vector<LlmMessage>& messages,
                            std::function<bool(const std::string& delta)> onChunk,
                            std::string& error) = 0;
};

} // namespace JeriBot
