/**
 * 日志系统主程序（使用LoggerProcess类）
 * BY ZF
 */

#include "logger_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

// 全局日志进程指针
LoggerProcess* g_logger = nullptr;

/**
 * 信号处理函数
 */
void signalHandler(int sig) {
    std::cout << "Logger process received signal: " << sig << std::endl;
    if (g_logger) {
        std::cout << "Stopping logger process..." << std::endl;
        g_logger->stop();
    }
    // 直接退出，不等待其他清理
    _exit(0);
}

/**
 * 主函数
 */
int main(int argc, char *argv[])
{
    std::cout << "=== TCU Logger Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);   // 重新加载配置
    
    // 创建日志进程
    LoggerProcess logger;
    g_logger = &logger;
    
    // 解析命令行参数
    std::string configPath = "/usr/app/config/logger.ini";  // 目标设备默认路径
    // std::string configPath = "/Users/seear/embedded_codes/01_TCU_Program/2510_RefactorProject/config/project_a/logger.ini";  // 本地测试用
    if (argc > 1) {
        configPath = argv[1];
    }
    
    std::cout << "Using config path: " << configPath << std::endl;
    
    // 初始化日志进程
    if (!logger.initialize(configPath.c_str())) {
        std::cout << "Failed to initialize logger process" << std::endl;
        return -1;
    }
    
    // 启动日志进程
    if (logger.start() != 0) {
        std::cout << "Failed to start logger process" << std::endl;
        return -1;
    }
    
    std::cout << "Logger process started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环（负责喂狗与保持主线程存活）
    while (logger.getStatus() == PROC_STATUS_RUNNING) {
        logger.feedWatchdog();  // BY ZF: 定期喂狗，避免被守护进程误判
        sleep(1);
    }
    
    std::cout << "Logger process stopped" << std::endl;
    return 0;
}
