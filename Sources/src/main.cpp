#include <windows.h>

#include "Config/ConfigManager.h"
#include "Error/ErrorManager.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();

    JeriBot::ConfigManager config;
    std::string error;
    if (!config.initialize(error)) {
        JeriBot::showError("配置初始化失败：" + error);
        return 1;
    }
    return 0;
}
