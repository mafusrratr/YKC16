#ifndef HMI_WINDOW_H
#define HMI_WINDOW_H

#include <QWidget>
#include <QMutex>
#include <QTimer>
#include <stdint.h>
#include <string>
#include <vector>

#include "../../base/mqtt/mqtt_client.h"

struct HmiConfig {
    // BY ZF: 基础显示配置
    int screenWidth;
    int screenHeight;
    bool fullScreen;

    // BY ZF: MQTT 连接配置
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;
    std::string mqttUsername;
    std::string mqttPassword;

    HmiConfig()
        : screenWidth(800)
        , screenHeight(480)
        , fullScreen(true)
        , mqttHost("127.0.0.1")
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttClientId("tcu_hmi")
        , mqttTopicPrefix("tcu")
    {
    }
};

class HmiWindow : public QWidget {
    Q_OBJECT

public:
    explicit HmiWindow(QWidget* parent = 0);
    virtual ~HmiWindow();

    bool initialize();

protected:
    virtual void paintEvent(QPaintEvent* event);

private slots:
    void onRefreshUi();

private:
    struct GunUiData {
        // BY ZF: 枪配置与状态
        bool configured;
        int gun;
        std::string gunId;
        std::string state;

        // BY ZF: 实时显示数据
        double voltage;
        double current;
        double power;
        double totalAmount;
        double totalEnergy;
        double chargedTime;

        // BY ZF: PREPARE 阶段二维码载荷
        std::string qrPayload;

        GunUiData()
            : configured(false)
            , gun(0)
            , state("IDLE")
            , voltage(0.0)
            , current(0.0)
            , power(0.0)
            , totalAmount(0.0)
            , totalEnergy(0.0)
            , chargedTime(0.0)
        {
        }
    };

    bool loadConfig();
    bool initMqtt();

    void onMqttConnected(int rc);
    void onMqttMessage(const std::string& topic, const std::string& payload);

    void handleLogicEvent(uint8_t gun, const std::string& payload);
    void handleFeeData(uint8_t gun, const std::string& payload);
    void handlePileData(uint8_t gun, const std::string& payload);
    void handlePlatEvent(uint8_t gun, const std::string& payload);

    void rebuildQrPayload(int gun);

    static bool parseTopicGun(const std::string& topic,
                              const std::string& prefix,
                              uint8_t& gun,
                              std::string& tail);

    void drawGunPanel(QPainter& painter, const QRect& rect, const GunUiData& data);
    void drawQrPlaceholder(QPainter& painter, const QRect& rect, const std::string& payload);

private:
    HmiConfig m_config;
    MqttClient m_mqtt;
    bool m_mqttReady;

    QTimer m_uiTimer;
    QMutex m_dataMutex;

    int m_gunCount;
    std::string m_cdzId;
    std::string m_operatorId;
    std::string m_macAddr;
    std::vector<GunUiData> m_guns;
};

#endif // HMI_WINDOW_H
