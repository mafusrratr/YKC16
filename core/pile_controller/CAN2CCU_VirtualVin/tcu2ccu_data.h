/**
 * TCU2CCU 协议业务数据结构定义
 * 用于定义 TCU 与 CCU（充电桩主控）之间通信的业务数据内容
 * BY ZF
 * 
 * 设计原则：
 * - 只定义业务数据内容，不包含帧格式（帧头、序列号等）
 * - 帧格式由协议层（can2ccu_protocol）负责处理
 * - 数据结构集中管理，便于维护和扩展
 * - 由于数据量大，部分数据需要分多帧传输，由协议层负责分帧和组装
 * 
 * 参考文档：core/pile_controller/CAN2CCU/doc/all_date.md
 */

#ifndef TCU2CCU_DATA_H
#define TCU2CCU_DATA_H

#include <stdint.h>

// ========== 命令帧业务数据 ==========

/**
 * 启动充电命令数据（表10 充电启动帧，用于下发）
 */
struct TCU2CCU_CmdStartChargeData {
    uint8_t loadControlSwitch;     // 负荷控制开关，01H 启用 02H 关闭
    uint8_t plugAndChargeFlag;     // 即插即充标识，01H 非即插即充 02H 即插即充
    uint8_t auxPowerVoltage;       // 辅助电源电压，0CH 12V 18H 24V
    uint8_t mergeChargeFlag;       // 合并充电标志
    uint8_t v2g;                   // BY ZF: 充放电模式标志，0=充电，1=放电
};

/**
 * 启动充电应答数据（表11 充电启动应答帧，用于接收）
 */
struct TCU2CCU_StartChargeResponseData {
    uint8_t loadControlSwitch;     // 负荷控制开关（应答字节2）
    uint8_t confirmFlag;           // 确认标识 00H 成功 01H 失败（应答字节3）
    uint8_t startFailReason;       // 失败原因数值，应用层解析（应答字节4）
    uint8_t plugAndChargeFlag;     // 即插即充标识（应答字节5）
    uint8_t auxPowerVoltage;       // 辅助电源电压（若有）
};

/**
 * 停止充电命令数据（表14 充电停止帧，用于下发）
 */
struct TCU2CCU_CmdStopChargeData {
    uint8_t stopReason;            // 停止充电原因 01H 正常停止 02H 自身故障 03H 充电控制模块故障等
    uint8_t tcuStopCode;           // 停止充电故障代码 01H 通讯 02H 读卡器 03H 电表 04H ESAM 05H 校验和 FFH 其他
};

/**
 * 车辆识别数据应答帧业务数据（表63，PGN=0x18，用于下发）
 */
struct TCU2CCU_VehicleIdConfirmData {
    uint8_t successFlag;           // 成功标识 00H 成功 01H 失败
    uint8_t failReason;            // 失败原因 00H 成功 01H 非法VIN
};

/**
 * 车辆识别鉴权帧业务数据（表64，PGN=0x19，用于下发）
 */
struct TCU2CCU_CmdVehicleAuthData {
    char vin[17];                  // 车辆VIN信息（ASCII，17字节）
    uint8_t successFlag;           // 成功标识 00H 成功 01H 失败
    uint8_t failReason;            // 失败原因 00H 成功 01H 非法VIN 02H 平台鉴权失败 03H 平台鉴权超时 FFH 其他
};

/**
 * 停止充电应答数据（表15 充电停止应答帧，用于接收）
 */
struct TCU2CCU_StopChargeResponseData {
    uint8_t confirmFlag;           // 确认标识 00H 成功 01H 失败（应答字节2）
};

/**
 * 版本校验命令数据（表22，TCU下发，PGN=0x07）
 * 字节1=充电接口标识（枪号），字节2-3=计费控制单元通信协议版本号（压缩BCD码）
 * 示例：V1.21 -> 主版本01H，次版本21H -> 报文字节 01H 21H
 */
struct TCU2CCU_CmdVersionCheckData {
    uint8_t versionMajor;          // 主版本号（BCD，如 V1.21 -> 0x01）
    uint8_t versionMinor;          // 次版本号（BCD，如 V1.21 -> 0x21）
};

