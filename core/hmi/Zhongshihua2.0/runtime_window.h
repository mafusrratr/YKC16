#ifndef TCU_HMI_RUNTIME_WINDOW_H
#define TCU_HMI_RUNTIME_WINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QString>
#include <QTimer>

#include <stdint.h>
#include <string>
#include <vector>

#include "../../base/mqtt/mqtt_client.h"

class QLabel;
class QAbstractButton;
class QButtonGroup;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QWidget;
class FeeModelChartWidget;

class RuntimeWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit RuntimeWindow(QWidget *parent = 0);
    ~RuntimeWindow();

    bool initialize();

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void checkScreenIdle();
    void refreshUi();
    void showAboutPage();
    void leaveAboutPage();
    void handleCellClick(int port);
    void returnToIdlePage();
    void showAllChargeRecords();
    void showAGunChargeRecords();
    void showBGunChargeRecords();
    void showPrevChargeRecordPage();
    void showNextChargeRecordPage();
    void showAllFaultRecords();
    void showAFaultRecords();
    void showBFaultRecords();
    void showPrevFaultRecordPage();
    void showNextFaultRecordPage();
    void submitAboutPermission();
    void showAboutPermissionPad();
    void handleAboutTabChanged(int index);
    void onStorageItemClicked(QListWidgetItem *item);
    void onStorageFileItemDoubleClicked(QListWidgetItem *item);
    void onStorageBackClicked();
    void onStorageRefreshClicked();
    void onStorageExportLogClicked();
    void onStorageUpgradeClicked();
    void onAuthorizeCardClicked();
    void onAuthorizeMergeChargeClicked();
    void onAuthorizeVinClicked();
    void onConfigEditMaskClicked();
    void onConfigKeyboardButtonClicked();
    void onConfigGunCountChanged();
    void onConfigSubmitClicked();
    void onConfigResetClicked();
    void rebootSystem();

public:
    enum PageId {
        PageIdle = 0,
        PageAuthorize,
        PageCharging,
        PageStopping,
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
        int biasNo;
        std::string mqttUsername;
        std::string mqttPassword;
        bool enableMergeChargeEntry;
        bool enableVinEntry;
        bool enableQrEntry;
        bool enableCardEntry;
        bool cardOnlineAuth;

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
        bool cardOfflineActive;
        std::string qrPayload;
        std::string stopReason;
        std::string lastFaultPointKey;
        std::string lastFaultMessage;
        std::string lastFaultOccurTime;
        uint64_t lastStateChangeMs;

        GunUiData();
    };

    struct ModuleVersionInfo {
        std::string version;
        std::string buildDate;

        ModuleVersionInfo();
    };

    struct MonStatusData {
        ModuleVersionInfo logic;
        ModuleVersionInfo meter;
        ModuleVersionInfo mon;
        int platformComm;
        int meterComm;
        int controllerComm;
        int activeFaultCount;
        std::string latestFaultName;
        std::string latestFaultTime;
        std::string lastEventTime;

        MonStatusData();
    };

    struct FeeSegment {
        std::string segFlag;
        std::string startTime;
        std::string endTime;
        double chargeFee;
        double serviceFee;

        FeeSegment();
    };

    struct FeeModelData {
        int feeModelNo;
        int timeNum;
        std::string feeModelId;
        std::string timeStamp;
        std::vector<FeeSegment> segments;
        int currentIndex;
        bool valid;

        FeeModelData();
    };

    struct ChargeRecordItem {
        int id;
        int gunNo;
        std::string tradeNo;
        std::string startTime;
        std::string endTime;
        double startSoc;
        double endSoc;
        double totalElect;
        double totalCost;
        int reason;

        ChargeRecordItem();
    };

    struct FaultRecordItem {
        int id;
        int gunNo;
        std::string occurTime;
        QString faultName;

        FaultRecordItem();
    };

