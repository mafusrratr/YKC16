/**
 * 测试主程序（守护进程版本）
 * 用于验证DaemonProcess和外部进程管理功能
 * BY ZF
 */

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include "test_process.h"
#include "daemon_process.h"

// 全局变量用于信号处理
DaemonProcess* g_daemon = nullptr;

void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", stopping daemon..." << std::endl;
    if (g_daemon) {
        g_daemon->stop();
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    std::cout << "=== TCU Daemon Process Test ===" << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建守护进程
    DaemonProcess daemon;
    g_daemon = &daemon;
    
    // 初始化守护进程（会自动从配置文件加载进程列表）
    std::string configPath = "config/project_a";
    if (!daemon.initialize(configPath.c_str())) {
        std::cout << "Failed to initialize daemon process" << std::endl;
        return -1;
    }
    
    // 启动守护进程（会自动启动所有业务进程）
    if (daemon.start() != 0) {
        std::cout << "Failed to start daemon process" << std::endl;
        return -1;
    }
    
    std::cout << "Daemon process started, will run for 30 seconds..." << std::endl;
    
    // 运行30秒，每5秒输出一次状态报告
    for (int i = 0; i < 6; i++) {
        sleep(5);
        std::cout << "\n" << daemon.getProcessStatusReport() << std::endl;
    }
    
    std::cout << "Stopping daemon process..." << std::endl;
    daemon.stop();
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}

