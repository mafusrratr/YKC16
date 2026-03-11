#include "app_shell.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include "../../../base/common/config_manager_lite.h"
#include "../../../base/cjson/include/cjson/cJSON.h"
#include "page/detail_page.h"
#include "page/state_page.h"

namespace {

static double getNumber(cJSON* obj, const char* key, double defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON* n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        return n->valuedouble;
    }
    return defVal;
}

} // namespace

AppShell::AppShell(QWidget* parent)
    : QWidget(parent)
    , m_stack(0)
    , m_detailButton(0)
    , m_pageIdle(0)
    , m_pagePrepare(0)
    , m_pageStarting(0)
    , m_pageCharging(0)
    , m_pageStopping(0)
    , m_pageStopped(0)
    , m_pageDetail(0)
    , m_inDetail(false)
    , m_prevPageIndex(0)
    , m_mqttReady(false)
{
    connect(&m_refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshUi()));
}

AppShell::~AppShell()
{
    if (m_mqttReady) {
        m_mqtt.loopStop(true);
        m_mqtt.disconnect();
    }
}

bool AppShell::initialize()
{
    if (!loadConfig()) {
        return false;
    }
    if (!initUi()) {
        return false;
    }
    if (!initMqtt()) {
        return false;
    }

    // BY ZF: 刷新周期 500ms，保持和现有 HMI 一致。
    m_refreshTimer.start(500);
    showFullScreen();
    return true;
}

bool AppShell::loadConfig()
{
    ConfigManagerLite& cfg = ConfigManagerLite::getInstance();
    if (!cfg.loadConfig("/usr/app/config/tcu_hmi.ini")) {
        std::cerr << "[ZSHV2_HMI] use default config /usr/app/config/tcu_hmi.ini not found" << std::endl;
        return true;
    }

    const std::string section = "Hmi";
    m_config.screenWidth = cfg.getInt(section, "screen_width", 800);
    m_config.screenHeight = cfg.getInt(section, "screen_height", 480);
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_hmi");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    return true;
}

bool AppShell::initUi()
{
    setWindowTitle(QString::fromUtf8("中石化V2 HMI"));
    resize(m_config.screenWidth, m_config.screenHeight);
    setMinimumSize(m_config.screenWidth, m_config.screenHeight);
    setMaximumSize(m_config.screenWidth, m_config.screenHeight);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setContentsMargins(10, 8, 10, 0);
    topBar->addStretch(1);

    m_detailButton = new QPushButton(QString::fromUtf8("详情(预留)"), this);
    m_detailButton->setMinimumHeight(38);
    topBar->addWidget(m_detailButton, 0);
    root->addLayout(topBar, 0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    m_pageIdle = new StatePage(this);
    m_pagePrepare = new StatePage(this);
    m_pageStarting = new StatePage(this);
    m_pageCharging = new StatePage(this);
    m_pageStopping = new StatePage(this);
    m_pageStopped = new StatePage(this);
    m_pageDetail = new DetailPage(this);

    m_stack->addWidget(m_pageIdle);      // 0
    m_stack->addWidget(m_pagePrepare);   // 1
    m_stack->addWidget(m_pageStarting);  // 2
    m_stack->addWidget(m_pageCharging);  // 3
    m_stack->addWidget(m_pageStopping);  // 4
    m_stack->addWidget(m_pageStopped);   // 5
    m_stack->addWidget(m_pageDetail);    // 6

    m_pageIdle->setTitle(QString::fromUtf8("待机"));
    m_pagePrepare->setTitle(QString::fromUtf8("准备中"));
    m_pageStarting->setTitle(QString::fromUtf8("启动中"));
    m_pageCharging->setTitle(QString::fromUtf8("充电中"));
    m_pageStopping->setTitle(QString::fromUtf8("停机中"));
    m_pageStopped->setTitle(QString::fromUtf8("已结束"));

    connect(m_detailButton, SIGNAL(clicked()), this, SLOT(onToggleDetail()));
    return true;
}

bool AppShell::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[ZSHV2_HMI] mqtt init failed" << std::endl;
        return false;
    }

    if (!m_config.mqttUsername.empty()) {
        m_mqtt.setUsernamePassword(m_config.mqttUsername, m_config.mqttPassword);
    }

    m_mqtt.setConnectHandler([this](int rc) { onMqttConnected(rc); });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        std::cerr << "[ZSHV2_HMI] mqtt connect failed " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }

    if (!m_mqtt.loopStart()) {
        std::cerr << "[ZSHV2_HMI] mqtt loop start failed" << std::endl;
        return false;
    }

    m_mqttReady = true;
    return true;
}

void AppShell::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[ZSHV2_HMI] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 2);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
}

