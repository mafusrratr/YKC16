/**
 * ChargeLogic process
 * BY ZF
 */

#ifndef CHARGE_LOGIC_PROCESS_H
#define CHARGE_LOGIC_PROCESS_H

#include "../base/process/base_process.h"
#include "../base/common/config_manager_lite.h"
#include "../base/mqtt/mqtt_client.h"
#include "../base/logger/log_sender.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <thread>
#include <chrono>

struct cJSON;

// BY ZF: tcu_logic 配置项（来自 /usr/app/config/tcu_logic.ini）
struct ChargeLogicConfig {
    uint8_t gunCount;               // 枪数量
    std::string mqttHost;           // MQTT broker 地址
    int mqttPort;                   // MQTT broker 端口
    int mqttKeepalive;              // MQTT keepalive 秒数
    std::string mqttClientId;       // MQTT client id
    std::string mqttTopicPrefix;    // 主题前缀（默认 tcu）
    std::string pileConfigPath;     // pile_controller 配置路径（用于复用枪数）
    double prechargeStopMargin;     // 预充触发停机的剩余金额阈值

    ChargeLogicConfig()
        : gunCount(1)
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttTopicPrefix("tcu")
        , prechargeStopMargin(1.0)
    {}
};

class ChargeLogicProcess : public BaseProcess {
public:
    ChargeLogicProcess();
    virtual ~ChargeLogicProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
    // BY ZF: 充电主状态机
    enum ChargeState {
        STATE_IDLE = 0,
        STATE_PREPARE,
        STATE_STARTING,
        STATE_CHARGING,
        STATE_STOPPING,
        STATE_STOPPED,
        STATE_ERROR
    };

    // BY ZF: 内部事件（统一驱动状态迁移）
    enum EventType {
        EVT_START_CMD = 0,
        EVT_STOP_CMD,
        EVT_RESET_ERROR,
        EVT_AUTH_OK,
        EVT_AUTH_FAIL,
        EVT_START_RESPONSE_OK,
        EVT_START_RESPONSE_FAIL,
        EVT_START_COMPLETE_OK,
        EVT_START_COMPLETE_FAIL,
        EVT_STOP_COMPLETE,
        EVT_DEVICE_ERR,
        EVT_TOTAL_FAULT,
        EVT_WORKSTATUS_ZERO,
        EVT_VEHICLE_CONNECTED,
        EVT_VEHICLE_DISCONNECTED,
        EVT_METER_STALE
    };

    struct GunState {
        ChargeState state;                      // 当前状态
        uint8_t lastWorkStatus;                 // 遥信工作状态缓存
        uint8_t lastVehicleConnectStatus;       // 遥信车辆连接状态缓存
        uint8_t lastTotalFault;                 // 遥信总故障位缓存
        bool hasWorkStatus;                     // 是否收到过 workStatus
        bool hasVehicleConnectStatus;           // 是否收到过 vehicleConnectStatus
        bool hasTotalFault;                     // 是否收到过 totalFault
        bool hasMeterValue;                     // 是否收到过电量
        double lastMeterValue;                  // 最近电量(kWh)
        bool hasMeterVoltage;                   // 是否收到过电压
        double lastMeterVoltage;                // 最近电压(V)
        bool hasMeterCurrent;                   // 是否收到过电流
        double lastMeterCurrent;                // 最近电流(A)
        bool hasTotalAmount;                    // 是否有总金额
        double lastTotalAmount;                 // 最近总金额(元)
        uint8_t meterStableCount;               // 电量连续不变化次数
        bool tcuStopReqSent;                    // 是否已发送 tcu_stop_request

        // BY ZF: 启动鉴权/订单基础信息（HMI/平台 start_charge.data）
        bool hasAuthBasis;                      // 是否已具备启动基础参数
        uint64_t startTimeMs;                   // 启动请求时间戳(ms)
        int startType;                          // 启动方式
        unsigned int chargeStartTime;           // 充电开始 UTC 秒
        unsigned int chargeEndTime;             // 充电结束 UTC 秒
        std::string chargeUserNo;               // 用户号
        std::string orderNo;                    // 订单号
        std::string preTradeNo;                 // 平台交易流水
        std::string tradeNo;                    // 本地交易流水
        bool hasVinCode;                        // VIN 是否有效
        std::string vinCode;                    // VIN
        int startSoc;                           // 启动 SOC
        int endSoc;                             // 结束 SOC
        unsigned int stopReason;                // 停止原因
        int chargeMode;                         // 充电模式
        double prechargeAmount;                 // 预充值金额(元)
        int feeModelNo;                         // 计费模型编号
        std::string feeModelId;                 // 计费模型ID
        uint8_t feeTimeNum;                     // 时段数
        std::vector<int> feeTimeSegMinutes;     // 时段起点(分钟, 0~1439)
        std::vector<double> feeChargePricePerKwh;   // 各时段电价(元/kWh)
        std::vector<double> feeServicePricePerKwh;  // 各时段服务费(元/kWh)
        std::vector<double> feeSegEnergyKwh;         // 各时段累计电量(kWh)
        std::vector<double> feeSegElectricAmount;    // 各时段累计电费(元)
        std::vector<double> feeSegServiceAmount;     // 各时段累计服务费(元)
        bool feeInitialized;                    // 计费模型是否完成初始化
        bool feeHasEnergyBase;                  // 是否已锁定计费起始电量
        double feeEnergyBaseKwh;                // 计费起始电量(kWh)
        double feeLastEnergyKwh;                // 上次计费电量(kWh)
        double feeTotalEnergyKwh;               // 总电量(kWh)
        double feeTotalElectricAmount;          // 总电费(元)
        double feeTotalServiceAmount;           // 总服务费(元)

