/**
 * 故障点表与原因映射公共模块
 * BY ZF
 */

#ifndef FAULT_REASON_MAPPER_H
#define FAULT_REASON_MAPPER_H

#include <string>

// BY ZF: 故障业务阶段分类
struct FaultTypeDef {
    enum Type {
        NONE = 0,
        STANDBY = 1,
        START_FAIL = 2,
        CHARGING_FAIL = 3
    };
};

// BY ZF: 点表规则项
struct FaultPointRule {
    bool treatAsFault;
    int faultType;
    unsigned int reason;
    std::string message;

    FaultPointRule();
    FaultPointRule(bool treatAsFault_, int faultType_, unsigned int reason_, const std::string& message_);
};

// BY ZF: 判定输出结果
struct FaultJudgeResult {
    bool valid;
    bool treatAsFault;
    int faultType;
    unsigned int reason;
    std::string pointKey;
    std::string message;

    FaultJudgeResult();
};

std::string MakeStandbyPointKey(unsigned int code);
std::string MakeStartPointKey(unsigned int code);
std::string MakeChargingPointKey(unsigned int code);

FaultJudgeResult JudgeFaultPoint(const std::string& pointKey);
FaultJudgeResult JudgeStandbyFaultPoint(const std::string& pointKey);
FaultJudgeResult JudgeStartFailPoint(const std::string& pointKey);
FaultJudgeResult JudgeChargingFailPoint(const std::string& pointKey);

const char* QueryReasonText(unsigned int reason);
bool IsFaultReason(unsigned int reason);

#endif // FAULT_REASON_MAPPER_H
