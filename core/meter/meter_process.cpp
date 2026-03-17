/**
 * Meter process implementation
 * BY ZF
 */

#include "meter_process.h"
#include "../base/cjson/include/cjson/cJSON.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <cctype>
#include <map>
#include <thread>

namespace {
    // BY ZF: 当前毫秒时间戳
    uint64_t nowMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    // BY ZF: 去除字符串首尾空白
    std::string trimCopy(const std::string& in)
    {
        if (in.empty()) {
            return in;
        }
        size_t b = 0;
        while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b]))) {
            ++b;
        }
        if (b == in.size()) {
            return "";
        }
        size_t e = in.size() - 1;
        while (e > b && std::isspace(static_cast<unsigned char>(in[e]))) {
            --e;
        }
        return in.substr(b, e - b + 1);
    }
}

// BY ZF: 构造函数
MeterProcess::MeterProcess()
    : BaseProcess(PROC_METER, "tcu_meter")
    , m_seq(0)
{
}

// BY ZF: 析构函数
MeterProcess::~MeterProcess()
{
}

// BY ZF: 进程初始化入口
bool MeterProcess::doInitialize()
{
    // BY ZF: 初始化顺序固定，避免使用未就绪资源
    if (!loadConfig()) {
        std::cerr << "[Meter] loadConfig failed" << std::endl;
        return false;
    }
    if (!initMqtt()) {
        std::cerr << "[Meter] initMqtt failed" << std::endl;
        return false;
    }
    if (!initMeter()) {
        std::cerr << "[Meter] initMeter failed" << std::endl;
        return false;
    }
    m_lastPoll = std::chrono::steady_clock::now();
    return true;
}

// BY ZF: 主循环周期调度
void MeterProcess::doRun()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - m_lastPoll >= std::chrono::milliseconds(m_cfg.pollIntervalMs)) {
        pollOnce();
        m_lastPoll = now;
        feedWatchdog();
    }
    // BY ZF: doRun 内自行节流，避免空转占用 CPU
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// BY ZF: 进程退出清理
void MeterProcess::doCleanup()
{
    for (size_t i = 0; i < m_ports.size(); i++) {
        m_ports[i].meter.close();
    }
    m_ports.clear();
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

// BY ZF: 加载并校验配置
bool MeterProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    const std::string section("Meter");

    // BY ZF: 枪数量至少为 1
    m_cfg.gunCount = static_cast<uint8_t>(cfg.getInt(section, "gun_count", 1));
    if (m_cfg.gunCount == 0) {
        m_cfg.gunCount = 1;
    }

    // BY ZF: 全局默认串口参数（设备号不再使用全局配置，必须按枪配置）
    m_cfg.serialDefault.device = "";
    m_cfg.serialDefault.baudrate = static_cast<uint32_t>(cfg.getInt(section, "serial_baudrate", 9600));
    m_cfg.serialDefault.dataBits = static_cast<uint8_t>(cfg.getInt(section, "serial_data_bits", 8));
    m_cfg.serialDefault.stopBits = static_cast<uint8_t>(cfg.getInt(section, "serial_stop_bits", 1));
    {
        const std::string parity = cfg.getString(section, "serial_parity", "E");
        m_cfg.serialDefault.parity = parity.empty() ? 'E' : parity[0];
    }

    m_cfg.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_cfg.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_cfg.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_cfg.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_meter");
    m_cfg.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");

    // BY ZF: 运行参数（轮询周期、单次读超时、失败重试次数）
    m_cfg.pollIntervalMs = cfg.getInt(section, "poll_interval_ms", 500);
    m_cfg.readTimeoutMs = cfg.getInt(section, "read_timeout_ms", 200);
    m_cfg.retryCount = cfg.getInt(section, "retry_count", 2);
    m_cfg.debug645 = cfg.getInt(section, "debug_645", 0) != 0;
    if (m_cfg.pollIntervalMs < 100) {
        m_cfg.pollIntervalMs = 100;
    }
    if (m_cfg.readTimeoutMs < 50) {
        m_cfg.readTimeoutMs = 50;
    }
    if (m_cfg.retryCount < 0) {
        m_cfg.retryCount = 0;
    }
    m_cfg.serialDefault.readTimeoutMs = m_cfg.readTimeoutMs;

    // BY ZF: 每枪配置（地址必填；串口参数可覆盖全局默认）
    m_cfg.guns.clear();
    m_cfg.guns.resize(m_cfg.gunCount);
    for (uint8_t i = 0; i < m_cfg.gunCount; i++) {
        std::ostringstream pfx;
        pfx << "gun" << static_cast<int>(i + 1) << "_";
        const std::string base = pfx.str();

        const std::string keyAddr = base + "meter_addr";
        m_cfg.guns[i].meterAddr = trimCopy(cfg.getString(section, keyAddr, ""));
        if (m_cfg.guns[i].meterAddr.empty()) {
            std::cerr << "[Meter] Missing " << keyAddr << std::endl;
            return false;
        }

        m_cfg.guns[i].serial = m_cfg.serialDefault;

        const std::string vDev = trimCopy(cfg.getString(section, base + "serial_device", ""));
        if (vDev.empty()) {
            std::cerr << "[Meter] Missing " << base << "serial_device" << std::endl;
            return false;
        }
        m_cfg.guns[i].serial.device = vDev;
        const int vBaud = cfg.getInt(section, base + "serial_baudrate", -1);
        if (vBaud > 0) {
            m_cfg.guns[i].serial.baudrate = static_cast<uint32_t>(vBaud);
        }
        const int vDb = cfg.getInt(section, base + "serial_data_bits", -1);
        if (vDb > 0) {
            m_cfg.guns[i].serial.dataBits = static_cast<uint8_t>(vDb);
        }
        const int vSb = cfg.getInt(section, base + "serial_stop_bits", -1);
        if (vSb > 0) {
            m_cfg.guns[i].serial.stopBits = static_cast<uint8_t>(vSb);
        }
        const std::string vParity = trimCopy(cfg.getString(section, base + "serial_parity", ""));
        if (!vParity.empty()) {
            m_cfg.guns[i].serial.parity = vParity[0];
        }
        const int vRt = cfg.getInt(section, base + "read_timeout_ms", -1);
        if (vRt > 0) {
            m_cfg.guns[i].serial.readTimeoutMs = vRt;
        }
    }

    // BY ZF: 允许多枪复用同一串口，实际在 initMeter() 中按串口参数归并为共享端口实例。

    m_gunState.clear();
    m_gunState.resize(m_cfg.gunCount);

    std::cout << "[Meter] Loaded config, gun_count=" << static_cast<int>(m_cfg.gunCount) << std::endl;
    return true;
}