/**
 * 版本校验应答数据（表23，CCU返回，PGN=0x08）
 * 字节1=充电接口标识，字节2-3=充电控制模块通信协议版本号（压缩BCD码）
 */
struct TCU2CCU_VersionCheckResponseData {
    uint8_t versionMajor;          // 主版本号（BCD）
    uint8_t versionMinor;          // 次版本号（BCD）
};

/**
 * 下发充电参数帧业务数据（表24，TCU下发，PGN=0x09）
 * 字节1=充电接口标识，字节2-8=充电桩(机)编号（压缩BCD码，7字节）
 */
struct TCU2CCU_CmdChargeParamData {
    uint8_t pileId[7];            // 充电桩(机)编号（压缩BCD码，7字节）
};

/**
 * 下发充电参数应答数据（表25，CCU返回，PGN=0x0A）
 * 字节1=充电接口标识，字节2=确认标识，字节3=失败原因
 */
struct TCU2CCU_ChargeParamResponseData {
    uint8_t confirmFlag;          // 确认标识 00H 成功 01H 失败
    uint8_t failReason;           // 失败原因 00H 成功 01H 数据有效性校验失败 02H 心跳超时 03H 版本校验未完成
};

/**
 * 充电桩编号业务数据
 * 对应命令：充电桩编号
 */
struct TCU2CCU_CmdPileIdData {
    uint8_t pileId[7];            // 充电桩（机）编号（压缩BCD码，7字节）
    uint8_t confirmFlag;           // 确认标识
};

/**
 * 充电服务启停业务数据
 * 对应命令：充电服务启停控制
 */
struct TCU2CCU_CmdServiceControlData {
    uint8_t serviceCmd;           // 充电服务启停操作指令（01H：停止，02H：启用，其它：无效）
};

/**
 * 电子锁控制业务数据
 * 对应命令：电子锁控制
 */
struct TCU2CCU_CmdElectronicLockData {
    uint8_t lockCmd;              // 操作指令（01H：上锁，02H：解锁）
    uint8_t confirmFlag;           // 确认标识（00H：成功，01H：失败）
};

/**
 * 功率调节业务数据
 * 对应命令：功率调节
 */
struct TCU2CCU_CmdPowerAdjustData {
    uint8_t adjustType;           // 功率调节指令类型（01H：绝对值，02H：百分比）
    uint16_t adjustParam;          // 功率调节参数（2字节，低字节在前）
                                    // 01H：绝对值，数据分辨率：0.1 kW/位；偏移量：-1000.0 kW；数据范围：-1000.0kW ~ +1000.0kW
                                    // 02H：百分比，数据分辨率：%1/位；偏移量：0%；数据范围：0%~100%
};

/**
 * 充电桩配置信息业务数据
 * 对应命令：充电桩配置信息
 */
struct TCU2CCU_CmdPileConfigData {
    uint32_t manufacturerCode;     // 厂家编码（4字节）
    uint8_t controllerSerial[12];  // 充电控制器序列号（压缩BCD码，12字节）
    uint8_t softwareVersion[3];    // 充电控制器软件版本（3字节：主版本号、次版本号、发行版本号）
    uint8_t softwareDate[4];       // 充电控制器软件日期（压缩BCD码，4字节：年、月、日）
    uint16_t maxOutputVoltage;     // 充电机最高输出电压（0.1V，低字节在前）
    uint16_t minOutputVoltage;     // 充电机最低输出电压（0.1V，低字节在前）
    uint16_t maxOutputCurrent;     // 充电机最大输出电流（0.1A，低字节在前，-400A偏移）
    uint16_t minOutputCurrent;     // 充电机最小输出电流（0.1A，低字节在前，-400A偏移）
};

/**
 * 设置充电参数业务数据
 * 对应命令：设置充电参数
 */
struct TCU2CCU_CmdSetParamsData {
    uint16_t voltage;              // 电压（0.1V，低字节在前）
    uint16_t current;              // 电流（0.1A，低字节在前）
};

/**
 * 输出电压电流调节业务数据
 * 对应命令：输出电压电流调节（4C帧）
 */
