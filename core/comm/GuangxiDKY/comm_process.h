/**
 * Guangxi DKY MODBUS RTU comm process
 * BY ZF
 */

#ifndef GUANGXI_DKY_COMM_PROCESS_H
#define GUANGXI_DKY_COMM_PROCESS_H

#include "../../base/process/base_process.h"
#include "../../base/mqtt/mqtt_client.h"
#include "../../base/communication/serial_communication.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class CommProcess : public BaseProcess {
public:
    CommProcess();
    virtual ~CommProcess();

    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;

private:
    struct CommConfig {
        uint8_t gunCount;
        uint8_t exportGun;

        std::string mqttHost;
        int mqttPort;
        int mqttKeepalive;
        std::string mqttClientId;
        std::string mqttTopicPrefix;
        std::string mqttUsername;
        std::string mqttPassword;

        std::string serialDevice;
        uint32_t serialBaudrate;
        uint8_t serialDataBits;
        uint8_t serialStopBits;
        char serialParity;

        uint8_t modbusAddr;
        uint16_t readStartReg;
        uint16_t readRegCount;
        uint16_t powerCtrlReg;
        uint8_t gunType;
        bool debugModbus;

        CommConfig()
            : gunCount(1)
            , exportGun(0)
            , mqttPort(1883)
            , mqttKeepalive(60)
            , mqttTopicPrefix("tcu")
            , serialDevice("/dev/ttyS4")
            , serialBaudrate(9600)
            , serialDataBits(8)
            , serialStopBits(1)
            , serialParity('N')
            , modbusAddr(1)
            , readStartReg(0x0000)
            , readRegCount(13)
            , powerCtrlReg(0x1001)
            , gunType(1)
            , debugModbus(false)
        {}
    };

    struct GunRuntimeData {
        uint16_t chargerNo;
        uint16_t gunNo;
        uint16_t gunStatus;
        uint16_t gunType;
        uint16_t soc;
        uint16_t connectStatus;
        uint16_t voltageDeciV;
        uint16_t currentDeciA;
        uint16_t powerDeciKw;
        uint16_t chargeTimeSec;
        uint32_t chargedTimeBaseRaw;
        bool chargedTimeBaseInited;
        uint16_t chargeEnergyDeciKwh;
        uint16_t amountDeciYuan;
        uint16_t meterDeciKwh;

        GunRuntimeData()
            : chargerNo(1)
            , gunNo(1)
            , gunStatus(0)
            , gunType(2)
            , soc(100)
            , connectStatus(2)
            , voltageDeciV(0)
            , currentDeciA(0)
            , powerDeciKw(0)
            , chargeTimeSec(0)
            , chargedTimeBaseRaw(0)
            , chargedTimeBaseInited(false)
            , chargeEnergyDeciKwh(0)
            , amountDeciYuan(0)
            , meterDeciKwh(0)
        {}
    };

    bool loadConfig();
    bool initMqtt();
    bool initSerial();

    void onMqttConnected(int rc);
    void onMqttMessage(const std::string& topic, const std::string& payload);
    bool parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const;

    void handleLogicEvent(uint8_t gun, const std::string& payload);
    void handleLogicFeeData(uint8_t gun, const std::string& payload);
    void handlePileData(uint8_t gun, const std::string& payload);
    void handlePileEvent(uint8_t gun, const std::string& payload);
    void handleMeterData(uint8_t gun, const std::string& payload);

    void pollSerial();
    bool tryExtractRequest(std::vector<uint8_t>& frame);
    void processRequest(const std::vector<uint8_t>& frame);

    std::vector<uint8_t> buildReadResponse(uint8_t addr, uint16_t startReg, uint16_t regCount) const;
    std::vector<uint8_t> buildExceptionResponse(uint8_t addr, uint8_t func, uint8_t exCode) const;
    std::vector<uint8_t> buildWriteEchoResponse(uint8_t addr, uint16_t reg, uint16_t value) const;
    uint16_t getRegisterValue(uint16_t reg) const;
    uint8_t selectedGun() const;

    void publishPowerControlCommand(uint16_t deciKw);

    static uint16_t crc16Modbus(const uint8_t* data, size_t len);
    static uint16_t readU16BE(const uint8_t* p);
    static void appendU16BE(std::vector<uint8_t>& out, uint16_t v);
    static uint16_t clampU16(double v, double scale);

private:
    CommConfig m_cfg;
    MqttClient m_mqtt;
    SerialCommunication m_serial;
    std::vector<uint8_t> m_rxBuf;
    std::vector<GunRuntimeData> m_gunData;
    std::atomic<uint64_t> m_seq;
    std::chrono::steady_clock::time_point m_lastPoll;
};

#endif // GUANGXI_DKY_COMM_PROCESS_H
