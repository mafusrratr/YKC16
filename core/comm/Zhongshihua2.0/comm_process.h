/**
 * tcu_comm process
 * BY ZF
 */

#ifndef COMM_PROCESS_H
#define COMM_PROCESS_H

#include "../../base/process/base_process.h"
#include "../../base/mqtt/mqtt_client.h"
#include "../../base/logger/log_sender.h"
#include <string>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>
#include <array>
#include <random>

struct cJSON;

// BY ZF: tcu_comm 配置项（固定从 /usr/app/config/tcu_comm.ini 读取）
struct CommConfig {
    uint8_t gunCount;                 // 内部枪数量
    std::string mqttHost;             // 内部 MQTT 地址
    int mqttPort;                     // 内部 MQTT 端口
    int mqttKeepalive;                // MQTT keepalive 秒
    std::string mqttClientId;         // MQTT clientId
    std::string mqttTopicPrefix;      // 主题前缀（默认 tcu）
    std::string mqttUsername;         // MQTT 用户名（可空）
    std::string mqttPassword;         // MQTT 密码（可空）


    std::string masterHost;           // 平台主站 IP
    int masterPort;                   // 平台主站端口
    int tcpReconnectSec;              // TCP 断线重连间隔

    std::string cdzNo;                // 充电终端编号
    std::string loginId;              // 登录ID（用于生成8字节登录秘钥）
    std::string macAddr;              // 24位十六进制 MAC（ASCII）
    std::string factoryCreditCode;    // 企业信用代码
    std::vector<std::string> gunQrCodeList; // 每枪二维码配置
    std::string sm2PublicKey;         // 平台提供的初始SM2公钥（HEX/ASCII）
    uint8_t chargerType;              // 桩类型（固定0x01：直流）
    std::vector<uint32_t> gunIdList;  // 枪ID列表（每枪4字节）
    std::vector<uint8_t> gunTypeList; // 枪类型列表（固定0x01：直流枪）
    int tcpHeartbeatSec;              // 登录成功后的心跳周期
    int loginRetrySec;                // 登录阶段重发周期
    bool offlineRunMode;              // 离线运行模式（开启后平台链路事件始终上报在线）
    bool debugTcp;                    // TCP调试开关（打印TX/RX与帧类型）

    
    CommConfig()
        : gunCount(1)
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttTopicPrefix("tcu")
        , masterPort(9000)
        , tcpReconnectSec(3)
        , chargerType(0x01)
        , tcpHeartbeatSec(30)
        , loginRetrySec(10)
        , offlineRunMode(false)
        , debugTcp(false)
    {}
};

class CommProcess : public BaseProcess {
public:
    CommProcess();
    virtual ~CommProcess();

    // BY ZF: BaseProcess 生命周期回调
    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
    // BY ZF: 启动完成(start_complete)缓存，供0x2D/0x15组帧复用。
    struct StartCompleteData {
        uint8_t successFlag;
        uint8_t failReason;
        std::array<uint8_t, 3> pileBmsVersion;
        uint8_t batteryType;
        uint16_t ratedCapacity;
        uint16_t ratedTotalVoltage;
        uint16_t cellMaxChargeVoltage;
        uint16_t bmsMaxChargeVoltage;
        uint16_t maxAllowChargeCurrent;
        uint16_t currentTotalVoltage;
        uint8_t maxAllowTemp;
        uint16_t pileMaxOutputVoltage;
        uint16_t pileMinOutputVoltage;
        uint16_t pileMaxOutputCurrent;
        uint16_t pileMinOutputCurrent;
        std::string batteryManufacturer;
        std::array<uint8_t, 4> batterySerial;
        uint8_t batteryPropertyFlag;
        uint8_t batteryProdYear;
        uint8_t batteryProdMonth;
        uint8_t batteryProdDay;
        std::array<uint8_t, 3> batteryChargeCount;
        uint8_t nominalEnergy;
        uint8_t soc;
        std::string vin;
        std::array<uint8_t, 8> bmsSoftwareVersion;
        uint8_t insulationFault;

