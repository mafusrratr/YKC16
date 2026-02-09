/**
 * 电表计量与计费接口定义
 * BY ZF
 * 
 * 注意：此接口用于进程内部多种实现（DL698、Modbus等），
 * 应该放在进程内部，而不是全局 interfaces/ 目录
 */

#ifndef IMETER_H
#define IMETER_H

#include <stdint.h>

// 电表类型
enum MeterType {
    METER_NONE = 0,
    METER_DC,              // 直流电表
    METER_AC,              // 交流电表
    METER_DUAL             // 交直流双电表
};

// 计费模型时段信息
struct FeeSegment {
    char startTime[6];        // 开始时间 HHMM
    uint32_t chargeFee;       // 电费（分/度）
    uint32_t serviceFee;      // 服务费（分/度）
};

// 电量信息
struct EnergyData {
    uint32_t value[5];        // 电量值（支持多费率）
    uint32_t sumValue;        // 总电量
};

// 交易计费信息
struct FeeInfo {
    char tradeNo[41];         // 订单号
    uint8_t gunNo;            // 枪号
    uint32_t startEnergy;     // 起始电量
    uint32_t endEnergy;       // 结束电量
    uint32_t totalEnergy;     // 总电量
    uint32_t totalChargeFee;  // 总电费（分）
    uint32_t totalServiceFee; // 总服务费（分）
    uint32_t totalFee;        // 总费用（分）
};

/**
 * 电表计量与计费接口
 * 
 * 使用方式：
 * MeterProcess 根据配置选择实现：
 * - DL698Meter（实现 IMeter）
 * - ModbusMeter（实现 IMeter）
 * - CustomMeter（实现 IMeter）
 */
class IMeter {
public:
    virtual ~IMeter() {}
    
    /**
     * 初始化电表模块
     * @param gunCount 枪数
     * @param meterType 电表类型
     * @return true成功, false失败
     */
    virtual bool initialize(int gunCount, MeterType meterType) = 0;
    
    /**
     * 反初始化
     */
    virtual void cleanup() = 0;
    
    /**
     * 读取电表实时数据
     * @param gunNo 枪号
     * @param energy 输出：电量数据
     * @return 0成功
     */
    virtual int readEnergy(uint8_t gunNo, EnergyData* energy) = 0;
    
    /**
     * 设置计费模型
     * @param modelId 模型ID
     * @param segments 时段数组
     * @param segmentCount 时段数量
     * @return 0成功
     */
    virtual int setFeeModel(const char* modelId, const FeeSegment* segments, int segmentCount) = 0;
    
    /**
     * 计算费用
     * @param tradeNo 订单号
     * @param gunNo 枪号
     * @param startEnergy 起始电量
     * @param endEnergy 结束电量
     * @param feeInfo 输出：计费信息
     * @return 0成功
     */
    virtual int calculateFee(const char* tradeNo, uint8_t gunNo,
                            uint32_t startEnergy, uint32_t endEnergy,
                            FeeInfo* feeInfo) = 0;
    
    /**
     * 生成交易记录
     * @param feeInfo 计费信息
     * @param tradeRecord 输出：交易记录（存入共享内存或数据库）
     * @return 0成功
     */
    virtual int generateTradeRecord(const FeeInfo* feeInfo, void* tradeRecord) = 0;
};

#endif // IMETER_H