        bool pendingStart;                      // 已收 start_charge，等待 PREPARE 窗口处理
        bool stopCompleteSeen;                  // 是否收到 stop_complete
        std::string pendingStartData;           // 缓存启动命令 data JSON
        std::chrono::steady_clock::time_point startingEnterTime;  // 进入 STARTING 时间
        std::chrono::steady_clock::time_point stoppingEnterTime;  // 进入 STOPPING 时间
        std::chrono::steady_clock::time_point lastMeterMsgTime;   // 最近电表消息时间
        std::chrono::steady_clock::time_point lastMeterValueTime; // 最近电量变化时间
        std::chrono::steady_clock::time_point lastStopCmdTime;    // 最近 stop_charge 下发时间
        GunState()
            : state(STATE_IDLE)
            , lastWorkStatus(0)
            , lastVehicleConnectStatus(0)
            , lastTotalFault(0)
            , hasWorkStatus(false)
            , hasVehicleConnectStatus(false)
            , hasTotalFault(false)
            , hasMeterValue(false)
            , lastMeterValue(0.0)
            , hasMeterVoltage(false)
            , lastMeterVoltage(0.0)
            , hasMeterCurrent(false)
            , lastMeterCurrent(0.0)
            , hasTotalAmount(false)
            , lastTotalAmount(0.0)
            , meterStableCount(0)
            , tcuStopReqSent(false)
            , hasAuthBasis(false)
            , startTimeMs(0)
            , startType(0)
            , chargeStartTime(0)
            , chargeEndTime(0)
            , hasVinCode(false)
            , startSoc(0)
            , endSoc(0)
            , stopReason(0)
            , chargeMode(0)
            , prechargeAmount(0.0)
            , feeModelNo(0)
            , feeTimeNum(0)
            , feeInitialized(false)
            , feeHasEnergyBase(false)
            , feeEnergyBaseKwh(0.0)
            , feeLastEnergyKwh(0.0)
            , feeTotalEnergyKwh(0.0)
            , feeTotalElectricAmount(0.0)
            , feeTotalServiceAmount(0.0)
            , pendingStart(false)
            , stopCompleteSeen(false)
        {}
    };

    // BY ZF: 初始化配置与 MQTT
    bool loadConfig();
    bool initMqtt();

    // BY ZF: MQTT 消息分发入口
    void onMqttMessage(const std::string& topic, const std::string& payload);
    // BY ZF: 各来源命令/数据处理
    void handleLogicCmd(uint8_t gun, const std::string& cmd, cJSON* data);
    void handlePlatCmd(uint8_t gun, const std::string& cmd, cJSON* data);
    void handlePileEvent(uint8_t gun, const std::string& type, cJSON* data);
    void handlePileData(uint8_t gun, const std::string& type, cJSON* data);
    void handleMeterData(uint8_t gun, cJSON* data);

    // BY ZF: 对外发布（下发 pile 命令、上报 logic 事件/计费）
    void publishPileCmd(uint8_t gun, const std::string& cmd, cJSON* data);
    void publishLogicEvent(uint8_t gun, const std::string& event, cJSON* data);
    void publishFeeData(uint8_t gun);
    void publishStateChange(uint8_t gun, ChargeState from, ChargeState to, const char* reason);
    // BY ZF: 结束充电后记录交易日志
    void logTradeRecordOnStopped(uint8_t gun, const char* reason);
    // BY ZF: 启动鉴权参数与计费模型解析
    void updateAuthBasis(uint8_t gun, cJSON* data, const char* source);
    bool parseFeeModel(uint8_t gun, cJSON* data);
    // BY ZF: 计费相关工具函数
    int getSegmentIndexByMinute(const GunState& gs, int minuteOfDay) const;
    int getCurrentMinuteOfDay() const;
    void applyEnergyDeltaToFee(uint8_t gun, double deltaKwh);
    void maybeTriggerTcuStopByPrecharge(uint8_t gun);
    // BY ZF: 状态机入口与迁移
    void handleEvent(uint8_t gun, EventType evt, const char* reason);
    void transitionTo(uint8_t gun, ChargeState to, const char* reason);
    void enterStopping(uint8_t gun, const char* reason);
    void resetChargeSessionState(uint8_t gun);
    static const char* stateToString(ChargeState s);

private:
    ChargeLogicConfig m_config;
    std::vector<GunState> m_gunStates;  // 每枪独立状态上下文
    MqttClient m_mqtt;                   // MQTT 基础客户端（复用 core/base/mqtt）
    LogSender m_logSender;               // 日志/交易记录发送
    std::atomic<uint64_t> m_seq;         // 本进程序列号（MQTT payload seq）
    std::thread m_mainThread;            // 主循环线程
};

#endif // CHARGE_LOGIC_PROCESS_H