        StartCompleteData()
            : successFlag(1)
            , failReason(0)
            , pileBmsVersion{{0}}
            , batteryType(0)
            , ratedCapacity(0)
            , ratedTotalVoltage(0)
            , cellMaxChargeVoltage(0)
            , bmsMaxChargeVoltage(0)
            , maxAllowChargeCurrent(0)
            , currentTotalVoltage(0)
            , maxAllowTemp(0)
            , pileMaxOutputVoltage(0)
            , pileMinOutputVoltage(0)
            , pileMaxOutputCurrent(0)
            , pileMinOutputCurrent(0)
            , batterySerial{{0}}
            , batteryPropertyFlag(0)
            , batteryProdYear(0)
            , batteryProdMonth(0)
            , batteryProdDay(0)
            , batteryChargeCount{{0}}
            , nominalEnergy(0)
            , soc(0)
            , bmsSoftwareVersion{{0}}
            , insulationFault(0)
        {}
    };

    // BY ZF: 分时段计费缓存项。
    struct FeeSegmentData {
        std::string startTs;
        std::string endTs;
        double energyKwh;
        double electricAmount;
        double serviceAmount;

        FeeSegmentData()
            : energyKwh(0.0)
            , electricAmount(0.0)
            , serviceAmount(0.0)
        {}
    };

    // BY ZF: 每枪运行态缓存（来自 MQTT 上行数据）。
    struct GunRuntimeData {
        // 启动命令解析结果
        std::array<uint8_t, 8> startTimeBcd;
        std::string chargeUserNo;
        std::string orderNo;
        int chargeMode;
        double prechargeAmount;
        int userStatus;
        uint8_t billingFlag;
        
        StartCompleteData startCompleteData;
        
        // 遥信数据
        uint8_t gunStatus;             // 枪工作状态（00空闲/01连接/02工作/03故障/04停止）
        uint8_t yxWorkStatus;          // 遥信-工作状态
        uint8_t yxTotalFault;          // 遥信-总故障
        uint8_t yxTotalAlarm;          // 遥信-总告警
        uint8_t yxEmergencyStopFault;  // 遥信-急停故障
        uint8_t yxVehicleConnectStatus;// 遥信-车辆连接状态
        uint8_t yxVinReq;              // 遥信-vinReq
        uint8_t yxGunSeatStatus;       // 遥信-枪座状态
        uint8_t yxElectronicLockStatus;// 遥信-电子锁状态
        uint8_t yxDcContactorStatus;   // 遥信-直流接触器状态
        uint8_t yxOtherFault;          // 遥信-otherFault

        //遥测数据
        double voltage;             // 充电输出电压（V）
        double current;             // 充电输出电流（A）
        double soc;                 // SOC（%）
        double batteryMinTemp;      // 电池组最低温度（℃）
        double batteryMaxTemp;      // 电池组最高温度（℃）
        double cellMaxVoltage;      // 单体电池最高电压（V）
        double cellMinVoltage;      // 单体电池最低电压（V）
        double pileEnvTemp;         // 充电机环境温度（℃）
        double guideVoltage;        // 充电导引电压（V）
        double bmsReqVoltage;       // BMS需求电压（V）
        double bmsReqCurrent;       // BMS需求电流（A）
        int ycChargeMode;           // 充电模式
        double bmsMeasuredVoltage;  // BMS充电电压测量值（V）
        double bmsMeasuredCurrent;  // BMS充电电流测量值（A）
        double chargedTime;         // 充电时长 (s)
        double estimatedRemainTime; // 估算剩余充电时间（min）
        double interfaceTemp1;      // 充电接口温度探头1（℃）
        double interfaceTemp2;      // 充电接口温度探头2（℃）
        double interfaceTemp3;      // 充电接口温度探头3（℃）
        double interfaceTemp4;      // 充电接口温度探头4（℃）
        int maxVoltageCellNo;       // 最高单体动力蓄电池电压所在编号
        int maxTempPointNo;         // 最高温度检测点编号
        int minTempPointNo;         // 最低动力蓄电池温度检测点编号
        double inletTemp;           // 充电机进风口温度（℃）
        double outletTemp;          // 充电机出风口温度（℃）
        double envHumidity;         // 充电机环境湿度（%）


