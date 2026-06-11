/**
 * TCU log uploader entry.
 * BY ZF
 */

#include "tcu_uploader_process.h"

#include <csignal>
#include <iostream>
#include <string>

namespace {
TcuUploaderProcess* g_process = nullptr;

// BY ZF: forward stop signal to the process instance.
void handleSignal(int)
{
    if (g_process) {
        g_process->stop();
    }
}
}

int main(int argc, char* argv[])
{
    std::string configPath = "/usr/app/config/tcu_uploader.ini";
    bool once = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--once") {
            once = true;
        } else {
            configPath = arg;
        }
    }

    TcuUploaderProcess process(configPath);
    g_process = &process;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (!process.initialize()) {
        std::cerr << "[tcu_uploader] initialize failed" << std::endl;
        return 1;
    }

    const int rc = process.run(once);
    g_process = nullptr;
    return rc;
}
