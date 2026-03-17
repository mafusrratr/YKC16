/**
 * Guangxi DKY MODBUS RTU comm process implementation
 * BY ZF
 */

#include "comm_process.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace {
static std::string trim(const std::string& in)
{
    size_t b = 0;
    while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b]))) {
        ++b;
    }
    size_t e = in.size();
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) {
        --e;
    }
    return in.substr(b, e - b);
}

static std::string hexDump(const uint8_t* data, size_t len)
{
    if (!data || len == 0) {
        return "";
    }
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        if (i + 1 < len) {
            oss << ' ';
        }
    }
    return oss.str();
}

static bool getNumber(cJSON* obj, const char* key, double& out)
{
    if (!obj || !key) {
        return false;
    }
    cJSON* n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        out = n->valuedouble;
        return true;
    }
    return false;
}

static bool getInt(cJSON* obj, const char* key, int& out)
{
    if (!obj || !key) {
        return false;
    }
    cJSON* n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        out = n->valueint;
        return true;
    }
    return false;
}
}

CommProcess::CommProcess()
    : BaseProcess(PROC_COMMUNICATION, "tcu_comm")
    , m_seq(0)
{
}

CommProcess::~CommProcess()
{
}

bool CommProcess::doInitialize()
{
    if (!loadConfig()) {
        std::cerr << "[GuangxiDKY] loadConfig failed" << std::endl;
        return false;
    }
    if (!initMqtt()) {
        std::cerr << "[GuangxiDKY] initMqtt failed" << std::endl;
        return false;
    }
    if (!initSerial()) {
        std::cerr << "[GuangxiDKY] initSerial failed" << std::endl;
        return false;
    }
    m_lastPoll = std::chrono::steady_clock::now();
    return true;
}

void CommProcess::doRun()
{
    pollSerial();
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // BY ZF: 喂狗节奏固定为15秒一次，避免过于频繁。
    if (now - m_lastPoll >= std::chrono::seconds(15)) {
        feedWatchdog();
        m_lastPoll = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void CommProcess::doCleanup()
{
    m_serial.close();
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

bool CommProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    const std::string section("Comm");

    m_cfg.gunCount = static_cast<uint8_t>(cfg.getInt(section, "gun_count", 1));
    if (m_cfg.gunCount == 0) {
        m_cfg.gunCount = 1;
    }

    m_cfg.exportGun = static_cast<uint8_t>(cfg.getInt(section, "modbus_export_gun", 0));
    if (m_cfg.exportGun >= m_cfg.gunCount) {
        m_cfg.exportGun = 0;
    }
    // BY ZF: 充电桩通讯地址统一使用 cdz_no（范围 1~16）。
    {
        int addr = cfg.getInt(section, "cdz_no", 1);
        if (addr < 1 || addr > 16) {
            addr = 1;
        }
        m_cfg.modbusAddr = static_cast<uint8_t>(addr);
    }
    // BY ZF: 调试开关统一使用 debug。
    m_cfg.debugModbus = cfg.getInt(section, "debug", 0) != 0;

    m_cfg.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_cfg.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_cfg.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_cfg.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_comm");
    m_cfg.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_cfg.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_cfg.mqttPassword = cfg.getString(section, "mqtt_password", "");

    // BY ZF: MODBUS 项目简化：串口/地址/寄存器映射统一使用结构体默认值，不从配置文件读取。
    m_cfg.serialDevice = trim(m_cfg.serialDevice);
    if (m_cfg.readRegCount == 0) {
        m_cfg.readRegCount = 13;
    }

    m_gunData.clear();
    m_gunData.resize(m_cfg.gunCount);
    for (uint8_t i = 0; i < m_cfg.gunCount; ++i) {
        m_gunData[i].chargerNo = m_cfg.modbusAddr;
        m_gunData[i].gunNo = static_cast<uint16_t>(i + 1);
        m_gunData[i].gunType = m_cfg.gunType;
    }

    std::cout << "[GuangxiDKY] config loaded, gunCount=" << static_cast<int>(m_cfg.gunCount)
              << " exportGun=" << static_cast<int>(m_cfg.exportGun)
              << " modbusAddr=" << static_cast<int>(m_cfg.modbusAddr) << std::endl;
    return true;
}

bool CommProcess::initMqtt()
{
    if (!m_mqtt.init(m_cfg.mqttClientId, true)) {
        return false;
    }
    if (!m_cfg.mqttUsername.empty()) {
        if (!m_mqtt.setUsernamePassword(m_cfg.mqttUsername, m_cfg.mqttPassword)) {
            return false;
        }
    }

    m_mqtt.setConnectHandler([this](int rc) { onMqttConnected(rc); });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_cfg.mqttHost, m_cfg.mqttPort, m_cfg.mqttKeepalive)) {
        return false;
    }
    return m_mqtt.loopStart();
}

