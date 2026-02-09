/**
 * 守护进程主程序
 * BY ZF
 */

#include "daemon_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

// 全局守护进程指针
DaemonProcess* g_daemon = nullptr;

/**
 * 信号处理函数
 */
void signalHandler(int sig) {
    std::cout << "Received signal: " << sig << std::endl;
    if (g_daemon) {
        std::cout << "Stopping daemon process..." << std::endl;
        g_daemon->stop();
    }
    // 直接退出，不等待其他清理
    _exit(0);
}

/**
 * 主函数
 */
int main(int argc, char *argv[])
{
    std::cout << "=== TCU Daemon Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);   // 重新加载配置
    
    // 创建守护进程
    DaemonProcess daemon;
    g_daemon = &daemon;
    
    // 解析命令行参数
    std::string configPath = "/usr/app/config/daemon.ini";  // 目标设备默认路径
    if (argc > 1) {
        configPath = argv[1];
    }
    
    std::cout << "Using config path: " << configPath << std::endl;
    
    // 初始化守护进程
    if (!daemon.initialize(configPath.c_str())) {
        std::cout << "Failed to initialize daemon process" << std::endl;
        return -1;
    }
    
    // 启动守护进程
    if (daemon.start() != 0) {
        std::cout << "Failed to start daemon process" << std::endl;
        return -1;
    }
    
    std::cout << "Daemon process started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环
    while (daemon.getStatus() == PROC_STATUS_RUNNING) {
        sleep(1);
        
        // 每30秒输出一次状态报告
        static int counter = 0;
        if (++counter >= 30) {
            std::cout << "\n" << daemon.getProcessStatusReport() << std::endl;
            counter = 0;
        }
    }
    
    std::cout << "Daemon process stopped" << std::endl;
    return 0;
}
