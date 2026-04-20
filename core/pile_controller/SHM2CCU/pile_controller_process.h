/**
 * SHM2CCU process.
 * BY ZF
 */

#ifndef SHM2CCU_PILE_CONTROLLER_PROCESS_H
#define SHM2CCU_PILE_CONTROLLER_PROCESS_H

#include "../../interfaces/iprocess.h"
#include "../../base/process/base_process.h"
#include "../../base/common/config_manager_lite.h"
#include "../../base/common/message_queue.h"
#include "../../base/logger/log_sender.h"
#include "../../base/mqtt/mqtt_client.h"
#include "ipile_controller.h"
#include "tcu2ccu_data.h"
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <functional>

struct cJSON;

struct GunConfig {
    uint8_t gunNo;
    std::string commType;
    std::string protocolType;
    int shmKey;

    GunConfig()
        : gunNo(0)
        , shmKey(85000)
    {}
};

struct PileControllerConfig {
    uint8_t gunCount;
    std::vector<GunConfig> gunConfigs;
    int packetTimeout;
    int retryCount;
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;
    int biasNo;

    PileControllerConfig()
        : gunCount(0)
        , packetTimeout(0)
        , retryCount(0)
        , mqttPort(1883)
        , mqttKeepalive(60)
        , biasNo(0)
    {}
};

class PileControllerProcess : public BaseProcess {
public:
    PileControllerProcess();
    virtual ~PileControllerProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;
    void feedWatchdog();

private:
    bool loadGunConfigs();
    bool createPileController();
    void processCommandMessage();
    void updateStatusFromController();
    bool initMqtt();
    void publishData(uint8_t gunNo, const std::string& type, const std::string& payload, bool retain = true);
    void publishCmdUpset(uint8_t gunNo, const std::string& payload, bool retain = false);
    void onMqttMessage(const std::string& topic, const std::string& payload);
    bool handleFeeDataMessage(int gun, struct cJSON* data);
    bool handleLogicEventMessage(int gun, struct cJSON* root, struct cJSON* data);
    std::string buildDataPayload(uint8_t gunNo,
                                 const std::string& type,
                                 const std::function<void(struct cJSON*)>& fillData);
    bool parseGunFromTopic(const std::string& topic, int& outGun) const;

private:
    struct DataCache {
        bool hasYc20;
        bool hasYx22;
        bool hasYx23;
        TCU2CCU_DataYC20 yc20;
        TCU2CCU_DataYX22 yx22;
        TCU2CCU_DataYX23 yx23;
        DataCache()
            : hasYc20(false)
            , hasYx22(false)
            , hasYx23(false)
        {}
    };

    struct EventCache {
        bool hasTotalFault;
        uint8_t totalFault;
        bool onlinePublished;
        EventCache()
            : hasTotalFault(false)
            , totalFault(0)
            , onlinePublished(false)
        {}
    };

    PileControllerConfig m_config;
    std::vector<std::unique_ptr<IPileController> > m_controllers;
    MessageQueue* m_cmdQueue;
    LogSender* m_logger;
    std::thread m_mainThread;
    std::atomic<bool> m_running;
    MqttClient m_mqtt;
    std::atomic<uint64_t> m_mqttSeq;
    std::vector<EventCache> m_eventCaches;
    std::vector<DataCache> m_dataCaches;
    std::chrono::steady_clock::time_point m_lastPublish;
};

#endif  // SHM2CCU_PILE_CONTROLLER_PROCESS_H
