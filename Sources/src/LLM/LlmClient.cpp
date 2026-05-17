#include "LlmClient.h"
#include "AnthropicProvider.h"
#include "Config/ConfigManager.h"
#include "OpenAiProvider.h"

#include <algorithm>
#include <cctype>

namespace JeriBot {

static std::string toLower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

LlmClient::LlmClient() = default;

LlmClient::~LlmClient() = default;

bool LlmClient::ensureInitialized(std::string& error)
{
    if (initialized_) return true;

    ConfigManager cfgMgr;
    if (!cfgMgr.initialize(error)) return false;

    const Json& llmCfg = cfgMgr.config()["LLM"];
    if (!llmCfg.isObject()) {
        error = "配置中缺少 LLM 节";
        return false;
    }

    config_.providerCompatible = llmCfg["Provider_Compatible"].asString();
    config_.baseUrl            = llmCfg["Base_URL"].asString();
    config_.apiKey             = llmCfg["API_Key"].asString();
    config_.modelId            = llmCfg["Model_ID"].asString();

    if (llmCfg.contains("Max_Tokens") && llmCfg["Max_Tokens"].isNumber())
        config_.maxTokens = static_cast<int>(llmCfg["Max_Tokens"].asNumber());
    if (llmCfg.contains("Temperature") && llmCfg["Temperature"].isNumber())
        config_.temperature = llmCfg["Temperature"].asNumber();
    if (llmCfg.contains("Top_P") && llmCfg["Top_P"].isNumber())
        config_.topP = llmCfg["Top_P"].asNumber();

    std::string kind = toLower(config_.providerCompatible);
    if (kind == "anthropic") {
        provider_ = std::make_unique<AnthropicProvider>(config_, &http_);
    } else {
        provider_ = std::make_unique<OpenAiProvider>(config_, &http_);
    }

    initialized_ = true;
    return true;
}

bool LlmClient::chat(const std::vector<LlmMessage>& messages,
                      std::string& response,
                      std::string& error)
{
    if (!ensureInitialized(error)) return false;
    return provider_->chat(messages, response, error);
}

bool LlmClient::chatStream(const std::vector<LlmMessage>& messages,
                            std::function<bool(const std::string& delta)> onChunk,
                            std::string& error)
{
    if (!ensureInitialized(error)) return false;
    return provider_->chatStream(messages, std::move(onChunk), error);
}

} // namespace JeriBot
