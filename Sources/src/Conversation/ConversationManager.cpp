#include "ConversationManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace JeriBot {

static bool isValidGroupName(const std::string& name)
{
    if (name.empty()) return false;
    static const char forbidden[] = R"(\/:*?"<>|\)";
    for (unsigned char c : name) {
        if (c < 32) return false;
        for (char f : forbidden) {
            if (c == f) return false;
        }
    }
    if (name == "." || name == "..") return false;
    return true;
}

bool ConversationManager::initialize(const std::string& configDir, std::string& error)
{
    std::filesystem::path base = std::filesystem::u8path(configDir);
    std::filesystem::path groupsPath = base / L"Conversations";
    std::filesystem::path convDir = groupsPath / L"Default";

    std::error_code ec;
    if (!std::filesystem::exists(convDir, ec)) {
        if (!std::filesystem::create_directories(convDir, ec)) {
            error = "无法创建会话目录：" + ec.message();
            return false;
        }
    } else if (!std::filesystem::is_directory(convDir, ec)) {
        error = "会话路径已存在但不是目录";
        return false;
    }

    auto u8ConvDir = convDir.u8string();
    conversationDir_ = {u8ConvDir.begin(), u8ConvDir.end()};
    auto u8GroupsDir = groupsPath.u8string();
    groupsDir_ = {u8GroupsDir.begin(), u8GroupsDir.end()};

    return true;
}

bool ConversationManager::addGroup(const std::string& name, std::string& error)
{
    if (!isValidGroupName(name)) {
        error = "分组名称无效";
        return false;
    }

    std::filesystem::path basePath = std::filesystem::u8path(groupsDir_);
    std::filesystem::path groupPath = basePath / std::filesystem::u8path(name);

    std::error_code ec;
    if (std::filesystem::exists(groupPath, ec)) {
        error = "分组已存在";
        return false;
    }

    if (!std::filesystem::create_directory(groupPath, ec)) {
        error = "创建分组失败：" + ec.message();
        return false;
    }

    return true;
}

bool ConversationManager::renameGroup(const std::string& oldName, const std::string& newName, std::string& error)
{
    if (!isValidGroupName(newName)) {
        error = "新分组名称无效";
        return false;
    }

    std::filesystem::path base = std::filesystem::u8path(groupsDir_);
    std::filesystem::path oldPath = base / std::filesystem::u8path(oldName);
    std::filesystem::path newPath = base / std::filesystem::u8path(newName);

    std::error_code ec;
    if (!std::filesystem::exists(oldPath, ec)) {
        error = "原分组不存在";
        return false;
    }

    if (std::filesystem::exists(newPath, ec)) {
        error = "目标分组名称已存在";
        return false;
    }

    std::filesystem::rename(oldPath, newPath, ec);
    if (ec) {
        error = "重命名分组失败：" + ec.message();
        return false;
    }

    return true;
}

bool ConversationManager::deleteGroup(const std::string& name, std::string& error)
{
    std::filesystem::path base = std::filesystem::u8path(groupsDir_);
    std::filesystem::path groupPath = base / std::filesystem::u8path(name);

    std::error_code ec;
    if (!std::filesystem::exists(groupPath, ec)) {
        error = "分组不存在";
        return false;
    }

    std::filesystem::remove_all(groupPath, ec);
    if (ec) {
        error = "删除分组失败：" + ec.message();
        return false;
    }

    return true;
}

static std::string generateId()
{
    auto now = std::chrono::system_clock::now();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count());
}

std::string ConversationManager::newConversation(std::string group, std::string& outId, std::string& error)
{
    if (group.empty()) group = "Default";

    std::filesystem::path base = std::filesystem::u8path(groupsDir_);
    std::filesystem::path groupPath = base / std::filesystem::u8path(group);

    std::error_code ec;
    if (!std::filesystem::exists(groupPath, ec)) {
        error = "分组不存在";
        return {};
    }

    std::string id = generateId();
    auto ts = std::stoll(id);

    std::string jsonStr =
        "{\n"
        "  \"id\": \"" + id + "\",\n"
        "  \"name\": \"\",\n"
        "  \"updateTime\": " + std::to_string(ts) + ",\n"
        "  \"data\": []\n"
        "}";

    std::filesystem::path filePath = groupPath / (id + ".json");
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs) {
        error = "无法创建会话文件";
        return {};
    }
    ofs.write(jsonStr.c_str(), static_cast<std::streamsize>(jsonStr.size()));
    if (!ofs) {
        error = "写入会话文件失败";
        return {};
    }

    outId = id;
    return group;
}

const std::string& ConversationManager::conversationDir() const
{
    return conversationDir_;
}

const std::string& ConversationManager::groupsDir() const
{
    return groupsDir_;
}

} // namespace JeriBot