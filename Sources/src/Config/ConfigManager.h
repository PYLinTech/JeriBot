#pragma once

#include <string>

namespace JeriBot {

class ConfigManager {
public:
    bool initialize(std::string& error);
    const std::string& configDir() const;
    std::string configFilePath() const;

private:
    std::string configDir_;
    std::string configFilePath_;
};

} // namespace JeriBot
