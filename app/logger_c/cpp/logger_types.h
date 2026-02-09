// BY ZF: 日志相关基础类型定义（供C封装独立使用）
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// BY ZF: 日志级别枚举
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
};

// BY ZF: 日志条目结构
struct LogEntry {
    LogLevel level;
    std::string module;
    std::string message;
    std::string details;
    uint64_t timestamp;

    LogEntry() : level(LOG_INFO), timestamp(0) {}
    LogEntry(LogLevel lvl, const std::string& mod,
             const std::string& msg, const std::string& det = "")
        : level(lvl), module(mod), message(msg), details(det), timestamp(0) {}
};

// BY ZF: 交易记录结构（供 logTradeRecord 序列化）
struct TradeRecord {
    int gunNo = 0;
    std::string preTradeNo;
    std::string tradeNo;
    std::string vinCode;
    int timeDivType = 0;
    int startType = 0;
    unsigned int chargeStartTime = 0;
    unsigned int chargeEndTime = 0;
    int startSoc = 0;
    int endSoc = 0;
    unsigned int reason = 0;
    std::string feeModelId;
    long long sumStart = 0;
    long long sumEnd = 0;
    unsigned int totalElect = 0;
    unsigned int totalPowerCost = 0;
    unsigned int totalServCost = 0;
    unsigned int totalCost = 0;
    int timeNum = 0;
    std::vector<unsigned int> partElect;
    std::vector<unsigned int> chargeFee;
    std::vector<unsigned int> serviceFee;
    int startPoint = 0;
    int crossPoints = 0;
    std::vector<unsigned int> pointsElect;
    std::string cardNumber;
};

// BY ZF: 计费模型结构（供 saveFeeModel 序列化）
struct FeeModel {
    std::string feeModelId;
    unsigned char timeNum = 0;
    std::vector<std::string> timeSeg;
    std::vector<unsigned int> segFlag;
    std::vector<unsigned int> chargeFee;
    std::vector<unsigned int> serviceFee;
};


