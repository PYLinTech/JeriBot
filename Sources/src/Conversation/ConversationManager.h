#pragma once

#include <string>

namespace JeriBot {

class ConversationManager {
public:
    bool initialize(const std::string& configDir, std::string& error);
    bool addGroup(const std::string& name, std::string& error);
    bool renameGroup(const std::string& oldName, const std::string& newName, std::string& error);
    bool deleteGroup(const std::string& name, std::string& error);
    std::string newConversation(std::string group, std::string& outId, std::string& error);
    const std::string& conversationDir() const;
    const std::string& groupsDir() const;

private:
    std::string conversationDir_;
    std::string groupsDir_;
};

} // namespace JeriBot