bool CommProcess::initSerial()
{
    SerialParams params;
    params.baudrate = m_cfg.serialBaudrate;
    params.dataBits = m_cfg.serialDataBits;
    params.stopBits = m_cfg.serialStopBits;
    params.parity = m_cfg.serialParity;
    params.flowControl = false;

    if (!m_serial.open(m_cfg.serialDevice, params)) {
        return false;
    }
    m_rxBuf.clear();
    m_rxBuf.reserve(512);
    return true;
}

void CommProcess::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[GuangxiDKY] mqtt connect rc=" << rc << std::endl;
        return;
    }
    const std::string topic = m_cfg.mqttTopicPrefix + "/+/+/+";
    if (!m_mqtt.subscribe(topic, 1)) {
        std::cerr << "[GuangxiDKY] mqtt subscribe failed: " << topic << std::endl;
        return;
    }
    std::cout << "[GuangxiDKY] mqtt subscribed: " << topic << std::endl;
}

bool CommProcess::parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const
{
    module.clear();
    leaf.clear();

    const std::string prefix = m_cfg.mqttTopicPrefix + "/";
    if (topic.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string rest = topic.substr(prefix.size());

    const size_t p1 = rest.find('/');
    if (p1 == std::string::npos) {
        return false;
    }
    const size_t p2 = rest.find('/', p1 + 1);
    if (p2 == std::string::npos) {
        return false;
    }

    module = rest.substr(0, p1);
    const std::string gunStr = rest.substr(p1 + 1, p2 - p1 - 1);
    leaf = rest.substr(p2 + 1);

    int g = std::atoi(gunStr.c_str());
    if (g < 0 || g > 255) {
        return false;
    }
    gun = static_cast<uint8_t>(g);
    return true;
}

void CommProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    if (m_cfg.debugModbus) {
        std::cout << "[GuangxiDKY][MQTT_RX] topic=" << topic << std::endl;
    }

    std::string module;
    std::string leaf;
    uint8_t gun = 0;
    if (!parseTopic(topic, module, gun, leaf)) {
        return;
    }
    if (gun >= m_gunData.size()) {
        return;
    }

    if (module == "logic") {
        if (leaf == "event") {
            handleLogicEvent(gun, payload);
        } else if (leaf == "feeData") {
            handleLogicFeeData(gun, payload);
        }
        return;
    }
    if (module == "pile") {
        if (leaf == "data") {
            handlePileData(gun, payload);
        } else if (leaf == "event") {
            handlePileEvent(gun, payload);
        }
        return;
    }
    if (module == "meter" && leaf == "data") {
        handleMeterData(gun, payload);
    }
}