        //遥脉数据
        double totalEnergy;
        double totalAmount;
        double electricAmount;
        double serviceAmount;
        
        double meterEnergy;         // 电表示数 (kWh)
        double meterVoltage;        // meter voltage (V)
        double meterCurrent;        // meter current (A)

        std::string feeModelId;
        int feeTimeNum;
        std::vector<FeeSegmentData> feeSegments;
        std::string pendingRecordTradeNo;  // 最近一次0x3D上送记录的tradeNo
        std::string pendingVinAuthVin;     // 最近一次VIN鉴权请求缓存VIN
        uint8_t pendingVinAuthPlugAndChargeFlag; // 最近一次VIN鉴权请求即插即充标志
        uint8_t pendingVinAuthMergeChargeFlag;   // 最近一次VIN鉴权请求并充标志

        GunRuntimeData()
            : startTimeBcd{{0}}
            , chargeMode(0)
            , prechargeAmount(0.0)
            , userStatus(0)
            , billingFlag(0)
            , gunStatus(0)
            , yxWorkStatus(0)
            , yxTotalFault(0)
            , yxTotalAlarm(0)
            , yxEmergencyStopFault(0)
            , yxVehicleConnectStatus(0)
            , yxVinReq(0)
            , yxGunSeatStatus(0)
            , yxElectronicLockStatus(0)
            , yxDcContactorStatus(0)
            , yxOtherFault(0)
            , voltage(0.0)
            , current(0.0)
            , soc(0.0)
            , batteryMinTemp(0.0)
            , batteryMaxTemp(0.0)
            , cellMaxVoltage(0.0)
            , cellMinVoltage(0.0)
            , pileEnvTemp(0.0)
            , guideVoltage(0.0)
            , bmsReqVoltage(0.0)
            , bmsReqCurrent(0.0)
            , ycChargeMode(0)
            , bmsMeasuredVoltage(0.0)
            , bmsMeasuredCurrent(0.0)
            , chargedTime(0.0)
            , estimatedRemainTime(0.0)
            , interfaceTemp1(0.0)
            , interfaceTemp2(0.0)
            , interfaceTemp3(0.0)
            , interfaceTemp4(0.0)
            , maxVoltageCellNo(0)
            , maxTempPointNo(0)
            , minTempPointNo(0)
            , inletTemp(0.0)
            , outletTemp(0.0)
            , envHumidity(0.0)
            , totalEnergy(0.0)
            , totalAmount(0.0)
            , electricAmount(0.0)
            , serviceAmount(0.0)
            , meterEnergy(0.0)
            , meterVoltage(0.0)
            , meterCurrent(0.0)
            , feeTimeNum(0)
            , pendingVinAuthPlugAndChargeFlag(0x02)
            , pendingVinAuthMergeChargeFlag(0x00)
        {}
    };

    // BY ZF: 中石化2.0上线状态机：登录认证 -> 计费模型同步 -> 在线心跳
    enum PlatformLoginState {
        LOGIN_IDLE = 0,          // 已连TCP，等待发起登录认证
        LOGIN_REQ_AUTH,          // 周期发送0x01登录认证，请求0x02应答
        LOGIN_REQ_FEE_MODEL,     // 周期发送0x0D枪计费模型请求，等待0x0A应答
        LOGIN_REQ_TIME_SYNC,     // 周期发送0x0B对时请求，等待0x0C应答
        LOGIN_ONLINE             // 已上线，维持0x03心跳与业务上送
    };

    // BY ZF: 初始化阶段
    bool loadConfig();
    bool initMqtt();

    // BY ZF: MQTT 回调入口
    void onMqttMessage(const std::string& topic, const std::string& payload);
    void onMqttConnected(int rc);

    // BY ZF: 主题路由
    bool parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const;
    bool publishPlatCmd(uint8_t gun, const std::string& payload);
    bool handleLogicEventForPlatform(uint8_t gun, const std::string& payload);
    bool handleLogicFeeForPlatform(uint8_t gun, const std::string& payload);
    bool handlePileDataForPlatform(uint8_t gun, const std::string& payload);
    bool handlePileEventForPlatform(uint8_t gun, const std::string& payload);
    bool handleMeterDataForPlatform(uint8_t gun, const std::string& payload);
    void publishInitialSetConfig();

