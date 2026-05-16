#pragma once

#include <cstdint>
#include <string>

namespace JeriBot {

class Server {
public:
    Server(uint16_t port);
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