/**
 * CAN通信测试程序
 * 直接使用 CANCommunication 基类进行测试
 * BY ZF
 * 
 * 功能：
 * 1. 使用 CAN ID 0x18230102 发送8字节测试数据
 * 2. 打印接收到的所有 CAN 报文
 */

#include "../../base/communication/can_communication.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <chrono>

// BY ZF: 全局运行标志（用于信号处理）
std::atomic<bool> g_running(true);

// BY ZF: 信号处理函数
void signalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n收到退出信号，正在停止...\n";
        g_running = false;
    }
}

// BY ZF: 测试CAN发送和接收
void testCANSendReceive()
{
    std::cout << "\n=== CAN通信测试 ===\n";
    std::cout << "CAN ID: 0x18230102\n";
    std::cout << "数据长度: 8字节\n";
    std::cout << "按 Ctrl+C 退出\n\n";
    
    // BY ZF: 创建CAN通信对象
    CANCommunication canComm;
    
    // BY ZF: 打开CAN设备（can0）
    std::string deviceName = "can0";
    uint32_t baudrate = 250000;
    
    std::cout << "正在打开CAN设备: " << deviceName << " (波特率: " << baudrate << ")...\n";
    if (!canComm.open(deviceName, baudrate)) {
        std::cerr << "✗ 打开CAN设备失败\n";
        std::cerr << "  请确认：\n";
        std::cerr << "  1. CAN设备是否存在（can0）\n";
        std::cerr << "  2. 设备是否已配置（ip link set can0 type can bitrate 250000 && ip link set up can0）\n";
        return;
    }
    
    std::cout << "✓ CAN设备打开成功\n\n";
    
    // BY ZF: 测试数据（8字节）
    uint8_t testData[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t testCanId = 0x18230102;
    
    // BY ZF: 发送测试数据
    std::cout << "发送测试数据:\n";
    std::cout << "  CAN ID: 0x" << std::hex << std::setfill('0') << std::setw(8) 
              << testCanId << std::dec << "\n";
    std::cout << "  数据: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) 
                  << static_cast<int>(testData[i]) << std::dec;
        if (i < 7) std::cout << " ";
    }
    std::cout << "\n\n";
    
    if (canComm.send(testCanId, testData, 8) == 0) {
        std::cout << "✓ 数据发送成功\n\n";
    } else {
        std::cerr << "✗ 数据发送失败\n\n";
    }
    
    // BY ZF: 循环接收CAN报文
    std::cout << "开始接收CAN报文（按 Ctrl+C 退出）...\n";
    std::cout << "----------------------------------------\n";
    
    uint32_t receiveCount = 0;
    uint32_t sendCount = 0;
    const uint32_t sendInterval = 1000000; // 1秒发送一次（微秒）
    auto lastSendTime = std::chrono::steady_clock::now();
    
    while (g_running.load()) {
        // BY ZF: 定期发送测试数据
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastSendTime).count();
        
        if (elapsed >= sendInterval) {
            if (canComm.send(testCanId, testData, 8) == 0) {
                sendCount++;
                std::cout << "[发送 #" << sendCount << "] CAN ID=0x" 
                          << std::hex << std::setfill('0') << std::setw(8) 
                          << testCanId << std::dec << ", 数据: ";
                for (int i = 0; i < 8; i++) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2) 
                              << static_cast<int>(testData[i]) << std::dec;
                    if (i < 7) std::cout << " ";
                }
                std::cout << "\n";
            }
            lastSendTime = now;
        }
        
        // BY ZF: 非阻塞接收CAN帧
        uint32_t canId;
        uint8_t data[8];
        uint8_t dataLen;
        
        if (canComm.receive(canId, data, dataLen) == 0) {
            receiveCount++;
            
            // BY ZF: 打印接收到的CAN报文
            std::cout << "[接收 #" << receiveCount << "] CAN ID=0x" 
                      << std::hex << std::setfill('0') << std::setw(8) 
                      << canId << std::dec
                      << ", 长度=" << static_cast<int>(dataLen)
                      << ", 数据: ";
            
            for (int i = 0; i < dataLen; i++) {
                std::cout << std::hex << std::setfill('0') << std::setw(2) 
                          << static_cast<int>(data[i]) << std::dec;
                if (i < dataLen - 1) std::cout << " ";
            }
            std::cout << "\n";
        } else {
            // BY ZF: 无数据，短暂休眠
            usleep(10000); // 10ms
        }
    }
    
    std::cout << "\n----------------------------------------\n";
    std::cout << "统计: 发送 " << sendCount << " 次, 接收 " << receiveCount << " 次\n";
    
    // BY ZF: 关闭CAN设备
    canComm.close();
    std::cout << "CAN设备已关闭\n";
}

int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "  CAN通信测试程序\n";
    std::cout << "  BY ZF\n";
    std::cout << "========================================\n";
    
    // BY ZF: 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // BY ZF: 运行测试
    testCANSendReceive();
    
    std::cout << "\n========================================\n";
    std::cout << "  测试完成\n";
    std::cout << "========================================\n";
    
    return 0;
}

