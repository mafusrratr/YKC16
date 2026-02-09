/**
 * 业务帧解析测试（数据来源：CAN 链路）
 * 使用 CANBusinessFrameController 从 CAN 总线接收报文并解析为业务帧（YX22 等）
 * BY ZF
 *
 * 用法：在目标机上连接 CAN 到主控后运行，观察解析出的 YX22 等业务数据
 *   ./test_business_frame_from_can [config]
 * 默认 config: device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1
 */

#include "../controllers/can_business_frame_controller.h"
#include <iostream>
#include <csignal>
#include <cstring>

static CANBusinessFrameController* g_controller = nullptr;

static void onSignal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n收到退出信号，正在停止...\n";
        if (g_controller) {
            g_controller->stop();
        }
    }
}

int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "  业务帧解析测试（数据来源：CAN 链路）\n";
    std::cout << "  BY ZF\n";
    std::cout << "========================================\n\n";

    const char* config = "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1";
    if (argc >= 2 && argv[1][0] != '\0') {
        config = argv[1];
        std::cout << "使用配置: " << config << "\n\n";
    }

    CANBusinessFrameController controller;
    g_controller = &controller;

    if (std::signal(SIGINT, onSignal) == SIG_ERR) {
        std::cerr << "注册 SIGINT 失败\n";
    }
    if (std::signal(SIGTERM, onSignal) == SIG_ERR) {
        std::cerr << "注册 SIGTERM 失败\n";
    }

    if (!controller.initialize(config)) {
        std::cerr << "初始化失败，请检查 CAN 设备与配置\n";
        return 1;
    }

    controller.run();
    controller.cleanup();
    g_controller = nullptr;

    std::cout << "\n========================================\n";
    std::cout << "  测试结束\n";
    std::cout << "========================================\n";
    return 0;
}
