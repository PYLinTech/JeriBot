#include "ErrorManager.h"

#include <windows.h>

namespace JeriBot {

void showError(const std::string& message)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wide[0], len);

    MessageBoxW(nullptr, wide.c_str(), L"杰睿 JeriBot", MB_OK | MB_ICONERROR);
}

} // namespace JeriBot