    std::string ensureGunField(const std::string& payload, uint8_t gun) const; // 确保 payload 含 gun 字段
    std::string buildTopic(const char* module, uint8_t gun, const char* leaf) const;

    // BY ZF: 平台 TCP 链路与登录状态机
    bool connectPlatformTcp();
    void closePlatformTcp();
    void maintainPlatformTcp();
    void driveLoginStateMachine(const std::chrono::steady_clock::time_point& now);

    // BY ZF: 原始文本发送（仅保留兼容入口，不作为主路径）
    bool sendPlatformText(const std::string& text);

    // BY ZF: 大端二进制帧封装/发送
    std::vector<uint8_t> buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride = -1);
    bool sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride = -1);
    bool sendRemoteStopAck(uint8_t gunNoBcd, uint8_t result, uint8_t failReason, int seqOverride = -1);

    // BY ZF: 中石化2.0上线阶段信息体构造
    std::vector<uint8_t> buildLoginRequestBody() const;      // 0x01 登录认证请求体
    std::vector<uint8_t> buildFeeModelRequestBody(uint8_t gunNoBcd) const; // 0x0D 枪计费模型请求体
    std::vector<uint8_t> buildTimeSyncRequestBody() const;   // 0x0B 对时请求体
    std::vector<uint8_t> buildHeartbeatBody();               // 0x03 心跳请求体
    std::vector<uint8_t> buildChargeInfoBody(uint8_t gun);
    std::vector<uint8_t> buildStartChargeResultBody(uint8_t gun) const; // 0x2D 启动完成结果体
    std::vector<uint8_t> buildBrmBody(uint8_t gun) const; // 0x15 BRM上送体
    std::vector<uint8_t> buildBcpBody(uint8_t gun) const; // 0x17 BCP参数配置上送体
    std::vector<uint8_t> buildChargeEndStageBody(uint8_t gun, cJSON* stopCompleteData) const; // 0x19 结束阶段上送体
    std::vector<uint8_t> buildBclBcsCcsBody(uint8_t gun) const; // 0x23 BCL/BCS/CCS上送体
    std::vector<uint8_t> buildBstBody(uint8_t gun, cJSON* stopCompleteData) const; // 0x1D BST停止上送体
    std::vector<uint8_t> buildCstBody(uint8_t gun) const; // 0x21 CST充电中止上送体
    std::vector<uint8_t> buildBsmBody(uint8_t gun) const; // 0x25 BSM充电中止BMS信息体
    std::vector<uint8_t> buildVinStartApplyBody(uint8_t gun, cJSON* data);
    std::vector<uint8_t> buildRemoteStartAckBody(uint8_t gun, uint8_t result) const; // 0xA7 远程启动应答体
    std::vector<uint8_t> buildQrCodeSetAckBody(uint8_t gunNoBcd, uint8_t result) const; // 0x5B 二维码设置应答体
    void reportChargeInfoPeriodic();

    // BY ZF: 平台来包解析
    void handlePlatformRxData(const char* data, size_t len);
    void processPlatformPacket(const uint8_t* frame, size_t frameLen);

    // BY ZF: 通用工具函数
    bool isHexString(const std::string& s, size_t needLen) const;
    static uint16_t calcCrc16Modbus(const uint8_t* data, size_t len);
    static uint8_t bcdByte(int value);
    static void appendU16BE(std::vector<uint8_t>& out, uint16_t v);
    static void appendU32BE(std::vector<uint8_t>& out, uint32_t v);
    static void appendU64BE(std::vector<uint8_t>& out, uint64_t v);
    static uint16_t readU16BE(const uint8_t* p);
    static uint32_t readU32BE(const uint8_t* p);
    static std::string toHex(const uint8_t* data, size_t len);
    static std::string bcdToDigitString(const uint8_t* data, size_t len);
    static std::string bcdHourToHhmm(uint8_t bcdHour);
    static uint8_t hhmmToBcdHour(const std::string& hhmm);
    static void appendOrderIdBcd10(std::vector<uint8_t>& out, const std::string& orderNo);
    bool syncSystemTime(int year, int month, int day, int hour, int minute, int second);
    void resetCryptoSession();
    void prepareLoginCryptoContext();
    bool tryUpdateSm2PubKeyFromLoginAck(const uint8_t* body, size_t bodyLen);
    bool persistSm2PubKeyToIni(const std::string& pubKey);

    // BY ZF: 平台命令解析
    bool parseRemoteStart0A8(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel);
    bool parseStartApplyAck0A6(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel);
    bool parseRemoteStop036(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData);
    bool parseRecordConfirm040(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData);
    bool parseQrCodeSet05A(const uint8_t* body, size_t bodyLen, uint8_t& gun, uint8_t& gunNoBcd, cJSON** outData) const;
    bool parseFeeModelAck00A(const uint8_t* body, size_t bodyLen, FeeModel& feeModel);
    bool buildChargeRecordBodyFromUpdateRecord(uint8_t gun, cJSON* data, std::vector<uint8_t>& body);
    uint16_t mapTradeStopReasonToPlatform(int mqttReason) const;
    std::string buildGunQrCode(uint8_t gun) const;

    // BY ZF: 平台命令发布
    bool publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj);
    bool publishSetConfig(uint8_t gun, cJSON* dataObj);
    void publishPlatformLinkEvent(bool online, const char* reason);
    bool persistGunQrCodeToIni(uint8_t gun, const std::string& qrCode);