private:
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
    void handleSaveEvent(uint8_t gun, const std::string &payload);
    void handleMonEvent(const std::string &payload);

    void rebuildQrPayload(int gun);
    void applyIdleLayout();
    void setupAboutTabs();
    void refreshFeeModelCache(bool forceReload);
    void setupFaultRecordTab();
    void refreshFaultRecordCache(bool forceReload);
    void refreshFaultRecordTable();
    void setupChargeRecordTab();
    void refreshChargeRecordCache(bool forceReload);
    void refreshChargeRecordTable();
    void setupConfigTab();
    void loadMeterConfigToUi();
    bool saveMeterConfigFromUi();
    void loadCommConfigToUi();
    bool saveCommConfigFromUi();
    bool saveHmiConfigFromUi();
    bool savePileConfigFromUi();
    void setupExternalStorageTab();
    void scanExternalStorage();
    void loadStorageFileList(const QString &path);
    QString selectedStoragePath() const;
    void openConfigKeyboard(QLineEdit *target, bool numericOnly);
    bool publishVinRequest(bool mergeCharge);
    void refreshConfigKeyboardLayout();
    void refreshIdlePage(const std::vector<GunUiData> &guns);
    void refreshAuthorizePage(const GunUiData &gun);
    void refreshChargingPage(const GunUiData &gun);
    void refreshStoppingPage(const GunUiData &gun);
    void refreshCheckoutPage(const GunUiData &gun);
    void refreshAboutPage();
    void markScreenActivity();
    void setScreenBacklight(bool on);
    PageId decidePage(const std::vector<GunUiData> &guns, int &focusGun) const;
    void feedWatchdog();

    bool parseTopicGun(const std::string &topic,
                       const std::string &prefix,
                       uint8_t &gun,
                       std::string &tail) const;

private:
    HmiConfig m_config;
    MqttClient m_mqtt;
    bool m_mqttReady;
    bool m_showAbout;
    bool m_forceIdleView;
    bool m_manualFocusLocked;
    bool m_aboutPermissionGranted;

    QTimer m_uiTimer;
    QTimer m_screenIdleTimer;
    QMutex m_dataMutex;

    int m_gunCount;
    int m_focusGun;
    std::string m_cdzNo;
    std::string m_cdzId;
    std::string m_operatorId;
    std::string m_macAddr;
    std::vector<GunUiData> m_guns;
    MonStatusData m_monStatus;
    FeeModelData m_feeModel;
    uint64_t m_lastFeeModelCheckMs;
    std::vector<ChargeRecordItem> m_chargeRecords;
    uint64_t m_lastChargeRecordCheckMs;
    int m_recordGunFilter;
    int m_chargeRecordPage;
    int m_chargeRecordPageSize;
    std::vector<FaultRecordItem> m_faultRecords;
    uint64_t m_lastFaultRecordCheckMs;
    int m_faultGunFilter;
    int m_faultRecordPage;
    int m_faultRecordPageSize;
    uint64_t m_lastWatchdogFeedMs;
    uint64_t m_lastScreenActivityMs;
    uint64_t m_uiSeq;
    bool m_platformOnline;
    bool m_screenBacklightOff;

    QStackedWidget *m_stack;
    QLabel *m_bottomTime;
    QLabel *m_bottomHostState;
    QWidget *m_idlePage;
    QWidget *m_authorizePage;
    QWidget *m_chargingPage;
    QWidget *m_stoppingPage;
    QWidget *m_checkoutPage;
    QWidget *m_aboutPage;
    QTabWidget *m_aboutTabWidget;
    QLineEdit *m_aboutPermissionEdit;
    QLabel *m_aboutPermissionHint;
    QWidget *m_aboutPermissionPad;
    QLineEdit *m_configPileNoEdit;
    QLineEdit *m_configSecretEdit;
    QLineEdit *m_configMeterAddr1Edit;
    QLineEdit *m_configMeterAddr2Edit;
    QLineEdit *m_configKeyboardTarget;
    QLabel *m_configHintLabel;
    QWidget *m_configKeyboard;
    QWidget *m_configMeter2Row;
    QPushButton *m_configKeyButtons[30];
    QPushButton *m_configShiftButton;
    QPushButton *m_configModeButton;
    QPushButton *m_configBackspaceButton;
    QPushButton *m_configClearButton;
    QPushButton *m_configConfirmButton;
    QRadioButton *m_configGunSingle;
    QRadioButton *m_configGunDual;
    bool m_configKeyboardUppercase;
    bool m_configKeyboardNumberMode;
    bool m_configKeyboardNumericOnly;
    FeeModelChartWidget *m_feeChart;
    QTableWidget *m_faultRecordTable;
    QLabel *m_faultRecordPageLabel;
    QTableWidget *m_chargeRecordTable;
    QLabel *m_chargeRecordPageLabel;
    QString m_storageCurrentPath;
};

#endif