struct TCU2CCU_CmdOutputVAData {
    uint16_t demandVoltage;        // 需求电压（0.1V，低字节在前）
    uint16_t demandCurrent;        // 需求电流（0.1A，低字节在前）
};

/**
 * 清除故障业务数据
 * 对应命令：清除故障
 */
struct TCU2CCU_CmdClearFaultData {
    uint16_t faultCode;            // 故障码（低字节在前）
};

// ========== 状态帧业务数据 ==========

/**
 * 充电启动完成状态业务数据
 * 对应：表 C.1 直流充电桩启动完成状态帧数据
 */
struct TCU2CCU_StatusStartCompleteData {
    uint8_t successFlag;                   // 成功标识（Data3）00H 成功 01H 失败
    uint8_t chargeFailReason;              // 失败原因（Data4）存十六进制数，应用层解析
    uint8_t pileBmsVersion[3];            // 充电桩与BMS通信协议版本号（3字节）
    uint8_t bmsPileVersion[3];            // BMS与充电桩通信协议版本号（3字节）
    uint8_t handshakeResult;              // 充电桩与BMS握手结果
    uint8_t batteryType;                  // 电池类型
    uint8_t maxAllowTemp;                 // 最高允许温度
    uint16_t bmsMaxChargeVoltage;         // BMS最高允许充电电压（0.1V，低字节在前）
    uint16_t cellMaxChargeVoltage;        // 单体最高允许充电电压（0.1V，低字节在前）
    uint16_t maxAllowChargeCurrent;       // 最高允许充电电流（0.1A，低字节在前）
    uint16_t ratedTotalVoltage;          // 整车动力蓄电池额定总电压（0.1V，低字节在前）
    uint16_t currentTotalVoltage;         // 整车动力蓄电池当前电压（0.1V，低字节在前）
    uint16_t ratedCapacity;              // 整车动力蓄电池额定容量（0.1Ah，低字节在前）
    uint16_t nominalEnergy;                // 整车动力蓄电池标称能量（0.1kWh，低字节在前）
    uint16_t soc;                         // 整车动力蓄电池荷电状态（%，低字节在前）
    uint16_t pileMaxOutputVoltage;        // 充电机最高输出电压（0.1V，低字节在前）
    uint16_t pileMinOutputVoltage;        // 充电机最低输出电压（0.1V，低字节在前）
    uint16_t pileMaxOutputCurrent;        // 充电机最大输出电流（0.1A，低字节在前）
    uint16_t pileMinOutputCurrent;        // 充电机最小输出电流（0.1A，低字节在前）
    char vin[17];                         // 车辆识别码（ASCII，17字节）
    char batteryManufacturer[4];          // 电池组生产厂商（ASCII，4字节）
    uint8_t batterySerial[4];             // 电池组序列号（4字节）
    uint8_t batteryProdYear;              // 电池组生产日期-年
    uint8_t batteryProdMonth;             // 电池组生产日期-月
    uint8_t batteryProdDay;               // 电池组生产日期-日
    uint8_t batteryChargeCount[3];        // 电池组充电次数（3字节）
    uint8_t batteryPropertyFlag;          // 电池组产权标识
    uint8_t bmsSoftwareVersion[8];        // BMS软件版本号（8字节）
};

/**
 * 充电停止完成状态业务数据
 * 对应：表 D.1 直流充电桩停止完成状态帧数据
 */
