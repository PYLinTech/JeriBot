#include <windows.h>

#include "Config/ConfigManager.h"
#include "Error/ErrorManager.h"
#include "Server/Server.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();

    JeriBot::ConfigManager config;
    std::string error;
    if (!config.initialize(error)) {
        JeriBot::showError("配置初始化失败：" + error);
        return 1;
    }

    const JeriBot::Json& cfg = config.config();
    uint16_t port = static_cast<uint16_t>(cfg["Port"].asInt());

    JeriBot::Server server(port);
    if (!server.start(error)) {
        JeriBot::showError("服务启动失败：" + error);
        return 1;
    }

    JeriBot::openBrowser(port);
    server.run();

    return 0;
}