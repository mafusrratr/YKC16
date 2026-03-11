/**
 * 主控通信进程入口
 * BY ZF
 */

#include "pile_controller_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <thread>

volatile sig_atomic_t g_stopRequested = 0;

void signalHandler(int sig) {
    std::cout << "PileController process received signal: " << sig << std::endl;
    // BY ZF: 信号处理函数仅置位标志，避免在异步信号上下文执行复杂清理逻辑。
    g_stopRequested = 1;
}

int main(int argc, char* argv[]) {
    std::cout << "=== PileController Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // BY ZF: 固定配置路径
    const char* configPath = "/usr/app/config/pile_controller.ini";
    
    // 创建进程实例
    PileControllerProcess process;
    // 初始化
    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize PileController process" << std::endl;
        return 1;
    }
    
    std::cout << "PileController process started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    // BY ZF: 将 start 放入工作线程，主线程负责信号与退出协调，避免 Ctrl+C 触发重复 stop。
    std::thread worker([&process]() {
        if (process.start() != 0) {
            std::cerr << "Failed to start PileController process" << std::endl;
        }
    });

    bool stopIssued = false;
    while (process.getStatus() != PROC_STATUS_STOPPED && process.getStatus() != PROC_STATUS_ERROR) {
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

    std::cout << "PileController process stopped" << std::endl;
    return 0;
}
