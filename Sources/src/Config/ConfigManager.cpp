#include "ConfigManager.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace JeriBot {

namespace {

std::wstring utf8ToWide(const std::string& value)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), len) <= 0) return {};
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

std::string wideToUtf8(const wchar_t* value, DWORD len)
{
    if (!value || len == 0) return {};
    int u8Len = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(len),
                                    nullptr, 0, nullptr, nullptr);
    if (u8Len <= 0) return {};
    std::string utf8(static_cast<size_t>(u8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(len),
                        utf8.data(), u8Len, nullptr, nullptr);
    return utf8;
}

} // namespace

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
    llm["Max_Tokens"] = 4096;
    llm["Temperature"] = 0.7;
    llm["Top_P"] = 1.0;
    cfg["LLM"] = std::move(llm);
    Json store = Json::Object{};
    store["Source"] = "china";
    Json sources = Json::Object{};
    sources["china"] = "https://raw.giteeusercontent.com/PYLinTech/JeriStore/raw/main/";
    sources["global"] = "https://raw.githubusercontent.com/PYLinTech/JeriStore/main/";
    sources["custom"] = "env:JERIBOT_STORE_URL";
    store["Sources"] = std::move(sources);
    cfg["Store"] = std::move(store);
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
    if (!ensureStoreConfig(error)) return false;
    return true;
}

bool ConfigManager::ensureStoreConfig(std::string& error)
{
    Json defaults = defaultConfig();
    const Json& defaultStore = defaults["Store"];
    const Json& defaultSources = defaultStore["Sources"];
    bool changed = false;

    if (!config_.contains("Store") || !config_["Store"].isObject()) {
        config_["Store"] = defaultStore;
        changed = true;
    }

    Json& store = config_["Store"];
    auto isValidSource = [](const std::string& source) {
        return source == "china" || source == "global" || source == "custom";
    };
    if (!store.contains("Source") || !store["Source"].isString()
        || !isValidSource(store["Source"].asString())) {
        store["Source"] = defaultStore["Source"];
        changed = true;
    }

    Json existingSources = store.contains("Sources") && store["Sources"].isObject()
                           ? store["Sources"] : Json::Object{};
    Json normalizedSources = Json::Object{};
    const char* fixedSources[] = {"china", "global", "custom"};
    for (const char* key : fixedSources) {
        if (existingSources.contains(key) && existingSources[key].isString()) {
            normalizedSources[key] = existingSources[key].asString();
        } else {
            normalizedSources[key] = defaultSources[key].asString();
        }
    }

    if (!store.contains("Sources") || !store["Sources"].isObject()
        || store["Sources"].dump() != normalizedSources.dump()) {
        store["Sources"] = std::move(normalizedSources);
        changed = true;
    }

    if (changed && !save(error)) {
        return false;
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

Json& ConfigManager::mutableConfig()
{
    return config_;
}

bool ConfigManager::save(std::string& error)
{
    std::ofstream ofs(configFilePath_, std::ios::binary);
    if (!ofs) {
        error = "无法写入配置文件";
        return false;
    }
    std::string content = config_.dump(2);
    ofs.write(content.c_str(), static_cast<std::streamsize>(content.size()));
    if (!ofs) {
        error = "写入配置文件失败";
        return false;
    }
    return true;
}

void ConfigManager::resolveEnvVars()
{
    static const std::string envPrefix = "env:";

    auto resolveObject = [&](Json& obj) {
        const Json::Object& ref = obj.asObject();
        std::vector<std::pair<std::string, std::string>> resolved;

        for (const auto& [key, value] : ref) {
            if (!value.isString()) continue;
            const std::string& str = value.asString();
            if (str.size() <= envPrefix.size() || str.compare(0, envPrefix.size(), envPrefix) != 0) continue;

            std::string varName = str.substr(envPrefix.size());

            std::wstring wVarName = utf8ToWide(varName);
            if (wVarName.empty()) continue;

            wchar_t buffer[32767];
            DWORD len = GetEnvironmentVariableW(wVarName.c_str(), buffer, 32767);
            if (len == 0) continue;

            std::string utf8 = wideToUtf8(buffer, len);
            if (!utf8.empty()) resolved.emplace_back(key, std::move(utf8));
        }

        for (auto& [k, v] : resolved) {
            obj[k] = std::move(v);
        }
    };

    if (config_.contains("LLM") && config_["LLM"].isObject()) {
        resolveObject(config_["LLM"]);
    }

}

} // namespace JeriBot
