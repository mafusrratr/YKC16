/**
 * 日志系统类型定义
 * 从 ilogger.h 提取，供所有模块使用
 * BY ZF
 */

#ifndef LOGGER_TYPES_H
#define LOGGER_TYPES_H

#include <stdint.h>
#include <string>
#include <vector>

// 日志级别枚举
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
};

// 日志条目结构
struct LogEntry {
    LogLevel level;            // 日志级别
    std::string module;        // 模块名称
    std::string message;       // 日志消息
    std::string details;       // 详细信息
    uint64_t timestamp;        // 时间戳（毫秒）
    
    LogEntry() : level(LOG_INFO), timestamp(0) {}
    LogEntry(LogLevel lvl, const std::string& mod, 
             const std::string& msg, const std::string& det = "")
        : level(lvl), module(mod), message(msg), details(det), timestamp(0) {}
};

// 充电交易记录结构（供logTradeRecord接口用）
struct TradeRecord {
    int gunNo = 0;                  // 充电枪编号
    std::string preTradeNo;         // 平台交易流水号
    std::string tradeNo;            // 设备交易流水号
    std::string vinCode;            // VIN
    int timeDivType = 0;            // 计量计费类型（0/1/2...）
    int startType = 0;              // 启动方式（刷卡/远程等）
    uint64_t chargeStartTime = 0; // 开始充电时间（YYYYMMDDHHMMSS）
    uint64_t chargeEndTime = 0;   // 结束充电时间（YYYYMMDDHHMMSS）
    double startSoc = 0.0;          // 启动时SOC（%）
    double endSoc = 0.0;            // 停止时SOC（%）
    unsigned int reason = 0;        // 停止充电原因
    std::string feeModelId;         // 计费模型编号
    double sumStart = 0.0;          // 电表总起示值（kWh）
    double sumEnd = 0.0;            // 电表总止示值（kWh）
    double totalElect = 0.0;        // 总电量（kWh）
    double totalPowerCost = 0.0;    // 总电费（元）
    double totalServCost = 0.0;     // 总服务费（元）
    double totalCost = 0.0;         // 总消费金额（元）
    int timeNum = 0;                // 时段数
    std::vector<double> partElect;         // 时段电量（kWh）
    std::vector<double> chargeFee;         // 时段电费（元）
    std::vector<double> serviceFee;        // 时段服务费（元）
    int startPoint = 0;             // 起始点标识
    int crossPoints = 0;            // 跨越点数
    std::vector<double> pointsElect;       // 跨越点电量（kWh）
    std::string cardNumber;         // 实体卡号
};

// BY ZF: 计费模型结构（对应共享内存 _evs_service_issue_feeModel）
struct FeeModel {
    std::string feeModelId;                              // 计费模型编号（最大17字符）
    unsigned char timeNum = 0;                          // 时段数（1-96）
    std::vector<std::string> timeSeg;                   // 时段开始时间点（每个4字符，如"0630"表示6点30分）
    std::vector<unsigned int> segFlag;                  // 时段标志数组
    std::vector<unsigned int> chargeFee;                 // 电费数组（单位：10^-5元，长度timeNum）
    std::vector<unsigned int> serviceFee;                // 服务费数组（单位：10^-5元，长度timeNum）
};

#endif // LOGGER_TYPES_H
