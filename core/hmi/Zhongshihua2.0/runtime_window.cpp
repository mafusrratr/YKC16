#include "runtime_window.h"

#include <QBrush>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../../base/common/config_manager_lite.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include "../../logger/sql/sqlite3.h"
#include "customwidgets.h"
#include "uipages.h"

namespace {

static uint64_t nowMs()
{
    return static_cast<uint64_t>(QDateTime::currentDateTime().toMSecsSinceEpoch());
}

static double getNumber(cJSON *obj, const char *key, double defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        return n->valuedouble;
    }
    return defVal;
}

static int getInt(cJSON *obj, const char *key, int defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        return n->valueint;
    }
    return defVal;
}

static std::string getString(cJSON *obj, const char *key, const std::string &defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) {
        return defVal;
    }
    if (cJSON_IsString(v) && v->valuestring) {
        return std::string(v->valuestring);
    }
    if (cJSON_IsNumber(v)) {
        char tmp[64] = {0};
        std::snprintf(tmp, sizeof(tmp), "%.0f", v->valuedouble);
        return std::string(tmp);
    }
    return defVal;
}

static QString formatMoney(double value)
{
    return QString::number(value, 'f', 2);
}

static QString formatEnergy(double value)
{
    return QString::number(value, 'f', 3);
}

static QString formatFeeValue(double value)
{
    return QString::number(value, 'f', 3);
}

static QString formatVoltage(double value)
{
    return QString::number(value, 'f', 1);
}

static QString formatCurrent(double value)
{
    return QString::number(value, 'f', 1);
}

static QString formatChargeTime(double seconds)
{
    const int total = seconds > 0.0 ? static_cast<int>(seconds + 0.5) : 0;
    const int hour = total / 3600;
    const int minute = (total % 3600) / 60;
    // const int second = total % 60;
    return QString::fromUtf8("%1小时%2分")
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'));
}

static QString formatRemainMinutes(double minutes)
{
    const int total = minutes > 0.0 ? static_cast<int>(minutes + 0.5) : 0;
    const int hour = total / 60;
    const int minute = total % 60;
    return QString::fromUtf8("%1小时%2分钟").arg(hour).arg(minute, 2, 10, QChar('0'));
}

static QString formatHourMinute(double seconds)
{
    const int total = seconds > 0.0 ? static_cast<int>(seconds + 0.5) : 0;
    const int hour = total / 3600;
    const int minute = (total % 3600) / 60;
    return QString::fromUtf8("%1:%2")
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'));
}

static QString stateText(const std::string &state)
{
    if (state == "IDLE") {
        return QString::fromUtf8("请连接\n电动汽车");
    }
    if (state == "PREPARE") {
        return QString::fromUtf8("车辆已连接\n请点击操作");
    }
    if (state == "STARTING") {
        return QString::fromUtf8("充电启动中...");
    }
    if (state == "CHARGING") {
        return QString::fromUtf8("充电进行中");
    }
    if (state == "STOPPING") {
        return QString::fromUtf8("停止充电中...");
    }
    if (state == "STOPPED") {
        return QString::fromUtf8("充电已完成");
    }
    if (state == "ERROR") {
        return QString::fromUtf8("枪口故障");
    }
    return QString::fromUtf8("等待状态");
}

static QString idleCellText(double totalEnergy,
                            double totalAmount,
                            double chargedTime,
                            double voltage,
                            double current,
                            int soc,
                            const std::string &state)
{
    if (state == "STOPPED") {
        return QString::fromUtf8("充电已停止\n充电电量: %1kWh\n充电金额: %2元\n充电时长: %3")
            .arg(formatEnergy(totalEnergy))
            .arg(formatMoney(totalAmount))
            .arg(formatChargeTime(chargedTime));
    }
    if (state == "CHARGING" || state == "STOPPING") {
        return QString::fromUtf8("充电电量: %1kWh\n充电金额: %2元\n充电时长: %3\n充电信息: %4V %5A\nSOC: %6%")
            .arg(formatEnergy(totalEnergy))
            .arg(formatMoney(totalAmount))
            .arg(formatChargeTime(chargedTime))
            .arg(formatVoltage(voltage))
            .arg(formatCurrent(current))
            .arg(soc >= 0 ? QString::number(soc) : QString::fromUtf8("--"));
    }
    return stateText(state);
}

static QString stopReasonText(const std::string &reason)
{
    if (!reason.empty()) {
        return QString::fromStdString(reason);
    }
    return QString::fromUtf8("充电完成");
}

static void setLabelText(QWidget *page, const char *name, const QString &text)
{
    QLabel *label = page ? page->findChild<QLabel *>(name) : 0;
    if (label) {
        label->setText(text);
    }
}

static void setLabelPixmap(QWidget *page, const char *name, const QString &res)
{
    QLabel *label = page ? page->findChild<QLabel *>(name) : 0;
    if (label) {
        label->setPixmap(QPixmap(res));
        label->setScaledContents(true);
    }
}

static QStringList splitSemicolon(const std::string &text)
{
    return QString::fromStdString(text).split(";", QString::SkipEmptyParts);
}

static int hhmmToMinutes(const std::string &hhmm)
{
    if (hhmm.size() < 4) {
        return -1;
    }
    const int hour = std::atoi(hhmm.substr(0, 2).c_str());
    const int minute = std::atoi(hhmm.substr(2, 2).c_str());
    return hour * 60 + minute;
}

static QString hhmmText(const std::string &hhmm)
{
    if (hhmm.size() < 4) {
        return QString::fromStdString(hhmm);
    }
    return QString("%1:%2").arg(QString::fromStdString(hhmm.substr(0, 2)))
                           .arg(QString::fromStdString(hhmm.substr(2, 2)));
}

static QString ymdhmsText(const std::string &stamp)
{
    if (stamp.size() < 14) {
        return QString::fromStdString(stamp);
    }
    return QString("%1-%2-%3 %4:%5:%6")
        .arg(QString::fromStdString(stamp.substr(0, 4)))
        .arg(QString::fromStdString(stamp.substr(4, 2)))
        .arg(QString::fromStdString(stamp.substr(6, 2)))
        .arg(QString::fromStdString(stamp.substr(8, 2)))
        .arg(QString::fromStdString(stamp.substr(10, 2)))
        .arg(QString::fromStdString(stamp.substr(12, 2)));
}

static QString chargeRecordReasonText(int reason)
{
    return QString::number(reason);
}

static QWidget *makeInfoCard(QWidget *parent, const QString &title, const QRect &rect)
{
    QFrame *frame = new QFrame(parent);
    frame->setObjectName(title);
    frame->setGeometry(rect);
    frame->setStyleSheet("QFrame{background:rgb(244,246,247);border:1px solid rgb(168,168,168);border-radius:12px;}");

    QLabel *header = new QLabel(title, frame);
    header->setGeometry(16, 10, rect.width() - 32, 28);
    header->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:20px 'MS Shell Dlg 2';font-weight:bold;}");
    return frame;
}

static QLabel *makeValueLabel(QWidget *parent, const char *name, const QRect &rect, int pointSize, bool bold)
{
    QLabel *label = new QLabel(parent);
    label->setObjectName(name);
    label->setGeometry(rect);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet(QString("QLabel{color:rgb(38,45,52);background:transparent;font:%1px 'MS Shell Dlg 2';font-weight:%2;}")
                         .arg(pointSize)
                         .arg(bold ? 700 : 500));
    return label;
}

static bool readFeeModelFromStmt(sqlite3_stmt *stmt, RuntimeWindow::FeeModelData &model)
{
    if (!stmt || sqlite3_step(stmt) != SQLITE_ROW) {
        return false;
    }

    const char *feeModelId = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const char *timeSeg = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const char *segFlag = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const char *chargeFee = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const char *serviceFee = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const char *timeStamp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

    const QStringList segs = splitSemicolon(timeSeg ? timeSeg : "");
    const QStringList flags = splitSemicolon(segFlag ? segFlag : "");
    const QStringList chargeFees = splitSemicolon(chargeFee ? chargeFee : "");
    const QStringList serviceFees = splitSemicolon(serviceFee ? serviceFee : "");
    const int count = std::min(std::min(segs.size(), chargeFees.size()), serviceFees.size());
    if (count <= 0) {
        return false;
    }

    int idx = 0;
    for (idx = 0; idx < count; ++idx) {
        RuntimeWindow::FeeSegment seg;
        seg.segFlag = idx < flags.size() ? flags.at(idx).toStdString() : std::string();
        seg.startTime = segs.at(idx).toStdString();
        seg.endTime = (idx + 1 < segs.size()) ? segs.at(idx + 1).toStdString() : std::string("2400");
        seg.chargeFee = chargeFees.at(idx).toDouble() / 1000.0;
        seg.serviceFee = serviceFees.at(idx).toDouble() / 1000.0;
        model.segments.push_back(seg);
    }

    model.feeModelId = feeModelId ? feeModelId : "";
    model.timeStamp = timeStamp ? timeStamp : "";
    model.valid = !model.segments.empty();
    if (!model.valid) {
        return false;
    }

    const QTime nowTime = QTime::currentTime();
    const int nowMinute = nowTime.hour() * 60 + nowTime.minute();
    model.currentIndex = 0;
    for (idx = 0; idx < static_cast<int>(model.segments.size()); ++idx) {
        const int start = hhmmToMinutes(model.segments[idx].startTime);
        const int end = hhmmToMinutes(model.segments[idx].endTime);
        if (start >= 0 && end > start && nowMinute >= start && nowMinute < end) {
            model.currentIndex = idx;
            break;
        }
        if (idx == static_cast<int>(model.segments.size()) - 1 && nowMinute >= start) {
            model.currentIndex = idx;
        }
    }

    return true;
}

} // namespace

