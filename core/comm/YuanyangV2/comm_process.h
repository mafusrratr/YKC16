/**
 * YuanyangV2 tcu_comm process
 * BY ZF
 */

#ifndef YUANYANG_V2_COMM_PROCESS_H
#define YUANYANG_V2_COMM_PROCESS_H

#include "../../base/process/base_process.h"
#include "../../base/mqtt/mqtt_client.h"
#include "../../base/logger/log_sender.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

// BY ZF: YuanyangV2 runtime config, loaded from /usr/app/config/tcu_comm.ini.
struct CommConfig {
    uint8_t gunCount;
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;
    int biasNo;
    std::string mqttUsername;
    std::string mqttPassword;

    std::string masterHost;
    int masterPort;
    int tcpReconnectSec;
    int tcpHeartbeatSec;
    int loginRetrySec;

    std::string assetCode;
    uint16_t stationAddr;
    std::vector<uint16_t> deviceAddrList;
    std::vector<std::string> gunAssetCodeList;
    std::string rsaPublicKey;
    bool offlineRunMode;
    bool debugTcp;

    CommConfig()
        : gunCount(1)
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttTopicPrefix("tcu")
        , biasNo(0)
        , masterPort(9000)
        , tcpReconnectSec(3)
        , tcpHeartbeatSec(20)
        , loginRetrySec(10)
        , stationAddr(1)
        , offlineRunMode(false)
        , debugTcp(false)
    {}
};

class CommProcess : public BaseProcess {
public:
    CommProcess();
    virtual ~CommProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
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

    struct GunRuntimeData {
        uint8_t gunStatus;
        uint8_t yxWorkStatus;
        uint8_t yxTotalFault;
        uint8_t yxTotalAlarm;
        uint8_t yxEmergencyStopFault;
        uint8_t yxVehicleConnectStatus;
        uint8_t yxGunSeatStatus;
        uint8_t yxElectronicLockStatus;
        uint8_t yxDcContactorStatus;
        uint16_t yxOtherFault;

        double voltage;
        double current;
        double soc;
        double batteryMinTemp;
        double batteryMaxTemp;
        double cellMaxVoltage;
        double cellMinVoltage;
        double pileEnvTemp;
        double bmsReqVoltage;
        double bmsReqCurrent;
        double meterEnergy;
        double meterVoltage;
        double meterCurrent;
        double totalEnergy;
        double totalAmount;
        double electricAmount;
        double serviceAmount;
        double chargedTime;

        std::string chargeUserNo;
        std::string orderNo;
        std::string feeModelId;
        int feeTimeNum;
        std::vector<FeeSegmentData> feeSegments;

        std::string pendingRecordTradeNo;

        GunRuntimeData()
            : gunStatus(0)
            , yxWorkStatus(0)
            , yxTotalFault(0)
            , yxTotalAlarm(0)
            , yxEmergencyStopFault(0)
            , yxVehicleConnectStatus(0)
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
            , bmsReqVoltage(0.0)
            , bmsReqCurrent(0.0)
            , meterEnergy(0.0)
            , meterVoltage(0.0)
            , meterCurrent(0.0)
            , totalEnergy(0.0)
            , totalAmount(0.0)
            , electricAmount(0.0)
            , serviceAmount(0.0)
            , chargedTime(0.0)
            , feeTimeNum(0)
        {}
    };

    struct ParsedFrame {
        uint32_t control;
        uint16_t targetStation;
        uint16_t targetDevice;
        uint16_t sourceStation;
        uint16_t sourceDevice;
        bool hasAsdu;
        uint8_t cmd;
        uint8_t vsq;
        uint16_t cot;
        uint16_t commonAddr;
        std::vector<uint8_t> tail;

        ParsedFrame()
            : control(0)
            , targetStation(0)
            , targetDevice(0)
            , sourceStation(0)
            , sourceDevice(0)
            , hasAsdu(false)
            , cmd(0)
            , vsq(0)
            , cot(0)
            , commonAddr(0)
        {}
    };

    struct PendingControl {
        bool active;
        uint8_t action;
        uint16_t targetStation;
        uint16_t targetDevice;
        uint8_t vsq;
        uint16_t commonAddr;
        std::vector<uint8_t> tailPlain;
        std::chrono::steady_clock::time_point createdAt;

        PendingControl()
            : active(false)
            , action(0)
            , targetStation(0)
            , targetDevice(0)
            , vsq(1)
            , commonAddr(0)
        {}
    };

    enum PlatformLoginState {
        LOGIN_IDLE = 0,
        LOGIN_REQ_RSA_PUBLIC_KEY,
        LOGIN_REQ_AUTH,
        LOGIN_ONLINE
    };

    bool loadConfig();
    bool initMqtt();
    void onMqttConnected(int rc);
    void onMqttMessage(const std::string& topic, const std::string& payload);
    bool parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const;
    std::string buildTopic(const char* module, uint8_t gun, const char* leaf) const;
    std::string ensureGunField(const std::string& payload, uint8_t gun) const;

