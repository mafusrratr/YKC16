#include "hmi_window.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QDateTime>
#include <QString>
#include <QMutexLocker>
#include <QMetaObject>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#include "../../base/common/config_manager_lite.h"
#include "../../base/cjson/include/cjson/cJSON.h"

namespace {

static const int BTN_CHARGE_START = 0;
static const int BTN_DISCHARGE_START = 1;
static const int BTN_STOP = 2;

// BY ZF: 将状态码映射为中文显示文本。
static QString stateMessage(const std::string& state, bool dischargeMode)
{
    if (state == "IDLE") {
        return QString::fromUtf8("请插枪");
    }
    if (state == "PREPARE") {
        return QString::fromUtf8("请点击启动按钮");
    }
    if (state == "STARTING") {
        if (dischargeMode) {
            return QString::fromUtf8("放电启动中...");
        }
        return QString::fromUtf8("充电启动中...");
    }
    if (state == "CHARGING") {
        if (dischargeMode) {
            return QString::fromUtf8("放电进行中");
        }
        return QString::fromUtf8("充电进行中");
    }
    if (state == "STOPPING") {
        return QString::fromUtf8("停止中...");
    }
    if (state == "STOPPED") {
        if (dischargeMode) {
            return QString::fromUtf8("已完成放电，如需再次放电请重新插拔充电枪");
        }
        return QString::fromUtf8("已完成充电，如需再次充电请重新插拔充电枪");
    }
    if (state == "ERROR") {
        return QString::fromUtf8("充电桩故障");
    }
    return QString::fromUtf8("状态未知");
}

static double getNumber(cJSON* obj, const char* key, double defVal = 0.0)
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

HmiWindow::HmiWindow(QWidget* parent)
    : QWidget(parent)
    , m_mqttReady(false)
    , m_gunCount(1)
    , m_operatorId("0")
    , m_macAddr("000000000000000000000000")
    , m_guns(2)
{
    // BY ZF: 默认准备 2 枪缓存，布局由 setConfig/gun_count 决定。
    m_guns[0].gun = 0;
    m_guns[1].gun = 1;

    connect(&m_uiTimer, SIGNAL(timeout()), this, SLOT(onRefreshUi()));
}

HmiWindow::~HmiWindow()
{
    // BY ZF: 退出时停止 MQTT 循环线程。
    if (m_mqttReady) {
        m_mqtt.loopStop(true);
        m_mqtt.disconnect();
    }
}

bool HmiWindow::initialize()
{
    if (!loadConfig()) {
        return false;
    }

    setWindowTitle(QString::fromUtf8("TCU HMI"));
    resize(m_config.screenWidth, m_config.screenHeight);
    setMinimumSize(m_config.screenWidth, m_config.screenHeight);
    setMaximumSize(m_config.screenWidth, m_config.screenHeight);

    if (!initMqtt()) {
        return false;
    }

    // BY ZF: 周期刷新降为 500ms，关键业务数据变化时主动触发刷新。
    m_uiTimer.start(500);

    // BY ZF: HMI 固定全屏运行，不通过配置开关控制。
    showFullScreen();

    return true;
}

bool HmiWindow::loadConfig()
{
    // BY ZF: HMI 固定读取路径。
    ConfigManagerLite& cfg = ConfigManagerLite::getInstance();
    if (!cfg.loadConfig("/usr/app/config/tcu_hmi.ini")) {
        // BY ZF: 读取失败使用默认值，便于本地调试。
        std::cerr << "[HMI] use default config /usr/app/config/tcu_hmi.ini not found" << std::endl;
        return true;
    }

    const std::string section = "Hmi";

    m_config.screenWidth = cfg.getInt(section, "screen_width", 800);
    m_config.screenHeight = cfg.getInt(section, "screen_height", 480);
    // BY ZF: 全屏模式固定开启，不读取配置项。
    m_config.fullScreen = true;

    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_hmi");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.biasNo = cfg.getInt(section, "bias_no", 0);
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");

    // BY ZF: 广西DKY HMI 固定单枪显示。
    m_gunCount = 1;

    return true;
}

bool HmiWindow::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[HMI] mqtt init failed" << std::endl;
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

void HmiWindow::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[HMI] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 2);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/cmd", 2);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/meter/+/data", 0);
}

void HmiWindow::onMqttMessage(const std::string& topic, const std::string& payload)
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
        if (gun <= 1 && tail == "cmd") {
            handlePileCmd(gun, payload);
        } else if (gun <= 1 && tail == "data") {
            handlePileData(gun, payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/meter/", gun, tail)) {
        if (gun <= 1 && tail == "data") {
            handleMeterData(gun, payload);
        }
        return;
    }

}

