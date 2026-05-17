#pragma once

#include <string>
#include "Json/Json.h"

namespace JeriBot {

class ConfigManager {
public:
    bool initialize(std::string& error);
    const std::string& configDir() const;
    std::string configFilePath() const;
    const Json& config() const;

private:
    std::string configDir_;
    std::string configFilePath_;
    Json config_;

    bool loadConfig(std::string& error);
    bool saveDefaultConfig(std::string& error);
    void resolveEnvVars();
};

} // namespace JeriBot