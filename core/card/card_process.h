/**
 * Card process definition
 * BY ZF
 */

#ifndef TCU_CARD_PROCESS_H
#define TCU_CARD_PROCESS_H

#include "../base/process/base_process.h"
#include "../base/common/message_queue.h"
#include "../base/mqtt/mqtt_client.h"
#include "card_reader.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct CardProcessConfig {
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;
    std::string cmdTopic;
    std::string eventTopic;

    CardSerialConfig serial;
    int loopIntervalMs;
    int maxQueueDepth;
    bool debugProtocol;

    CardProcessConfig()
        : mqttHost("127.0.0.1")
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttClientId("tcu_card")
        , mqttTopicPrefix("tcu")
        , loopIntervalMs(50)
        , maxQueueDepth(32)
        , debugProtocol(false)
    {}
};

enum CardBusinessOp {
    CARD_BIZ_NONE = 0,
    CARD_BIZ_OPEN_RF,
    CARD_BIZ_CLOSE_RF,
    CARD_BIZ_READ,
    CARD_BIZ_WRITE,
    CARD_BIZ_LOCK,
    CARD_BIZ_UNLOCK
};

struct CardBusinessCommand {
    CardBusinessOp op;
    bool hasCardNo;
    std::vector<uint8_t> cardNo;
    bool hasCardBalance;
    uint32_t cardBalance;

    CardBusinessCommand()
        : op(CARD_BIZ_NONE)
        , hasCardNo(false)
        , hasCardBalance(false)
        , cardBalance(0)
    {}
};

struct CardBusinessInfo {
    std::vector<uint8_t> uid;
    std::vector<uint8_t> cardNo;
    uint32_t cardBalance;
    bool locked;

    CardBusinessInfo()
        : cardBalance(0)
        , locked(false)
    {}
};

class CardProcess : public BaseProcess {
public:
    CardProcess();
    virtual ~CardProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
    bool loadConfig();
    bool initMqtt();
    bool initReader();

    void onMqttConnect(int rc);
    void onMqttMessage(const std::string& topic, const std::string& payload);

    bool parseCommandJson(const std::string& payload, CardBusinessCommand& out, std::string& err);
    bool enqueueCommand(const CardBusinessCommand& cmd, std::string& err);
    bool popNextCommand(CardBusinessCommand& out);

    void processOneCommand();
    bool handleBusinessCommand(const CardBusinessCommand& cmd, std::string& err);
    bool handleOpenRf(std::string& err);
    bool handleCloseRf(std::string& err);
    bool handleCardRead(std::string& err);
    bool handleCardWrite(const CardBusinessCommand& cmd, std::string& err);
    bool handleCardLock(bool locked, std::string& err);

    bool executeReaderCommand(const ReaderCommand& cmd, ReaderResult& result, std::string& err);
    bool activateCard(std::vector<uint8_t>& uid, std::string& err);
    bool authBlock(uint8_t blockNo, const std::vector<uint8_t>& uid, bool forWrite, std::string& err);
    bool readBlock(uint8_t blockNo, std::vector<uint8_t>& data, std::string& err);
    bool writeBlock(uint8_t blockNo, const std::vector<uint8_t>& data, std::string& err);
    bool readCardInfo(CardBusinessInfo& info, std::string& err);
    bool readCardInfoByUid(const std::vector<uint8_t>& uid, CardBusinessInfo& info, std::string& err);
    void triggerBuzzer();
    void debugLog(const std::string& msg) const;

    void publishEvent(const std::string& event, const std::string& reason);
    void publishSimpleEvent(const std::string& event);
    void publishCardInfo(const CardBusinessInfo& info);
    void publishCardWritten(const CardBusinessCommand& cmd);
    void publishLockEvent(bool locked);
    void publishOpFailed(CardBusinessOp op,
                         const std::string& reason,
                         const ReaderResult* result = NULL);
    void publishDeviceOnline(bool online, const std::string& reason);
    void feedDaemonWatchdog();

private:
    CardProcessConfig m_cfg;
    CardReader m_reader;
    MqttClient m_mqtt;
    std::deque<CardBusinessCommand> m_cmdQueue;
    std::mutex m_cmdMutex;
    std::atomic<uint64_t> m_seq;
    bool m_deviceOnline;
    std::chrono::steady_clock::time_point m_lastDaemonWatchdogFeed;
    std::chrono::steady_clock::time_point m_lastProcessWatchdogFeed;
};

#endif // TCU_CARD_PROCESS_H