void CommProcess::handleLogicEvent(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* event = cJSON_GetObjectItem(root, "event");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (event && cJSON_IsString(event) && data && cJSON_IsObject(data)) {
        if (std::strcmp(event->valuestring, "state_change") == 0) {
            cJSON* to = cJSON_GetObjectItem(data, "to");
            if (to && cJSON_IsString(to) && to->valuestring) {
                if (std::strcmp(to->valuestring, "IDLE") == 0) {
                    m_gunData[gun].gunStatus = 0;
                    m_gunData[gun].connectStatus = 2;
                    m_gunData[gun].chargeTimeSec = 0;
                    m_gunData[gun].chargedTimeBaseRaw = 0;
                    m_gunData[gun].chargedTimeBaseInited = false;
                } else if (std::strcmp(to->valuestring, "PREPARE") == 0 ||
                           std::strcmp(to->valuestring, "STARTING") == 0) {
                    m_gunData[gun].gunStatus = 1;
                    m_gunData[gun].connectStatus = 1;
                } else if (std::strcmp(to->valuestring, "CHARGING") == 0) {
                    m_gunData[gun].gunStatus = 2;
                    m_gunData[gun].connectStatus = 1;
                } else if (std::strcmp(to->valuestring, "STOPPING") == 0 ||
                           std::strcmp(to->valuestring, "STOPPED") == 0) {
                    m_gunData[gun].gunStatus = 3;
                    m_gunData[gun].connectStatus = 2;
                } else if (std::strcmp(to->valuestring, "ERROR") == 0) {
                    m_gunData[gun].gunStatus = 4;
                }
            }
        }
    }
    cJSON_Delete(root);
}

void CommProcess::handleLogicFeeData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data)) {
        double totalEnergy = 0.0;
        if (getNumber(data, "totalEnergy", totalEnergy)) {
            m_gunData[gun].chargeEnergyDeciKwh = clampU16(totalEnergy, 10.0);
        }
        double totalAmount = 0.0;
        if (getNumber(data, "totalAmount", totalAmount)) {
            m_gunData[gun].amountDeciYuan = clampU16(totalAmount, 10.0);
        }
        double chargedTime = 0.0;
        if (getNumber(data, "chargedTime", chargedTime)) {
            const uint32_t rawSec = (chargedTime <= 0.0) ? 0U : static_cast<uint32_t>(chargedTime + 0.5);
            if (!m_gunData[gun].chargedTimeBaseInited || rawSec < m_gunData[gun].chargedTimeBaseRaw) {
                m_gunData[gun].chargedTimeBaseRaw = rawSec;
                m_gunData[gun].chargedTimeBaseInited = true;
            }
            const uint32_t elapsedSec = rawSec - m_gunData[gun].chargedTimeBaseRaw;
            m_gunData[gun].chargeTimeSec = static_cast<uint16_t>(
                std::min<uint32_t>(elapsedSec, 65535U));
            if (m_cfg.debugModbus) {
                std::cout << "[GuangxiDKY][FEE] gun=" << static_cast<int>(gun)
                          << " chargedTimeRaw=" << chargedTime
                          << " base=" << m_gunData[gun].chargedTimeBaseRaw
                          << " elapsed=" << elapsedSec
                          << " reg=" << m_gunData[gun].chargeTimeSec << std::endl;
            }
        }
    }
    cJSON_Delete(root);
}

void CommProcess::handlePileData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data)) {
        int connect = 0;
        if (getInt(data, "yxVehicleConnectStatus", connect) || getInt(data, "vehicleConnectStatus", connect)) {
            m_gunData[gun].connectStatus = (connect == 1) ? 1 : 2;
        }
        int soc = 0;
        if (getInt(data, "soc", soc)) {
            if (soc < 0) {
                soc = 0;
            }
            if (soc > 100) {
                soc = 100;
            }
            m_gunData[gun].soc = static_cast<uint16_t>(soc);
        }

        // BY ZF: pile/yc 支持有符号电流（放电为负值）。
        if (type && cJSON_IsString(type) && type->valuestring &&
                std::strcmp(type->valuestring, "yc") == 0) {
            double v = 0.0;
            if (getNumber(data, "outputVoltage", v)) {
                m_gunData[gun].voltageDeciV = clampU16(v, 10.0);
            }

            double c = 0.0;
            if (getNumber(data, "outputCurrent", c)) {
                m_gunData[gun].currentDeciA = clampS16(c, 10.0);
            } else if (getNumber(data, "bmsMeasuredCurrent", c)) {
                m_gunData[gun].currentDeciA = clampS16(c, 10.0);
            } else if (getNumber(data, "bmsReqCurrent", c)) {
                m_gunData[gun].currentDeciA = clampS16(c, 10.0);
            }

            const double powerKw = (static_cast<double>(m_gunData[gun].voltageDeciV) / 10.0) *
                                   (static_cast<double>(m_gunData[gun].currentDeciA) / 10.0) / 1000.0;
            m_gunData[gun].powerDeciKw = clampU16(std::fabs(powerKw), 10.0);
        }
    }
    cJSON_Delete(root);
}

