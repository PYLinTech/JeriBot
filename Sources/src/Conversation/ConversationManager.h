#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace JeriBot {

struct ConversationInfo {
    std::string id;
    std::string name;
    long long updateTime;
};

struct GroupInfo {
    std::string name;
    std::vector<ConversationInfo> conversations;
};

class ConversationManager {
public:
    bool initialize(const std::string& configDir, std::string& error);
    bool addGroup(const std::string& name, std::string& error);
    bool renameGroup(const std::string& oldName, const std::string& newName, std::string& error);
    bool deleteGroup(const std::string& name, std::string& error);
    std::string newConversation(std::string group, std::string& outId, std::string& error);
    bool deleteConversation(const std::string& group, const std::string& id, std::string& error);
    bool renameConversation(const std::string& group, const std::string& id, const std::string& newName, std::string& error);
    std::vector<GroupInfo> listGroups(std::string& error);
    const std::string& conversationDir() const;
    const std::string& groupsDir() const;

private:
    std::filesystem::path convFilePath(const std::string& group, const std::string& id) const;
    bool writeFile(const std::filesystem::path& path, const std::string& data, std::string& error);

    std::string conversationDir_;
    std::string groupsDir_;
};

} // namespace JeriBot