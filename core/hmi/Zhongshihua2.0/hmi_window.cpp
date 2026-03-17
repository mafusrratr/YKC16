#include "hmi_window.h"

#include <QPainter>
#include <QPaintEvent>
#include <QDateTime>
#include <QString>
#include <QMutexLocker>
#include <QMetaObject>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../../base/common/config_manager_lite.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include <qrencode.h>

namespace {

// BY ZF: 将状态码映射为中文显示文本。
static QString stateMessage(const std::string& state)
{
    if (state == "IDLE") {
        return QString::fromUtf8("请插枪");
    }
    if (state == "PREPARE") {
        return QString::fromUtf8("请扫码启动");
    }
    if (state == "STARTING") {
        return QString::fromUtf8("充电启动中...");
    }
    if (state == "CHARGING") {
        return QString::fromUtf8("充电进行中");
    }
    if (state == "STOPPING") {
        return QString::fromUtf8("停止中...");
    }
    if (state == "STOPPED") {
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
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");

    m_gunCount = cfg.getInt(section, "gun_count", 1);
    if (m_gunCount < 1) {
        m_gunCount = 1;
    }
    if (m_gunCount > 2) {
        m_gunCount = 2;
    }

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
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/plat/+/event", 1);
    m_mqtt.subscribe(p + "/plat/+/setConfig", 1);
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
        }
    }

    cJSON_Delete(root);
    // BY ZF: 状态变化后主动刷新（跨线程使用队列调用）。
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
        const double v = getNumber(data, "outputVoltage", 0.0);
        const double c = getNumber(data, "outputCurrent", 0.0);
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].voltage = v;
        m_guns[gun].current = c;
        m_guns[gun].power = (v * c) / 1000.0;
    }

    cJSON_Delete(root);
    // BY ZF: 遥测变化后主动刷新（跨线程使用队列调用）。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::handlePlatEvent(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON* data = cJSON_GetObjectItem(root, "data");

    const bool isSetConfigType = (type && cJSON_IsString(type) && type->valuestring && std::strcmp(type->valuestring, "setConfig") == 0);
    const bool isSetConfigCmd = (cmd && cJSON_IsString(cmd) && cmd->valuestring && std::strcmp(cmd->valuestring, "setConfig") == 0);
    if ((!isSetConfigType && !isSetConfigCmd) || !cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return;
    }

    QMutexLocker locker(&m_dataMutex);

    // BY ZF: 读取枪数量并限制在 1~2。
    cJSON* gunCount = cJSON_GetObjectItem(data, "gunCount");
    if (gunCount && cJSON_IsNumber(gunCount)) {
        int gc = gunCount->valueint;
        if (gc < 1) {
            gc = 1;
        }
        if (gc > 2) {
            gc = 2;
        }
        m_gunCount = gc;
    }

    // BY ZF: 保存桩编号（兼容 cdzNo/cdzId）。
    cJSON* cdzId = cJSON_GetObjectItem(data, "cdzNo");
    if (!cdzId) {
        cdzId = cJSON_GetObjectItem(data, "cdzId");
    }
    if (cdzId) {
        if (cJSON_IsString(cdzId) && cdzId->valuestring) {
            m_cdzId = cdzId->valuestring;
        } else if (cJSON_IsNumber(cdzId)) {
            char tmp[64] = {0};
            std::snprintf(tmp, sizeof(tmp), "%.0f", cdzId->valuedouble);
            m_cdzId = tmp;
        }
    }

    // BY ZF: 保存运营商 ID（二维码规则使用）。
    cJSON* operatorId = cJSON_GetObjectItem(data, "operatorId");
    if (operatorId) {
        if (cJSON_IsString(operatorId) && operatorId->valuestring) {
            m_operatorId = operatorId->valuestring;
        } else if (cJSON_IsNumber(operatorId)) {
            char tmp[64] = {0};
            std::snprintf(tmp, sizeof(tmp), "%.0f", operatorId->valuedouble);
            m_operatorId = tmp;
        }
    }

    // BY ZF: 保存 MAC 地址（优先 macAddr，兼容 mac）。
    cJSON* macAddr = cJSON_GetObjectItem(data, "macAddr");
    if (!macAddr) {
        macAddr = cJSON_GetObjectItem(data, "mac");
    }
    if (macAddr) {
        if (cJSON_IsString(macAddr) && macAddr->valuestring) {
            m_macAddr = macAddr->valuestring;
        } else if (cJSON_IsNumber(macAddr)) {
            char tmp[64] = {0};
            std::snprintf(tmp, sizeof(tmp), "%.0f", macAddr->valuedouble);
            m_macAddr = tmp;
        }
    }

    // BY ZF: 读取每枪 gunId（兼容 guns[] 与 data 直传）。
    cJSON* guns = cJSON_GetObjectItem(data, "guns");
    if (guns && cJSON_IsArray(guns)) {
        const int n = cJSON_GetArraySize(guns);
        for (int i = 0; i < n; ++i) {
            cJSON* item = cJSON_GetArrayItem(guns, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            cJSON* g = cJSON_GetObjectItem(item, "gun");
            if (!g || !cJSON_IsNumber(g)) {
                continue;
            }
            const int idx = g->valueint;
            if (idx < 0 || idx > 1) {
                continue;
            }

            cJSON* gid = cJSON_GetObjectItem(item, "gunId");
            if (gid) {
                if (cJSON_IsString(gid) && gid->valuestring) {
                    m_guns[idx].gunId = gid->valuestring;
                } else if (cJSON_IsNumber(gid)) {
                    char tmp[64] = {0};
                    std::snprintf(tmp, sizeof(tmp), "%.0f", gid->valuedouble);
                    m_guns[idx].gunId = tmp;
                }
            }
            cJSON* qrc = cJSON_GetObjectItem(item, "qrCode");
            if (qrc && cJSON_IsString(qrc) && qrc->valuestring) {
                m_guns[idx].qrPayload = qrc->valuestring;
                m_guns[idx].hasCustomQr = !m_guns[idx].qrPayload.empty();
            }

            m_guns[idx].configured = true;
            rebuildQrPayload(idx);
        }
    } else if (gun <= 1) {
        // BY ZF: 中石化 setConfig 采用按枪 topic 下发，优先读取 data 内直接字段。
        cJSON* gid = cJSON_GetObjectItem(data, "gunId");
        if (gid) {
            if (cJSON_IsString(gid) && gid->valuestring) {
                m_guns[gun].gunId = gid->valuestring;
            } else if (cJSON_IsNumber(gid)) {
                char tmp[64] = {0};
                std::snprintf(tmp, sizeof(tmp), "%.0f", gid->valuedouble);
                m_guns[gun].gunId = tmp;
            }
        }

        cJSON* qrCode = cJSON_GetObjectItem(data, "qrCode");
        if (qrCode && cJSON_IsString(qrCode) && qrCode->valuestring) {
            m_guns[gun].qrPayload = qrCode->valuestring;
            m_guns[gun].hasCustomQr = !m_guns[gun].qrPayload.empty();
        }

        m_guns[gun].configured = true;
        rebuildQrPayload(gun);
    }

    // BY ZF: 保障在仅更新运营商/MAC时，未配置自定义二维码的枪仍可自动刷新。
    for (int i = 0; i < 2; ++i) {
        rebuildQrPayload(i);
    }

    cJSON_Delete(root);
    // BY ZF: 配置变化后主动刷新（跨线程使用队列调用）。
    QMetaObject::invokeMethod(this, "onRefreshUi", Qt::QueuedConnection);
}