RuntimeWindow::HmiConfig::HmiConfig()
    : screenWidth(800)
    , screenHeight(480)
    , fullScreen(true)
    , mqttHost("127.0.0.1")
    , mqttPort(1883)
    , mqttKeepalive(60)
    , mqttClientId("tcu_hmi_zsh")
    , mqttTopicPrefix("tcu")
    , enableMergeChargeEntry(false)
    , enableVinEntry(false)
    , enableCardEntry(false)
{
}

RuntimeWindow::GunUiData::GunUiData()
    : configured(false)
    , gun(0)
    , state("IDLE")
    , voltage(0.0)
    , current(0.0)
    , reqVoltage(0.0)
    , reqCurrent(0.0)
    , remainMinutes(0.0)
    , batteryMaxTemp(0.0)
    , power(0.0)
    , totalAmount(0.0)
    , totalEnergy(0.0)
    , chargedTime(0.0)
    , soc(-1)
    , stopReason("充电完成")
    , lastStateChangeMs(0)
{
}

RuntimeWindow::ModuleVersionInfo::ModuleVersionInfo()
{
}

RuntimeWindow::MonStatusData::MonStatusData()
    : platformComm(0)
    , meterComm(0)
    , controllerComm(0)
    , activeFaultCount(0)
{
}

RuntimeWindow::FeeSegment::FeeSegment()
    : chargeFee(0.0)
    , serviceFee(0.0)
{
}

RuntimeWindow::FeeModelData::FeeModelData()
    : currentIndex(-1)
    , valid(false)
{
}

RuntimeWindow::ChargeRecordItem::ChargeRecordItem()
    : id(0)
    , gunNo(0)
    , totalElect(0.0)
    , totalCost(0.0)
    , reason(0)
{
}

RuntimeWindow::RuntimeWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mqttReady(false)
    , m_showAbout(false)
    , m_forceIdleView(false)
    , m_manualFocusLocked(false)
    , m_gunCount(1)
    , m_focusGun(0)
    , m_operatorId("0")
    , m_macAddr("000000000000000000000000")
    , m_guns(2)
    , m_lastFeeModelCheckMs(0)
    , m_lastChargeRecordCheckMs(0)
    , m_recordGunFilter(-1)
    , m_chargeRecordPage(0)
    , m_chargeRecordPageSize(8)
    , m_stack(new QStackedWidget(this))
    , m_bottomTime(new QLabel(this))
    , m_idlePage(0)
    , m_authorizePage(0)
    , m_chargingPage(0)
    , m_checkoutPage(0)
    , m_aboutPage(0)
    , m_aboutTabWidget(0)
    , m_feeChart(0)
    , m_chargeRecordTable(0)
    , m_chargeRecordPageLabel(0)
{
    m_guns[0].gun = 0;
    m_guns[1].gun = 1;

    setCentralWidget(m_stack);
    m_bottomTime->setParent(this);
    m_bottomTime->setGeometry(220, 448, 360, 28);
    m_bottomTime->setAlignment(Qt::AlignCenter);
    m_bottomTime->setStyleSheet("QLabel{color:white;background:transparent;font:20px 'MS Shell Dlg 2';font-weight:bold;}");
    m_bottomTime->show();
    m_bottomTime->raise();
    connect(&m_uiTimer, SIGNAL(timeout()), this, SLOT(refreshUi()));
}

RuntimeWindow::~RuntimeWindow()
{
    if (m_mqttReady) {
        m_mqtt.loopStop(true);
        m_mqtt.disconnect();
    }
}

bool RuntimeWindow::initialize()
{
    // BY ZF: 运行态窗口复用提取 UI，但数据源改为当前项目 MQTT 流程。
    if (!loadConfig()) {
        return false;
    }

    setWindowTitle(QString::fromUtf8("中石化 HMI 运行版"));
    resize(m_config.screenWidth, m_config.screenHeight);
    setMinimumSize(m_config.screenWidth, m_config.screenHeight);
    setMaximumSize(m_config.screenWidth, m_config.screenHeight);

    if (!buildPages()) {
        return false;
    }

    if (!initMqtt()) {
        return false;
    }

    m_uiTimer.start(500);
    refreshUi();

    if (m_config.fullScreen) {
        showFullScreen();
    } else {
        show();
    }
    return true;
}

bool RuntimeWindow::loadConfig()
{
    ConfigManagerLite &cfg = ConfigManagerLite::getInstance();
    if (!cfg.loadConfig("/usr/app/config/tcu_hmi.ini")) {
        std::cerr << "[HMI] use default config /usr/app/config/tcu_hmi.ini not found" << std::endl;
        return true;
    }

    const std::string section = "Hmi";
    m_config.screenWidth = cfg.getInt(section, "screen_width", 800);
    m_config.screenHeight = cfg.getInt(section, "screen_height", 480);
    m_config.fullScreen = true;
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_hmi_zsh");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.enableMergeChargeEntry = cfg.getBool(section, "enable_merge_charge_entry", false);
    m_config.enableVinEntry = cfg.getBool(section, "enable_vin_entry", false);
    m_config.enableCardEntry = cfg.getBool(section, "enable_card_entry", false);

    m_gunCount = cfg.getInt(section, "gun_count", 1);
    if (m_gunCount < 1) {
        m_gunCount = 1;
    }
    if (m_gunCount > 2) {
        m_gunCount = 2;
    }
    return true;
}

bool RuntimeWindow::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[HMI] mqtt init failed" << std::endl;
        return false;
    }

    if (!m_config.mqttUsername.empty()) {
        m_mqtt.setUsernamePassword(m_config.mqttUsername, m_config.mqttPassword);
    }

    m_mqtt.setConnectHandler([this](int rc) { onMqttConnected(rc); });
    m_mqtt.setMessageHandler([this](const std::string &topic, const std::string &payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        std::cerr << "[HMI] mqtt connect failed " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }

    if (!m_mqtt.loopStart()) {
        std::cerr << "[HMI] mqtt loop start failed" << std::endl;
        return false;
    }

    m_mqttReady = true;
    return true;
}

bool RuntimeWindow::buildPages()
{
    m_idlePage = createPage(":/ui/b1idle.ui");
    m_authorizePage = createPage(":/ui/c6flushcad.ui");
    m_chargingPage = createPage(":/ui/e1chargeinfo.ui");
    m_checkoutPage = createPage(":/ui/f7checkoutok.ui");
    m_aboutPage = createPage(":/ui/a3about.ui");

    if (!m_idlePage || !m_authorizePage || !m_chargingPage || !m_checkoutPage || !m_aboutPage) {
        return false;
    }

    m_stack->addWidget(m_idlePage);
    m_stack->addWidget(m_authorizePage);
    m_stack->addWidget(m_chargingPage);
    m_stack->addWidget(m_checkoutPage);
    m_stack->addWidget(m_aboutPage);

    bindStaticUi();
    return true;
}

QWidget *RuntimeWindow::createPage(const QString &path)
{
    QWidget *page = createPreviewPage(path, this);
    if (!page) {
        return 0;
    }

    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setFixedSize(800, 480);
    page->setAutoFillBackground(true);

    QPalette palette = page->palette();
    palette.setBrush(QPalette::Window, QBrush(QPixmap(":/cui/Resources/Main.png")));
    page->setPalette(palette);
    return page;
}

