#include "ConfigManager.h"

#include <windows.h>
#include <filesystem>
#include <fstream>

namespace {

const char defaultConfigContent[] = R"({
  "App_Version": "0.1.0",
  "Provider_Compatible": "OpenAI",
  "Base_URL": "https://api.deepseek.com",
  "API_Key": "env:JERIBOT_API_KEY",
  "Model_ID": "deepseek-v4-flash"
}
)";

} // namespace

namespace JeriBot {

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
        std::ofstream ofs(jeriDir / L"JeriBot.json", std::ios::binary);
        if (!ofs) {
            error = "无法创建默认配置文件 JeriBot.json";
            return false;
        }
        ofs.write(defaultConfigContent, sizeof(defaultConfigContent) - 1);
        if (!ofs) {
            error = "写入默认配置文件失败";
            return false;
        }
    }

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

} // namespace JeriBot