struct TCU2CCU_StatusStopCompleteData {
    uint8_t stopReason;                    // 停止原因（Data2）存十六进制数，应用层解析
    uint8_t stopSuccessFlag;              // 停机成功标识
    uint8_t bmsStopReason;                 // BMS中止充电原因
    uint16_t bmsChargeFaultReason;        // BMS充电故障原因（2字节）
    uint8_t bmsStopErrorReason;            // BMS中止错误原因
    uint8_t stopSoc;                       // 中止荷电状态 SOC（%）
    uint16_t cellMinVoltage;              // 动力蓄电池单体最低电压（0.1V，低字节在前）
    uint16_t cellMaxVoltage;              // 动力蓄电池单体最高电压（0.1V，低字节在前）
    int16_t batteryMinTemp;                // 动力蓄电池最低温度（℃，已做-50偏移）
    int16_t batteryMaxTemp;                // 动力蓄电池最高温度（℃，已做-50偏移）
    // BY ZF: 超时信息（位字段）
    uint8_t timeoutSpn2560_00 : 2;         // 接收SPN2560=0x00的充电机辨识报文超时（2 bit）
    uint8_t timeoutSpn2560_AA : 2;         // 接收SPN2560=0xAA的充电机辨识报文超时（2 bit）
    uint8_t timeoutTimeSync : 2;           // 接收充电机的时间同步和充电机最大输出能力报文超时（2 bit）
    uint8_t timeoutChargeReady : 2;        // 接收充电机完成充电准备报文超时（2 bit）
    uint8_t timeoutChargeStatus : 2;       // 接收充电机充电状态报文超时（2 bit）
    uint8_t timeoutChargeStop : 2;         // 接收充电机中止充电报文超时（2 bit）
    uint8_t timeoutChargeStat : 2;         // 接收充电机充电统计报文超时（2 bit）
    uint8_t bmsOtherError : 6;            // BMS其他错误（6 bit）
    uint8_t timeoutBmsVehicleId : 2;      // 接收BMS和车辆的辨识报文超时（2 bit）
    uint8_t timeoutBatteryParams : 2;      // 接收电池充电参数报文超时（2 bit）
    uint8_t timeoutBmsReady : 2;           // 接收BMS完成充电准备报文超时（2 bit）
    uint8_t timeoutBatteryStatus : 2;      // 接收电池充电总状态报文超时（2 bit）
    uint8_t timeoutBatteryReq : 2;        // 接收电池充电要求报文超时（2 bit）
    uint8_t timeoutBmsStop : 2;            // 接收BMS中止充电报文超时（2 bit）
    uint8_t timeoutBmsStat : 2;            // 接收BMS充电统计报文超时（2 bit）
    uint8_t pileOtherError : 6;           // 充电机其他错误（6 bit）
};

/**
 * 充电桩状态信息业务数据
 * 对应状态：充电桩状态信息
 */
struct TCU2CCU_StatusPileStateData {
    uint8_t pileStatus;                   // 充电桩状态
    char vin[17];                         // 车辆VIN信息（17字节）
    uint8_t batteryChargeCount[3];        // 电池组充电次数（3字节）
    uint16_t soc;                         // 整车动力蓄电池荷电状态（%，低字节在前）
    uint16_t currentBatteryVoltage;       // 整车动力蓄电池当前电池电压（0.1V，低字节在前）
};

/**
 * 车辆识别信息业务数据
 * 对应状态：车辆识别信息
 */
struct TCU2CCU_StatusVehicleIdData {
    char vin[17];                         // 车辆VIN信息（17字节）
    uint8_t batteryChargeCount[3];        // 电池组充电次数（3字节）
    uint16_t soc;                         // 整车动力蓄电池荷电状态（%，低字节在前）
    uint16_t currentBatteryVoltage;       // 整车动力蓄电池当前电池电压（0.1V，低字节在前）
    uint8_t vinAuthResult;                // VIN鉴权结果标志
};

/**
 * 车辆识别鉴权应答帧业务数据（表65，PGN=0x1A，用于接收）
 */
struct TCU2CCU_VehicleAuthAckData {
    uint8_t successFlag;                  // 成功标识 00H 成功 01H 失败
    uint8_t failReason;                   // 失败原因 00H 成功 01H VIN不一致
};

// ========== 数据帧业务数据 ==========

/**
 * 遥测帧20业务数据（YC20）
 * 对应数据：遥测帧20
 */