private:
    CommConfig m_config;                              // 运行配置
    MqttClient m_mqtt;                                // 内部 MQTT 客户端
    LogSender m_logSender;                            // 日志输出
    std::atomic<uint64_t> m_seq;                      // 本进程序号
    std::atomic<bool> m_platformConnected;            // 平台 TCP 连通状态
    int m_tcpFd;                                      // 平台 TCP socket fd
    PlatformLoginState m_loginState;                  // 登录状态机状态
    std::chrono::steady_clock::time_point m_lastTcpConnectTry; // 最近重连尝试时间
    std::chrono::steady_clock::time_point m_lastLoginAction;   // 最近登录动作时间
    std::chrono::steady_clock::time_point m_nextLoginAllowedTime; // 登录流程限流截止时间
    std::chrono::steady_clock::time_point m_lastHeartbeat;     // 最近心跳发送时间


    std::chrono::steady_clock::time_point m_lastHeartbeatRecv;     // 最近心跳应答接受时间

    std::chrono::steady_clock::time_point m_lastChargeInfoReport; // reportChargeInfoPeriodic调度节拍
    std::chrono::steady_clock::time_point m_lastPeriodicSetConfigPublish; // 周期setConfig广播节拍
    std::vector<std::chrono::steady_clock::time_point> m_lastChargeInfoReportByGun; // 每枪最近0x13上报时间
    std::vector<std::chrono::steady_clock::time_point> m_lastSetConfigPublishByGun;  // 每枪最近setConfig发布时间
    std::vector<std::string> m_lastSetConfigPayloadByGun;                              // 每枪最近setConfig内容
    std::vector<uint8_t> m_runtimeChangedByGun;      // 每枪运行态变化标记（1=有变化待立即上送）
    
    std::vector<uint8_t> m_tcpRxCache;                // TCP 粘包缓存
    std::vector<GunRuntimeData> m_gunRuntimeData;     // 来自 pile/logic 的实时业务数据缓存
    std::vector<FeeModel> m_feeModelByGun;            // 按枪保存的当前计费模型
    uint8_t m_heartbeatCounter;                       // 心跳计数

    std::array<uint8_t, 16> m_sm4SessionKey;         // 登录后会话SM4密钥A（16字节）
    bool m_sm4SessionKeyReady;                       // 会话密钥已生成并可用于解密
    bool m_loginCryptoPrepared;                      // 当前登录轮次已完成密钥准备
    bool m_platformOnlineEventActive;                // 平台在线事件状态（完成登录流程后才为true）
    std::string m_sm2PublicKeyActive;                // 当前生效SM2公钥（可被平台下发更新）
    
};

#endif // COMM_PROCESS_H