// BY ZF: 初始化 MQTT 客户端
bool MeterProcess::initMqtt()
{
    if (!m_mqtt.init(m_cfg.mqttClientId, true)) {
        return false;
    }
    if (!m_mqtt.connect(m_cfg.mqttHost, m_cfg.mqttPort, m_cfg.mqttKeepalive)) {
        return false;
    }
    return m_mqtt.loopStart();
}

// BY ZF: 初始化每枪绑定的串口/电表实例
bool MeterProcess::initMeter()
{
    m_ports.clear();
    // BY ZF: 先按枪数预留，避免扩容搬移已打开串口对象导致 fd 失效
    m_ports.reserve(m_cfg.gunCount);
    std::map<std::string, int> portMap;

    for (uint8_t gun = 0; gun < m_cfg.gunCount; gun++) {
        const MeterSerialConfig& sc = m_cfg.guns[gun].serial;
        const std::string key = buildPortKey(sc);

        std::map<std::string, int>::const_iterator it = portMap.find(key);
        if (it == portMap.end()) {
            // BY ZF: 不能先在局部对象 open 再 push_back，避免复制后 fd 失效
            m_ports.emplace_back();
            PortRuntime& pr = m_ports.back();
            pr.serial = sc;
            if (!pr.meter.open(pr.serial)) {
                m_ports.pop_back();
                std::cerr << "[Meter] Open serial failed for gun"
                          << static_cast<int>(gun)
                          << " device=" << pr.serial.device << std::endl;
                return false;
            }
            pr.meter.setDebug(m_cfg.debug645);
            const int idx = static_cast<int>(m_ports.size()) - 1;
            portMap[key] = idx;
            m_gunState[gun].portIndex = idx;
            std::cout << "[Meter] Gun" << static_cast<int>(gun)
                      << " bind meter=" << m_cfg.guns[gun].meterAddr
                      << " serial=" << pr.serial.device
                      << " portIndex=" << idx << std::endl;
        } else {
            m_gunState[gun].portIndex = it->second;
            std::cout << "[Meter] Gun" << static_cast<int>(gun)
                      << " share serial=" << m_cfg.guns[gun].serial.device
                      << " portIndex=" << it->second << std::endl;
        }
    }
    if (m_cfg.debug645) {
        std::cout << "[Meter] debug_645 enabled" << std::endl;
    }
    return true;
}

