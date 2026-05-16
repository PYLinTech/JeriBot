#include "ConversationManager.h"
#include "Json/Json.h"

#include <chrono>
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

std::filesystem::path ConversationManager::convFilePath(const std::string& group, const std::string& id) const
{
    std::filesystem::path basePath = std::filesystem::u8path(groupsDir_);
    return basePath / std::filesystem::u8path(group) / (id + ".json");
}

bool ConversationManager::writeFile(const std::filesystem::path& path, const std::string& data, std::string& error)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        error = "无法写入文件";
        return false;
    }
    ofs.write(data.c_str(), static_cast<std::streamsize>(data.size()));
    if (!ofs) {
        error = "写入文件失败";
        return false;
    }
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

bool ConversationManager::deleteConversation(const std::string& group, const std::string& id, std::string& error)
{
    std::filesystem::path filePath = convFilePath(group, id);

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        error = "会话不存在";
        return false;
    }

    if (!std::filesystem::remove(filePath, ec)) {
        error = "删除会话失败：" + ec.message();
        return false;
    }

    return true;
}

bool ConversationManager::renameConversation(const std::string& group, const std::string& id, const std::string& newName, std::string& error)
{
    std::filesystem::path filePath = convFilePath(group, id);

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        error = "会话不存在";
        return false;
    }

    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs) {
        error = "无法读取会话文件";
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    std::string parseErr;
    Json doc = Json::parse(content, parseErr);
    if (!parseErr.empty() || !doc.isObject()) {
        error = "会话文件格式无效";
        return false;
    }

    doc["name"] = newName;
    doc["updateTime"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    return writeFile(filePath, doc.dump(2), error);
}

std::string ConversationManager::newConversation(std::string group, std::string& outId, std::string& error)
{
    if (group.empty()) group = "Default";

    std::filesystem::path basePath = std::filesystem::u8path(groupsDir_);
    std::filesystem::path groupPath = basePath / std::filesystem::u8path(group);

    std::error_code ec;
    if (!std::filesystem::exists(groupPath, ec)) {
        error = "分组不存在";
        return {};
    }

    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string id = std::to_string(ts);

    Json doc = Json::Object{};
    doc["id"] = id;
    doc["name"] = std::string();
    doc["updateTime"] = static_cast<double>(ts);
    doc["data"] = Json::Array{};

    std::filesystem::path filePath = convFilePath(group, id);
    if (!writeFile(filePath, doc.dump(2), error)) {
        return {};
    }

    outId = id;
    return group;
}

std::vector<GroupInfo> ConversationManager::listGroups(std::string& error)
{
    std::vector<GroupInfo> result;
    std::filesystem::path base = std::filesystem::u8path(groupsDir_);

    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (!entry.is_directory()) continue;

        GroupInfo group;
        auto u8Name = entry.path().filename().u8string();
        group.name = {u8Name.begin(), u8Name.end()};

        std::error_code ec2;
        for (const auto& file : std::filesystem::directory_iterator(entry.path(), ec2)) {
            if (!file.is_regular_file()) continue;
            auto ext = file.path().extension();
            if (ext != L".json") continue;

            std::ifstream ifs(file.path(), std::ios::binary);
            if (!ifs) continue;
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            ifs.close();

            std::string parseErr;
            Json doc = Json::parse(content, parseErr);
            if (!parseErr.empty() || !doc.isObject()) continue;

            ConversationInfo conv;
            if (doc.contains("id") && doc["id"].isString()) conv.id = doc["id"].asString();
            if (doc.contains("name") && doc["name"].isString()) conv.name = doc["name"].asString();
            if (doc.contains("updateTime") && doc["updateTime"].isNumber()) conv.updateTime = static_cast<long long>(doc["updateTime"].asNumber());
            group.conversations.push_back(std::move(conv));
        }

        result.push_back(std::move(group));
    }

    return result;
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