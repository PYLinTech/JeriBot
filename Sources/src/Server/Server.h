#pragma once

#include <cstdint>
#include <string>

namespace JeriBot {

class ConversationManager;
class StoreManager;

class Server {
public:
    Server(uint16_t port, ConversationManager* conversationMgr, StoreManager* storeMgr);
    ~Server();

    bool start(std::string& error);
    void run();
    void stop();

private:
    struct Impl;
    Impl* p_;
};

void openBrowser(uint16_t port);

} // namespace JeriBot