struct TCU2CCU_DataYC20 {
    uint16_t outputVoltage;             // 充电输出电压（0.1V，低字节在前）
    int16_t outputCurrent;                // 充电输出电流（0.1A，低字节在前，含符号）
    uint8_t soc;                         // SOC（%）
    int16_t batteryMinTemp;               // 电池组最低温度（℃，已做-50偏移）
    int16_t batteryMaxTemp;               // 电池组最高温度（℃，已做-50偏移）
    uint16_t cellMaxVoltage;             // 单体电池最高电压（0.1V，低字节在前）
    uint16_t cellMinVoltage;             // 单体电池最低电压（0.1V，低字节在前）
    int16_t pileEnvTemp;                 // 充电机环境温度（℃，已做-50偏移）
    uint16_t guideVoltage;               // 充电导引电压（0.1V，低字节在前）
    uint16_t bmsReqVoltage;              // BMS需求电压（0.1V，低字节在前）
    int16_t bmsReqCurrent;               // BMS需求电流（0.1A，低字节在前，含符号）
    uint8_t chargeMode;                   // 充电模式
    uint16_t bmsMeasuredVoltage;          // BMS充电电压测量值（0.1V，低字节在前）
    int16_t bmsMeasuredCurrent;          // BMS充电电流测量值（0.1A，低字节在前，含符号）
    uint16_t estimatedRemainTime;        // 估算剩余充电时间（min，低字节在前）
    int16_t interfaceTemp1;               // 充电接口温度探头1温度（充电接口1 DC+，℃，已做-50偏移）
    int16_t interfaceTemp2;              // 充电接口温度探头2温度（充电接口1 DC-，℃，已做-50偏移）
    int16_t interfaceTemp3;               // 充电接口温度探头3温度（充电接口2 DC+，℃，已做-50偏移）
    int16_t interfaceTemp4;               // 充电接口温度探头4温度（充电接口2 DC-，℃，已做-50偏移）
    uint8_t maxVoltageCellNo;             // 最高单体动力蓄电池电压所在编号
    uint8_t maxTempPointNo;               // 最高温度检测点编号
    uint8_t minTempPointNo;               // 最低动力蓄电池温度检测点编号
    int16_t inletTemp;                   // 充电机进风口温度（℃，已做-50偏移）
    int16_t outletTemp;                  // 充电机出风口温度（℃，已做-50偏移）
    uint8_t envHumidity;                  // 充电机环境湿度（%）
};

/**
 * 遥测帧21业务数据（YC21）
 * 对应数据：遥测帧21
 */
struct TCU2CCU_DataYC21 {
    uint16_t currentEnergy;              // 当前充电电量（0.1 kWh，低字节在前，数据范围：0 kWh~1000 kWh）
    uint16_t chargeDuration;             // 累计充电时长（1 min，低字节在前，数据范围：0min~6000 min）
};

/**
 * 遥信数据22业务数据（YX22）
 * 对应数据：遥信数据22
 * 注意：使用位字段表示各种故障和状态
 */
struct TCU2CCU_DataYX22 {
    // BY ZF: Data2 字节（8 bit）
    uint8_t workStatus : 2;              // 工作状态（2 bit）
    uint8_t totalFault : 1;               // 总故障（1 bit）
    uint8_t totalAlarm : 1;               // 总告警（1 bit）
    uint8_t emergencyStopFault : 1;      // 急停按钮动作故障（1 bit）
    uint8_t smokeFault : 1;               // 烟雾故障（1 bit）
    uint8_t acInputBreakerFault : 1;     // 交流输入断路器故障（1 bit）
    uint8_t dcBusContactorFault : 1;     // 直流母线输出接触器拒动/误动故障（1 bit）
    
    // BY ZF: Data3 字节（8 bit）
    uint8_t dcBusFuseFault : 1;          // 直流母线输出熔断器故障（1 bit）
    uint8_t electronicLockFault : 1;     // 充电接口电子锁故障（1 bit）
    uint8_t fanFault : 1;                 // 充电桩风扇故障（1 bit）
    uint8_t lightningArresterFault : 1;  // 避雷器故障（1 bit）
    uint8_t insulationMonitorFault : 1;  // 绝缘监测故障（1 bit）
    uint8_t batteryReverseFault : 1;      // 电池极性反接故障（1 bit）
    uint8_t guideFault : 1;              // 充电中车辆控制导引故障（1 bit）
    uint8_t pileOverTempFault : 1;       // 充电桩过温故障（1 bit）
    
