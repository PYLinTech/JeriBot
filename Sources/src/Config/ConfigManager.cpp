#include "ConfigManager.h"

#include <windows.h>
#include <filesystem>
#include <fstream>

namespace JeriBot {

static Json defaultConfig()
{
    Json cfg = Json::Object{};
    cfg["App_Version"] = "0.1.0";
    cfg["Port"] = 11111;
    Json llm = Json::Object{};
    llm["Provider_Compatible"] = "OpenAI";
    llm["Base_URL"] = "https://api.deepseek.com";
    llm["API_Key"] = "env:JERIBOT_API_KEY";
    llm["Model_ID"] = "deepseek-v4-flash";
    cfg["LLM"] = std::move(llm);
    return cfg;
}

bool ConfigManager::saveDefaultConfig(std::string& error)
{
    std::ofstream ofs(configFilePath_, std::ios::binary);
    if (!ofs) {
        error = "无法创建默认配置文件 JeriBot.json";
        return false;
    }
    std::string content = defaultConfig().dump(2);
    ofs.write(content.c_str(), static_cast<std::streamsize>(content.size()));
    if (!ofs) {
        error = "写入默认配置文件失败";
        return false;
    }
    return true;
}

bool ConfigManager::loadConfig(std::string& error)
{
    config_ = Json::loadFile(configFilePath_, error);
    if (!error.empty() || !config_.isObject()
        || !config_.contains("Port") || !config_["Port"].isNumber()
        || !config_.contains("LLM") || !config_["LLM"].isObject()) {
        std::filesystem::path cfgPath = std::filesystem::u8path(configFilePath_);
        if (std::filesystem::exists(cfgPath)) {
            std::error_code ec;
            std::filesystem::remove(cfgPath, ec);
        }
        if (!saveDefaultConfig(error) || !(config_ = Json::loadFile(configFilePath_, error), error.empty())) {
            error = "配置初始化失败，尝试修复后仍无法读取：" + error;
            return false;
        }
    }
    return true;
}

bool ConfigManager::initialize(std::string& error)
{
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
        error = "无法读取 LocalAppData 环境变量";
        return false;
    }

    std::filesystem::path base(localAppData);
    std::filesystem::path jeriDir = base / L"JeriBot";

    std::error_code ec;
    if (!std::filesystem::exists(jeriDir, ec)) {
        if (!std::filesystem::create_directories(jeriDir, ec)) {
            error = "无法创建 JeriBot 配置目录：" + ec.message();
            return false;
        }
    } else if (!std::filesystem::is_directory(jeriDir, ec)) {
        error = "JeriBot 路径已存在但不是目录";
        return false;
    }

    auto u8Dir = jeriDir.u8string();
    configDir_ = {u8Dir.begin(), u8Dir.end()};
    auto u8Path = (jeriDir / L"JeriBot.json").u8string();
    configFilePath_ = {u8Path.begin(), u8Path.end()};

    if (!std::filesystem::exists(jeriDir / L"JeriBot.json", ec)) {
        if (!saveDefaultConfig(error)) return false;
    }

    if (!loadConfig(error)) return false;

    resolveEnvVars();

    return true;
}

const std::string& ConfigManager::configDir() const
{
    return configDir_;
}

std::string ConfigManager::configFilePath() const
{
    return configFilePath_;
}

const Json& ConfigManager::config() const
{
    return config_;
}

void ConfigManager::resolveEnvVars()
{
    static const std::string envPrefix = "env:";

    const Json::Object& obj = config_["LLM"].asObject();
    std::vector<std::pair<std::string, std::string>> resolved;

    for (const auto& [key, value] : obj) {
        if (!value.isString()) continue;
        const std::string& str = value.asString();
        if (str.size() <= envPrefix.size() || str.compare(0, envPrefix.size(), envPrefix) != 0) continue;

        std::string varName = str.substr(envPrefix.size());

        int wLen = MultiByteToWideChar(CP_UTF8, 0, varName.c_str(), -1, nullptr, 0);
        if (wLen <= 0) continue;
        std::wstring wVarName(static_cast<size_t>(wLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, varName.c_str(), -1, wVarName.data(), wLen);

        wchar_t buffer[32767];
        DWORD len = GetEnvironmentVariableW(wVarName.c_str(), buffer, 32767);
        if (len == 0) continue;

        int u8Len = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len),
                                        nullptr, 0, nullptr, nullptr);
        if (u8Len <= 0) continue;
        std::string utf8(static_cast<size_t>(u8Len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len),
                           utf8.data(), u8Len, nullptr, nullptr);

        resolved.emplace_back(key, std::move(utf8));
    }

    for (auto& [k, v] : resolved) {
        config_["LLM"][k] = std::move(v);
    }
}

} // namespace JeriBot