void HmiWindow::handleLogicEvent(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* evt = cJSON_GetObjectItem(root, "event");
    cJSON* data = cJSON_GetObjectItem(root, "data");

    if (evt && cJSON_IsString(evt) && evt->valuestring && std::strcmp(evt->valuestring, "state_change") == 0 && cJSON_IsObject(data)) {
        cJSON* to = cJSON_GetObjectItem(data, "to");
        if (to && cJSON_IsString(to) && to->valuestring) {
            QMutexLocker locker(&m_dataMutex);
            m_guns[gun].state = to->valuestring;
            if (m_guns[gun].state == "IDLE" || m_guns[gun].state == "STOPPED") {
                m_guns[gun].dischargeMode = false;
            }
        }
    } else if (evt && cJSON_IsString(evt) && evt->valuestring &&
               std::strcmp(evt->valuestring, "auth_basis") == 0 && cJSON_IsObject(data)) {
        cJSON* v2g = cJSON_GetObjectItem(data, "v2g");
        if (v2g && cJSON_IsNumber(v2g)) {
            QMutexLocker locker(&m_dataMutex);
            m_guns[gun].dischargeMode = (v2g->valueint != 0);
        }
    }

    cJSON_Delete(root);
    // BY ZF: 状态变化后主动刷新（跨线程使用队列调用）。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::handlePileCmd(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cmd && cJSON_IsString(cmd) && cmd->valuestring &&
        std::strcmp(cmd->valuestring, "start_charge") == 0 && cJSON_IsObject(data)) {
        cJSON* v2g = cJSON_GetObjectItem(data, "v2g");
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].dischargeMode = (v2g && cJSON_IsNumber(v2g) && v2g->valueint != 0);
    }

    cJSON_Delete(root);
    // BY ZF: 直接监听 tcu_logic 下发给 pile_controller 的启动命令，确保远程放电流程切到放电界面。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::handleFeeData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].totalEnergy = getNumber(data, "totalEnergy", m_guns[gun].totalEnergy);
        m_guns[gun].totalAmount = getNumber(data, "totalAmount", m_guns[gun].totalAmount);
        m_guns[gun].chargedTime = getNumber(data, "chargedTime", m_guns[gun].chargedTime);
    }

    cJSON_Delete(root);
    // BY ZF: 费用变化后主动刷新（跨线程使用队列调用）。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::handlePileData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");

    if (type && cJSON_IsString(type) && type->valuestring && std::strcmp(type->valuestring, "yc") == 0 && cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        // BY ZF: 支持 pile/yc 有符号电流；若 meter 未覆盖时也可直接显示。
        m_guns[gun].voltage = getNumber(data, "outputVoltage", m_guns[gun].voltage);
        m_guns[gun].current = getNumber(data, "outputCurrent",
                             getNumber(data, "bmsMeasuredCurrent",
                             getNumber(data, "bmsReqCurrent", m_guns[gun].current)));
        m_guns[gun].power = (m_guns[gun].voltage * m_guns[gun].current) / 1000.0;
        m_guns[gun].soc = static_cast<int>(getNumber(data, "soc", static_cast<double>(m_guns[gun].soc)));
        const int conn = static_cast<int>(getNumber(data, "yxVehicleConnectStatus",
                                                    getNumber(data, "vehicleConnectStatus",
                                                              static_cast<double>(m_guns[gun].connectStatus))));
        m_guns[gun].connectStatus = conn;
    }

    cJSON_Delete(root);
    // BY ZF: 遥测变化后主动刷新（跨线程使用队列调用）。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::handleMeterData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    (void)gun;
    // BY ZF: HMI 电压/电流/功率统一使用 pile/yc 数据，meter 消息不再覆盖显示值。
    cJSON_Delete(root);
}

bool HmiWindow::parseTopicGun(const std::string& topic,
                              const std::string& prefix,
                              uint8_t& gun,
                              std::string& tail) const
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

    const int g = std::atoi(gunStr.c_str()) - m_config.biasNo;
    if (g < 0 || g > 255) {
        return false;
    }

    gun = static_cast<uint8_t>(g);
    tail = rest.substr(p + 1);
    return true;
}

void HmiWindow::onRefreshUi()
{
    update();
}