void CommProcess::handlePileEvent(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data)) {
        int connect = 0;
        if (getInt(data, "vehicleConnectStatus", connect) || getInt(data, "yxVehicleConnectStatus", connect)) {
            m_gunData[gun].connectStatus = (connect == 1) ? 1 : 2;
        }
    }
    cJSON_Delete(root);
}

void CommProcess::handleMeterData(uint8_t gun, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data)) {
        double energy = 0.0;
        if (getNumber(data, "totalEnergy", energy) || getNumber(data, "meterEnergy", energy)) {
            m_gunData[gun].meterDeciKwh = clampU16(energy, 10.0);
            if (m_cfg.debugModbus) {
                std::cout << "[GuangxiDKY][METER] gun=" << static_cast<int>(gun)
                          << " totalEnergy=" << energy
                          << " meterDeciKwh=" << m_gunData[gun].meterDeciKwh << std::endl;
            }
        }
        // BY ZF: 电压/电流/功率统一使用 pile/yc 遥测，不再由 meter 覆盖。
    }
    cJSON_Delete(root);
}

void CommProcess::pollSerial()
{
    uint8_t buf[256];
    const int n = m_serial.receive(buf, sizeof(buf));
    if (n > 0) {
        m_rxBuf.insert(m_rxBuf.end(), buf, buf + n);
        if (m_cfg.debugModbus) {
            std::cout << "[GuangxiDKY][RX_RAW] bytes=" << n
                      << " hex=" << hexDump(buf, static_cast<size_t>(n)) << std::endl;
        }
    }

    std::vector<uint8_t> req;
    while (tryExtractRequest(req)) {
        if (m_cfg.debugModbus) {
            std::cout << "[GuangxiDKY][RX_FRAME] len=" << req.size()
                      << " hex=" << hexDump(req.data(), req.size()) << std::endl;
        }
        processRequest(req);
    }
}

bool CommProcess::tryExtractRequest(std::vector<uint8_t>& frame)
{
    while (m_rxBuf.size() >= 8) {
        const uint8_t func = m_rxBuf[1];
        if (func != 0x03 && func != 0x06) {
            m_rxBuf.erase(m_rxBuf.begin());
            continue;
        }

        const uint16_t gotCrc = static_cast<uint16_t>(m_rxBuf[6]) |
                                (static_cast<uint16_t>(m_rxBuf[7]) << 8);
        const uint16_t calc = crc16Modbus(m_rxBuf.data(), 6);
        if (gotCrc != calc) {
            m_rxBuf.erase(m_rxBuf.begin());
            continue;
        }

        frame.assign(m_rxBuf.begin(), m_rxBuf.begin() + 8);
        m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + 8);
        return true;
    }
    return false;
}

