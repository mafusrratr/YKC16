/**
 * 主控通信进程入口
 * BY ZF
 */

#include "pile_controller_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

PileControllerProcess* g_process = nullptr;

void signalHandler(int sig) {
    std::cout << "PileController process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== PileController Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 检查参数
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    // 创建进程实例
    PileControllerProcess process;
    g_process = &process;
    
    // 初始化
    if (!process.initialize(argv[1])) {
        std::cerr << "Failed to initialize PileController process" << std::endl;
        return 1;
    }
    
    // 启动
    if (process.start() != 0) {
        std::cerr << "Failed to start PileController process" << std::endl;
        return 1;
    }
    
    std::cout << "PileController process started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环（喂狗）
    while (process.getStatus() != PROC_STATUS_STOPPED) {
        process.feedWatchdog();
        sleep(1);
    }
    
    std::cout << "PileController process stopped" << std::endl;
    return 0;
}