// BY ZF: 执行一轮全枪轮询
void MeterProcess::pollOnce()
{
    // BY ZF: 每枪一轮读取，失败时按 retry_count 重试
    for (uint8_t gun = 0; gun < m_cfg.gunCount && m_running.load(); gun++) {
        MeterReading reading;
        std::string err;
        bool ok = false;
        for (int i = 0; i <= m_cfg.retryCount && m_running.load(); i++) {
            if (readGunMeter(gun, reading, err)) {
                ok = true;
                break;
            }
        }

        GunRuntime& rt = m_gunState[gun];
        if (ok) {
            rt.lastReading = reading;
            rt.lastTs = nowMs();
            rt.inited = true;
            publishData(gun, reading);
            // BY ZF: 离线恢复时仅上报一次 online 事件
            if (!rt.online) {
                publishEvent(gun, "meter_online", "read_ok");
                rt.online = true;
            }
        } else {
            // BY ZF: 首次失败或在线转离线时上报 offline 事件
            if (rt.online || !rt.inited) {
                publishEvent(gun, "meter_offline", err.empty() ? "read_failed" : err);
                rt.online = false;
                rt.inited = true;
            }
        }
    }
}

// BY ZF: 读取指定枪的电表数据
bool MeterProcess::readGunMeter(uint8_t gun, MeterReading& out, std::string& err)
{
    if (!m_running.load()) {
        err = "stopping";
        return false;
    }
    if (gun >= m_cfg.guns.size()) {
        err = "invalid_gun";
        return false;
    }
    const int portIdx = m_gunState[gun].portIndex;
    if (portIdx < 0 || portIdx >= static_cast<int>(m_ports.size())) {
        err = "invalid_port_binding";
        return false;
    }
    if (!m_ports[portIdx].meter.isOpen()) {
        err = "serial_not_open";
        return false;
    }
    if (m_cfg.debug645) {
        std::cout << "[Meter][READ_BEGIN] gun=" << static_cast<int>(gun)
                  << " portIndex=" << portIdx
                  << " serial=" << m_ports[portIdx].serial.device
                  << " addr=" << m_cfg.guns[gun].meterAddr << std::endl;
    }

    const bool ok = m_ports[portIdx].meter.readAll(m_cfg.guns[gun].meterAddr, out, err);
    if (m_cfg.debug645) {
        if (ok) {
            std::cout << "[Meter][READ_OK] gun=" << static_cast<int>(gun)
                      << " portIndex=" << portIdx
                      << " serial=" << m_ports[portIdx].serial.device
                      << " energy=" << out.totalEnergy
                      << " reverseEnergy=" << out.reverseEnergy
                      << " voltage=" << out.voltage
                      << " current=" << out.current << std::endl;
        } else {
            std::cout << "[Meter][READ_FAIL] gun=" << static_cast<int>(gun)
                      << " portIndex=" << portIdx
                      << " serial=" << m_ports[portIdx].serial.device
                      << " reason=" << err << std::endl;
        }
    }
    return ok;
}

// BY ZF: 发布电表数据到 data topic
void MeterProcess::publishData(uint8_t gun, const MeterReading& value)
{
    // BY ZF: data topic 只携带计量值，不承载链路状态
    std::ostringstream topic;
    topic << m_cfg.mqttTopicPrefix << "/meter/" << static_cast<int>(gun) << "/data";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_meter");
    cJSON_AddNumberToObject(root, "gun", static_cast<double>(gun));

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "totalEnergy", value.totalEnergy);
    // BY ZF: 增加反向有功总电能，供 V2G 场景使用
    cJSON_AddNumberToObject(data, "ReverseEnergy", value.reverseEnergy);
    cJSON_AddNumberToObject(data, "voltage", value.voltage);
    cJSON_AddNumberToObject(data, "current", value.current);
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(topic.str(), payload, 0, false);
}

// BY ZF: 发布电表状态事件到 event topic
void MeterProcess::publishEvent(uint8_t gun, const std::string& event, const std::string& reason)
{
    // BY ZF: event topic 表达链路状态（online/offline）与原因
    std::ostringstream topic;
    topic << m_cfg.mqttTopicPrefix << "/meter/" << static_cast<int>(gun) << "/event";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_meter");
    cJSON_AddNumberToObject(root, "gun", static_cast<double>(gun));
    cJSON_AddStringToObject(root, "event", event.c_str());

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "reason", reason.c_str());
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(topic.str(), payload, 1, true);
}

// BY ZF: 生成串口参数键，用于端口绑定与索引
std::string MeterProcess::buildPortKey(const MeterSerialConfig& sc)
{
    std::ostringstream oss;
    oss << sc.device << "|"
        << sc.baudrate << "|"
        << static_cast<int>(sc.dataBits) << "|"
        << static_cast<int>(sc.stopBits) << "|"
        << sc.parity << "|"
        << sc.readTimeoutMs;
    return oss.str();
}
