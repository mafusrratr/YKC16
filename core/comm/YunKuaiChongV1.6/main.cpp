/**
 * tcu_comm YLC process entry
 * BY LZW
 */

#include "comm_process.h"
#include "../../base/common/message_queue.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <chrono>

CommProcess* g_process = nullptr;

static void feedDaemonWatchdog()
{
    // BY LZW: 通过守护进程看门狗消息队列上报 tcu_comm 存活状态。
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_comm";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
    }
}

static void signalHandler(int sig)
{
    std::cout << "YLC Comm process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=== YLC Comm Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // BY LZW: 固定配置路径
    const char* configPath = "/usr/app/config/tcu_comm.ini";

    CommProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize YLC Comm process" << std::endl;
        return 1;
    }
    if (process.start() != 0) {
        std::cerr << "Failed to start YLC Comm process" << std::endl;
        return 1;
    }

    std::chrono::steady_clock::time_point lastFeedTime = std::chrono::steady_clock::now()
        - std::chrono::seconds(5);
    while (process.getStatus() != PROC_STATUS_STOPPED) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if ((now - lastFeedTime) >= std::chrono::seconds(5)) {
            feedDaemonWatchdog();
            lastFeedTime = now;
        }
        sleep(1);
    }
    return 0;
}