void HmiWindow::rebuildQrPayload(int gun)
{
    if (gun < 0 || gun > 1) {
        return;
    }
    if (m_guns[gun].hasCustomQr) {
        return;
    }

    const std::string gunId = m_guns[gun].gunId.empty() ? std::to_string(gun) : m_guns[gun].gunId;
    const std::string operatorId = m_operatorId.empty() ? std::string("0") : m_operatorId;
    const std::string macAddr = m_macAddr.empty() ? std::string("000000000000000000000000") : m_macAddr;

    // BY ZF: PREPARE 阶段二维码规则：hlht://[枪ID].[运营商ID]/[MAC地址]
    m_guns[gun].qrPayload = std::string("hlht://") + gunId + "." + operatorId + "/" + macAddr;
}

bool HmiWindow::parseTopicGun(const std::string& topic,
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
    painter.drawText(rect.adjusted(16, 44, -16, -12), Qt::AlignLeft | Qt::TextWordWrap, stateMessage(data.state));

    if (data.state == "PREPARE") {
        const QRect qrRect(rect.left() + 16, rect.top() + 90, rect.width() - 32, rect.height() - 150);
        drawQrPlaceholder(painter, qrRect, data.qrPayload);
    } else if (data.state == "CHARGING") {
        const double chargedMinutes = data.chargedTime / 60.0;
        // BY ZF: 直接用 Qt 字符串拼接中文，避免 std::string 转码导致乱码。
        const QString detail = QString::fromUtf8(
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
        painter.drawText(rect.adjusted(16, 90, -16, -16), Qt::AlignLeft | Qt::AlignTop, detail);
    } else if (data.state == "STOPPED") {
        const QString detail = QString::fromUtf8(
                                   "充电电量: %1 kWh\n"
                                   "充电金额: %2 元")
                                   .arg(QString::number(data.totalEnergy, 'f', 3))
                                   .arg(QString::number(data.totalAmount, 'f', 2));
        painter.drawText(rect.adjusted(16, 120, -16, -16), Qt::AlignLeft | Qt::AlignTop, detail);
    }

    painter.setFont(smallFont);
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    painter.drawText(rect.adjusted(16, 0, -16, -10), Qt::AlignLeft | Qt::AlignBottom, ts);

    painter.restore();
}

void HmiWindow::drawQrPlaceholder(QPainter& painter, const QRect& rect, const std::string& payload)
{
    painter.save();

    const int side = std::min(rect.width(), rect.height() - 32);
    const QRect codeRect(rect.left() + (rect.width() - side) / 2, rect.top(), side, side);

    painter.fillRect(codeRect, Qt::white);
    painter.setPen(Qt::NoPen);

    // BY ZF: 使用 qrencode 生成真实二维码矩阵并绘制。
    QRcode* qr = QRcode_encodeString8bit(payload.c_str(), 0, QR_ECLEVEL_L);
    if (qr && qr->width > 0 && qr->data) {
        const int modules = qr->width;
        const int cell = std::max(1, side / modules);
        const int drawSide = cell * modules;
        const int ox = codeRect.left() + (codeRect.width() - drawSide) / 2;
        const int oy = codeRect.top() + (codeRect.height() - drawSide) / 2;

        const unsigned char* p = qr->data;
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x, ++p) {
                if ((*p & 0x1U) != 0U) {
                    painter.fillRect(ox + x * cell, oy + y * cell, cell, cell, Qt::black);
                }
            }
        }
    }

    if (qr) {
        QRcode_free(qr);
    }

    painter.setPen(QColor(20, 35, 55));
    painter.setFont(QFont("WenQuanYi Zen Hei", 10, QFont::Normal));
    painter.drawText(QRect(rect.left(), codeRect.bottom() + 6, rect.width(), 24), Qt::AlignCenter,
                     QString::fromStdString(payload));

    painter.restore();
}
