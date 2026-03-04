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
    std::string macAddr;              // 24位十六进制 MAC（ASCII）
    std::string factoryCreditCode;    // 企业信用代码
    uint8_t chargerType;              // 桩类型（固定0x01：直流）
    std::vector<uint32_t> gunIdList;  // 枪ID列表（每枪4字节）
    std::vector<uint8_t> gunTypeList; // 枪类型列表（固定0x01：直流枪）
    int tcpHeartbeatSec;              // 登录成功后的心跳周期
    int loginRetrySec;                // 登录阶段重发周期
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

        //遥测数据
        double voltage;
        double current;


        //遥脉数据
        double totalEnergy;
        double totalAmount;
        double electricAmount;
        double serviceAmount;
        std::string feeModelId;
        int feeTimeNum;
        std::vector<FeeSegmentData> feeSegments;
        std::string pendingRecordTradeNo;  // 最近一次0x60上送记录的tradeNo
        uint8_t pendingCtrlCmd;            // BY ZF: 待回控制命令（0x00启动/0x01停止/0xFF无）
        std::chrono::steady_clock::time_point pendingCtrlSince; // BY ZF: 控制命令等待应答起始时刻

        GunRuntimeData()
            : startTimeBcd{{0}}
            , chargeMode(0)
            , prechargeAmount(0.0)
            , userStatus(0)
            , billingFlag(0)
            , voltage(0.0)
            , current(0.0)
            , totalEnergy(0.0)
            , totalAmount(0.0)
            , electricAmount(0.0)
            , serviceAmount(0.0)
            , feeTimeNum(0)
            , pendingCtrlCmd(0xFF)
            , pendingCtrlSince()
        {}
    };

    // BY ZF: 平台登录流程状态机（模板）
    enum PlatformLoginState {
        LOGIN_IDLE = 0,        // 已连 TCP，等待发起 setConfig 请求
        LOGIN_REQ_SET_CONFIG,  // 周期发送 setConfig 请求(0x00)，等待 0x10
        LOGIN_REQ_LOGIN,       // 周期发送登录请求(0x20)，等待 0x30
        LOGIN_DONE             // 登录完成，维持心跳
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
    std::vector<uint8_t> buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body);
    bool sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body);

    // BY ZF: 登录阶段业务信息体构造
    std::vector<uint8_t> buildSetConfigRequestBody() const; // 0x00
    std::vector<uint8_t> buildLoginRequestBody() const;     // 0x20
    std::vector<uint8_t> buildHeartbeatBody();
    std::vector<uint8_t> buildChargeInfoBody(uint8_t gun);
    std::vector<uint8_t> buildRemoteStartAckBody(uint8_t gun, uint8_t result);
    std::vector<uint8_t> buildRemoteStopAckBody(uint8_t gun, uint8_t result);
    void checkRemoteCtrlAckTimeout(const std::chrono::steady_clock::time_point& now);
    void reportChargeInfoPeriodic();

    // BY ZF: 平台来包解析
    void handlePlatformRxData(const char* data, size_t len);
    void processPlatformPacket(const uint8_t* frame, size_t frameLen);

    // BY ZF: 将平台参数下发到内部 setConfig 主题
    bool publishSetConfig(uint8_t gun, cJSON* data);

    // BY ZF: 通用工具函数
    bool isHexString(const std::string& s, size_t needLen) const;
    static uint16_t calcCrc16Modbus(const uint8_t* data, size_t len);
    static uint8_t bcdByte(int value);
    static void appendU16BE(std::vector<uint8_t>& out, uint16_t v);
    static void appendU32BE(std::vector<uint8_t>& out, uint32_t v);
    static uint16_t readU16BE(const uint8_t* p);
    static uint32_t readU32BE(const uint8_t* p);
    static std::string toHex(const uint8_t* data, size_t len);
    static std::string bcdToDigitString(const uint8_t* data, size_t len);
    static std::string bcdHourToHhmm(uint8_t bcdHour);
    static uint8_t hhmmToBcdHour(const std::string& hhmm);
    static void appendOrderIdBcd10(std::vector<uint8_t>& out, const std::string& orderNo);
    bool syncSystemTime(int year, int month, int day, int hour, int minute, int second);

    // BY ZF: 平台命令解析
    bool extractSetConfigData(uint8_t cmd, const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData);
    bool parseRemoteStart014(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel);
    bool parseRemoteStop015(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData);
    bool parseRecordConfirm070(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData);
    bool buildChargeRecordBodyFromUpdateRecord(uint8_t gun, cJSON* data, std::vector<uint8_t>& body);

    // BY ZF: 平台命令发布
    bool publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj);

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
    std::chrono::steady_clock::time_point m_lastHeartbeat;     // 最近心跳发送时间
    std::chrono::steady_clock::time_point m_lastChargeInfoReport; // 最近0x22上报时间
    std::vector<uint8_t> m_tcpRxCache;                // TCP 粘包缓存
    
    std::vector<uint8_t> m_gunStatus;                 // 心跳上报枪状态缓存（00空闲/01连接/02工作/03故障）
    std::vector<uint8_t> m_vehicleConnectStatus;      // 来自 pile yx 的车辆连接状态（0未连接/1已连接）
    std::vector<GunRuntimeData> m_gunRuntimeData;     // 来自 pile/logic 的实时业务数据缓存
    std::vector<FeeModel> m_feeModelByGun;            // 按枪保存的当前计费模型
    uint8_t m_heartbeatCounter;                       // 心跳计数
};

#endif // COMM_PROCESS_H