    // BY ZF: Data4 字节（8 bit）
    uint8_t interfaceOverTempFault : 1;   // 充电接口过温故障（1 bit）
    uint8_t gunNotReturnAlarm : 1;        // 充电枪未归位告警（1 bit）
    uint8_t bmsCommFault : 1;             // BMS通信故障（1 bit）
    uint8_t inputOverVoltageFault : 1;   // 输入电压过压故障（1 bit）
    uint8_t inputUnderVoltageFault : 1;  // 输入电压欠压故障（1 bit）
    uint8_t dcBusOverVoltageFault : 1;    // 直流母线输出过压故障（1 bit）
    uint8_t dcBusUnderVoltageFault : 1;   // 直流母线输出欠压故障（1 bit）
    uint8_t dcBusOverCurrentFault : 1;    // 直流母线输出过流故障（1 bit）
    
    // BY ZF: Data5 字节（8 bit）
    uint8_t moduleFault : 1;             // 充电模块故障（1 bit）
    uint8_t moduleAcInputFault : 1;        // 充电模块交流输入故障（1 bit）
    uint8_t moduleAcOverVoltageFault : 1;  // 充电模块交流输入过压故障（1 bit）
    uint8_t moduleAcUnderVoltageFault : 1; // 充电模块交流输入欠压故障（1 bit）
    uint8_t moduleAcPhaseLossFault : 1;   // 充电模块交流输入缺相故障（1 bit）
    uint8_t moduleDcShortFault : 1;       // 充电模块直流输出短路故障（1 bit）
    uint8_t moduleDcOverCurrentFault : 1; // 充电模块直流输出过流故障（1 bit）
    uint8_t moduleDcOverVoltageFault : 1;  // 充电模块直流输出过压故障（1 bit）
    
    // BY ZF: Data6 字节（8 bit）
    uint8_t moduleDcUnderVoltageFault : 1; // 充电模块直流输出欠压故障（1 bit）
    uint8_t moduleOverTempFault : 1;      // 充电模块过温故障（1 bit）
    uint8_t moduleCommFault : 1;           // 充电模块通信故障（1 bit）
    uint8_t moduleFanFault : 1;            // 充电模块风扇故障（1 bit）
    uint8_t vehicleConnectStatus : 1;      // 车辆连接状态（1 bit）
    uint8_t gunSeatStatus : 1;            // 充电桩充电枪座状态（1 bit）
    uint8_t electronicLockStatus : 1;     // 充电接口电子锁状态（1 bit）
    uint8_t dcContactorStatus : 1;        // 直流输出接触器状态（1 bit）
    
    // BY ZF: Data7～8 字节（16 bit）
    uint16_t otherFault;                  // 其它类型故障（2字节）
};

/**
 * 遥信数据23业务数据（YX23）
 * 对应数据：遥信数据23
 * 注意：使用位字段表示各种故障和状态
 */
struct TCU2CCU_DataYX23 {
    // BY ZF: Data2 字节（8 bit）
    uint8_t pileDoorFault : 1;            // 充电桩门禁故障（1 bit）
    uint8_t dcContactorStickFault : 1;    // 直流输出接触器粘连故障（1 bit）
    uint8_t insulationMonitorAlarm : 1;   // 绝缘监测告警（1 bit）
    uint8_t dischargeFault : 1;           // 泄放回路故障（1 bit）
    uint8_t pileOverTempAlarm : 1;        // 充电桩过温告警（1 bit）
    uint8_t gunOverTempAlarm : 1;         // 充电枪过温告警（1 bit）
    uint8_t acInputContactorFault : 1;     // 交流输入接触器拒动/误动故障（1 bit）
    uint8_t acInputContactorStickFault : 1; // 交流输入接触器粘连故障（1 bit）
    
    // BY ZF: Data3 字节（8 bit）
    uint8_t auxPowerFault : 1;            // 辅助电源故障（1 bit）
    uint8_t bridgeContactorFault : 1;     // 桥接接触器拒动/误动故障（1 bit）
    uint8_t bridgeContactorStickFault : 1;  // 桥接接触器粘连故障（1 bit）
    uint8_t bridgeContactorStatus : 1;      // 桥接接触器状态（1 bit）
    uint8_t moduleEmergencyStopStatus : 1;  // 模块紧急停机状态（1 bit）
    uint8_t pileWaterFault : 1;           // 充电桩水浸故障（1 bit）
    uint8_t cabinetDoorFault : 1;          // 充电机柜门禁故障（1 bit）
    uint8_t cabinetWaterFault : 1;        // 充电机柜水浸故障（1 bit）
    