void RuntimeWindow::bindStaticUi()
{
    // BY ZF: 页面静态图片与按钮关系在启动时一次性绑定。
    setLabelPixmap(m_idlePage, "label_zq", ":/cui/Resources/qt_zq.png");
    setLabelPixmap(m_idlePage, "label_yq", ":/cui/Resources/qt_yq.png");
    setLabelPixmap(m_idlePage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_authorizePage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_chargingPage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_authorizePage, "label_ewm", ":/cui/Resources/ewm.png");
    setLabelPixmap(m_authorizePage, "qcq", ":/cui/Resources/qcq.png");
    setLabelPixmap(m_checkoutPage, "f7_ok", ":/cui/Resources/f7_ok.png");
    setLabelPixmap(m_checkoutPage, "c8_back", ":/cui/Resources/back.png");
    setLabelPixmap(m_checkoutPage, "ts_tb", ":/cui/Resources/ts.png");

    QPushButton *btnAboutIdle = m_idlePage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnAboutAuthorize = m_authorizePage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnAboutCharging = m_chargingPage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnBackAuthorize = m_authorizePage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackCharging = m_chargingPage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackCheckout = m_checkoutPage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackAbout = m_aboutPage->findChild<QPushButton *>("btnBack");
    QLabel *idleDatetime = m_idlePage->findChild<QLabel *>("lblDatetimeb");
    QPushButton *btnMergeCharge = m_authorizePage->findChild<QPushButton *>("bbms");
    QPushButton *btnVin = m_authorizePage->findChild<QPushButton *>("vin");
    QPushButton *btnCard = m_authorizePage->findChild<QPushButton *>("card");

    if (btnAboutIdle) {
        connect(btnAboutIdle, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnAboutAuthorize) {
        connect(btnAboutAuthorize, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnAboutCharging) {
        connect(btnAboutCharging, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnBackAuthorize) {
        btnBackAuthorize->setVisible(true);
        btnBackAuthorize->setFlat(true);
        connect(btnBackAuthorize, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackCharging) {
        btnBackCharging->setVisible(true);
        btnBackCharging->setFlat(true);
        connect(btnBackCharging, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackCheckout) {
        btnBackCheckout->setVisible(true);
        btnBackCheckout->setFlat(true);
        connect(btnBackCheckout, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackAbout) {
        connect(btnBackAbout, SIGNAL(clicked()), this, SLOT(leaveAboutPage()));
    }
    if (idleDatetime) {
        idleDatetime->hide();
    }
    if (btnMergeCharge) {
        btnMergeCharge->setVisible(m_config.enableMergeChargeEntry);
    }
    if (btnVin) {
        btnVin->setVisible(m_config.enableVinEntry);
    }
    if (btnCard) {
        btnCard->setVisible(m_config.enableCardEntry);
    }
    QLabel *idleId = m_idlePage->findChild<QLabel *>("label_id");
    if (idleId) {
        idleId->raise();
    }
    QLabel *authorizePole = m_authorizePage->findChild<QLabel *>("lblPole");
    QLabel *authorizeTips = m_authorizePage->findChild<QLabel *>("lblTips");
    QLabel *authorizeTips2 = m_authorizePage->findChild<QLabel *>("lblTips_2");
    QRWidget *authorizeQr = m_authorizePage->findChild<QRWidget *>("qrMain");
    if (btnAboutAuthorize) {
        btnAboutAuthorize->raise();
    }
    if (btnBackAuthorize) {
        btnBackAuthorize->raise();
    }
    if (authorizePole) {
        authorizePole->raise();
    }
    if (authorizeTips) {
        authorizeTips->raise();
    }
    if (authorizeTips2) {
        authorizeTips2->raise();
    }
    if (authorizeQr) {
        authorizeQr->raise();
    }
    QPushButton *chargingBack = m_chargingPage->findChild<QPushButton *>("btnBack");
    QLabel *chargingTitle = m_chargingPage->findChild<QLabel *>("lblTitle");
    if (chargingBack) {
        chargingBack->raise();
    }
    if (btnAboutCharging) {
        btnAboutCharging->raise();
    }
    if (chargingTitle) {
        chargingTitle->raise();
    }
    QPushButton *checkoutBack = m_checkoutPage->findChild<QPushButton *>("btnBack");
    QLabel *checkoutHint = m_checkoutPage->findChild<QLabel *>("lblHint");
    if (checkoutBack) {
        checkoutBack->raise();
    }
    if (checkoutHint) {
        checkoutHint->raise();
    }
    if (btnBackAbout) {
        btnBackAbout->raise();
    }

    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    if (cell0) {
        cell0->setPort(0);
        connect(cell0, SIGNAL(cellclick(int)), this, SLOT(handleCellClick(int)));
    }
    if (cell1) {
        cell1->setPort(1);
        connect(cell1, SIGNAL(cellclick(int)), this, SLOT(handleCellClick(int)));
    }

    QTabWidget *tab = m_aboutPage->findChild<QTabWidget *>("tabWidget");
    if (tab) {
        tab->setCurrentIndex(0);
    }
    m_aboutTabWidget = tab;
    QLabel *aboutIcon = m_aboutPage->findChild<QLabel *>("label_cdz_icon");
    if (!aboutIcon) {
        aboutIcon = new QLabel(m_aboutPage);
        aboutIcon->setObjectName("label_cdz_icon");
        aboutIcon->setGeometry(8, 2, 32, 32);
        aboutIcon->setPixmap(QPixmap(":/cui/Resources/cdz_tb.png"));
        aboutIcon->setScaledContents(true);
        aboutIcon->show();
    }
    QLabel *aboutTitle = m_aboutPage->findChild<QLabel *>("label_cdz_title");
    if (!aboutTitle) {
        aboutTitle = new QLabel(QString::fromUtf8("直流充电桩"), m_aboutPage);
        aboutTitle->setObjectName("label_cdz_title");
        aboutTitle->setGeometry(44, 0, 180, 41);
        aboutTitle->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
        aboutTitle->show();
    }
    QLabel *aboutId = m_aboutPage->findChild<QLabel *>("label_id");
    if (!aboutId) {
        aboutId = new QLabel(m_aboutPage);
        aboutId->setObjectName("label_id");
        aboutId->setGeometry(590, 0, 211, 41);
        aboutId->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        aboutId->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
        aboutId->show();
    }
    setupAboutTabs();
}

void RuntimeWindow::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[HMI] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 2);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/plat/+/event", 1);
    m_mqtt.subscribe(p + "/plat/+/setConfig", 1);
    m_mqtt.subscribe(p + "/mon/0/event", 1);
}

void RuntimeWindow::onMqttMessage(const std::string &topic, const std::string &payload)
{
    uint8_t gun = 0;
    std::string tail;

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/logic/", gun, tail)) {
        if (gun <= 1 && tail == "event") {
            handleLogicEvent(gun, payload);
        } else if (gun <= 1 && tail == "feeData") {
            handleFeeData(gun, payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/pile/", gun, tail)) {
        if (gun <= 1 && tail == "data") {
            handlePileData(gun, payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/plat/", gun, tail)) {
        if (gun <= 1 && (tail == "event" || tail == "setConfig")) {
            handlePlatEvent(gun, payload);
        }
        return;
    }

    if (topic == m_config.mqttTopicPrefix + "/mon/0/event") {
        handleMonEvent(payload);
    }
}

void RuntimeWindow::handleLogicEvent(uint8_t gun, const std::string &payload)
{
    // BY ZF: 逻辑事件只负责刷新状态缓存，不在回调线程直接操作界面。
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *evt = cJSON_GetObjectItem(root, "event");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (evt && cJSON_IsString(evt) && evt->valuestring &&
        std::strcmp(evt->valuestring, "state_change") == 0 &&
        cJSON_IsObject(data)) {
        cJSON *to = cJSON_GetObjectItem(data, "to");
        if (to && cJSON_IsString(to) && to->valuestring) {
            QMutexLocker locker(&m_dataMutex);
            m_guns[gun].state = to->valuestring;
            m_guns[gun].lastStateChangeMs = nowMs();
            if (m_guns[gun].state != "PREPARE" && m_guns[gun].state != "STARTING") {
                m_forceIdleView = false;
                m_manualFocusLocked = false;
            }
            if (!m_manualFocusLocked && m_guns[gun].state != "IDLE") {
                m_focusGun = gun;
            }
            m_guns[gun].stopReason = getString(data, "reasonText",
                getString(data, "stopReasonText",
                getString(data, "reason",
                getString(data, "stopReason", m_guns[gun].stopReason))));
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handleFeeData(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].totalEnergy = getNumber(data, "totalEnergy", m_guns[gun].totalEnergy);
        m_guns[gun].totalAmount = getNumber(data, "totalAmount", m_guns[gun].totalAmount);
        m_guns[gun].chargedTime = getNumber(data, "chargedTime", m_guns[gun].chargedTime);
        m_guns[gun].soc = getInt(data, "soc", getInt(data, "vehicleSoc", m_guns[gun].soc));
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handlePileData(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (type && cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "yc") == 0 &&
        cJSON_IsObject(data)) {
        const double v = getNumber(data, "outputVoltage", m_guns[gun].voltage);
        const double c = getNumber(data, "outputCurrent", m_guns[gun].current);

        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].voltage = v;
        m_guns[gun].current = c;
        m_guns[gun].reqVoltage = getNumber(data, "bmsReqVoltage", m_guns[gun].reqVoltage);
        m_guns[gun].reqCurrent = getNumber(data, "bmsReqCurrent", m_guns[gun].reqCurrent);
        m_guns[gun].remainMinutes = getNumber(data, "estimatedRemainTime", m_guns[gun].remainMinutes);
        m_guns[gun].batteryMaxTemp = getNumber(data, "batteryMaxTemp", m_guns[gun].batteryMaxTemp);
        m_guns[gun].power = (v * c) / 1000.0;
        m_guns[gun].soc = getInt(data, "soc", getInt(data, "vehicleSoc", m_guns[gun].soc));
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handlePlatEvent(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    const bool isSetConfigType = (type && cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "setConfig") == 0);
    const bool isSetConfigCmd = (cmd && cJSON_IsString(cmd) && cmd->valuestring &&
        std::strcmp(cmd->valuestring, "setConfig") == 0);
    if ((!isSetConfigType && !isSetConfigCmd) || !cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return;
    }

    QMutexLocker locker(&m_dataMutex);

    int gc = getInt(data, "gunCount", m_gunCount);
    if (gc < 1) {
        gc = 1;
    }
    if (gc > 2) {
        gc = 2;
    }
    m_gunCount = gc;

    m_cdzId = getString(data, "cdzId", m_cdzId);
    m_cdzNo = getString(data, "cdzNo", m_cdzNo);
    m_operatorId = getString(data, "operatorId", m_operatorId);

    std::string mac = getString(data, "macAddr", "");
    if (mac.empty()) {
        mac = getString(data, "mac", m_macAddr);
    }
    if (!mac.empty()) {
        m_macAddr = mac;
    }

    if (gun < m_guns.size()) {
        const std::string gunId = getString(data, "gunId", "");
        if (!gunId.empty()) {
            m_guns[gun].gunId = gunId;
        }

        const std::string qrCode = getString(data, "qrCode",
            getString(data, "qrcode",
            getString(data, "qrPayload",
            getString(data, "payUrl", m_guns[gun].qrPayload))));
        if (!qrCode.empty()) {
            m_guns[gun].qrPayload = qrCode;
        }

        m_guns[gun].configured = true;
    }

    cJSON *guns = cJSON_GetObjectItem(data, "guns");
    if (guns && cJSON_IsArray(guns)) {
        const int n = cJSON_GetArraySize(guns);
        for (int i = 0; i < n; ++i) {
            cJSON *item = cJSON_GetArrayItem(guns, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            const int idx = getInt(item, "gun", -1);
            if (idx < 0 || idx > 1) {
                continue;
            }
            m_guns[idx].gunId = getString(item, "gunId", m_guns[idx].gunId);
            const std::string qrCode = getString(item, "qrcode",
                getString(item, "qrCode",
                getString(item, "qrPayload",
                getString(item, "payUrl", m_guns[idx].qrPayload))));
            if (!qrCode.empty()) {
                m_guns[idx].qrPayload = qrCode;
            }
            m_guns[idx].configured = true;
            if (m_guns[idx].qrPayload.empty()) {
                rebuildQrPayload(idx);
            }
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (m_guns[i].qrPayload.empty()) {
            rebuildQrPayload(i);
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::rebuildQrPayload(int gun)
{
    if (gun < 0 || gun >= static_cast<int>(m_guns.size())) {
        return;
    }

    const std::string gunId = m_guns[gun].gunId.empty() ? std::to_string(gun) : m_guns[gun].gunId;
    const std::string operatorId = m_operatorId.empty() ? std::string("0") : m_operatorId;
    const std::string macAddr = m_macAddr.empty() ? std::string("000000000000000000000000") : m_macAddr;
    m_guns[gun].qrPayload = std::string("hlht://") + gunId + "." + operatorId + "/" + macAddr;
}

void RuntimeWindow::setupAboutTabs()
{
    // BY ZF: A3 前两页改为运行态重建，避免继续维护旧项目遗留的大量静态控件。
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *feeTab = m_aboutTabWidget->widget(0);
    QWidget *deviceTab = m_aboutTabWidget->widget(1);
    if (!feeTab || !deviceTab) {
        return;
    }

    m_aboutTabWidget->setTabText(0, QString::fromUtf8("分时信息"));
    m_aboutTabWidget->setTabText(1, QString::fromUtf8("设备信息"));

    QList<QWidget *> feeChildren;
    const QObjectList feeObjects = feeTab->children();
    int i = 0;
    for (i = 0; i < feeObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(feeObjects.at(i));
        if (child && child->parent() == feeTab) {
            feeChildren.append(child);
        }
    }
    for (i = 0; i < feeChildren.size(); ++i) {
        feeChildren.at(i)->hide();
    }

    QList<QWidget *> deviceChildren;
    const QObjectList deviceObjects = deviceTab->children();
    for (i = 0; i < deviceObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(deviceObjects.at(i));
        if (child && child->parent() == deviceTab) {
            deviceChildren.append(child);
        }
    }
    for (i = 0; i < deviceChildren.size(); ++i) {
        deviceChildren.at(i)->hide();
    }

    QWidget *feeOverlay = feeTab->findChild<QWidget *>("feeInfoOverlay");
    if (!feeOverlay) {
        feeOverlay = new QWidget(feeTab);
        feeOverlay->setObjectName("feeInfoOverlay");
        feeOverlay->setGeometry(0, 0, 755, 345);
        feeOverlay->setStyleSheet("background:transparent;");

        QFrame *topCard = static_cast<QFrame *>(makeInfoCard(feeOverlay, QString(), QRect(12, 8, 731, 48)));
        topCard->setStyleSheet("QFrame{background:transparent;border:none;}");
        makeValueLabel(topCard, "lblCurrentSeg", QRect(18, 20, 170, 24), 14, true);
        makeValueLabel(topCard, "lblCurrentChargeFee", QRect(194, 20, 160, 24), 14, false);
        makeValueLabel(topCard, "lblCurrentServiceFee", QRect(364, 20, 160, 24), 14, false);
        makeValueLabel(topCard, "lblCurrentTotalFee", QRect(534, 20, 170, 24), 14, true);

        QLabel *legendCharge = new QLabel(QString::fromUtf8("电费"), feeOverlay);
        legendCharge->setGeometry(548, 64, 60, 20);
        legendCharge->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';}");
        QLabel *legendChargeColor = new QLabel(feeOverlay);
        legendChargeColor->setGeometry(522, 67, 18, 14);
        legendChargeColor->setStyleSheet("QLabel{background:rgb(0,176,102);border-radius:3px;}");
        QLabel *legendService = new QLabel(QString::fromUtf8("服务费"), feeOverlay);
        legendService->setGeometry(662, 64, 60, 20);
        legendService->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';}");
        QLabel *legendServiceColor = new QLabel(feeOverlay);
        legendServiceColor->setGeometry(636, 67, 18, 14);
        legendServiceColor->setStyleSheet("QLabel{background:rgb(18,108,245);border-radius:3px;}");

        m_feeChart = new FeeModelChartWidget(feeOverlay);
        m_feeChart->setObjectName("feeModelChart");
        m_feeChart->setGeometry(12, 74, 731, 248);
        legendChargeColor->raise();
        legendCharge->raise();
        legendServiceColor->raise();
        legendService->raise();
        feeOverlay->show();
    }

    QWidget *deviceOverlay = deviceTab->findChild<QWidget *>("deviceInfoOverlay");
    if (!deviceOverlay) {
        deviceOverlay = new QWidget(deviceTab);
        deviceOverlay->setObjectName("deviceInfoOverlay");
        deviceOverlay->setGeometry(0, 0, 755, 345);
        deviceOverlay->setStyleSheet("background:transparent;");

        QFrame *leftCard = static_cast<QFrame *>(makeInfoCard(deviceOverlay, QString::fromUtf8("软件版本信息"), QRect(12, 10, 360, 300)));
        QFrame *rightCard = static_cast<QFrame *>(makeInfoCard(deviceOverlay, QString::fromUtf8("通信状态信息"), QRect(383, 10, 360, 300)));

        const int rowY[] = {48, 84, 120, 156, 192, 228};
        const char *nameLabels[] = {
            "logic", "meter", "mon", "comm", "daemon", "hmi"
        };
        const char *valueNames[] = {
            "lblVersionLogic", "lblVersionMeter", "lblVersionMon",
            "lblVersionComm", "lblVersionDaemon", "lblVersionHmi"
        };
        for (i = 0; i < 6; ++i) {
            QLabel *name = new QLabel(QString::fromLatin1(nameLabels[i]), leftCard);
            name->setGeometry(18, rowY[i], 70, 24);
            name->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
            makeValueLabel(leftCard, valueNames[i], QRect(88, rowY[i], 250, 24), 14, false);
        }

        QLabel *deviceIdTitle = new QLabel(QString::fromUtf8("设备身份"), leftCard);
        deviceIdTitle->setGeometry(18, 264, 80, 20);
        deviceIdTitle->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
        makeValueLabel(leftCard, "lblDeviceIdentity", QRect(100, 260, 240, 26), 12, false);

        const QString commTitles[] = {
            QString::fromUtf8("平台通信状态"),
            QString::fromUtf8("电表通信状态"),
            QString::fromUtf8("主控通信状态"),
            QString::fromUtf8("当前故障数量"),
            QString::fromUtf8("最近故障名称"),
            QString::fromUtf8("最近故障时间")
        };
        const char *commValueNames[] = {
            "lblCommPlatform", "lblCommMeter", "lblCommController",
            "lblDeviceFaultCount", "lblDeviceLatestFault", "lblDeviceLatestFaultTime"
        };
        for (i = 0; i < 6; ++i) {
            QLabel *title = new QLabel(commTitles[i], rightCard);
            title->setGeometry(18, rowY[i], 120, 24);
            title->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
            QLabel *value = makeValueLabel(rightCard, commValueNames[i], QRect(150, rowY[i], 180, 24), 14, true);
            if (i < 3) {
                value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
        }
        deviceOverlay->show();
    }

    setupChargeRecordTab();
}

void RuntimeWindow::setupChargeRecordTab()
{
    // BY ZF: 充电记录统一收敛为单页，A/B/全部通过筛选按钮切换。
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *summaryTab = m_aboutPage->findChild<QWidget *>("tab_9");
    QWidget *aTab = m_aboutPage->findChild<QWidget *>("tab_7");
    QWidget *bTab = m_aboutPage->findChild<QWidget *>("tab_8");

    int idx = -1;
    if (summaryTab) {
        idx = m_aboutTabWidget->indexOf(summaryTab);
        if (idx >= 0) {
            m_aboutTabWidget->removeTab(idx);
        }
    }
    if (bTab) {
        idx = m_aboutTabWidget->indexOf(bTab);
        if (idx >= 0) {
            m_aboutTabWidget->removeTab(idx);
        }
    }
    if (!aTab) {
        return;
    }

    idx = m_aboutTabWidget->indexOf(aTab);
    if (idx >= 0) {
        m_aboutTabWidget->setTabText(idx, QString::fromUtf8("充电记录"));
    }

    QList<QWidget *> tabChildren;
    const QObjectList tabObjects = aTab->children();
    int i = 0;
    for (i = 0; i < tabObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(tabObjects.at(i));
        if (child && child->parent() == aTab) {
            tabChildren.append(child);
        }
    }
    for (i = 0; i < tabChildren.size(); ++i) {
        tabChildren.at(i)->hide();
    }

    QWidget *recordOverlay = aTab->findChild<QWidget *>("chargeRecordOverlay");
    if (recordOverlay) {
        return;
    }

    recordOverlay = new QWidget(aTab);
    recordOverlay->setObjectName("chargeRecordOverlay");
    recordOverlay->setGeometry(0, 0, 755, 345);
    recordOverlay->setStyleSheet("background:transparent;");

    QFrame *filterBar = static_cast<QFrame *>(makeInfoCard(recordOverlay, QString(), QRect(12, 8, 731, 50)));
    filterBar->setStyleSheet("QFrame{background:transparent;border:none;}");

    QPushButton *btnAll = new QPushButton(QString::fromUtf8("全部"), filterBar);
    btnAll->setObjectName("btnChargeRecordAll");
    btnAll->setGeometry(18, 12, 92, 28);
    btnAll->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                          "QPushButton:pressed{background:rgb(205,208,210);}");
    connect(btnAll, SIGNAL(clicked()), this, SLOT(showAllChargeRecords()));

    QPushButton *btnA = new QPushButton(QString::fromUtf8("A枪"), filterBar);
    btnA->setObjectName("btnChargeRecordA");
    btnA->setGeometry(122, 12, 92, 28);
    btnA->setStyleSheet(btnAll->styleSheet());
    connect(btnA, SIGNAL(clicked()), this, SLOT(showAGunChargeRecords()));

    QPushButton *btnB = new QPushButton(QString::fromUtf8("B枪"), filterBar);
    btnB->setObjectName("btnChargeRecordB");
    btnB->setGeometry(226, 12, 92, 28);
    btnB->setStyleSheet(btnAll->styleSheet());
    connect(btnB, SIGNAL(clicked()), this, SLOT(showBGunChargeRecords()));

    m_chargeRecordPageLabel = new QLabel(filterBar);
    m_chargeRecordPageLabel->setObjectName("lblChargeRecordPage");
    m_chargeRecordPageLabel->setGeometry(432, 12, 92, 28);
    m_chargeRecordPageLabel->setAlignment(Qt::AlignCenter);
    m_chargeRecordPageLabel->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");

    QPushButton *btnPrevLeft = new QPushButton(QString::fromUtf8("上一页"), filterBar);
    btnPrevLeft->setObjectName("btnChargeRecordPrevLeft");
    btnPrevLeft->setGeometry(536, 12, 92, 28);
    btnPrevLeft->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                               "QPushButton:pressed{background:rgb(205,208,210);}");
    connect(btnPrevLeft, SIGNAL(clicked()), this, SLOT(showPrevChargeRecordPage()));

    QPushButton *btnNextLeft = new QPushButton(QString::fromUtf8("下一页"), filterBar);
    btnNextLeft->setObjectName("btnChargeRecordNextLeft");
    btnNextLeft->setGeometry(640, 12, 92, 28);
    btnNextLeft->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnNextLeft, SIGNAL(clicked()), this, SLOT(showNextChargeRecordPage()));

    m_chargeRecordTable = new QTableWidget(recordOverlay);
    m_chargeRecordTable->setObjectName("chargeRecordTable");
    m_chargeRecordTable->setGeometry(12, 52, 731, 270);
    m_chargeRecordTable->setColumnCount(7);
    m_chargeRecordTable->setRowCount(0);
    QStringList headers;
    headers << QString::fromUtf8("编号")
            << QString::fromUtf8("枪号")
            << QString::fromUtf8("启动时间")
            << QString::fromUtf8("结束时间")
            << QString::fromUtf8("电量(kWh)")
            << QString::fromUtf8("金额(元)")
            << QString::fromUtf8("停机原因");
    m_chargeRecordTable->setHorizontalHeaderLabels(headers);
    m_chargeRecordTable->verticalHeader()->hide();
    m_chargeRecordTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_chargeRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chargeRecordTable->setFocusPolicy(Qt::NoFocus);
    m_chargeRecordTable->setShowGrid(true);
    m_chargeRecordTable->setAlternatingRowColors(true);
    m_chargeRecordTable->setStyleSheet(
        "QTableWidget{background:rgb(244,246,247);alternate-background-color:rgb(236,238,239);"
        "gridline-color:rgb(188,188,188);border:1px solid rgb(168,168,168);color:rgb(38,45,52);font:13px 'MS Shell Dlg 2';}"
        "QHeaderView::section{background:rgb(214,217,219);color:rgb(38,45,52);border:1px solid rgb(168,168,168);font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    m_chargeRecordTable->setColumnWidth(0, 54);
    m_chargeRecordTable->setColumnWidth(1, 52);
    m_chargeRecordTable->setColumnWidth(2, 156);
    m_chargeRecordTable->setColumnWidth(3, 156);
    m_chargeRecordTable->setColumnWidth(4, 96);
    m_chargeRecordTable->setColumnWidth(5, 86);
    m_chargeRecordTable->setColumnWidth(6, 122);

    QPushButton *btnPrevRight = new QPushButton(QString::fromUtf8("上一页"), recordOverlay);
    btnPrevRight->setObjectName("btnChargeRecordPrevRight");
    btnPrevRight->setGeometry(12, 324, 86, 22);
    btnPrevRight->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnPrevRight, SIGNAL(clicked()), this, SLOT(showPrevChargeRecordPage()));
    btnPrevRight->hide();

    QPushButton *btnNextRight = new QPushButton(QString::fromUtf8("下一页"), recordOverlay);
    btnNextRight->setObjectName("btnChargeRecordNextRight");
    btnNextRight->setGeometry(104, 324, 86, 22);
    btnNextRight->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnNextRight, SIGNAL(clicked()), this, SLOT(showNextChargeRecordPage()));
    btnNextRight->hide();
    recordOverlay->show();
}

void RuntimeWindow::refreshChargeRecordCache(bool forceReload)
{
    const uint64_t now = nowMs();
    if (!forceReload && now - m_lastChargeRecordCheckMs < 5000) {
        return;
    }
    m_lastChargeRecordCheckMs = now;

    const QString targetPathAbs = QString::fromUtf8("/mnt/nandflash/data/chargerecords.db");
    const QString targetPathRel = QString::fromUtf8("mnt/nandflash/data/chargerecords.db");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/Zhongshihua2.0/release/chargerecords.db");
    QString dbPath;
    if (QFileInfo(targetPathAbs).exists()) {
        dbPath = targetPathAbs;
    } else if (QFileInfo(targetPathRel).exists()) {
        dbPath = targetPathRel;
    } else {
        dbPath = fallbackPath;
    }
    if (!QFileInfo(dbPath).exists()) {
        return;
    }

    sqlite3 *db = 0;
    if (sqlite3_open(dbPath.toLocal8Bit().constData(), &db) != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const char *sql =
        "SELECT id,gun_no,trade_no,charge_start_time,charge_end_time,total_elect,total_cost,reason "
        "FROM charge_trade_info ORDER BY charge_start_time DESC,id DESC LIMIT 100;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    std::vector<ChargeRecordItem> records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChargeRecordItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.gunNo = sqlite3_column_int(stmt, 1);
        const char *tradeNo = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const char *startTime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        const char *endTime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        item.tradeNo = tradeNo ? tradeNo : "";
        item.startTime = startTime ? startTime : "";
        item.endTime = endTime ? endTime : "";
        item.totalElect = sqlite3_column_double(stmt, 5);
        item.totalCost = sqlite3_column_double(stmt, 6);
        item.reason = sqlite3_column_int(stmt, 7);
        records.push_back(item);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    m_chargeRecords = records;
}

void RuntimeWindow::refreshChargeRecordTable()
{
    if (!m_chargeRecordTable) {
        return;
    }

    QPushButton *btnAll = m_aboutPage->findChild<QPushButton *>("btnChargeRecordAll");
    QPushButton *btnA = m_aboutPage->findChild<QPushButton *>("btnChargeRecordA");
    QPushButton *btnB = m_aboutPage->findChild<QPushButton *>("btnChargeRecordB");
    const QString activeStyle =
        "QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:8px;color:white;font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    const QString normalStyle =
        "QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    if (btnAll) btnAll->setStyleSheet(m_recordGunFilter < 0 ? activeStyle : normalStyle);
    if (btnA) btnA->setStyleSheet(m_recordGunFilter == 0 ? activeStyle : normalStyle);
    if (btnB) btnB->setStyleSheet(m_recordGunFilter == 1 ? activeStyle : normalStyle);

    std::vector<ChargeRecordItem> filtered;
    int i = 0;
    for (i = 0; i < static_cast<int>(m_chargeRecords.size()); ++i) {
        const ChargeRecordItem &item = m_chargeRecords[i];
        if (m_recordGunFilter >= 0 && item.gunNo != m_recordGunFilter) {
            continue;
        }
        filtered.push_back(item);
    }

    const int totalPages = filtered.empty() ? 1 : ((static_cast<int>(filtered.size()) - 1) / m_chargeRecordPageSize + 1);
    if (m_chargeRecordPage >= totalPages) {
        m_chargeRecordPage = totalPages - 1;
    }
    if (m_chargeRecordPage < 0) {
        m_chargeRecordPage = 0;
    }
    if (m_chargeRecordPageLabel) {
        m_chargeRecordPageLabel->setText(QString::fromUtf8("%1/%2").arg(m_chargeRecordPage + 1).arg(totalPages));
    }

    QPushButton *btnPrevLeft = m_aboutPage->findChild<QPushButton *>("btnChargeRecordPrevLeft");
    QPushButton *btnNextLeft = m_aboutPage->findChild<QPushButton *>("btnChargeRecordNextLeft");
    QPushButton *btnPrevRight = m_aboutPage->findChild<QPushButton *>("btnChargeRecordPrevRight");
    QPushButton *btnNextRight = m_aboutPage->findChild<QPushButton *>("btnChargeRecordNextRight");
    const bool hasPrev = (m_chargeRecordPage > 0);
    const bool hasNext = (m_chargeRecordPage + 1 < totalPages);
    if (btnPrevLeft) btnPrevLeft->setEnabled(hasPrev);
    if (btnPrevRight) btnPrevRight->setEnabled(hasPrev);
    if (btnNextLeft) btnNextLeft->setEnabled(hasNext);
    if (btnNextRight) btnNextRight->setEnabled(hasNext);

    m_chargeRecordTable->setRowCount(0);
    int displayRow = 0;
    const int startIndex = m_chargeRecordPage * m_chargeRecordPageSize;
    const int endIndex = qMin(startIndex + m_chargeRecordPageSize, static_cast<int>(filtered.size()));
    for (i = startIndex; i < endIndex; ++i) {
        const ChargeRecordItem &item = filtered[i];

        m_chargeRecordTable->insertRow(displayRow);
        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(item.id));
        QTableWidgetItem *gunItem = new QTableWidgetItem(item.gunNo == 0 ? QString::fromUtf8("A枪") : QString::fromUtf8("B枪"));
        QTableWidgetItem *startItem = new QTableWidgetItem(ymdhmsText(item.startTime));
        QTableWidgetItem *endItem = new QTableWidgetItem(ymdhmsText(item.endTime));
        QTableWidgetItem *electItem = new QTableWidgetItem(QString::number(item.totalElect, 'f', 3));
        QTableWidgetItem *costItem = new QTableWidgetItem(QString::number(item.totalCost, 'f', 2));
        QTableWidgetItem *reasonItem = new QTableWidgetItem(chargeRecordReasonText(item.reason));
        m_chargeRecordTable->setItem(displayRow, 0, idItem);
        m_chargeRecordTable->setItem(displayRow, 1, gunItem);
        m_chargeRecordTable->setItem(displayRow, 2, startItem);
        m_chargeRecordTable->setItem(displayRow, 3, endItem);
        m_chargeRecordTable->setItem(displayRow, 4, electItem);
        m_chargeRecordTable->setItem(displayRow, 5, costItem);
        m_chargeRecordTable->setItem(displayRow, 6, reasonItem);
        ++displayRow;
    }
}

void RuntimeWindow::refreshFeeModelCache(bool forceReload)
{
    // BY ZF: 分时模型统一从数据库最新记录读取，空库时保持旧缓存不被覆盖。
    const uint64_t now = nowMs();
    if (!forceReload && now - m_lastFeeModelCheckMs < 3000) {
        return;
    }
    m_lastFeeModelCheckMs = now;

    const QString targetPathAbs = QString::fromUtf8("/mnt/nandflash/data/feemodel.db");
    QString dbPath;
    dbPath = targetPathAbs;
    
    if (!QFileInfo(dbPath).exists()) {
        std::cerr << "[A3] feemodel db not found: " << dbPath.toStdString() << std::endl;
        return;
    }

    sqlite3 *db = 0;
    if (sqlite3_open(dbPath.toLocal8Bit().constData(), &db) != SQLITE_OK || !db) {
        std::cerr << "[A3] open feemodel db failed: " << dbPath.toStdString() << std::endl;
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const char *sqlByTime =
        "SELECT feeModelId,timeNum,timeSeg,segFlag,chargeFee,serviceFee,timeStamp "
        "FROM tbFeeModel ORDER BY timeStamp DESC,id DESC LIMIT 1;";
    const char *sqlById =
        "SELECT feeModelId,timeNum,timeSeg,segFlag,chargeFee,serviceFee,timeStamp "
        "FROM tbFeeModel ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sqlByTime, -1, &stmt, 0) != SQLITE_OK) {
        std::cerr << "[A3] prepare feemodel query by time failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    FeeModelData newModel;
    bool ok = readFeeModelFromStmt(stmt, newModel);
    sqlite3_finalize(stmt);

    if (!ok) {
        if (sqlite3_prepare_v2(db, sqlById, -1, &stmt, 0) == SQLITE_OK) {
            newModel = FeeModelData();
            ok = readFeeModelFromStmt(stmt, newModel);
            sqlite3_finalize(stmt);
        } else {
            std::cerr << "[A3] prepare feemodel query by id failed: " << sqlite3_errmsg(db) << std::endl;
        }
    }

    sqlite3_close(db);

    if (!ok || !newModel.valid) {
        std::cerr << "[A3] feemodel parse empty from db: " << dbPath.toStdString() << std::endl;
        return;
    }

    if (!m_feeModel.valid || m_feeModel.timeStamp != newModel.timeStamp || m_feeModel.feeModelId != newModel.feeModelId) {
        m_feeModel = newModel;
        std::cerr << "[A3] feemodel loaded: " << m_feeModel.feeModelId
                  << " segments=" << m_feeModel.segments.size()
                  << " time=" << m_feeModel.timeStamp << std::endl;
    }
}

void RuntimeWindow::handleMonEvent(const std::string &payload)
{
    // BY ZF: 设备信息页的通信状态与模块版本由 mon 事件缓存驱动。
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) {
        data = root;
    }

    QMutexLocker locker(&m_dataMutex);
    m_monStatus.logic.version = getString(data, "logicVersion", m_monStatus.logic.version);
    m_monStatus.logic.buildDate = getString(data, "logicDate", getString(data, "logicBuildDate", m_monStatus.logic.buildDate));
    m_monStatus.meter.version = getString(data, "meterVersion", m_monStatus.meter.version);
    m_monStatus.meter.buildDate = getString(data, "meterDate", getString(data, "meterBuildDate", m_monStatus.meter.buildDate));
    m_monStatus.mon.version = getString(data, "monVersion", m_monStatus.mon.version);
    m_monStatus.mon.buildDate = getString(data, "monDate", getString(data, "monBuildDate", m_monStatus.mon.buildDate));

    m_monStatus.platformComm = getInt(data, "platformComm", getInt(data, "platformLink", m_monStatus.platformComm));
    m_monStatus.meterComm = getInt(data, "meterComm", getInt(data, "meterLink", m_monStatus.meterComm));
    m_monStatus.controllerComm = getInt(data, "controllerComm", getInt(data, "mainCtrlComm", m_monStatus.controllerComm));
    m_monStatus.activeFaultCount = getInt(data, "faultCount", getInt(data, "activeFaultCount", m_monStatus.activeFaultCount));
    m_monStatus.latestFaultName = getString(data, "latestFaultName", getString(data, "faultName", m_monStatus.latestFaultName));
    m_monStatus.latestFaultTime = getString(data, "latestFaultTime", getString(data, "faultTime", m_monStatus.latestFaultTime));
    m_monStatus.lastEventTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString();

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::refreshUi()
{
    // BY ZF: 统一在 UI 线程做页面选择和控件刷新，避免多线程直接改界面。
    std::vector<GunUiData> guns;
    int focusGun = 0;
    bool showAbout = false;
    bool forceIdleView = false;
    const QString nowText = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    {
        QMutexLocker locker(&m_dataMutex);
        guns = m_guns;
        focusGun = m_focusGun;
        showAbout = m_showAbout;
        forceIdleView = m_forceIdleView;
    }
    m_bottomTime->setText(nowText);
    m_bottomTime->raise();

    applyIdleLayout();
    refreshIdlePage(guns);
    if (focusGun < 0 || focusGun >= static_cast<int>(guns.size())) {
        focusGun = 0;
    }
    refreshAuthorizePage(guns[focusGun]);
    refreshChargingPage(guns[focusGun]);
    refreshCheckoutPage(guns[focusGun]);
    refreshAboutPage();

    if (showAbout) {
        m_stack->setCurrentWidget(m_aboutPage);
        return;
    }

    if (forceIdleView) {
        m_stack->setCurrentWidget(m_idlePage);
        return;
    }

    PageId page = decidePage(guns, focusGun);
    {
        QMutexLocker locker(&m_dataMutex);
        m_focusGun = focusGun;
    }

    if (page == PageIdle) {
        m_stack->setCurrentWidget(m_idlePage);
    } else if (page == PageAuthorize) {
        m_stack->setCurrentWidget(m_authorizePage);
    } else if (page == PageCharging) {
        m_stack->setCurrentWidget(m_chargingPage);
    } else if (page == PageCheckout) {
        m_stack->setCurrentWidget(m_checkoutPage);
    }
}

void RuntimeWindow::showAboutPage()
{
    {
        QMutexLocker locker(&m_dataMutex);
        m_showAbout = true;
    }
    refreshFeeModelCache(true);
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
    m_stack->setCurrentWidget(m_aboutPage);
}

void RuntimeWindow::leaveAboutPage()
{
    {
        QMutexLocker locker(&m_dataMutex);
        m_showAbout = false;
    }
    refreshUi();
}

void RuntimeWindow::handleCellClick(int port)
{
    {
        QMutexLocker locker(&m_dataMutex);
        m_forceIdleView = false;
        m_manualFocusLocked = true;
        if (port >= 0 && port < static_cast<int>(m_guns.size())) {
            m_focusGun = port;
        }
    }
    refreshUi();
}

void RuntimeWindow::returnToIdlePage()
{
    {
        QMutexLocker locker(&m_dataMutex);
        m_forceIdleView = true;
        m_manualFocusLocked = false;
    }
    refreshUi();
}

void RuntimeWindow::showAllChargeRecords()
{
    m_recordGunFilter = -1;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showAGunChargeRecords()
{
    m_recordGunFilter = 0;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showBGunChargeRecords()
{
    m_recordGunFilter = 1;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showPrevChargeRecordPage()
{
    if (m_chargeRecordPage > 0) {
        --m_chargeRecordPage;
    }
    refreshChargeRecordTable();
}

void RuntimeWindow::showNextChargeRecordPage()
{
    ++m_chargeRecordPage;
    refreshChargeRecordTable();
}

void RuntimeWindow::refreshIdlePage(const std::vector<GunUiData> &guns)
{
    setLabelText(m_idlePage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));

    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    QLabel *leftFault = m_idlePage->findChild<QLabel *>("label_gth");
    QLabel *rightFault = m_idlePage->findChild<QLabel *>("label_gth_3");

    if (cell0 && guns.size() > 0) {
        const GunUiData &g = guns[0];
        cell0->setTips(idleCellText(g.totalEnergy, g.totalAmount, g.chargedTime,
                                    g.voltage, g.current, g.soc, g.state));
    }

    if (cell1) {
        if (m_gunCount > 1 && guns.size() > 1) {
            cell1->show();
            const GunUiData &g = guns[1];
            cell1->setTips(idleCellText(g.totalEnergy, g.totalAmount, g.chargedTime,
                                        g.voltage, g.current, g.soc, g.state));
        } else {
            cell1->hide();
        }
    }

    if (leftFault) {
        leftFault->setVisible(guns.size() > 0 && guns[0].state == "ERROR");
        if (leftFault->isVisible()) {
            leftFault->setPixmap(QPixmap(":/cui/Resources/gth.png"));
            leftFault->setScaledContents(true);
            leftFault->raise();
        }
    }
    if (rightFault) {
        rightFault->setVisible(m_gunCount > 1 && guns.size() > 1 && guns[1].state == "ERROR");
        if (rightFault->isVisible()) {
            rightFault->setPixmap(QPixmap(":/cui/Resources/gth.png"));
            rightFault->setScaledContents(true);
            rightFault->raise();
        }
    }
}

void RuntimeWindow::applyIdleLayout()
{
    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    QWidget *line = m_idlePage->findChild<QWidget *>("line");
    QLabel *labelA = m_idlePage->findChild<QLabel *>("label_2");
    QLabel *labelB = m_idlePage->findChild<QLabel *>("label_3");
    QLabel *leftBg = m_idlePage->findChild<QLabel *>("label_zq");
    QLabel *rightBg = m_idlePage->findChild<QLabel *>("label_yq");
    QLabel *leftFault = m_idlePage->findChild<QLabel *>("label_gth");
    QLabel *rightFault = m_idlePage->findChild<QLabel *>("label_gth_3");
    QLabel *leftMask = m_idlePage->findChild<QLabel *>("label_tk_zq");
    QLabel *rightMask = m_idlePage->findChild<QLabel *>("label_tk_zq_2");

    if (!cell0 || !cell1 || !labelA || !labelB || !leftBg || !rightBg || !leftFault || !rightFault || !leftMask || !rightMask) {
        return;
    }

    if (m_gunCount <= 1) {
        if (line) {
            line->hide();
        }
        cell0->setGeometry(235, 100, 330, 240);
        labelA->setGeometry(355, 95, 91, 61);
        labelA->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        leftBg->setGeometry(275, 90, 290, 296);
        leftMask->setGeometry(275, 90, 290, 296);
        leftFault->setGeometry(501, 148, 48, 48);

        cell1->hide();
        labelB->hide();
        rightBg->hide();
        rightMask->hide();
        rightFault->hide();
    } else {
        if (line) {
            line->show();
            line->setGeometry(400, 90, 2, 320);
        }
        cell0->setGeometry(40, 100, 330, 240);
        labelA->setGeometry(80, 95, 91, 61);
        labelA->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        leftBg->setGeometry(80, 90, 290, 296);
        leftMask->setGeometry(80, 90, 290, 296);
        leftFault->setGeometry(306, 148, 48, 48);

        cell1->show();
        cell1->setGeometry(400, 100, 330, 240);
        labelB->show();
        labelB->setGeometry(440, 95, 81, 51);
        labelB->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        rightBg->show();
        rightBg->setGeometry(440, 90, 290, 296);
        rightMask->show();
        rightMask->setGeometry(440, 90, 290, 296);
        rightFault->setGeometry(666, 148, 48, 48);
    }
}

void RuntimeWindow::refreshAuthorizePage(const GunUiData &gun)
{
    const bool hasExtraEntry = m_config.enableMergeChargeEntry || m_config.enableVinEntry || m_config.enableCardEntry;
    const bool inserted = (gun.state != "IDLE");
    setLabelText(m_authorizePage, "lblPole", QString::fromUtf8("%1枪").arg(gun.gun == 0 ? "A" : "B"));
    setLabelText(m_authorizePage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));

    QLabel *tips = m_authorizePage->findChild<QLabel *>("lblTips");
    QLabel *tips2 = m_authorizePage->findChild<QLabel *>("lblTips_2");
    QLabel *labelMs = m_authorizePage->findChild<QLabel *>("label_ms");
    QLabel *ewmBg = m_authorizePage->findChild<QLabel *>("label_ewm");
    QPushButton *btnMergeCharge = m_authorizePage->findChild<QPushButton *>("bbms");
    QPushButton *btnVin = m_authorizePage->findChild<QPushButton *>("vin");
    QPushButton *btnCard = m_authorizePage->findChild<QPushButton *>("card");

    QRWidget *qr = m_authorizePage->findChild<QRWidget *>("qrMain");
    QLabel *qcq = m_authorizePage->findChild<QLabel *>("qcq");
    if (qr) {
        if (!gun.qrPayload.empty()) {
            qr->setQRData(QString::fromStdString(gun.qrPayload));
        }
        if (inserted) {
            if (hasExtraEntry) {
                qr->setGeometry(40, 80, 280, 280);
            } else {
                qr->setGeometry(260, 80, 280, 280);
            }
            qr->show();
        } else {
            qr->hide();
        }
    }
    if (ewmBg) {
        if (inserted) {
            if (hasExtraEntry) {
                ewmBg->setGeometry(40, 84, 280, 312);
            } else {
                ewmBg->setGeometry(260, 84, 280, 312);
            }
            ewmBg->show();
            ewmBg->raise();
        } else {
            ewmBg->hide();
        }
    }
    if (btnMergeCharge) {
        if (inserted && m_config.enableMergeChargeEntry) {
            btnMergeCharge->setGeometry(448, 132, 88, 88);
            btnMergeCharge->show();
        } else {
            btnMergeCharge->hide();
        }
    }
    if (btnVin) {
        if (inserted && m_config.enableVinEntry) {
            btnVin->setGeometry(628, 132, 88, 88);
            btnVin->show();
        } else {
            btnVin->hide();
        }
    }
    if (btnCard) {
        if (inserted && m_config.enableCardEntry) {
            btnCard->setGeometry(448, 242, 88, 88);
            btnCard->show();
        } else {
            btnCard->hide();
        }
    }
    if (tips) {
        tips->hide();
    }
    if (tips2) {
        if (inserted) {
            tips2->setText(QString::fromUtf8("扫码启动充电"));
            tips2->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
            if (hasExtraEntry) {
                tips2->setGeometry(46, 348, 268, 44);
            } else {
                tips2->setGeometry(240, 348, 320, 44);
            }
            tips2->show();
            tips2->raise();
        } else {
            tips2->hide();
        }
    }
    if (qr && qr->isVisible()) {
        qr->raise();
    }
    if (labelMs) {
        labelMs->hide();
    }
    if (qcq) {
        QPixmap qcqPixmap(":/cui/Resources/qcq.png");
        qcq->setPixmap(qcqPixmap);
        qcq->setScaledContents(false);
        if (!inserted && !qcqPixmap.isNull()) {
            const int x = (800 - qcqPixmap.width()) / 2;
            const int y = (480 - qcqPixmap.height()) / 2;
            qcq->setGeometry(x, y, qcqPixmap.width(), qcqPixmap.height());
            qcq->show();
            qcq->raise();
        } else {
            qcq->hide();
        }
    }
}

void RuntimeWindow::refreshChargingPage(const GunUiData &gun)
{
    setLabelText(m_chargingPage, "lblTitle", QString());
    setLabelText(m_chargingPage, "lblMoney", QString::fromUtf8("%1 元").arg(formatMoney(gun.totalAmount)));
    setLabelText(m_chargingPage, "lblKwh", QString::fromUtf8("%1 kWh").arg(formatEnergy(gun.totalEnergy)));
    setLabelText(m_chargingPage, "lblChargeTime", formatChargeTime(gun.chargedTime));
    QString voltageText = QString::fromUtf8("%1 V").arg(formatVoltage(gun.voltage));
    QString currentText = QString::fromUtf8("%1 A").arg(formatCurrent(gun.current));
    if (gun.reqVoltage > 0.0) {
        voltageText += QString::fromUtf8("/%1 V").arg(formatVoltage(gun.reqVoltage));
    }
    if (gun.reqCurrent != 0.0) {
        currentText += QString::fromUtf8("/%1 A").arg(formatCurrent(gun.reqCurrent));
    }
    setLabelText(m_chargingPage, "lblU", voltageText);
    setLabelText(m_chargingPage, "lblI", currentText);
    setLabelText(m_chargingPage, "lblSoc", gun.soc >= 0 ? QString::fromUtf8("%1 %").arg(gun.soc) : "-- %");
    setLabelText(m_chargingPage, "lblRemainTime", formatRemainMinutes(gun.remainMinutes));
    setLabelText(m_chargingPage, "lblBatteryMaxTemp", QString::fromUtf8("%1 ℃").arg(QString::number(gun.batteryMaxTemp, 'f', 0)));
    setLabelText(m_chargingPage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));
}

void RuntimeWindow::refreshCheckoutPage(const GunUiData &gun)
{
    setLabelText(m_checkoutPage, "lblHint", QString::fromUtf8("本次充电已结束"));
    setLabelText(m_checkoutPage, "lblChargeTime", formatHourMinute(gun.chargedTime));
    setLabelText(m_checkoutPage, "lblMoney", QString::fromUtf8("%1元").arg(formatMoney(gun.totalAmount)));
    setLabelText(m_checkoutPage, "lblKwh", QString::fromUtf8("%1kWh").arg(formatEnergy(gun.totalEnergy)));
    setLabelText(m_checkoutPage, "lblendmoney", QString::fromUtf8("请手机中查看"));
    setLabelText(m_checkoutPage, "stop_reason", stopReasonText(gun.stopReason));
}

void RuntimeWindow::refreshAboutPage()
{
    refreshFeeModelCache(false);
    refreshChargeRecordCache(false);
    refreshChargeRecordTable();
    setLabelText(m_aboutPage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));
    setLabelText(m_aboutPage, "current_time", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    setLabelText(m_aboutPage, "label_timeshow", QString::fromStdString(m_cdzId));
    setLabelText(m_aboutPage, "lblVersion", QString::fromUtf8("Zhongshihua2.0"));
    setLabelText(m_aboutPage, "lblVersion_cui", QString::fromUtf8("流程适配版"));
    setLabelText(m_aboutPage, "lblVersion_pro", QString::fromUtf8("MQTT Runtime"));
    setLabelText(m_aboutPage, "lblnetstat", m_mqttReady ? QString::fromUtf8("已连接") : QString::fromUtf8("未连接"));
    setLabelText(m_aboutPage, "lblcomm", QString::fromUtf8("逻辑订阅模式"));
    setLabelText(m_aboutPage, "lblPoleId_2", QString::fromStdString(m_cdzId));
    setLabelText(m_aboutPage, "lblJzqAddr", QString::fromStdString(m_macAddr));

    setLabelText(m_aboutPage, "lblVersionLogic",
                 QString("logic    %1    %2")
                    .arg(QString::fromStdString(m_monStatus.logic.version.empty() ? "--" : m_monStatus.logic.version))
                    .arg(QString::fromStdString(m_monStatus.logic.buildDate.empty() ? "--" : m_monStatus.logic.buildDate)));
    setLabelText(m_aboutPage, "lblVersionMeter",
                 QString("meter    %1    %2")
                    .arg(QString::fromStdString(m_monStatus.meter.version.empty() ? "--" : m_monStatus.meter.version))
                    .arg(QString::fromStdString(m_monStatus.meter.buildDate.empty() ? "--" : m_monStatus.meter.buildDate)));
    setLabelText(m_aboutPage, "lblVersionMon",
                 QString("mon      %1    %2")
                    .arg(QString::fromStdString(m_monStatus.mon.version.empty() ? "--" : m_monStatus.mon.version))
                    .arg(QString::fromStdString(m_monStatus.mon.buildDate.empty() ? "--" : m_monStatus.mon.buildDate)));
    setLabelText(m_aboutPage, "lblVersionComm", QString("comm     --    --"));
    setLabelText(m_aboutPage, "lblVersionDaemon", QString("daemon   --    --"));
    setLabelText(m_aboutPage, "lblVersionHmi", QString("hmi      Zhongshihua2.0    MQTT Runtime"));

    setLabelText(m_aboutPage, "lblCommPlatform", QString::number(m_monStatus.platformComm));
    setLabelText(m_aboutPage, "lblCommMeter", QString::number(m_monStatus.meterComm));
    setLabelText(m_aboutPage, "lblCommController", QString::number(m_monStatus.controllerComm));
    setLabelText(m_aboutPage, "lblDeviceFaultCount", QString::number(m_monStatus.activeFaultCount));
    setLabelText(m_aboutPage, "lblDeviceLatestFault",
                 QString::fromStdString(m_monStatus.latestFaultName.empty() ? "--" : m_monStatus.latestFaultName));
    setLabelText(m_aboutPage, "lblDeviceLatestFaultTime",
                 QString::fromStdString(m_monStatus.latestFaultTime.empty() ? "--" : m_monStatus.latestFaultTime));
    setLabelText(m_aboutPage, "lblDeviceIdentity",
                 QString("No:%1  ID:%2  MAC:%3")
                    .arg(QString::fromStdString(m_cdzNo.empty() ? "--" : m_cdzNo))
                    .arg(QString::fromStdString(m_cdzId.empty() ? "--" : m_cdzId))
                    .arg(QString::fromStdString(m_macAddr.empty() ? "--" : m_macAddr)));

    QString currentSeg = QString::fromUtf8("当前时段：--");
    QString currentCharge = QString::fromUtf8("电费：-- 元");
    QString currentService = QString::fromUtf8("服务费：-- 元");
    QString currentTotal = QString::fromUtf8("总价：-- 元");
    QVector<FeeBarData> bars;
    if (m_feeModel.valid) {
        int idx = 0;
        for (idx = 0; idx < static_cast<int>(m_feeModel.segments.size()); ++idx) {
            FeeBarData bar;
            bar.startTime = hhmmText(m_feeModel.segments[idx].startTime);
            bar.endTime = hhmmText(m_feeModel.segments[idx].endTime);
            bar.chargeFee = m_feeModel.segments[idx].chargeFee;
            bar.serviceFee = m_feeModel.segments[idx].serviceFee;
            bars.push_back(bar);
        }

        if (m_feeModel.currentIndex >= 0 && m_feeModel.currentIndex < static_cast<int>(m_feeModel.segments.size())) {
            const FeeSegment &seg = m_feeModel.segments[m_feeModel.currentIndex];
            currentSeg = QString::fromUtf8("当前时段：%1 - %2")
                .arg(hhmmText(seg.startTime))
                .arg(hhmmText(seg.endTime));
            currentCharge = QString::fromUtf8("电费：%1 元").arg(formatFeeValue(seg.chargeFee));
            currentService = QString::fromUtf8("服务费：%1 元").arg(formatFeeValue(seg.serviceFee));
            currentTotal = QString::fromUtf8("总价：%1 元").arg(formatFeeValue(seg.chargeFee + seg.serviceFee));
        }
    }
    setLabelText(m_aboutPage, "lblCurrentSeg", currentSeg);
    setLabelText(m_aboutPage, "lblCurrentChargeFee", currentCharge);
    setLabelText(m_aboutPage, "lblCurrentServiceFee", currentService);
    setLabelText(m_aboutPage, "lblCurrentTotalFee", currentTotal);
    if (m_feeChart) {
        m_feeChart->setBars(bars, m_feeModel.currentIndex);
    }
}

RuntimeWindow::PageId RuntimeWindow::decidePage(const std::vector<GunUiData> &guns, int &focusGun) const
{
    if (guns.empty()) {
        focusGun = 0;
        return PageIdle;
    }

    if (m_manualFocusLocked && focusGun >= 0 && focusGun < static_cast<int>(guns.size())) {
        const std::string &state = guns[focusGun].state;
        if (state == "CHARGING" || state == "STOPPING") {
            return PageCharging;
        }
        if (state == "PREPARE" || state == "STARTING") {
            return PageAuthorize;
        }
        if (state == "IDLE") {
            return PageAuthorize;
        }
        if (state == "STOPPED") {
            return PageCheckout;
        }
        return PageIdle;
    }

    int bestGun = focusGun;
    int bestPriority = 0;
    uint64_t bestTs = 0;
    PageId bestPage = PageIdle;

    for (int i = 0; i < m_gunCount && i < static_cast<int>(guns.size()); ++i) {
        const std::string &state = guns[i].state;
        PageId page = PageIdle;
        int priority = 0;

        if (state == "CHARGING" || state == "STOPPING") {
            page = PageCharging;
            priority = 4;
        } else if (state == "PREPARE" || state == "STARTING") {
            page = PageAuthorize;
            priority = 3;
        } else if (state == "STOPPED") {
            page = PageCheckout;
            priority = 2;
        } else if (state == "ERROR") {
            page = PageIdle;
            priority = 1;
        }

        if (priority == 0) {
            continue;
        }

        if (priority > bestPriority ||
            (priority == bestPriority && guns[i].lastStateChangeMs >= bestTs)) {
            bestPriority = priority;
            bestPage = page;
            bestGun = i;
            bestTs = guns[i].lastStateChangeMs;
        }
    }

    focusGun = bestGun;
    return bestPage;
}

bool RuntimeWindow::parseTopicGun(const std::string &topic,
                                  const std::string &prefix,
                                  uint8_t &gun,
                                  std::string &tail)
{
    if (topic.find(prefix) != 0U) {
        return false;
    }

    const std::string rest = topic.substr(prefix.size());
    const size_t p = rest.find('/');
    if (p == std::string::npos) {
        return false;
    }

    const std::string gunStr = rest.substr(0, p);
    if (gunStr.empty()) {
        return false;
    }

    const int g = std::atoi(gunStr.c_str());
    if (g < 0 || g > 255) {
        return false;
    }

    gun = static_cast<uint8_t>(g);
    tail = rest.substr(p + 1);
    return true;
}
