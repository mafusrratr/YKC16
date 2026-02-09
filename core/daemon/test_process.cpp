/**
 * 测试外部进程
 * 用于测试守护进程的监控和看门狗功能
 * BY ZF
 */

#include "../base/common/message_queue.h"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <string>

// 全局变量
bool g_running = true;
std::string g_processName = "test_process";

/**
 * 信号处理函数
 */
void signalHandler(int sig) {
    std::cout << "Test process received signal: " << sig << std::endl;
    g_running = false;
}

/**
 * 喂狗函数
 */
void feedWatchdog() {
    MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    if (watchdogQueue.open()) {
        watchdogQueue.send(MSG_WATCHDOG_FEED, g_processName.c_str(), g_processName.length());
        std::cout << "Watchdog fed: " << g_processName << std::endl;
    } else {
        std::cout << "Failed to open watchdog queue" << std::endl;
    }
}

/**
 * 主函数
 */
int main(int argc, char *argv[])
{
    std::cout << "=== Test External Process ===" << std::endl;
    
    // 设置进程名称
    if (argc > 1) {
        g_processName = argv[1];
    }
    
    std::cout << "Process name: " << g_processName << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 主循环
    int counter = 0;
    while (g_running) {
        counter++;
        
        // 每5秒输出一次状态
        if (counter % 5 == 0) {
            std::cout << "Test process running, counter=" << counter << std::endl;
        }
        
        // 前10秒正常喂狗，之后停止喂狗测试看门狗超时
        if (counter <= 10 && counter % 2 == 0) {
            feedWatchdog();
        } else if (counter == 11) {
            std::cout << "Stopping watchdog feed to test timeout..." << std::endl;
        }
        
        // 模拟一些工作
        sleep(1);
        
        // 模拟异常退出（测试用）
        if (counter > 30 && counter % 10 == 0) {
            std::cout << "Simulating process exit for testing..." << std::endl;
            break;
        }
    }
    
    std::cout << "Test process exiting..." << std::endl;
    return 0;
}