    // BY ZF: Data4 字节（8 bit）
    uint8_t cabinetSmokeFault : 1;         // 充电机柜烟雾故障（1 bit）
    uint8_t powerCtrlCommFault : 1;        // 功率控制模块通信故障（1 bit）
    uint8_t powerCtrlCommAlarm : 1;        // 功率控制模块通信告警（1 bit）
    uint8_t powerCtrlFault : 1;            // 功率控制模块故障（1 bit）
    uint8_t powerCtrlAlarm : 1;            // 功率控制模块告警（1 bit）
    uint8_t switchModuleCommFault : 1;     // 开关模块通信故障（1 bit）
    uint8_t switchModuleCommAlarm : 1;     // 开关模块通信告警（1 bit）
    uint8_t switchModuleFault : 1;         // 开关模块故障（1 bit）
    
    // BY ZF: Data5 字节（8 bit）
    uint8_t switchModuleAlarm : 1;         // 开关模块告警（1 bit）
    uint8_t bmsFaultByCtrl : 1;            // 直流充电控制模块判断BMS故障（1 bit）
    uint8_t bmsSendFaultInfo : 1;          // BMS发送故障信息（1 bit）
    uint8_t moduleDischargeFault : 1;       // 充电模块泄放故障（1 bit）
    uint8_t vinReq : 1;                   // VIN请求/充电机请求即插即充启动（1 bit）
    uint8_t acInputContactorStatus : 1;    // 交流输入接触器状态（1 bit）
    uint8_t acContactorCtrlStatus : 1;      // 交流接触器控制状态（1 bit）
    uint8_t portK1CtrlStatus : 1;          // 充电端口直流接触器K1控制状态（1 bit）
    
    // BY ZF: Data6 字节（8 bit）
    uint8_t portK2CtrlStatus : 1;          // 充电端口直流接触器K2控制状态（1 bit）
    uint8_t reserved1 : 2;                 // 预留（2 bit）
    uint8_t portAuxPowerCtrlStatus : 1;    // 充电端口辅助电源输出开关控制状态（1 bit）
    uint8_t portAuxPowerCtrlStatus2 : 1;    // 充电端口辅助电源输出开关控制状态（1 bit，重复字段）
    uint8_t cabinetFanCtrlStatus : 1;      // 充电机柜风机开关控制状态（1 bit）
    uint8_t portLockCtrlStatus : 1;        // 充电端口电子锁控制状态（1 bit）
    
    // BY ZF: Data7 字节（8 bit）
    uint8_t peBreakFault : 1;              // PE断线故障（1 bit）
    uint8_t guideVoltageAbnormal : 1;       // 控制导引电压异常（1 bit）
    uint8_t moduleStartFail : 1;            // 充电模块启动失败（1 bit）
    uint8_t moduleShutdownFail : 1;         // 充电模块关机失败（1 bit）
    uint8_t moduleAddrAbnormal : 1;         // 充电模块地址异常（1 bit）
    uint8_t coolingSystemFault : 1;        // 散热系统故障（1 bit）
    uint8_t cabinetOverTempAlarm : 1;       // 充电机柜过温告警（1 bit）
    uint8_t cabinetOverTempFault : 1;      // 充电机柜过温故障（1 bit）
    
    // BY ZF: Data8 字节（8 bit）
    uint8_t powerDistStrategyFail : 1;      // 充电桩执行功率分配策略失败故障（1 bit）
    uint8_t noIdleModuleFault : 1;          // 无空闲模块可用故障（1 bit）
    uint8_t prechargeVoltageFail : 1;       // 预充阶段调压失败（1 bit）
    uint8_t powerCtrlCommTimeout : 1;       // 功率控制模块间通信超时（1 bit）
    uint8_t envHumidityAlarm : 1;           // 充电机环境湿度告警（1 bit）
    uint8_t reserved2 : 3;                 // 预留（3 bit）
};

#endif // TCU2CCU_DATA_H