    bool handleLogicEventForPlatform(uint8_t gun, const std::string& payload);
    bool handleLogicFeeForPlatform(uint8_t gun, const std::string& payload);
    bool handlePileDataForPlatform(uint8_t gun, const std::string& payload);
    bool handlePileEventForPlatform(uint8_t gun, const std::string& payload);
    bool handleMeterDataForPlatform(uint8_t gun, const std::string& payload);
    bool publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj);
    void publishPlatformLinkEvent(bool online, const char* reason);
    void publishInitialSetConfig();

    bool connectPlatformTcp();
    void closePlatformTcp();
    void maintainPlatformTcp();
    void driveLoginStateMachine(const std::chrono::steady_clock::time_point& now);
    void checkPendingControlTimeouts(const std::chrono::steady_clock::time_point& now);

    std::vector<uint8_t> buildHeartbeatFrame(uint32_t control);
    std::vector<uint8_t> buildAsduFrame(uint8_t cmd, uint8_t vsq, uint16_t cot, uint16_t commonAddr,
                                        const std::vector<uint8_t>& tailPlain,
                                        uint16_t targetStation, uint16_t targetDevice,
                                        uint16_t sourceStation, uint16_t sourceDevice);
    bool sendFrame(const std::vector<uint8_t>& frame);
    bool sendAsdu(uint8_t cmd, uint8_t vsq, uint16_t cot, uint16_t commonAddr,
                  const std::vector<uint8_t>& tailPlain,
                  uint16_t targetStation = 0, uint16_t targetDevice = 0);
    bool sendHeartbeat();
    bool sendRsaPublicKeyRequest();
    bool sendDeviceAuthRequest();
    bool sendControlAck(uint8_t gun, const PendingControl& pending, uint16_t cot);
    bool sendFeeModelAck(const ParsedFrame& pf, bool ok);

    void handlePlatformRxData(const char* data, size_t len);
    void processPlatformPacket(const uint8_t* frame, size_t frameLen);
    bool parseFrame(const uint8_t* frame, size_t frameLen, ParsedFrame& out);

    void resetCryptoSession();
    void prepareLoginCryptoContext();
    bool tryUpdateRsaPubKeyFromResponse(const uint8_t* tail, size_t tailLen);
    bool persistRsaPubKeyToIni(const std::string& pubKey);
    bool encryptTail(uint8_t cmd, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out);
    bool decryptTail(uint8_t cmd, const uint8_t* body, size_t bodyLen, std::vector<uint8_t>& out);

    bool parseRemoteControl041(const ParsedFrame& pf, uint8_t& gun, uint8_t& action, cJSON** outData);
    bool parsePowerControl02E(const ParsedFrame& pf, uint8_t& gun, cJSON** outData);
    bool parseFeeModel051(const ParsedFrame& pf, FeeModel& feeModel);
    bool parseRecordConfirm042(const ParsedFrame& pf, uint8_t& gun, cJSON** outData);

    std::vector<uint8_t> buildInfoAddrTail(uint32_t infoAddr) const;
    std::vector<uint8_t> buildAllTelesignalTail() const;
    std::vector<uint8_t> buildChangedTelesignalTail(uint8_t gun) const;
    std::vector<uint8_t> buildAllTelemetryTail() const;
    std::vector<uint8_t> buildChangedTelemetryTail(uint8_t gun) const;
    std::vector<uint8_t> buildExtPulseTail() const;
    std::vector<uint8_t> buildFaultTail(uint8_t gun, uint32_t faultCode, bool active) const;
    bool buildChargeRecordTail(uint8_t gun, cJSON* data, std::vector<uint8_t>& tail);
    void reportRuntimePeriodic();
    void sendTotalCallResponse();

    uint8_t gunFromDeviceAddr(uint16_t deviceAddr) const;
    uint16_t deviceAddrFromGun(uint8_t gun) const;
    bool syncSystemTimeFromTail(const uint8_t* tail, size_t tailLen);

    static void appendU16LE(std::vector<uint8_t>& out, uint16_t v);
    static void appendU32LE(std::vector<uint8_t>& out, uint32_t v);
    static void appendI32LE(std::vector<uint8_t>& out, int32_t v);
    static uint16_t readU16LE(const uint8_t* p);
    static uint32_t readU32LE(const uint8_t* p);
    static void appendFloatLE(std::vector<uint8_t>& out, float v);
    static double jsonNumber(cJSON* obj, const char* key, double defValue);
    static int jsonInt(cJSON* obj, const char* key, int defValue);
    static std::string jsonString(cJSON* obj, const char* key);
    static uint32_t scaleToU32(double v, double scale);
    static int32_t scaleToI32(double v, double scale);
    static std::string toHex(const uint8_t* data, size_t len);
    static bool isHexString(const std::string& s);
    static uint64_t nowMs();

private:
    CommConfig m_config;
    MqttClient m_mqtt;
    LogSender m_logSender;
    std::atomic<uint64_t> m_seq;
    std::atomic<bool> m_platformConnected;
    int m_tcpFd;
    PlatformLoginState m_loginState;

    std::chrono::steady_clock::time_point m_lastTcpConnectTry;
    std::chrono::steady_clock::time_point m_lastLoginAction;
    std::chrono::steady_clock::time_point m_nextLoginAllowedTime;
    std::chrono::steady_clock::time_point m_lastHeartbeat;
    std::chrono::steady_clock::time_point m_lastHeartbeatRecv;
    std::chrono::steady_clock::time_point m_lastRuntimeReport;
    std::chrono::steady_clock::time_point m_lastPeriodicSetConfigPublish;

    std::vector<uint8_t> m_tcpRxCache;
    std::vector<GunRuntimeData> m_gunRuntimeData;
    std::vector<FeeModel> m_feeModelByGun;
    std::vector<PendingControl> m_pendingControls;
    std::vector<uint8_t> m_runtimeChangedByGun;
    std::vector<uint8_t> m_lastFaultActiveByGun;
    std::vector<uint32_t> m_lastFaultCodeByGun;

    std::array<uint8_t, 16> m_aesSessionKey;
    bool m_aesSessionKeyReady;
    bool m_loginCryptoPrepared;
    bool m_platformOnlineEventActive;
    std::string m_rsaPublicKeyActive;
};

#endif // YUANYANG_V2_COMM_PROCESS_H