void HmiWindow::paintEvent(QPaintEvent* event)
{
    (void)event;

    QPainter painter(this);
    painter.fillRect(rect(), QColor(18, 28, 40));

    std::vector<GunUiData> snapshot;
    int gunCount = 1;
    {
        QMutexLocker locker(&m_dataMutex);
        gunCount = m_gunCount;
        if (gunCount < 1) {
            gunCount = 1;
        }
        if (gunCount > 2) {
            gunCount = 2;
        }
        snapshot = m_guns;
        m_clickAreas.clear();
    }

    const int w = width();
    const int h = height();
    const int margin = 10;

    if (gunCount == 1) {
        const QRect panel(margin, margin, w - margin * 2, h - margin * 2);
        drawGunPanel(painter, panel, snapshot[0]);
    } else {
        const int panelW = (w - margin * 3) / 2;
        const QRect leftRect(margin, margin, panelW, h - margin * 2);
        const QRect rightRect(margin * 2 + panelW, margin, panelW, h - margin * 2);
        drawGunPanel(painter, leftRect, snapshot[0]);
        drawGunPanel(painter, rightRect, snapshot[1]);
    }
}

void HmiWindow::drawGunPanel(QPainter& painter, const QRect& rect, const GunUiData& data)
{
    painter.save();

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(240, 245, 250));
    painter.drawRoundedRect(rect, 12, 12);

    painter.setPen(QColor(20, 35, 55));

    const bool singlePanel = (rect.width() >= 600);
    QFont titleFont("WenQuanYi Zen Hei", 32, QFont::Bold);
    QFont bodyFont("WenQuanYi Zen Hei", singlePanel ? 28 : 24, QFont::Normal);
    QFont smallFont("WenQuanYi Zen Hei", singlePanel ? 16 : 12, QFont::Normal);

    painter.setFont(titleFont);
    const QString title = QString::fromUtf8("枪%1").arg(data.gun);
    painter.drawText(rect.adjusted(16, 12, -16, -12), Qt::AlignLeft | Qt::AlignTop, title);

    painter.setFont(bodyFont);
    painter.drawText(rect.adjusted(16, 44, -16, -12), Qt::AlignLeft | Qt::TextWordWrap, stateMessage(data.state, data.dischargeMode));

    if (data.state == "CHARGING") {
        const double chargedMinutes = data.chargedTime / 60.0;
        QString detail;
        if (data.dischargeMode) {
            detail = QString::fromUtf8(
                         "放电电压: %1 V\n"
                         "放电电流: %2 A\n"
                         "实时功率: %3 kW\n"
                         "实时金额: %4 元\n"
                         "放电电量: %5 kWh\n"
                         "放电时长: %6 分钟")
                         .arg(QString::number(data.voltage, 'f', 1))
                         .arg(QString::number(data.current, 'f', 1))
                         .arg(QString::number(data.power, 'f', 2))
                         .arg(QString::number(data.totalAmount, 'f', 2))
                         .arg(QString::number(data.totalEnergy, 'f', 3))
                         .arg(QString::number(chargedMinutes, 'f', 1));
        } else {
            detail = QString::fromUtf8(
                         "充电电压: %1 V\n"
                         "充电电流: %2 A\n"
                         "实时功率: %3 kW\n"
                         "实时金额: %4 元\n"
                         "充电电量: %5 kWh\n"
                         "充电时长: %6 分钟")
                         .arg(QString::number(data.voltage, 'f', 1))
                         .arg(QString::number(data.current, 'f', 1))
                         .arg(QString::number(data.power, 'f', 2))
                         .arg(QString::number(data.totalAmount, 'f', 2))
                         .arg(QString::number(data.totalEnergy, 'f', 3))
                         .arg(QString::number(chargedMinutes, 'f', 1));
        }
        painter.drawText(rect.adjusted(16, 90, -16, -16), Qt::AlignLeft | Qt::AlignTop, detail);
    } else if (data.state == "STOPPED") {
        QString detail;
        if (data.dischargeMode) {
            detail = QString::fromUtf8(
                         "放电电量: %1 kWh\n"
                         "放电金额: %2 元")
                         .arg(QString::number(data.totalEnergy, 'f', 3))
                         .arg(QString::number(data.totalAmount, 'f', 2));
        } else {
            detail = QString::fromUtf8(
                         "充电电量: %1 kWh\n"
                         "充电金额: %2 元")
                         .arg(QString::number(data.totalEnergy, 'f', 3))
                         .arg(QString::number(data.totalAmount, 'f', 2));
        }
        painter.drawText(rect.adjusted(16, 120, -16, -16), Qt::AlignLeft | Qt::AlignTop, detail);
    }

    drawActionButtons(painter, rect, data, static_cast<uint8_t>(data.gun));

    painter.setFont(smallFont);
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    painter.drawText(rect.adjusted(16, 0, -16, -10), Qt::AlignLeft | Qt::AlignBottom, ts);

    painter.restore();
}

