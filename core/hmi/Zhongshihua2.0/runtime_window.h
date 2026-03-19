#ifndef TCU_HMI_RUNTIME_WINDOW_H
#define TCU_HMI_RUNTIME_WINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QTimer>

#include <stdint.h>
#include <string>
#include <vector>

#include "../../base/mqtt/mqtt_client.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class QWidget;

class RuntimeWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit RuntimeWindow(QWidget *parent = 0);
    ~RuntimeWindow();

    bool initialize();

private slots:
    void refreshUi();
    void showAboutPage();
    void leaveAboutPage();
    void handleCellClick(int port);
    void returnToIdlePage();

private:
    enum PageId {
        PageIdle = 0,
        PageAuthorize,
        PageCharging,
        PageCheckout,
        PageAbout
    };

    struct HmiConfig {
        int screenWidth;
        int screenHeight;
        bool fullScreen;
        std::string mqttHost;
        int mqttPort;
        int mqttKeepalive;
        std::string mqttClientId;
        std::string mqttTopicPrefix;
        std::string mqttUsername;
        std::string mqttPassword;
        bool enableMergeChargeEntry;
        bool enableVinEntry;
        bool enableCardEntry;

        HmiConfig();
    };

    struct GunUiData {
        bool configured;
        int gun;
        std::string gunId;
        std::string state;
        double voltage;
        double current;
        double reqVoltage;
        double reqCurrent;
        double remainMinutes;
        double batteryMaxTemp;
        double power;
        double totalAmount;
        double totalEnergy;
        double chargedTime;
        int soc;
        std::string qrPayload;
        std::string stopReason;
        uint64_t lastStateChangeMs;

        GunUiData();
    };

    bool loadConfig();
    bool initMqtt();
    bool buildPages();
    QWidget *createPage(const QString &path);
    void bindStaticUi();

    void onMqttConnected(int rc);
    void onMqttMessage(const std::string &topic, const std::string &payload);

    void handleLogicEvent(uint8_t gun, const std::string &payload);
    void handleFeeData(uint8_t gun, const std::string &payload);
    void handlePileData(uint8_t gun, const std::string &payload);
    void handlePlatEvent(uint8_t gun, const std::string &payload);

    void rebuildQrPayload(int gun);
    void applyIdleLayout();
    void refreshIdlePage(const std::vector<GunUiData> &guns);
    void refreshAuthorizePage(const GunUiData &gun);
    void refreshChargingPage(const GunUiData &gun);
    void refreshCheckoutPage(const GunUiData &gun);
    void refreshAboutPage();
    PageId decidePage(const std::vector<GunUiData> &guns, int &focusGun) const;

    static bool parseTopicGun(const std::string &topic,
                              const std::string &prefix,
                              uint8_t &gun,
                              std::string &tail);

private:
    HmiConfig m_config;
    MqttClient m_mqtt;
    bool m_mqttReady;
    bool m_showAbout;
    bool m_forceIdleView;
    bool m_manualFocusLocked;

    QTimer m_uiTimer;
    QMutex m_dataMutex;

    int m_gunCount;
    int m_focusGun;
    std::string m_cdzNo;
    std::string m_cdzId;
    std::string m_operatorId;
    std::string m_macAddr;
    std::vector<GunUiData> m_guns;

    QStackedWidget *m_stack;
    QLabel *m_bottomTime;
    QWidget *m_idlePage;
    QWidget *m_authorizePage;
    QWidget *m_chargingPage;
    QWidget *m_checkoutPage;
    QWidget *m_aboutPage;
};

#endif
