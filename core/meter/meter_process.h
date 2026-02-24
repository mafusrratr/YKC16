/**
 * Meter process (RS485 DLT645 -> MQTT)
 * BY ZF
 */

#ifndef TCU_METER_PROCESS_H
#define TCU_METER_PROCESS_H

#include "../base/process/base_process.h"
#include "../base/mqtt/mqtt_client.h"
#include "dlt645_meter.h"
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <map>

struct MeterConfig {
    struct GunConfig {
        std::string meterAddr;  // 电表地址
        MeterSerialConfig serial; // 串口参数（每枪可独立配置）
    };

    uint8_t gunCount; // 枪数量

    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;

    int pollIntervalMs;
    int readTimeoutMs;
    int retryCount;
    bool debug645;

    // BY ZF: 全局默认串口参数，枪级可覆盖
    MeterSerialConfig serialDefault;
    std::vector<GunConfig> guns;

    MeterConfig()
        : gunCount(1)
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttTopicPrefix("tcu")
        , pollIntervalMs(500)
        , readTimeoutMs(200)
        , retryCount(2)
        , debug645(false)
    {}
};

class MeterProcess : public BaseProcess {
public:
    MeterProcess();
    virtual ~MeterProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
    struct PortRuntime {
        MeterSerialConfig serial;
        Dlt645Meter meter;
        bool online;

        PortRuntime()
            : online(false)
        {}
    };

    struct GunRuntime {
        bool online;   // 当前在线状态
        bool inited;   // 是否已产生过首次状态
        uint64_t lastTs; // 最近成功读表时间戳(ms)
        int portIndex; // 该枪对应的串口实例
        MeterReading lastReading;

        GunRuntime() : online(false), inited(false), lastTs(0), portIndex(-1) {}
    };

    bool loadConfig();
    bool initMqtt();
    bool initMeter();

    // BY ZF: 单轮轮询：按枪读表并上报 data/event
    void pollOnce();
    bool readGunMeter(uint8_t gun, MeterReading& out, std::string& err);
    void publishData(uint8_t gun, const MeterReading& value);
    void publishEvent(uint8_t gun, const std::string& event, const std::string& reason);
    static std::string buildPortKey(const MeterSerialConfig& sc);

private:
    MeterConfig m_cfg;
    MqttClient m_mqtt;
    std::vector<PortRuntime> m_ports;
    std::vector<GunRuntime> m_gunState;
    std::atomic<uint64_t> m_seq;
    std::chrono::steady_clock::time_point m_lastPoll;
};

#endif // TCU_METER_PROCESS_H
