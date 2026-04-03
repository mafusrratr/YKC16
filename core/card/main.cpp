/**
 * Card process entry
 * BY ZF
 */

#include "card_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

static CardProcess* g_process = NULL;

static void signalHandler(int sig)
{
    std::cout << "Card process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    std::cout << "=== Card Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    const char* configPath = "/usr/app/config/tcu_card.ini";

    CardProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize Card process" << std::endl;
        return 1;
    }

    if (process.start() != 0) {
        std::cerr << "Failed to start Card process" << std::endl;
        return 1;
    }

    while (process.getStatus() != PROC_STATUS_STOPPED) {
        sleep(1);
    }

    return 0;
}
