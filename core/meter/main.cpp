/**
 * Meter process entry
 * BY ZF
 */

#include "meter_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

static MeterProcess* g_process = nullptr;

// BY ZF: 信号处理，触发优雅停机
static void signalHandler(int sig)
{
    std::cout << "Meter process received signal: " << sig << std::endl;
    if (g_process) {
        g_process->stop();
    }
}

// BY ZF: 进程入口
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    std::cout << "=== Meter Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // BY ZF: 固定配置路径
    const char* configPath = "/usr/app/config/tcu_meter.ini";

    MeterProcess process;
    g_process = &process;

    if (!process.initialize(configPath)) {
        std::cerr << "Failed to initialize Meter process" << std::endl;
        return 1;
    }

    if (process.start() != 0) {
        std::cerr << "Failed to start Meter process" << std::endl;
        return 1;
    }

    while (process.getStatus() != PROC_STATUS_STOPPED) {
        sleep(1);
    }

    return 0;
}
