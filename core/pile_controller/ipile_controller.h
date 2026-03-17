/**
 * 充电桩主控通信接口定义
 * 负责与充电桩主控板通信，获取实时充电状态、控制指令下发等
 * 后续可能通过CAN、共享内存、串口等多种方式实现
 * BY ZF
 * 
 * 注意：此接口用于进程内部多种实现（CAN、485、共享内存等），
 * 应该放在进程内部，而不是全局 interfaces/ 目录
 */

#ifndef IPILE_CONTROLLER_H
#define IPILE_CONTROLLER_H

#include <stdint.h>
// 充电桩主控状态
struct PileStatus {
    uint8_t gunNo;              // 枪号
    uint16_t outputVoltage;     // 输出电压（0.1V）
    int16_t outputCurrent;      // 输出电流（0.1A，含符号）
    uint32_t outputPower;       // 输出功率（W）
    uint32_t cumulativeEnergy;  // 累计电量（Wh）
    uint8_t workStatus;         // 工作状态
    uint16_t faultCode;         // 故障代码
    uint8_t soc;                // 电池SOC（%）
    uint16_t temperature;       // 温度（0.1℃）
};

/**
 * 充电桩主控通信接口
 * 不同现场可能使用不同的通信方式（CAN/485/共享内存/网络等）
 * 支持的通信方式：
 * - "can" - CAN总线通信
 * - "485" - RS485串口通信
 * - "shm" - 共享内存通信
 * - "tcp" - TCP网络通信
 * - "udp" - UDP网络通信
 * 
 * 使用方式：
 * PileControllerProcess 根据配置选择实现：
 * - CANPileController（实现 IPileController）
 * - RS485PileController（实现 IPileController）
 * - SharedMemoryPileController（实现 IPileController）
 * - NetworkPileController（实现 IPileController）
 */
class IPileController {
public:
    virtual ~IPileController() {}
    
    /**
     * 初始化主控通信模块
     * @param commType 通信类型（如"can"、"485"、"shm"、"tcp"、"udp"等）
     * @param config 配置参数（设备路径、波特率、网络地址等）
     * @return true成功, false失败
     */
    virtual bool initialize(const char* commType, const char* config) = 0;
    
    /**
     * 反初始化
     */
    virtual void cleanup() = 0;
    
    /**
     * 启动充电
     * @return 0成功, 其他失败
     */
    virtual int startCharge() = 0;
    
    /**
     * 停止充电
     * @return 0成功
     */
    virtual int stopCharge() = 0;
    
    /**
     * 获取充电桩状态
     * @param gunNo 枪号
     * @param status 输出：状态信息
     * @return 0成功
     */
    virtual int getStatus(uint8_t gunNo, PileStatus* status) = 0;

};

#endif // IPILE_CONTROLLER_H

