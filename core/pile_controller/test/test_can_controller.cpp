/**
 * CAN控制器测试程序
 * 用于测试 CANPileController 的基本功能
 * BY ZF
 * 
 * 测试内容：
 * 1. 配置解析测试
 * 2. CAN设备打开/关闭测试
 * 3. 协议初始化测试
 * 4. 基本命令发送测试（框架测试，不涉及具体协议编码）
 */

#include "../controllers/can_pile_controller.h"
#include "../ipile_controller.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>

// BY ZF: 测试配置解析
void testConfigParsing()
{
    std::cout << "\n=== 测试1: 配置解析 ===\n";
    
    CANPileController controller;
    
    // BY ZF: 测试有效配置（包含必需的 bcu_addr 和 cdz_addr）
    const char* validConfig = "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    
    if (controller.initialize("can", validConfig)) {
        std::cout << "✓ 配置解析成功\n";
        controller.cleanup();
    } else {
        std::cout << "✗ 配置解析失败\n";
    }
    
    // BY ZF: 测试无效配置（缺少必需参数）
    // 注意：需要创建新的控制器实例，避免复用之前的成员变量值
    CANPileController controller2;
    const char* invalidConfig = "device_path=can0,baudrate=250000";
    
    if (!controller2.initialize("can", invalidConfig)) {
        std::cout << "✓ 无效配置正确拒绝\n";
    } else {
        std::cout << "✗ 无效配置未被拒绝\n";
        controller2.cleanup();
    }
}

// BY ZF: 测试CAN设备打开（需要实际CAN设备）
void testCANDeviceOpen()
{
    std::cout << "\n=== 测试2: CAN设备打开 ===\n";
    std::cout << "注意：此测试需要实际的CAN设备（can0）\n";
    std::cout << "如果设备不存在，测试会失败（这是正常的）\n\n";
    
    CANPileController controller;
    
    // BY ZF: 测试打开CAN设备
    const char* config = "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    
    if (controller.initialize("can", config)) {
        std::cout << "✓ CAN设备打开成功（或设备不存在但初始化流程正常）\n";
        controller.cleanup();
    } else {
        std::cout << "✗ CAN设备打开失败（可能是设备不存在，这是正常的）\n";
    }
}

// BY ZF: 测试协议初始化
void testProtocolInitialization()
{
    std::cout << "\n=== 测试3: 协议初始化 ===\n";
    
    CANPileController controller;
    
    // BY ZF: 测试协议初始化（不打开实际设备）
    const char* config = "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    
    // BY ZF: 注意：这里会尝试打开设备，如果设备不存在会失败
    // 但协议对象的创建和初始化逻辑会被测试
    if (controller.initialize("can", config)) {
        std::cout << "✓ 协议初始化成功\n";
        controller.cleanup();
    } else {
        std::cout << "✗ 协议初始化失败（可能是CAN设备不存在）\n";
    }
}

// BY ZF: 测试基本命令接口（框架测试）
void testBasicCommands()
{
    std::cout << "\n=== 测试4: 基本命令接口（框架测试） ===\n";
    std::cout << "注意：当前为框架实现，编码/解码逻辑待完善\n\n";
    
    CANPileController controller;
    
    // BY ZF: 初始化（不打开实际设备，仅测试接口）
    const char* config = "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    
    // BY ZF: 如果设备不存在，初始化会失败，但我们可以测试接口调用
    bool initialized = controller.initialize("can", config);
    
    if (initialized) {
        // BY ZF: 测试获取状态（框架实现，返回空状态）
        PileStatus status;
        int ret = controller.getStatus(1, &status);
        if (ret == 0) {
            std::cout << "✓ getStatus() 接口调用成功\n";
            std::cout << "  枪号: " << static_cast<int>(status.gunNo) << "\n";
        } else {
            std::cout << "✗ getStatus() 接口调用失败\n";
        }
        
        // BY ZF: 测试设置充电参数（框架实现）
        ret = controller.setChargeParams(1, 3800, 100);  // 380.0V, 10.0A
        if (ret == 0) {
            std::cout << "✓ setChargeParams() 接口调用成功\n";
        } else {
            std::cout << "✗ setChargeParams() 接口调用失败\n";
        }
        
        // BY ZF: 测试获取故障码（框架实现）
        uint16_t faultCode = 0;
        ret = controller.getFaultCode(1, &faultCode);
        if (ret == 0) {
            std::cout << "✓ getFaultCode() 接口调用成功，故障码: " << faultCode << "\n";
        } else {
            std::cout << "✗ getFaultCode() 接口调用失败\n";
        }
        
        // BY ZF: 测试心跳（框架实现）
        bool heartbeatOk = controller.heartbeat();
        if (heartbeatOk) {
            std::cout << "✓ heartbeat() 接口调用成功\n";
        } else {
            std::cout << "✗ heartbeat() 接口调用失败\n";
        }
        
        controller.cleanup();
    } else {
        std::cout << "⚠ 初始化失败（可能是CAN设备不存在），跳过接口测试\n";
        std::cout << "  这是正常的，因为当前环境可能没有CAN设备\n";
    }
}

// BY ZF: 测试多枪配置
void testMultiGunConfig()
{
    std::cout << "\n=== 测试5: 多枪配置 ===\n";
    
    // BY ZF: 测试多个控制器实例（模拟多枪）
    const int gunCount = 2;
    CANPileController controllers[gunCount];
    
    bool allOk = true;
    for (int i = 0; i < gunCount; i++) {
        uint8_t gunNo = i + 1;
        uint32_t canIdBase = 0x180 + i;
        
        std::ostringstream configStream;
        configStream << "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0x" 
                     << std::hex << (0xE0 + i) << std::dec  // BY ZF: 不同枪使用不同的CDZ地址
                     << ",gun_no=" << static_cast<int>(gunNo)
                     << ",packet_timeout=5,retry_count=3";
        
        if (controllers[i].initialize("can", configStream.str().c_str())) {
            std::cout << "✓ 枪" << static_cast<int>(gunNo) << " 初始化成功\n";
        } else {
            std::cout << "✗ 枪" << static_cast<int>(gunNo) << " 初始化失败\n";
            allOk = false;
        }
    }
    
    if (allOk) {
        std::cout << "✓ 多枪配置测试通过\n";
    } else {
        std::cout << "⚠ 多枪配置测试部分失败（可能是CAN设备不存在）\n";
    }
    
    // BY ZF: 清理
    for (int i = 0; i < gunCount; i++) {
        controllers[i].cleanup();
    }
}

int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "  CAN控制器测试程序\n";
    std::cout << "  BY ZF\n";
    std::cout << "========================================\n";
    
    // BY ZF: 运行测试
    testConfigParsing();
    testCANDeviceOpen();
    testProtocolInitialization();
    testBasicCommands();
    testMultiGunConfig();
    
    std::cout << "\n========================================\n";
    std::cout << "  测试完成\n";
    std::cout << "========================================\n";
    
    return 0;
}

