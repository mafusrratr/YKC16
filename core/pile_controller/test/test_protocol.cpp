/**
 * CAN2CCU协议测试程序
 * 用于测试 CAN2CCUProtocol 的基本功能
 * BY ZF
 * 
 * 测试内容：
 * 1. 协议初始化测试
 * 2. CAN ID计算测试
 * 3. 编码/解码接口测试（框架测试，不涉及具体协议格式）
 */

#include "../protocol/can2ccu_protocol.h"
#include "../protocol/tcu2ccu_data.h"
#include "../ipile_controller.h"
#include <iostream>
#include <cstring>
#include <cassert>

// BY ZF: 测试协议初始化
void testProtocolInit()
{
    std::cout << "\n=== 测试1: 协议初始化 ===\n";
    
    CAN2CCUProtocol protocol;
    
    // BY ZF: 测试有效配置
    const char* config = "bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    
    if (protocol.initialize(config)) {
        std::cout << "✓ 协议初始化成功\n";
        protocol.cleanup();
    } else {
        std::cout << "✗ 协议初始化失败\n";
    }
}

// BY ZF: 测试CAN ID计算
void testCANIdCalculation()
{
    std::cout << "\n=== 测试2: CAN ID计算 ===\n";
    
    CAN2CCUProtocol protocol;
    
    const char* config = "bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    if (!protocol.initialize(config)) {
        std::cout << "✗ 协议初始化失败，跳过CAN ID测试\n";
        return;
    }
    
    // BY ZF: 测试不同PGN的CAN ID计算
    uint8_t priority = 0x10;
    uint8_t pgns[] = {0x01, 0x03, 0x05, 0x07, 0x40};  // 启动充电、停止充电、对时、版本校验、心跳
    const char* pgnNames[] = {"启动充电", "停止充电", "对时", "版本校验", "心跳"};
    uint8_t srcAddr = 0x8A;  // BCU地址
    uint8_t dstAddr = 0xE0;  // CDZ地址
    
    for (int i = 0; i < 5; i++) {
        uint32_t canId = protocol.calculateCANId(priority, pgns[i], srcAddr, dstAddr);
        std::cout << "  PGN: " << pgnNames[i] 
                  << " (0x" << std::hex << static_cast<int>(pgns[i]) << std::dec << ")"
                  << " -> CAN ID: 0x" << std::hex << canId << std::dec << "\n";
    }
    
    std::cout << "✓ CAN ID计算测试完成\n";
    protocol.cleanup();
}

// BY ZF: 测试编码接口（框架测试）
void testEncodeInterfaces()
{
    std::cout << "\n=== 测试3: 编码接口（框架测试） ===\n";
    std::cout << "注意：当前为框架实现，编码逻辑待完善\n";
    std::cout << "注意：编码方法会直接发送，不再返回数据\n\n";
    
    CAN2CCUProtocol protocol;
    
    const char* config = "bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    if (!protocol.initialize(config)) {
        std::cout << "✗ 协议初始化失败，跳过编码测试\n";
        return;
    }
    
    // BY ZF: 注意：由于编码方法会直接发送，需要设置发送回调
    // 测试环境中，如果没有实际的CAN设备，发送会失败，这是正常的
    protocol.setSendCallback([](uint32_t canId, const uint8_t* data, uint8_t dataLen) -> int {
        std::cout << "  [模拟发送] CAN ID: 0x" << std::hex << canId << std::dec
                  << ", 数据长度: " << static_cast<int>(dataLen) << "\n";
        return 0;  // 模拟发送成功
    });
    
    // BY ZF: 测试编码启动充电命令
    TCU2CCU_CmdStartChargeData startCmd;
    memset(&startCmd, 0, sizeof(startCmd));
    startCmd.loadControlSwitch = 0x02;
    startCmd.plugAndChargeFlag = 0x01;
    startCmd.auxPowerVoltage = 0x0C;
    protocol.setStartChargeData(&startCmd);
    
    int ret = protocol.encodeStartCharge();
    if (ret == 0) {
        std::cout << "✓ encodeStartCharge() 调用成功\n";
    } else {
        std::cout << "✗ encodeStartCharge() 调用失败\n";
    }
    
    // BY ZF: 测试编码停止充电命令
    TCU2CCU_CmdStopChargeData stopCmd;
    memset(&stopCmd, 0, sizeof(stopCmd));
    stopCmd.stopReason = 1;
    stopCmd.tcuStopCode = 0;
    protocol.setStopChargeData(&stopCmd);
    
    ret = protocol.encodeStopCharge();
    if (ret == 0) {
        std::cout << "✓ encodeStopCharge() 调用成功\n";
    } else {
        std::cout << "✗ encodeStopCharge() 调用失败\n";
    }
    
    // BY ZF: 测试编码设置充电参数命令
    ret = protocol.encodeSetChargeParams(1, 3800, 100);
    if (ret == 0) {
        std::cout << "✓ encodeSetChargeParams() 调用成功\n";
    } else {
        std::cout << "✗ encodeSetChargeParams() 调用失败\n";
    }
    
    // BY ZF: 测试编码读取状态命令
    ret = protocol.encodeGetStatus(1);
    if (ret == 0) {
        std::cout << "✓ encodeGetStatus() 调用成功\n";
    } else {
        std::cout << "✗ encodeGetStatus() 调用失败\n";
    }
    
    // BY ZF: 测试编码清除故障命令
    ret = protocol.encodeClearFault(1);
    if (ret == 0) {
        std::cout << "✓ encodeClearFault() 调用成功\n";
    } else {
        std::cout << "✗ encodeClearFault() 调用失败\n";
    }
    
    // BY ZF: 测试编码心跳帧
    ret = protocol.encodeHeartbeat();
    if (ret == 0) {
        std::cout << "✓ encodeHeartbeat() 调用成功\n";
    } else {
        std::cout << "✗ encodeHeartbeat() 调用失败\n";
    }
    
    protocol.cleanup();
}

