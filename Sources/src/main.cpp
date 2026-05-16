#include <windows.h>

#include "Config/ConfigManager.h"
#include "Error/ErrorManager.h"
#include "Server/Server.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"JeriBot_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);

        JeriBot::ConfigManager config;
        std::string error;
        if (config.initialize(error)) {
            uint16_t port = static_cast<uint16_t>(config.config()["Port"].asInt());
            JeriBot::openBrowser(port);
        }
        return 0;
    }

    JeriBot::ConfigManager config;
    std::string error;
    if (!config.initialize(error)) {
        JeriBot::showError("配置初始化失败：" + error);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }

    const JeriBot::Json& cfg = config.config();
    uint16_t port = static_cast<uint16_t>(cfg["Port"].asInt());

    JeriBot::Server server(port);
    if (!server.start(error)) {
        JeriBot::showError("服务启动失败：" + error);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }

    JeriBot::openBrowser(port);
    server.run();

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}