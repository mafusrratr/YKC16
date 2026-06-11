/**
 * NBT33007 V2 tcu_comm process entry
 * BY ZF
 */

#include "comm_process.h"
#include "../../base/common/message_queue.h"
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <unistd.h>

CommProcess* g_process = nullptr;

static void feedDaemonWatchdog()
{
    // BY ZF: Feed monitor watchdog from main thread.
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_comm";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, std::strlen(processName));
    }
}

static void signalHandler(int sig)
{
    std::cout << "33007_V2 comm received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=== 33007_V2 Comm Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    const char* configPath = "/usr/app/config/tcu_comm.ini";
    CommProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize 33007_V2 comm process" << std::endl;
        return 1;
    }
    if (process.start() != 0) {
        std::cerr << "Failed to start 33007_V2 comm process" << std::endl;
        return 1;
    }

    std::chrono::steady_clock::time_point lastFeedTime =
            std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (process.getStatus() != PROC_STATUS_STOPPED) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedDaemonWatchdog();
            lastFeedTime = now;
        }
        sleep(1);
    }
    return 0;
}
