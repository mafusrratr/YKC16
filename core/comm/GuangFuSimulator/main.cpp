/**
 * tcu_comm (GuangxiDKY Modbus) entry
 * BY ZF
 */

#include "comm_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

CommProcess* g_process = nullptr;

static void signalHandler(int sig)
{
    std::cout << "Comm process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=== Comm Process (GuangxiDKY Modbus) ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    const char* configPath = "/usr/app/config/tcu_comm.ini";

    CommProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize Comm process" << std::endl;
        return 1;
    }
    if (process.start() != 0) {
        std::cerr << "Failed to start Comm process" << std::endl;
        return 1;
    }

    while (process.getStatus() != PROC_STATUS_STOPPED) {
        sleep(1);
    }
    return 0;
}
