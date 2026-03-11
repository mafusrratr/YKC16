#ifndef ZSHV2_APP_SHELL_H
#define ZSHV2_APP_SHELL_H

#include <QWidget>
#include <QMutex>
#include <QTimer>
#include <stdint.h>
#include <string>

#include "../../../base/mqtt/mqtt_client.h"

class QStackedWidget;
class QPushButton;
class StatePage;
class DetailPage;

struct HmiConfig {
    // BY ZF: 基础显示参数。
    int screenWidth;
    int screenHeight;

    // BY ZF: MQTT 连接参数。
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
        , mqttHost("127.0.0.1")
        , mqttPort(1883)
        , mqttKeepalive(60)
        , mqttClientId("tcu_hmi")
        , mqttTopicPrefix("tcu")
    {
    }
};

class AppShell : public QWidget {
    Q_OBJECT

public:
    explicit AppShell(QWidget* parent = 0);
    virtual ~AppShell();

    bool initialize();

private slots:
    void onRefreshUi();
    void onToggleDetail();

private:
    enum ChargeState {
        STATE_IDLE = 0,
        STATE_PREPARE,
        STATE_STARTING,
        STATE_CHARGING,
        STATE_STOPPING,
        STATE_STOPPED
    };

    struct RuntimeData {
        ChargeState state;
        double voltage;
        double current;
        double power;
        double totalAmount;
        double totalEnergy;
        double chargedTimeSec;

        RuntimeData()
            : state(STATE_IDLE)
            , voltage(0.0)
            , current(0.0)
            , power(0.0)
            , totalAmount(0.0)
            , totalEnergy(0.0)
            , chargedTimeSec(0.0)
        {
        }
    };

    bool loadConfig();
    bool initUi();
    bool initMqtt();

    void onMqttConnected(int rc);
    void onMqttMessage(const std::string& topic, const std::string& payload);

    void handleLogicEvent(const std::string& payload);
    void handleFeeData(const std::string& payload);
    void handlePileData(const std::string& payload);

    void renderByState(const RuntimeData& rd);
    static ChargeState mapState(const char* stateText);

    static bool parseTopicGun(const std::string& topic,
                              const std::string& prefix,
                              uint8_t& gun,
                              std::string& tail);

private:
    HmiConfig m_config;

    QStackedWidget* m_stack;
    QPushButton* m_detailButton;
    StatePage* m_pageIdle;
    StatePage* m_pagePrepare;
    StatePage* m_pageStarting;
    StatePage* m_pageCharging;
    StatePage* m_pageStopping;
    StatePage* m_pageStopped;
    DetailPage* m_pageDetail;

    bool m_inDetail;
    int m_prevPageIndex;

    MqttClient m_mqtt;
    bool m_mqttReady;

    QTimer m_refreshTimer;
    QMutex m_dataMutex;
    RuntimeData m_runtime;
};

#endif // ZSHV2_APP_SHELL_H