// BY ZF: 测试解码接口（框架测试）
void testDecodeInterfaces()
{
    std::cout << "\n=== 测试4: 解码接口（框架测试） ===\n";
    std::cout << "注意：当前为框架实现，解码逻辑待完善\n\n";
    
    CAN2CCUProtocol protocol;
    
    const char* config = "bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    if (!protocol.initialize(config)) {
        std::cout << "✗ 协议初始化失败，跳过解码测试\n";
        return;
    }
    
    // BY ZF: 测试解码入口 decodeFrame + 状态汇总 getAggregatedStatus
    uint8_t testData[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t canId = 0x18208A8A;  // 示例：PGN=0x20 遥测帧
    int ret = protocol.decodeFrame(canId, testData, 8);
    if (ret == 0 || ret == 1) {
        std::cout << "✓ decodeFrame() 调用成功，返回值: " << ret << "\n";
    } else {
        std::cout << "✗ decodeFrame() 调用失败或非本协议帧，返回值: " << ret << "\n";
    }
    
    PileStatus status;
    ret = protocol.getAggregatedStatus(&status);
    if (ret == 0) {
        std::cout << "✓ getAggregatedStatus() 调用成功，枪号: " << static_cast<int>(status.gunNo) << "\n";
    } else {
        std::cout << "✗ getAggregatedStatus() 调用失败\n";
    }
    
    protocol.cleanup();
}

// BY ZF: 业务帧解析测试（YX22 单帧，可与主控联调）
void testBusinessFrameDecode()
{
    std::cout << "\n=== 测试5: 业务帧解析（YX22 单帧）===\n";
    std::cout << "参考帧: can0 10228AE0 [8] 01 04 00 00 00 20 57 00\n\n";
    
    CAN2CCUProtocol protocol;
    const char* config = "bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1,packet_timeout=5,retry_count=3";
    if (!protocol.initialize(config)) {
        std::cout << "✗ 协议初始化失败，跳过业务帧解析测试\n";
        return;
    }
    
    // BY ZF: 参考主控遥信帧 10228AE0（优先级0x10, PGN=0x22, 目标=0x8A, 源=0xE0）
    uint32_t canId = 0x10228AE0;
    uint8_t data[8] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x20, 0x57, 0x00};
    
    int ret = protocol.decodeFrame(canId, data, 8);
    if (ret != 0) {
        std::cout << "✗ decodeFrame(YX22) 失败，返回值: " << ret << "\n";
        protocol.cleanup();
        return;
    }
    std::cout << "✓ decodeFrame(YX22) 成功\n";
    
    if (!protocol.isYX22DataValid()) {
        std::cout << "✗ isYX22DataValid() 为 false\n";
        protocol.cleanup();
        return;
    }
    
    TCU2CCU_DataYX22 yx22;
    if (protocol.getYX22Data(&yx22) != 0) {
        std::cout << "✗ getYX22Data() 失败\n";
        protocol.cleanup();
        return;
    }
    
    // BY ZF: 按参考帧 01 04 00 00 00 20 57 00 校验解析结果
    // Data2=0x04: workStatus=0(待机), totalFault=1, totalAlarm=0; Data6=0x20: vehicleConnectStatus=1(未连接); Data7~8: otherFault=0x0057
    assert(yx22.workStatus == 0);
    assert(yx22.totalFault == 1);
    assert(yx22.totalAlarm == 0);
    assert(yx22.vehicleConnectStatus == 1);
    assert(yx22.otherFault == 0x0057);  // 低字节=告警编号0x57, 高字节=告警值0x00(正常)
    
    std::cout << "  工作状态 workStatus: " << static_cast<int>(yx22.workStatus) << " (0=待机)\n";
    std::cout << "  总故障 totalFault: " << static_cast<int>(yx22.totalFault) << "\n";
    std::cout << "  总告警 totalAlarm: " << static_cast<int>(yx22.totalAlarm) << "\n";
    std::cout << "  车辆连接状态 vehicleConnectStatus: " << static_cast<int>(yx22.vehicleConnectStatus) << " (1=未连接)\n";
    std::cout << "  其它类型故障 otherFault: 0x" << std::hex << yx22.otherFault << std::dec << " (编号=0x57, 值=0x00)\n";
    std::cout << "✓ 业务帧解析（YX22）校验通过\n";
    
    protocol.cleanup();
}

int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "  CAN2CCU协议测试程序\n";
    std::cout << "  BY ZF\n";
    std::cout << "========================================\n";
    
    // BY ZF: 运行测试
    testProtocolInit();
    testCANIdCalculation();
    testEncodeInterfaces();
    testDecodeInterfaces();
    testBusinessFrameDecode();  // 业务帧解析（YX22），可与主控联调
    std::cout << "\n========================================\n";
    std::cout << "  测试完成\n";
    std::cout << "========================================\n";
    
    return 0;
}

