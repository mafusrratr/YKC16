/**
 * ChargeLogic process entry
 * BY ZF
 */

#include "charge_logic_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

ChargeLogicProcess* g_process = nullptr;

void signalHandler(int sig)
{
    std::cout << "ChargeLogic process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[])
{
    std::cout << "=== ChargeLogic Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // BY ZF: 固定配置路径
    const char* configPath = "/usr/app/config/tcu_logic.ini";

    ChargeLogicProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize ChargeLogic process" << std::endl;
        return 1;
    }

    if (process.start() != 0) {
        std::cerr << "Failed to start ChargeLogic process" << std::endl;
        return 1;
    }

    std::cout << "ChargeLogic process started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    while (process.getStatus() != PROC_STATUS_STOPPED) {
        sleep(1);
    }

    std::cout << "ChargeLogic process stopped" << std::endl;
    return 0;
}