void CommProcess::processRequest(const std::vector<uint8_t>& frame)
{
    if (frame.size() != 8) {
        return;
    }
    const uint8_t addr = frame[0];
    const uint8_t func = frame[1];
    // BY ZF: 仅响应与本机配置地址一致的请求，不处理广播地址。
    if (addr != m_cfg.modbusAddr) {
        return;
    }

    std::vector<uint8_t> resp;
    if (func == 0x03) {
        const uint16_t startReg = readU16BE(&frame[2]);
        const uint16_t count = readU16BE(&frame[4]);
        if (startReg == m_cfg.readStartReg && count == m_cfg.readRegCount) {
            resp = buildReadResponse(m_cfg.modbusAddr, startReg, count);
        } else {
            resp = buildExceptionResponse(m_cfg.modbusAddr, func, 0x02);
        }
    } else if (func == 0x06) {
        const uint16_t reg = readU16BE(&frame[2]);
        const uint16_t value = readU16BE(&frame[4]);
        if (reg == m_cfg.powerCtrlReg) {
            if (publishPowerLimitCommand(value)) {
                resp = buildWriteEchoResponse(m_cfg.modbusAddr, reg, value);
            } else {
                // BY ZF: 数据值非法或命令发布失败。
                resp = buildExceptionResponse(m_cfg.modbusAddr, func, 0x03);
            }
        } else if (reg == m_cfg.startStopReg) {
            if (publishStartStopCommand(value)) {
                resp = buildWriteEchoResponse(m_cfg.modbusAddr, reg, value);
            } else {
                // BY ZF: 数据值非法或命令发布失败。
                resp = buildExceptionResponse(m_cfg.modbusAddr, func, 0x03);
            }
        } else {
            resp = buildExceptionResponse(m_cfg.modbusAddr, func, 0x02);
        }
    } else {
        resp = buildExceptionResponse(m_cfg.modbusAddr, func, 0x01);
    }

    if (!resp.empty()) {
        if (m_cfg.debugModbus) {
            std::cout << "[GuangxiDKY][TX] bytes=" << resp.size()
                      << " hex=" << hexDump(resp.data(), resp.size()) << std::endl;
        }
        (void)m_serial.send(resp.data(), resp.size());
    }
}

