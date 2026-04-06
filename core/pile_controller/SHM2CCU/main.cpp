/**
 * SHM2CCU process entry.
 * BY ZF
 */

#include "pile_controller_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <thread>

volatile sig_atomic_t g_stopRequested = 0;

static void signalHandler(int sig)
{
    std::cout << "SHM2CCU process received signal: " << sig << std::endl;
    g_stopRequested = 1;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=== SHM2CCU Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    const char* configPath = "/usr/app/config/pile_controller.ini";

    PileControllerProcess process;
    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize SHM2CCU process" << std::endl;
        return 1;
    }

    std::thread worker([&process]() {
        if (process.start() != 0) {
            std::cerr << "Failed to start SHM2CCU process" << std::endl;
        }
    });

    bool stopIssued = false;
    while (process.getStatus() != PROC_STATUS_STOPPED &&
           process.getStatus() != PROC_STATUS_ERROR) {
        if (g_stopRequested != 0 && !stopIssued) {
            stopIssued = true;
            process.stop();
        }
        process.feedWatchdog();
        sleep(1);
    }

    if (worker.joinable()) {
        worker.join();
    }

    std::cout << "SHM2CCU process stopped" << std::endl;
    return 0;
}
