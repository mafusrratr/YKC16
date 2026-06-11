// BY ZF
#ifndef VIRTUAL_PLUG_BACKEND_H
#define VIRTUAL_PLUG_BACKEND_H

#include "../base/mqtt/mqtt_client.h"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct VirtualPlugConfig {
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string topicPrefix;
    std::string httpHost;
    int httpPort;
    int biasNo;
    int gunCount;
};

struct GunState {
    int localGun;
    bool connected;
    bool running;
    bool fault;
    bool meterOnline;
    std::string logicState;
    std::string pileState;
    std::string mode;
    std::string vin;
    double voltage;
    double current;
    double soc;
    double targetSoc;
    double dischargeTargetSoc;
    double socRatePerMinute;
    double totalEnergy;
    double reverseEnergy;
    int chargeMode;
};

class VirtualPlugBackend {
public:
    VirtualPlugBackend();
    ~VirtualPlugBackend();

    bool loadConfig(const std::string& path);
    bool start();
    void stop();
    bool handleAction(int localGun, const std::string& action);
    std::string statusJson();

private:
    bool startMqtt();
    bool startHttp();
    void httpLoop();
    void meterLoop();
    void handleMqttMessage(const std::string& topic, const std::string& payload);
    void updateFromCmd(int globalGun, const std::string& payload);
    void updateFromLogicEvent(int globalGun, const std::string& payload);

    int globalGunNo(const GunState& gun) const;
    GunState* findGunByLocal(int localGun);
    GunState* findGunByGlobal(int globalGun);

    void publishPileData(const GunState& gun, const std::string& type, const std::string& dataJson);
    void publishPileEvent(const GunState& gun, const std::string& type, const std::string& dataJson);
    void publishMeterData(const GunState& gun);
    void publishMeterEvent(const GunState& gun, const std::string& event, const std::string& reason);
    void tickMeter(GunState& gun, int intervalSec);

    std::string yxJson(const GunState& gun) const;
    std::string ycJson(const GunState& gun) const;
    std::string startCompleteJson(const GunState& gun, bool ok) const;
    std::string stopCompleteJson(const GunState& gun) const;
    std::string vehicleIdJson(const GunState& gun) const;
    std::string rootPayload(const GunState& gun, const std::string& source, const std::string& extraJson);

    static std::string jsonEscape(const std::string& value);
    static std::string queryValue(const std::string& query, const std::string& key);
    static std::string urlDecode(const std::string& value);

private:
    VirtualPlugConfig m_config;
    MqttClient m_mqtt;
    std::vector<GunState> m_guns;
    std::mutex m_mutex;
    bool m_running;
    bool m_mqttConnected;
    int m_seq;
    int m_httpFd;
    std::thread m_httpThread;
    std::thread m_meterThread;
};

#endif