void AppShell::onMqttMessage(const std::string& topic, const std::string& payload)
{
    uint8_t gun = 0;
    std::string tail;

    // BY ZF: 基础框架先按单枪接入，默认监听 gun0。
    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/logic/", gun, tail)) {
        if (gun != 0) {
            return;
        }
        if (tail == "event") {
            handleLogicEvent(payload);
        } else if (tail == "feeData") {
            handleFeeData(payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/pile/", gun, tail)) {
        if (gun == 0 && tail == "data") {
            handlePileData(payload);
        }
    }
}

void AppShell::handleLogicEvent(const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* evt = cJSON_GetObjectItem(root, "event");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (evt && cJSON_IsString(evt) && evt->valuestring
        && std::strcmp(evt->valuestring, "state_change") == 0
        && cJSON_IsObject(data)) {
        cJSON* to = cJSON_GetObjectItem(data, "to");
        if (to && cJSON_IsString(to) && to->valuestring) {
            QMutexLocker locker(&m_dataMutex);
            m_runtime.state = mapState(to->valuestring);
            if (m_runtime.state == STATE_IDLE) {
                m_runtime.chargedTimeSec = 0.0;
            }
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void AppShell::handleFeeData(const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        m_runtime.totalEnergy = getNumber(data, "totalEnergy", m_runtime.totalEnergy);
        m_runtime.totalAmount = getNumber(data, "totalAmount", m_runtime.totalAmount);
        const double chargedTime = getNumber(data, "chargedTime", -1.0);
        if (chargedTime >= 0.0) {
            m_runtime.chargedTimeSec = chargedTime;
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void AppShell::handlePileData(const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (type && cJSON_IsString(type) && type->valuestring
        && std::strcmp(type->valuestring, "yc") == 0
        && cJSON_IsObject(data)) {
        const double v = getNumber(data, "outputVoltage", 0.0);
        const double c = getNumber(data, "outputCurrent", 0.0);

        QMutexLocker locker(&m_dataMutex);
        m_runtime.voltage = v;
        m_runtime.current = c;
        m_runtime.power = (v * c) / 1000.0;
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void AppShell::renderByState(const RuntimeData& rd)
{
    const int minutes = static_cast<int>(rd.chargedTimeSec / 60.0);

    m_pageIdle->setMessage(QString::fromUtf8("请插枪"));
    m_pageIdle->setMetrics(QString::fromUtf8("当前无充电任务"));
    m_pageIdle->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    m_pagePrepare->setMessage(QString::fromUtf8("请等待启动命令"));
    m_pagePrepare->setMetrics(QString::fromUtf8("系统已就绪，等待充电启动"));
    m_pagePrepare->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    m_pageStarting->setMessage(QString::fromUtf8("充电启动中..."));
    m_pageStarting->setMetrics(QString::fromUtf8("正在下发启动指令并校验状态"));
    m_pageStarting->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(1);
        ss << "电压: " << rd.voltage << " V\n";
        ss << "电流: " << rd.current << " A\n";
        ss << "功率: " << rd.power << " kW\n";
        ss << "电量: ";
        ss.precision(3);
        ss << rd.totalEnergy << " kWh\n";
        ss << "金额: ";
        ss.precision(2);
        ss << rd.totalAmount << " 元\n";
        ss << "充电时间: " << minutes << " 分钟";
        m_pageCharging->setMessage(QString::fromUtf8("充电进行中"));
        m_pageCharging->setMetrics(QString::fromUtf8(ss.str().c_str()));
        m_pageCharging->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    }

    m_pageStopping->setMessage(QString::fromUtf8("停止中..."));
    m_pageStopping->setMetrics(QString::fromUtf8("正在执行停机流程"));
    m_pageStopping->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(3);
        ss << "本次电量: " << rd.totalEnergy << " kWh\n";
        ss.precision(2);
        ss << "本次金额: " << rd.totalAmount << " 元\n";
        ss << "充电时长: " << minutes << " 分钟\n";
        ss << "如需再次充电请重新插拔充电枪";
        m_pageStopped->setMessage(QString::fromUtf8("已完成充电"));
        m_pageStopped->setMetrics(QString::fromUtf8(ss.str().c_str()));
        m_pageStopped->setFooter(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    }

    if (!m_inDetail) {
        int idx = 0;
        switch (rd.state) {
        case STATE_IDLE: idx = 0; break;
        case STATE_PREPARE: idx = 1; break;
        case STATE_STARTING: idx = 2; break;
        case STATE_CHARGING: idx = 3; break;
        case STATE_STOPPING: idx = 4; break;
        case STATE_STOPPED: idx = 5; break;
        }
        m_stack->setCurrentIndex(idx);
        m_prevPageIndex = idx;
    }
}

AppShell::ChargeState AppShell::mapState(const char* stateText)
{
    if (!stateText) {
        return STATE_IDLE;
    }
    if (std::strcmp(stateText, "IDLE") == 0) {
        return STATE_IDLE;
    }
    if (std::strcmp(stateText, "PREPARE") == 0) {
        return STATE_PREPARE;
    }
    if (std::strcmp(stateText, "STARTING") == 0) {
        return STATE_STARTING;
    }
    if (std::strcmp(stateText, "CHARGING") == 0) {
        return STATE_CHARGING;
    }
    if (std::strcmp(stateText, "STOPPING") == 0) {
        return STATE_STOPPING;
    }
    if (std::strcmp(stateText, "STOPPED") == 0) {
        return STATE_STOPPED;
    }
    return STATE_IDLE;
}

bool AppShell::parseTopicGun(const std::string& topic,
                             const std::string& prefix,
                             uint8_t& gun,
                             std::string& tail)
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

void AppShell::onRefreshUi()
{
    RuntimeData snapshot;
    {
        QMutexLocker locker(&m_dataMutex);
        snapshot = m_runtime;
    }
    renderByState(snapshot);
}

void AppShell::onToggleDetail()
{
    if (!m_stack) {
        return;
    }

    if (!m_inDetail) {
        m_prevPageIndex = m_stack->currentIndex();
        m_stack->setCurrentWidget(m_pageDetail);
        m_inDetail = true;
        m_detailButton->setText(QString::fromUtf8("返回"));
    } else {
        m_stack->setCurrentIndex(m_prevPageIndex);
        m_inDetail = false;
        m_detailButton->setText(QString::fromUtf8("详情(预留)"));
    }
}