std::vector<uint8_t> CommProcess::buildReadResponse(uint8_t addr, uint16_t startReg, uint16_t regCount) const
{
    std::vector<uint8_t> out;
    if (regCount > 0x7D) {
        return out;
    }

    out.reserve(static_cast<size_t>(5 + regCount * 2 + 2));
    out.push_back(addr);
    out.push_back(0x03);
    out.push_back(static_cast<uint8_t>(regCount * 2));
    for (uint16_t i = 0; i < regCount; ++i) {
        appendU16BE(out, getRegisterValue(static_cast<uint16_t>(startReg + i)));
    }
    const uint16_t crc = crc16Modbus(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return out;
}

std::vector<uint8_t> CommProcess::buildExceptionResponse(uint8_t addr, uint8_t func, uint8_t exCode) const
{
    std::vector<uint8_t> out;
    out.reserve(5);
    out.push_back(addr);
    out.push_back(static_cast<uint8_t>(func | 0x80));
    out.push_back(exCode);
    const uint16_t crc = crc16Modbus(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return out;
}

std::vector<uint8_t> CommProcess::buildWriteEchoResponse(uint8_t addr, uint16_t reg, uint16_t value) const
{
    std::vector<uint8_t> out;
    out.reserve(8);
    out.push_back(addr);
    out.push_back(0x06);
    appendU16BE(out, reg);
    appendU16BE(out, value);
    const uint16_t crc = crc16Modbus(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return out;
}

uint16_t CommProcess::getRegisterValue(uint16_t reg) const
{
    const uint8_t gun = selectedGun();
    const GunRuntimeData& rd = m_gunData[gun];

    switch (reg) {
    case 0x0000: return rd.chargerNo;
    case 0x0001: return rd.gunNo;
    case 0x0002: return rd.gunStatus;
    case 0x0003: return rd.gunType;
    case 0x0004: return rd.soc;
    case 0x0005: return rd.connectStatus;
    case 0x0006: return rd.voltageDeciV;
    case 0x0007: return static_cast<uint16_t>(rd.currentDeciA);
    case 0x0008: return rd.powerDeciKw;
    case 0x0009: return rd.chargeTimeSec;
    case 0x000A: return rd.chargeEnergyDeciKwh;
    case 0x000B: return rd.amountDeciYuan;
    case 0x000C: return rd.meterDeciKwh;
    default: return 0;
    }
}

uint8_t CommProcess::selectedGun() const
{
    if (m_gunData.empty()) {
        return 0;
    }
    return static_cast<uint8_t>(std::min<size_t>(m_cfg.exportGun, m_gunData.size() - 1));
}

bool CommProcess::publishPowerLimitCommand(uint16_t deciKw)
{
    const uint8_t gun = selectedGun();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", "power_ctrl");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "maxChargePowerKw", static_cast<double>(deciKw) / 10.0);
    cJSON_AddNumberToObject(data, "powerLimitRaw", deciKw);
    cJSON_AddItemToObject(root, "data", data);

    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        std::ostringstream topic;
        topic << m_cfg.mqttTopicPrefix << "/plat/" << static_cast<int>(gun) << "/cmd";
        ok = m_mqtt.publish(topic.str(), out, 1, false);
        free(out);
    }
    cJSON_Delete(root);
    return ok;
}

bool CommProcess::publishStartStopCommand(uint16_t value)
{
    const uint8_t mode = static_cast<uint8_t>((value >> 8) & 0xFF);   // BY ZF: byte0 充放电标识
    const uint8_t action = static_cast<uint8_t>(value & 0xFF);        // BY ZF: byte1 启停标识
    if (m_cfg.debugModbus) {
        std::cout << "[GuangxiDKY][CTRL] reg=0x" << std::hex << m_cfg.startStopReg
                  << " value=0x" << value
                  << " mode=0x" << static_cast<int>(mode)
                  << " action=0x" << static_cast<int>(action)
                  << std::dec << std::endl;
    }
    if (!((mode == 0x01) || (mode == 0x02))) {
        return false;
    }
    if (!((action == 0x55) || (action == 0xAA))) {
        return false;
    }

    const uint8_t gun = selectedGun();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", (action == 0xAA) ? "start_charge" : "stop_charge");

    cJSON* data = cJSON_CreateObject();
    
    if (action == 0xAA) {
        // BY ZF: mode=0x02 表示放电，透传 v2g=1 给 tcu_logic/pile_controller。
        cJSON_AddNumberToObject(data, "v2g", (mode == 0x02) ? 1 : 0);
    } else {
        cJSON_AddNumberToObject(data, "stopReason", 1);
    }
    cJSON_AddItemToObject(root, "data", data);

    char* out = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (out) {
        std::ostringstream topic;
        topic << m_cfg.mqttTopicPrefix << "/plat/" << static_cast<int>(gun) << "/cmd";
        ok = m_mqtt.publish(topic.str(), out, 1, false);
        free(out);
    }
    cJSON_Delete(root);
    return ok;
}

uint16_t CommProcess::crc16Modbus(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x0001U) != 0U) {
                crc >>= 1;
                crc ^= 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint16_t CommProcess::readU16BE(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

void CommProcess::appendU16BE(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint16_t CommProcess::clampU16(double v, double scale)
{
    if (v <= 0.0 || scale <= 0.0) {
        return 0;
    }
    const double t = std::floor(v * scale + 0.5);
    if (t >= 65535.0) {
        return 65535;
    }
    return static_cast<uint16_t>(t);
}

int16_t CommProcess::clampS16(double v, double scale)
{
    if (scale <= 0.0) {
        return 0;
    }
    const double t = std::floor(v * scale + (v >= 0.0 ? 0.5 : -0.5));
    if (t >= 32767.0) {
        return 32767;
    }
    if (t <= -32768.0) {
        return -32768;
    }
    return static_cast<int16_t>(t);
}
