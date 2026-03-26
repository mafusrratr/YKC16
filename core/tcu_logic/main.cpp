/**
 * ChargeLogic process entry
 * BY ZF
 */

#include "charge_logic_process.h"
#include "../base/common/message_queue.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <chrono>

ChargeLogicProcess* g_process = nullptr;

static void feedDaemonWatchdog()
{
    // BY ZF: 通过守护进程看门狗消息队列上报 tcu_logic 存活状态。
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_logic";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
    }
}

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

    std::cout << "ChargeLogic process stopped" << std::endl;
    return 0;
}