bool HmiWindow::buttonVisible(const std::string& state, int btnType) const
{
    if (btnType == BTN_CHARGE_START || btnType == BTN_DISCHARGE_START) {
        return (state == "IDLE" || state == "PREPARE" || state == "STOPPED");
    }
    if (btnType == BTN_STOP) {
        return (state == "STARTING" || state == "CHARGING" || state == "STOPPING");
    }
    return false;
}

QRect HmiWindow::buttonRect(const QRect& panelRect, int index, int total) const
{
    const int btnW = (panelRect.width() - 16 * (total + 1)) / total;
    const int btnH = 60;
    const int y = panelRect.bottom() - btnH - 20;
    const int x = panelRect.left() + 16 + index * (btnW + 16);
    return QRect(x, y, btnW, btnH);
}

void HmiWindow::drawActionButtons(QPainter& painter, const QRect& rect, const GunUiData& data, uint8_t gun)
{
    struct BtnDef {
        int type;
        QString text;
        QColor color;
    };
    std::vector<BtnDef> defs;
    if (buttonVisible(data.state, BTN_CHARGE_START)) {
        BtnDef d;
        d.type = BTN_CHARGE_START;
        d.text = QString::fromUtf8("充电启动");
        d.color = QColor(34, 139, 34);
        defs.push_back(d);
    }
    if (buttonVisible(data.state, BTN_DISCHARGE_START)) {
        BtnDef d;
        d.type = BTN_DISCHARGE_START;
        d.text = QString::fromUtf8("放电启动");
        d.color = QColor(30, 110, 200);
        defs.push_back(d);
    }
    if (buttonVisible(data.state, BTN_STOP)) {
        BtnDef d;
        d.type = BTN_STOP;
        d.text = QString::fromUtf8("停止");
        d.color = QColor(210, 70, 50);
        defs.push_back(d);
    }
    if (defs.empty()) {
        return;
    }

    QFont btnFont("WenQuanYi Zen Hei", rect.width() >= 600 ? 26 : 20, QFont::Bold);
    painter.setFont(btnFont);
    painter.setPen(Qt::white);
    for (size_t i = 0; i < defs.size(); ++i) {
        QRect br = buttonRect(rect, static_cast<int>(i), static_cast<int>(defs.size()));
        painter.setBrush(defs[i].color);
        painter.drawRoundedRect(br, 10, 10);
        painter.drawText(br, Qt::AlignCenter, defs[i].text);
        ButtonClickArea area;
        area.gun = gun;
        area.type = defs[i].type;
        area.rect = br;
        m_clickAreas.push_back(area);
    }
}

void HmiWindow::publishLogicCmd(uint8_t gun, const char* cmd, bool v2g)
{
    if (!m_mqttReady || !cmd) {
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON* data = cJSON_CreateObject();
    if (std::strcmp(cmd, "start_charge") == 0) {
        cJSON_AddNumberToObject(data, "v2g", v2g ? 1 : 0);
    }
    cJSON_AddItemToObject(root, "data", data);
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        return;
    }

    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gun) + m_config.biasNo) << "/cmd";
    m_mqtt.publish(topic.str(), txt, 1, false);
    cJSON_free(txt);

    if (std::strcmp(cmd, "start_charge") == 0 && gun < m_guns.size()) {
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].dischargeMode = v2g;
    } else if (std::strcmp(cmd, "stop_charge") == 0 && gun < m_guns.size()) {
        QMutexLocker locker(&m_dataMutex);
        if (m_guns[gun].state == "IDLE" || m_guns[gun].state == "STOPPED") {
            m_guns[gun].dischargeMode = false;
        }
    }
}

void HmiWindow::mousePressEvent(QMouseEvent* event)
{
    if (!event) {
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const QPoint pt = event->pos();
    std::vector<ButtonClickArea> areas;
    {
        QMutexLocker locker(&m_dataMutex);
        areas = m_clickAreas;
    }

    for (size_t i = 0; i < areas.size(); ++i) {
        const ButtonClickArea& a = areas[i];
        if (!a.rect.contains(pt)) {
            continue;
        }
        if (a.type == BTN_CHARGE_START) {
            publishLogicCmd(a.gun, "start_charge", false);
        } else if (a.type == BTN_DISCHARGE_START) {
            publishLogicCmd(a.gun, "start_charge", true);
        } else if (a.type == BTN_STOP) {
            publishLogicCmd(a.gun, "stop_charge", false);
        }
        break;
    }
}
