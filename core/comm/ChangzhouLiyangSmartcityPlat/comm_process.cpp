/**
 * tcu_comm process implementation
 * BY ZF
 */

#include "comm_process.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

namespace {
    // BY ZF: 智能城市平台命令字（固定值，不走配置）。
    static const uint8_t kCmdSetConfigReq = 0x00;
    static const uint8_t kCmdSetConfigAck = 0x10;
    static const uint8_t kCmdLoginReq = 0x20;
    static const uint8_t kCmdLoginAck = 0x30;
    static const uint8_t kCmdHeartbeat = 0x21;
    static const uint8_t kCmdChargeInfo = 0x22;
    static const uint8_t kCmdRemoteStartAck = 0x04;
    static const uint8_t kCmdRemoteStopAck = 0x05;
    
    static const uint8_t kCmdChargeRecord = 0x60;
    static const uint8_t kCmdRemoteStart = 0x14;
    static const uint8_t kCmdRemoteStop = 0x15;
    static const uint8_t kCmdRecordConfirm = 0x70;

    //固定充电桩/枪类型
    static const uint8_t kFixedChargerType = 0x01;
    static const uint8_t kFixedGunType = 0x01;

    static std::vector<std::string> split(const std::string& s, char ch)
    {
        std::vector<std::string> out;
        std::string cur;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == ch) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(s[i]);
            }
        }
        out.push_back(cur);
        return out;
    }

    static int hexNibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    static void appendAsciiFixed(std::vector<uint8_t>& out, const std::string& s, size_t width)
    {
        const size_t n = (s.size() < width) ? s.size() : width;
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
        if (width > n) {
            out.insert(out.end(), width - n, 0x00);
        }
    }

    // BY ZF: JSON 字段读取工具。
    static bool jsonGetNumber(cJSON* obj, const char* key, double& out)
    {
        cJSON* n = cJSON_GetObjectItem(obj, key);
        if (n && cJSON_IsNumber(n)) {
            out = n->valuedouble;
            return true;
        }
        return false;
    }

    static bool jsonGetInt(cJSON* obj, const char* key, int& out)
    {
        cJSON* n = cJSON_GetObjectItem(obj, key);
        if (n && cJSON_IsNumber(n)) {
            out = n->valueint;
            return true;
        }
        return false;
    }

    static std::string jsonGetString(cJSON* obj, const char* key)
    {
        cJSON* s = cJSON_GetObjectItem(obj, key);
        if (s && cJSON_IsString(s) && s->valuestring) {
            return std::string(s->valuestring);
        }
        return std::string();
    }

    // BY ZF: YYYYMMDDhhmmss 转 8 字节 BCD（末字节固定 0xFF）。
    static void ymdHmsToBcd8(uint64_t ymdhms, uint8_t out[8])
    {
        std::memset(out, 0, 8);
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
        auto bcd2 = [](char a, char b) -> uint8_t {
            const uint8_t hi = (a >= '0' && a <= '9') ? static_cast<uint8_t>(a - '0') : 0;
            const uint8_t lo = (b >= '0' && b <= '9') ? static_cast<uint8_t>(b - '0') : 0;
            return static_cast<uint8_t>((hi << 4) | lo);
        };
        out[0] = bcd2(buf[0], buf[1]);
        out[1] = bcd2(buf[2], buf[3]);
        out[2] = bcd2(buf[4], buf[5]);
        out[3] = bcd2(buf[6], buf[7]);
        out[4] = bcd2(buf[8], buf[9]);
        out[5] = bcd2(buf[10], buf[11]);
        out[6] = bcd2(buf[12], buf[13]);
        out[7] = 0xFF;
    }

    // BY ZF: 浮点缩放并四舍五入到无符号整数。
    static uint32_t scaleToU32(double v, double scale)
    {
        if (v <= 0.0) {
            return 0U;
        }
        return static_cast<uint32_t>(v * scale + 0.5);
    }

    // BY ZF: 平台命令字转名称，便于TCP调试输出。
    static const char* platformCmdName(uint8_t cmd)
    {
        switch (cmd) {
        case 0x00: return "set_config_req";
        case 0x10: return "set_config_ack";
        case 0x20: return "login_req";
        case 0x30: return "login_ack";
        case 0x21: return "heartbeat";
        case 0x22: return "charge_info";
        case 0x04: return "remote_start_ack";
        case 0x05: return "remote_stop_ack";
        case 0x60: return "charge_record";
        case 0x14: return "remote_start";
        case 0x15: return "remote_stop";
        case 0x70: return "record_confirm";
        default: return "unknown";
        }
    }

    // BY ZF: 平台0x04/0x05中的枪状态定义：00空闲/01连接/02工作/03故障。
    static uint8_t mapGunStatusForCtrlAck(uint8_t gunStatus, uint8_t vehicleConnectStatus)
    {
        // BY ZF: 当前实现中 m_gunStatus 已被平台上报映射复用，故在此收敛到控制应答所需定义。
        if (gunStatus == 0x01) {   // ERROR
            return 0x03;
        }
        if (gunStatus == 0x03) {   // CHARGING/STOPPING
            return 0x02;
        }
        if (vehicleConnectStatus != 0) {
            return 0x01;
        }
        return 0x00;
    }
}

CommProcess::CommProcess()
    : BaseProcess(PROC_COMMUNICATION, "tcu_comm")
    , m_logSender("tcu_comm")
    , m_seq(0)
    , m_platformConnected(false)
    , m_tcpFd(-1)
    , m_loginState(LOGIN_IDLE)
    , m_lastChargeInfoReport(std::chrono::steady_clock::now())
    , m_heartbeatCounter(0)
{
}

CommProcess::~CommProcess()
{
    doCleanup();
}

bool CommProcess::doInitialize()
{
    if (!loadConfig()) {
        return false;
    }
    if (!initMqtt()) {
        return false;
    }
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void CommProcess::doRun()
{
    m_running = true;
    // BY ZF: 喂狗频率控制为 5 秒一次。
    auto lastFeedTime = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (m_running.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedWatchdog();
            lastFeedTime = now;
        }
        // BY ZF: 平台 TCP 链路维护与登录流程状态机驱动。
        maintainPlatformTcp();
        usleep(10000);
    }
}

void CommProcess::doCleanup()
{
    m_running = false;
    closePlatformTcp();
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

bool CommProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";

    m_config.gunCount = static_cast<uint8_t>(cfg.getInt(section, "gun_count", 2));
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_comm");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.masterHost = cfg.getString(section, "master_host", "127.0.0.1");
    m_config.masterPort = cfg.getInt(section, "master_port", 9000);
    m_config.cdzNo = cfg.getString(section, "cdz_no", "CDZ000001");
    m_config.macAddr = cfg.getString(section, "mac_addr", "");
    m_config.factoryCreditCode = cfg.getString(section, "factory_credit_code", "");
    m_config.tcpReconnectSec = cfg.getInt(section, "tcp_reconnect_sec", 3);
    m_config.tcpHeartbeatSec = cfg.getInt(section, "tcp_heartbeat_sec", 5);
    m_config.loginRetrySec = cfg.getInt(section, "login_retry_sec", 10);
    m_config.debugTcp = (cfg.getInt(section, "debug_tcp", 0) != 0);

    m_config.gunIdList.clear();
    m_config.gunTypeList.clear();
    m_gunStatus.clear();
    m_vehicleConnectStatus.clear();
    m_gunRuntimeData.clear();
    m_feeModelByGun.clear();
    m_config.gunIdList.reserve(m_config.gunCount);
    m_config.gunTypeList.reserve(m_config.gunCount);
    m_gunStatus.reserve(m_config.gunCount);
    m_vehicleConnectStatus.reserve(m_config.gunCount);
    m_gunRuntimeData.reserve(m_config.gunCount);
    m_feeModelByGun.reserve(m_config.gunCount);
    m_config.chargerType = kFixedChargerType;


    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        std::ostringstream idKey;
        idKey << "gun" << static_cast<int>(i + 1) << "_id";

        const std::string idText = cfg.getString(section, idKey.str(), "0xFFFFFFFF");
        const uint32_t gunId = static_cast<uint32_t>(std::strtoul(idText.c_str(), nullptr, 0));
        m_config.gunIdList.push_back(gunId);
        m_config.gunTypeList.push_back(kFixedGunType);
        m_gunStatus.push_back(0x00);
        m_vehicleConnectStatus.push_back(0x00);
        m_gunRuntimeData.push_back(GunRuntimeData());
        m_feeModelByGun.push_back(FeeModel());
    }

    if (!m_config.macAddr.empty() && !isHexString(m_config.macAddr, 24)) {
        m_logSender.warn("invalid_mac_addr", m_config.macAddr);
    }
    if (!m_config.factoryCreditCode.empty() && m_config.factoryCreditCode.size() > 32) {
        m_logSender.warn("factory_credit_code_too_long", m_config.factoryCreditCode);
    }

    return true;
}

bool CommProcess::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[Comm] mqtt init failed" << std::endl;
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
        std::cerr << "[Comm] mqtt connect failed " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }
    if (!m_mqtt.loopStart()) {
        std::cerr << "[Comm] mqtt loop start failed" << std::endl;
        return false;
    }
    return true;
}

void CommProcess::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[Comm] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 1);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/pile/+/event", 1);
}

void CommProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    std::string module;
    std::string leaf;
    uint8_t gun = 0;
    if (!parseTopic(topic, module, gun, leaf)) {
        return;
    }

    if (module == "logic" && leaf == "event") {
        handleLogicEventForPlatform(gun, payload);
        return;
    }
    if (module == "logic" && leaf == "feeData") {
        handleLogicFeeForPlatform(gun, payload);
        return;
    }
    if (module == "pile" && leaf == "data") {
        handlePileDataForPlatform(gun, payload);
        return;
    }
    if (module == "pile" && leaf == "event") {
        handlePileEventForPlatform(gun, payload);
        return;
    }
}

bool CommProcess::parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const
{
    const std::vector<std::string> seg = split(topic, '/');
    if (seg.size() != 4) {
        return false;
    }
    if (seg[0] != m_config.mqttTopicPrefix) {
        return false;
    }
    module = seg[1];
    gun = static_cast<uint8_t>(std::atoi(seg[2].c_str()));
    leaf = seg[3];
    return true;
}

bool CommProcess::publishPlatCmd(uint8_t gun, const std::string& payload)
{
    const std::string outTopic = buildTopic("plat", gun, "cmd");
    return m_mqtt.publish(outTopic, ensureGunField(payload, gun), 1, false);
}

bool CommProcess::handleLogicEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunStatus.size() && gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* evt = cJSON_GetObjectItem(root, "event");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            //更新充电状态
            if (cJSON_IsString(evt) && std::strcmp(evt->valuestring, "state_change") == 0 && cJSON_IsObject(data)) {
                cJSON* from = cJSON_GetObjectItem(data, "from");
                cJSON* to = cJSON_GetObjectItem(data, "to");
                if (cJSON_IsString(to) && to->valuestring) {
                    // BY ZF: tcu_logic 状态 -> 平台枪状态
                    // BY ZF: IDLE=00, PREPARE=01, STARTING=01, CHARGING=02, STOPPING=02, ERROR=03, STOPPED=04
                    if (std::strcmp(to->valuestring, "IDLE") == 0) {
                        m_gunStatus[gun] = 0x00;
                    } else if (std::strcmp(to->valuestring, "PREPARE") == 0) {
                        m_gunStatus[gun] = 0x01;
                    } else if (std::strcmp(to->valuestring, "STARTING") == 0) {
                        m_gunStatus[gun] = 0x01;
                    } else if (std::strcmp(to->valuestring, "CHARGING") == 0) {
                        m_gunStatus[gun] = 0x02;
                    } else if (std::strcmp(to->valuestring, "STOPPING") == 0) {
                        m_gunStatus[gun] = 0x02;
                    } else if (std::strcmp(to->valuestring, "ERROR") == 0) {
                        m_gunStatus[gun] = 0x03;
                    } else if (std::strcmp(to->valuestring, "STOPPED") == 0) {
                        m_gunStatus[gun] = 0x04;
                    } else {
                        m_gunStatus[gun] = 0x01;
                    }

                    // BY ZF: 远程控制应答触发规则：
                    // BY ZF: STARTING->CHARGING 启动成功；STARTING->其他 启动失败。
                    // BY ZF: CHARGING->STOPPED 停机成功。
                    const char* fromState = (cJSON_IsString(from) && from->valuestring) ? from->valuestring : "";
                    GunRuntimeData& rd = m_gunRuntimeData[gun];
                    if (rd.pendingCtrlCmd == 0x00 && std::strcmp(fromState, "STARTING") == 0) {
                        const uint8_t result = (std::strcmp(to->valuestring, "CHARGING") == 0) ? 0x00 : 0x01;
                        const std::vector<uint8_t> ackBody = buildRemoteStartAckBody(gun, result);
                        if (!ackBody.empty()) {
                            sendPlatformFrame(kCmdRemoteStartAck, ackBody);
                        }
                        rd.pendingCtrlCmd = 0xFF;
                        rd.pendingCtrlSince = std::chrono::steady_clock::time_point();
                    } else if (rd.pendingCtrlCmd == 0x01 &&
                               std::strcmp(fromState, "CHARGING") == 0 &&
                               std::strcmp(to->valuestring, "STOPPED") == 0) {
                        const std::vector<uint8_t> ackBody = buildRemoteStopAckBody(gun, 0x00);
                        if (!ackBody.empty()) {
                            sendPlatformFrame(kCmdRemoteStopAck, ackBody);
                        }
                        rd.pendingCtrlCmd = 0xFF;
                        rd.pendingCtrlSince = std::chrono::steady_clock::time_point();
                    }
                }
            }
            // BY ZF: 充电记录上报事件 -> 平台0x60充电记录包请求。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "update_record") == 0 &&
                cJSON_IsObject(data)) {
                std::vector<uint8_t> body;
                if (buildChargeRecordBodyFromUpdateRecord(gun, data, body) && !body.empty()) {
                    sendPlatformFrame(kCmdChargeRecord, body);
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 平台上报发送链路后续在此扩展。
    return true;
}

bool CommProcess::handleLogicFeeForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsObject(data)) {
                cJSON* totalEnergy = cJSON_GetObjectItem(data, "totalEnergy");
                if (cJSON_IsNumber(totalEnergy)) {
                    m_gunRuntimeData[gun].totalEnergy = totalEnergy->valuedouble;
                }
                cJSON* totalAmount = cJSON_GetObjectItem(data, "totalAmount");
                if (cJSON_IsNumber(totalAmount)) {
                    m_gunRuntimeData[gun].totalAmount = totalAmount->valuedouble;
                }
                cJSON* electricAmount = cJSON_GetObjectItem(data, "electicAmount");
                if (!cJSON_IsNumber(electricAmount)) {
                    electricAmount = cJSON_GetObjectItem(data, "electricAmount");
                }
                if (cJSON_IsNumber(electricAmount)) {
                    m_gunRuntimeData[gun].electricAmount = electricAmount->valuedouble;
                }
                cJSON* serviceAmount = cJSON_GetObjectItem(data, "serviceAmount");
                if (cJSON_IsNumber(serviceAmount)) {
                    m_gunRuntimeData[gun].serviceAmount = serviceAmount->valuedouble;
                }

                cJSON* feeModelId = cJSON_GetObjectItem(data, "feeModelId");
                if (cJSON_IsString(feeModelId) && feeModelId->valuestring) {
                    m_gunRuntimeData[gun].feeModelId = feeModelId->valuestring;
                }

                cJSON* timeNum = cJSON_GetObjectItem(data, "timeNum");
                if (!cJSON_IsNumber(timeNum)) {
                    timeNum = cJSON_GetObjectItem(data, "sgemtentNum");
                }
                if (cJSON_IsNumber(timeNum)) {
                    m_gunRuntimeData[gun].feeTimeNum = timeNum->valueint;
                }

                cJSON* segments = cJSON_GetObjectItem(data, "segmentsAmount");
                if (cJSON_IsArray(segments)) {
                    m_gunRuntimeData[gun].feeSegments.clear();
                    const int cnt = cJSON_GetArraySize(segments);
                    if (cnt > 0) {
                        m_gunRuntimeData[gun].feeSegments.reserve(static_cast<size_t>(cnt));
                    }
                    for (int i = 0; i < cnt; ++i) {
                        cJSON* item = cJSON_GetArrayItem(segments, i);
                        if (!cJSON_IsObject(item)) {
                            continue;
                        }
                        FeeSegmentData seg;
                        cJSON* startTs = cJSON_GetObjectItem(item, "startTs");
                        if (cJSON_IsString(startTs) && startTs->valuestring) {
                            seg.startTs = startTs->valuestring;
                        }
                        cJSON* endTs = cJSON_GetObjectItem(item, "endTs");
                        if (cJSON_IsString(endTs) && endTs->valuestring) {
                            seg.endTs = endTs->valuestring;
                        }
                        cJSON* energyKwh = cJSON_GetObjectItem(item, "energyKwh");
                        if (cJSON_IsNumber(energyKwh)) {
                            seg.energyKwh = energyKwh->valuedouble;
                        }
                        cJSON* segElectric = cJSON_GetObjectItem(item, "electicAmount");
                        if (!cJSON_IsNumber(segElectric)) {
                            segElectric = cJSON_GetObjectItem(item, "electricAmount");
                        }
                        if (cJSON_IsNumber(segElectric)) {
                            seg.electricAmount = segElectric->valuedouble;
                        }
                        cJSON* segService = cJSON_GetObjectItem(item, "serviceAmount");
                        if (cJSON_IsNumber(segService)) {
                            seg.serviceAmount = segService->valuedouble;
                        }
                        m_gunRuntimeData[gun].feeSegments.push_back(seg);
                    }
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 预留平台上报发送链路。
    return true;
}

bool CommProcess::handlePileDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_vehicleConnectStatus.size() && gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* type = cJSON_GetObjectItem(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yx") == 0 && cJSON_IsObject(data)) {
                //更新车辆连接状态
                cJSON* vc = cJSON_GetObjectItem(data, "vehicleConnectStatus");
                if (cJSON_IsNumber(vc)) {
                    m_vehicleConnectStatus[gun] = (vc->valueint != 0) ? 0x01 : 0x00;
                }
            }
            // BY ZF: 缓存遥测电压/电流（来自 yc）。
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yc") == 0 && cJSON_IsObject(data)) {
                cJSON* outputVoltage = cJSON_GetObjectItem(data, "outputVoltage");
                if (cJSON_IsNumber(outputVoltage)) {
                    m_gunRuntimeData[gun].voltage = outputVoltage->valuedouble;
                }
                cJSON* outputCurrent = cJSON_GetObjectItem(data, "outputCurrent");
                if (cJSON_IsNumber(outputCurrent)) {
                    m_gunRuntimeData[gun].current = outputCurrent->valuedouble;
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 平台上报发送链路后续在此扩展。
    return true;
}

bool CommProcess::handlePileEventForPlatform(uint8_t gun, const std::string& payload)
{
    (void)gun;
    (void)payload;
    // BY ZF: 预留平台上报链路（TCP/HTTP/私有协议）。当前先透传日志。
    return true;
}

bool CommProcess::buildChargeRecordBodyFromUpdateRecord(uint8_t gun, cJSON* data, std::vector<uint8_t>& body)
{
    if (!data || gun >= m_config.gunIdList.size()) {
        return false;
    }

    const std::string tradeNo = jsonGetString(data, "tradeNo");
    const double totalElect = [&](){ double v = 0.0; jsonGetNumber(data, "totalElect", v); return v; }();
    const double totalCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalCost", v); return v; }();
    const double totalServCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalServCost", v); return v; }();
    const double sumStart = [&](){ double v = 0.0; jsonGetNumber(data, "sumStart", v); return v; }();
    const double sumEnd = [&](){ double v = 0.0; jsonGetNumber(data, "sumEnd", v); return v; }();
    const int reason = [&](){ int v = 0; jsonGetInt(data, "reason", v); return v; }();
    const uint64_t startTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeStartTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();
    const uint64_t endTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeEndTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();

    // BY ZF: 1 枪ID
    appendU32BE(body, m_config.gunIdList[gun]);
    // BY ZF: 2 IC卡卡号(8字节BCD)，当前固定0。
    for (int i = 0; i < 8; ++i) body.push_back(0x00);
    // BY ZF: 3 订单ID(10字节BCD)
    appendOrderIdBcd10(body, tradeNo);
    // BY ZF: 缓存本次上送的交易号，供0x70应答使用（协议不带订单号时回填）。
    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    }
    // BY ZF: 4~8 电量与金额。
    appendU32BE(body, scaleToU32(totalElect, 100.0));     // totalEnergy 0.01kWh
    appendU32BE(body, scaleToU32(totalCost, 100.0));      // totalAmount 0.01元
    appendU32BE(body, scaleToU32(totalServCost, 100.0));  // serviceFee 0.01元
    appendU32BE(body, scaleToU32(sumStart, 100.0));       // startEnergy 0.01kWh
    appendU32BE(body, scaleToU32(sumEnd, 100.0));         // endEnergy 0.01kWh

    // BY ZF: 9~10 起止时间BCD。
    uint8_t startBcd[8];
    uint8_t endBcd[8];
    ymdHmsToBcd8(startTime, startBcd);
    ymdHmsToBcd8(endTime, endBcd);
    body.insert(body.end(), startBcd, startBcd + 8);
    body.insert(body.end(), endBcd, endBcd + 8);

    // BY ZF: 11 停止原因（当前按正常上送0x01）。
    (void)reason;
    body.push_back(0x00);
    // BY ZF: 12 停止异常代码（暂置0）。
    appendU32BE(body, 0U);
    // BY ZF: 13 计费标志（来自启动指令缓存）。
    const uint8_t billingFlag = (gun < m_gunRuntimeData.size()) ? m_gunRuntimeData[gun].billingFlag : 0x00;
    body.push_back(billingFlag);

    // BY ZF: 14~15 时段费用明细。
    cJSON* partElectArr = cJSON_GetObjectItem(data, "partElect");
    cJSON* chargeFeeArr = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFeeArr = cJSON_GetObjectItem(data, "serviceFee");

    int timeNum = 0;
    jsonGetInt(data, "timeNum", timeNum);
    int periodCount = std::max(0, timeNum);
    if (cJSON_IsArray(partElectArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(partElectArr));
    }
    if (cJSON_IsArray(chargeFeeArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(chargeFeeArr));
    }
    if (cJSON_IsArray(serviceFeeArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(serviceFeeArr));
    }
    if (periodCount < 0) periodCount = 0;
    if (periodCount > 24) periodCount = 24;
    body.push_back(static_cast<uint8_t>(periodCount));

    for (int i = 0; i < periodCount; ++i) {
        std::string startTs;
        std::string endTs;
        if (gun < m_gunRuntimeData.size() &&
            i < static_cast<int>(m_gunRuntimeData[gun].feeSegments.size())) {
            startTs = m_gunRuntimeData[gun].feeSegments[i].startTs;
            endTs = m_gunRuntimeData[gun].feeSegments[i].endTs;
        }

        body.push_back(hhmmToBcdHour(startTs)); // 开始时间（1字节）
        body.push_back(hhmmToBcdHour(endTs));   // 结束时间（1字节）

        double periodChargeFee = 0.0;
        double periodServiceFee = 0.0;
        double periodEnergy = 0.0;
        if (cJSON_IsArray(chargeFeeArr)) {
            cJSON* n = cJSON_GetArrayItem(chargeFeeArr, i);
            if (n && cJSON_IsNumber(n)) periodChargeFee = n->valuedouble;
        }
        if (cJSON_IsArray(serviceFeeArr)) {
            cJSON* n = cJSON_GetArrayItem(serviceFeeArr, i);
            if (n && cJSON_IsNumber(n)) periodServiceFee = n->valuedouble;
        }
        if (cJSON_IsArray(partElectArr)) {
            cJSON* n = cJSON_GetArrayItem(partElectArr, i);
            if (n && cJSON_IsNumber(n)) periodEnergy = n->valuedouble;
        }

        appendU32BE(body, scaleToU32(periodChargeFee, 100.0));  // 0.01元
        appendU32BE(body, scaleToU32(periodServiceFee, 100.0)); // 0.01元
        appendU32BE(body, scaleToU32(periodEnergy, 100.0));     // 0.01kWh
    }

    return true;
}

std::string CommProcess::ensureGunField(const std::string& payload, uint8_t gun) const
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return payload;
    }

    cJSON* gunItem = cJSON_GetObjectItem(root, "gun");
    if (!gunItem || !cJSON_IsNumber(gunItem)) {
        cJSON_AddNumberToObject(root, "gun", gun);
    }

    char* out = cJSON_PrintUnformatted(root);
    std::string text = payload;
    if (out) {
        text = out;
        free(out);
    }
    cJSON_Delete(root);
    return text;
}

std::string CommProcess::buildTopic(const char* module, uint8_t gun, const char* leaf) const
{
    std::ostringstream oss;
    oss << m_config.mqttTopicPrefix << "/" << module << "/" << static_cast<int>(gun) << "/" << leaf;
    return oss.str();
}

bool CommProcess::connectPlatformTcp()
{
    closePlatformTcp();

    m_tcpFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_tcpFd < 0) {
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(m_tcpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_tcpFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.masterPort));
    if (::inet_pton(AF_INET, m_config.masterHost.c_str(), &addr.sin_addr) != 1) {
        closePlatformTcp();
        return false;
    }

    if (::connect(m_tcpFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        closePlatformTcp();
        return false;
    }

    m_platformConnected = true;
    m_loginState = LOGIN_IDLE;
    m_lastLoginAction = std::chrono::steady_clock::now();
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastChargeInfoReport = std::chrono::steady_clock::now();
    m_tcpRxCache.clear();
    m_logSender.info("platform_tcp_connected", m_config.masterHost + ":" + std::to_string(m_config.masterPort));
    return true;
}

void CommProcess::closePlatformTcp()
{
    if (m_tcpFd >= 0) {
        ::close(m_tcpFd);
        m_tcpFd = -1;
    }
    m_platformConnected = false;
    m_loginState = LOGIN_IDLE;
}

bool CommProcess::sendPlatformText(const std::string& text)
{
    if (m_tcpFd < 0) {
        return false;
    }
    ssize_t n = ::send(m_tcpFd, text.data(), text.size(), 0);
    if (n < 0) {
        return false;
    }
    return true;
}

uint16_t CommProcess::calcCrc16Modbus(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint8_t CommProcess::bcdByte(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

void CommProcess::appendU16BE(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void CommProcess::appendU32BE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint16_t CommProcess::readU16BE(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t CommProcess::readU32BE(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

std::string CommProcess::toHex(const uint8_t* data, size_t len)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

std::string CommProcess::bcdToDigitString(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(static_cast<char>('0' + ((data[i] >> 4) & 0x0F)));
        out.push_back(static_cast<char>('0' + (data[i] & 0x0F)));
    }
    return out;
}

std::string CommProcess::bcdHourToHhmm(uint8_t bcdHour)
{
    const int hh = (((bcdHour >> 4) & 0x0F) * 10) + (bcdHour & 0x0F);
    char buf[5] = {0};
    std::snprintf(buf, sizeof(buf), "%02d00", hh);
    return std::string(buf);
}

uint8_t CommProcess::hhmmToBcdHour(const std::string& hhmm)
{
    if (hhmm.size() < 2) {
        return 0x00;
    }
    const int h0 = hhmm[0] - '0';
    const int h1 = hhmm[1] - '0';
    if (h0 < 0 || h0 > 9 || h1 < 0 || h1 > 9) {
        return 0x00;
    }
    int hour = h0 * 10 + h1;
    if (hour < 0 || hour > 23) {
        hour = 0;
    }
    return static_cast<uint8_t>(((hour / 10) << 4) | (hour % 10));
}

void CommProcess::appendOrderIdBcd10(std::vector<uint8_t>& out, const std::string& orderNo)
{
    std::string digits;
    digits.reserve(orderNo.size());
    for (size_t i = 0; i < orderNo.size(); ++i) {
        if (orderNo[i] >= '0' && orderNo[i] <= '9') {
            digits.push_back(orderNo[i]);
        }
    }
    if (digits.size() > 20) {
        digits = digits.substr(digits.size() - 20);
    }
    if (digits.size() < 20) {
        digits.insert(digits.begin(), 20 - digits.size(), '0');
    }
    for (size_t i = 0; i < 20; i += 2) {
        const uint8_t hi = static_cast<uint8_t>(digits[i] - '0');
        const uint8_t lo = static_cast<uint8_t>(digits[i + 1] - '0');
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
}

bool CommProcess::syncSystemTime(int year, int month, int day, int hour, int minute, int second)
{
    // BY ZF: 按平台下发时间执行系统校时（失败仅记录日志）。
    struct tm tmv;
    std::memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = minute;
    tmv.tm_sec = second;
    tmv.tm_isdst = -1;

    const time_t tt = std::mktime(&tmv);
    if (tt <= 0) {
        m_logSender.warn("platform_time_sync_fail", "invalid_bcd_time");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<long>(tt);
    tv.tv_usec = 0;
    if (::settimeofday(&tv, nullptr) != 0) {
        m_logSender.warn("platform_time_sync_fail", std::string("settimeofday_errno=") + std::to_string(errno));
        return false;
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body)
{
    // BY ZF: 协议头部：F5 E2 F3 06 + 版本(1) + 时间戳(8) + 长度(2) + 序号(1) + cmd(1) + body + crc(2)
    std::vector<uint8_t> frame;
    frame.reserve(32 + body.size());
    frame.push_back(0xF5);
    frame.push_back(0xE2);
    frame.push_back(0xF3);
    frame.push_back(0x06);
    frame.push_back(0x04);

    std::time_t tt = std::time(nullptr);
    std::tm* tmv = std::localtime(&tt);
    const int year = tmv ? (tmv->tm_year + 1900) : 2026;
    frame.push_back(bcdByte((year / 100) % 100));
    frame.push_back(bcdByte(year % 100));
    frame.push_back(bcdByte((tmv ? tmv->tm_mon + 1 : 1)));
    frame.push_back(bcdByte((tmv ? tmv->tm_mday : 1)));
    frame.push_back(bcdByte((tmv ? tmv->tm_hour : 0)));
    frame.push_back(bcdByte((tmv ? tmv->tm_min : 0)));
    frame.push_back(bcdByte((tmv ? tmv->tm_sec : 0)));
    frame.push_back(0xFF);

    const uint16_t seq = static_cast<uint16_t>((++m_seq) & 0xFF);
    const uint16_t totalLen = static_cast<uint16_t>(1 + 1 + body.size() + 2);
    appendU16BE(frame, totalLen);
    frame.push_back(static_cast<uint8_t>(seq & 0xFF));
    frame.push_back(cmd);
    frame.insert(frame.end(), body.begin(), body.end());

    const uint16_t crc = calcCrc16Modbus(frame.data(), frame.size());
    appendU16BE(frame, crc);
    return frame;
}

bool CommProcess::sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body)
{
    if (m_tcpFd < 0) {
        return false;
    }
    const std::vector<uint8_t> frame = buildPlatformFrame(cmd, body);
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][TX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frame.size()
                  << " hex=" << toHex(frame.data(), frame.size()) << std::endl;
    }
    ssize_t n = ::send(m_tcpFd, frame.data(), frame.size(), 0);
    if (n < 0 || static_cast<size_t>(n) != frame.size()) {
        return false;
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildSetConfigRequestBody() const
{
    // BY ZF: 按智能城市 0x00 配置请求构建信息体：
    // BY ZF: MAC(12) + 桩类型(1) + 枪数(1) + N*(枪ID4+枪类型1) + 加密标志(1)。
    std::vector<uint8_t> body;

    // BY ZF: 1) MAC 地址，配置为24位hex，转换为12字节。
    uint8_t macBytes[12];
    std::memset(macBytes, 0xFF, sizeof(macBytes));
    if (isHexString(m_config.macAddr, 24)) {
        for (int i = 0; i < 12; ++i) {
            const int hi = hexNibble(m_config.macAddr[static_cast<size_t>(i) * 2]);
            const int lo = hexNibble(m_config.macAddr[static_cast<size_t>(i) * 2 + 1]);
            if (hi >= 0 && lo >= 0) {
                macBytes[i] = static_cast<uint8_t>((hi << 4) | lo);
            }
        }
    }
    body.insert(body.end(), macBytes, macBytes + 12);

    // BY ZF: 2) 桩类型固定直流 0x01。
    body.push_back(m_config.chargerType);

    // BY ZF: 3) 枪数量（按配置 gun_count）。
    body.push_back(m_config.gunCount);

    // BY ZF: 4) 每枪信息：枪ID(4字节大端) + 枪类型固定 0x01（直流枪）。
    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        const uint32_t gunId = (i < m_config.gunIdList.size()) ? m_config.gunIdList[i] : 0xFFFFFFFFU;
        appendU32BE(body, gunId);
        body.push_back(kFixedGunType);
    }

    // BY ZF: 5) 是否支持加密：0x00支持，0x01不支持（当前不支持）。
    body.push_back(0x01);

    return body;
}

std::vector<uint8_t> CommProcess::buildLoginRequestBody() const
{
    // BY ZF: 按智能城市 0x20 签到/登录请求构建信息体：
    // BY ZF: cdzId(4) + 桩类型(1) + 枪数量(1) + 固件版本(2)
    // BY ZF: + N*(枪ID4+枪状态1) + DTU版本(3) + 信用编码(9) + ICCID(20)。
    std::vector<uint8_t> body;

    // 1) 充电桩ID（4字节，大端）
    const uint32_t cdzId = static_cast<uint32_t>(std::strtoul(m_config.cdzNo.c_str(), nullptr, 0));
    appendU32BE(body, cdzId);

    // 2) 充电桩类型（固定直流 0x01）
    body.push_back(kFixedChargerType);

    // 3) 充电枪数量
    body.push_back(m_config.gunCount);

    // 4) 固件版本号（2字节，258 => V2.58）
    appendU16BE(body, 258);

    // 5) 枪信息（枪ID + 枪状态）
    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        const uint32_t gunId = (i < m_config.gunIdList.size()) ? m_config.gunIdList[i] : 0xFFFFFFFFU;
        appendU32BE(body, gunId);
        // BY ZF: 枪状态暂按空闲上送；后续可接入 pile yx 状态映射 00/01/02/03。
        body.push_back(0x00);
    }

    // 6) DTU 版本号（3字节BCD，默认 V1.1.1）
    body.push_back(0x01);
    body.push_back(0x01);
    body.push_back(0x01);

    // 7) 桩企信用编码（9字节 ASCII，超长截断，不足补0）
    appendAsciiFixed(body, m_config.factoryCreditCode, 9);

    // 8) SIM ICCID（20字节 ASCII，当前未接底层读取，先补0）
    body.insert(body.end(), 20, 0x00);

    return body;
}

std::vector<uint8_t> CommProcess::buildHeartbeatBody()
{
    // BY ZF: 0x21 心跳信息体：
    // BY ZF: cdzId(4) + 桩类型(1) + 枪数(1)
    // BY ZF: + N*(枪ID4 + 枪类型1 + 枪状态1 + 车位检测1 + 停车状态1 + 地锁状态1 + 车辆连接状态1)
    // BY ZF: + 心跳计数1 + 信号强度1 + 能源路由状态1
    std::vector<uint8_t> body;

    const uint32_t cdzId = static_cast<uint32_t>(std::strtoul(m_config.cdzNo.c_str(), nullptr, 0));
    appendU32BE(body, cdzId);
    body.push_back(m_config.chargerType);
    body.push_back(m_config.gunCount);

    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        const uint32_t gunId = (i < m_config.gunIdList.size()) ? m_config.gunIdList[i] : 0xFFFFFFFFU;
        appendU32BE(body, gunId);
        const uint8_t gunType = (i < m_config.gunTypeList.size()) ? m_config.gunTypeList[i] : kFixedGunType;
        body.push_back(gunType);

        const uint8_t st = (i < m_gunStatus.size()) ? m_gunStatus[i] : 0x00;
        body.push_back(st);                    // 枪状态
        body.push_back(0xFF);                  // 车位检测仪状态
        body.push_back(0x02);                  // 车辆停车状态
        body.push_back(0xFF);                  // 地锁状态
        const uint8_t vc = (i < m_vehicleConnectStatus.size()) ? m_vehicleConnectStatus[i] : 0x00;
        body.push_back(vc);                    // 车辆连接状态（来自 pile yx）
    }

    body.push_back(m_heartbeatCounter++);
    body.push_back(0x11); // 信号强度默认值
    body.push_back(0xFF); // 能源路由器状态未配置
    return body;
}

std::vector<uint8_t> CommProcess::buildChargeInfoBody(uint8_t gun)
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || gun >= m_config.gunIdList.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    appendU32BE(body, m_config.gunIdList[gun]); // 1 枪ID
    body.push_back(0x02);                       // 2 枪状态：charging/stopping统一按工作态上报

    appendOrderIdBcd10(body, rd.orderNo);       // 3 订单ID（10字节BCD）

    const uint16_t voltageA = static_cast<uint16_t>(std::max(0.0, rd.voltage * 10.0));    // 0.1V
    const uint16_t currentA = static_cast<uint16_t>(std::max(0.0, rd.current * 100.0));   // 0.01A
    appendU16BE(body, voltageA); // 4 A相电压
    appendU16BE(body, currentA); // 5 A相电流
    appendU16BE(body, 0);        // 6 B相电压
    appendU16BE(body, 0);        // 7 B相电流
    appendU16BE(body, 0);        // 8 C相电压
    appendU16BE(body, 0);        // 9 C相电流

    const uint16_t outputPower = static_cast<uint16_t>((static_cast<uint32_t>(voltageA) * static_cast<uint32_t>(currentA)) / 10000U);
    appendU16BE(body, outputPower); // 10 输出功率(0.01kW)

    body.push_back(rd.billingFlag ); // 11 计费标志

    const uint32_t startEnergy = 0;
    const uint32_t endEnergy = static_cast<uint32_t>(std::max(0.0, rd.totalEnergy * 100.0)); // 0.01kWh
    appendU32BE(body, startEnergy); // 12 起始电量
    appendU32BE(body, endEnergy);   // 13 结束电量
    appendU32BE(body, endEnergy);   // 14 累计电量

    const uint32_t totalAmount = static_cast<uint32_t>(std::max(0.0, rd.totalAmount * 100.0));      // 0.01元
    const uint32_t electricAmount = static_cast<uint32_t>(std::max(0.0, rd.electricAmount * 100.0)); // 0.01元
    const uint32_t serviceAmount = static_cast<uint32_t>(std::max(0.0, rd.serviceAmount * 100.0));   // 0.01元
    appendU32BE(body, totalAmount);    // 15 累计总金额
    appendU32BE(body, electricAmount); // 16 累计电费
    appendU32BE(body, serviceAmount);  // 17 累计服务费

    uint8_t periodCount = static_cast<uint8_t>(rd.feeSegments.size());
    if (periodCount > 24) {
        periodCount = 24;
    }
    body.push_back(periodCount); // 18 时段数

    for (uint8_t i = 0; i < periodCount; ++i) {
        const FeeSegmentData& seg = rd.feeSegments[i];
        body.push_back(hhmmToBcdHour(seg.startTs)); // 开始时间(小时BCD)
        body.push_back(hhmmToBcdHour(seg.endTs));   // 结束时间(小时BCD)
        appendU32BE(body, static_cast<uint32_t>(std::max(0.0, seg.electricAmount * 100.0))); // 总电费 0.01元
        appendU32BE(body, static_cast<uint32_t>(std::max(0.0, seg.serviceAmount * 100.0)));  // 总服务费 0.01元
        appendU32BE(body, static_cast<uint32_t>(std::max(0.0, seg.energyKwh * 100.0)));      // 总电量 0.01kWh
    }

    // 21 充电起始时间（8字节BCD）
    bool hasStartTime = false;
    for (size_t i = 0; i < rd.startTimeBcd.size(); ++i) {
        if (rd.startTimeBcd[i] != 0x00) {
            hasStartTime = true;
            break;
        }
    }
    if (hasStartTime) {
        body.insert(body.end(), rd.startTimeBcd.begin(), rd.startTimeBcd.end());
    } else {
        std::time_t tt = std::time(nullptr);
        std::tm* tmv = std::localtime(&tt);
        const int year = tmv ? (tmv->tm_year + 1900) : 2026;
        body.push_back(bcdByte((year / 100) % 100));
        body.push_back(bcdByte(year % 100));
        body.push_back(bcdByte((tmv ? tmv->tm_mon + 1 : 1)));
        body.push_back(bcdByte((tmv ? tmv->tm_mday : 1)));
        body.push_back(bcdByte((tmv ? tmv->tm_hour : 0)));
        body.push_back(bcdByte((tmv ? tmv->tm_min : 0)));
        body.push_back(bcdByte((tmv ? tmv->tm_sec : 0)));
        body.push_back(0xFF);
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteStartAckBody(uint8_t gun, uint8_t result)
{
    std::vector<uint8_t> body;
    if (gun >= m_config.gunIdList.size() ||
        gun >= m_gunRuntimeData.size() ||
        gun >= m_gunStatus.size() ||
        gun >= m_vehicleConnectStatus.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    const uint8_t gunStatus = mapGunStatusForCtrlAck(m_gunStatus[gun], m_vehicleConnectStatus[gun]);

    // BY ZF: 0x04 信息体：result(1) + gunId(4) + gunStatus(1) + orderId(10BCD) + userId(5BCD)
    body.push_back(result);
    appendU32BE(body, m_config.gunIdList[gun]);
    body.push_back(gunStatus);
    appendOrderIdBcd10(body, rd.orderNo);

    std::string userDigits;
    userDigits.reserve(rd.chargeUserNo.size());
    for (size_t i = 0; i < rd.chargeUserNo.size(); ++i) {
        const char c = rd.chargeUserNo[i];
        if (c >= '0' && c <= '9') {
            userDigits.push_back(c);
        }
    }
    if (userDigits.size() > 10) {
        userDigits = userDigits.substr(userDigits.size() - 10);
    }
    if (userDigits.size() < 10) {
        userDigits.insert(userDigits.begin(), 10 - userDigits.size(), '0');
    }
    for (size_t i = 0; i < 10; i += 2) {
        const uint8_t hi = static_cast<uint8_t>(userDigits[i] - '0');
        const uint8_t lo = static_cast<uint8_t>(userDigits[i + 1] - '0');
        body.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteStopAckBody(uint8_t gun, uint8_t result)
{
    std::vector<uint8_t> body;
    if (gun >= m_config.gunIdList.size() ||
        gun >= m_gunRuntimeData.size() ||
        gun >= m_gunStatus.size() ||
        gun >= m_vehicleConnectStatus.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    const uint8_t gunStatus = mapGunStatusForCtrlAck(m_gunStatus[gun], m_vehicleConnectStatus[gun]);

    // BY ZF: 0x05 信息体：result(1) + gunId(4) + gunStatus(1) + orderId(10BCD)
    body.push_back(result);
    appendU32BE(body, m_config.gunIdList[gun]);
    body.push_back(gunStatus);
    appendOrderIdBcd10(body, rd.orderNo);
    return body;
}

void CommProcess::checkRemoteCtrlAckTimeout(const std::chrono::steady_clock::time_point& now)
{
    // BY ZF: 远程启停控制应答超时120秒，超时后回对应0x04/0x05失败并清理等待态。
    const std::chrono::seconds timeoutSec(120);
    const size_t cnt = m_gunRuntimeData.size();
    for (size_t i = 0; i < cnt; ++i) {
        GunRuntimeData& rd = m_gunRuntimeData[i];
        if (rd.pendingCtrlCmd == 0xFF) {
            continue;
        }
        if (rd.pendingCtrlSince.time_since_epoch().count() == 0) {
            rd.pendingCtrlSince = now;
            continue;
        }
        if (now - rd.pendingCtrlSince >= timeoutSec) {
            const uint8_t gun = static_cast<uint8_t>(i);
            if (rd.pendingCtrlCmd == 0x00) {
                const std::vector<uint8_t> ackBody = buildRemoteStartAckBody(gun, 0x01);
                if (!ackBody.empty()) {
                    sendPlatformFrame(kCmdRemoteStartAck, ackBody);
                }
            } else if (rd.pendingCtrlCmd == 0x01) {
                const std::vector<uint8_t> ackBody = buildRemoteStopAckBody(gun, 0x01);
                if (!ackBody.empty()) {
                    sendPlatformFrame(kCmdRemoteStopAck, ackBody);
                }
            }
            rd.pendingCtrlCmd = 0xFF;
            rd.pendingCtrlSince = std::chrono::steady_clock::time_point();
        }
    }
}

void CommProcess::reportChargeInfoPeriodic()
{
    const uint8_t count = static_cast<uint8_t>(m_gunStatus.size());
    for (uint8_t gun = 0; gun < count; ++gun) {
        // BY ZF: 仅在 charging/stopping 上送（当前映射均为 0x02）。
        if (m_gunStatus[gun] != 0x02) {
            continue;
        }
        const std::vector<uint8_t> body = buildChargeInfoBody(gun);
        if (!body.empty()) {
            sendPlatformFrame(kCmdChargeInfo, body);
        }
    }
}

void CommProcess::driveLoginStateMachine(const std::chrono::steady_clock::time_point& now)
{
    if (!m_platformConnected.load()) {
        return;
    }

    switch (m_loginState) {
    case LOGIN_IDLE:
        m_loginState = LOGIN_REQ_SET_CONFIG;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        break;
    case LOGIN_REQ_SET_CONFIG:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (sendPlatformFrame(kCmdSetConfigReq, buildSetConfigRequestBody())) {
                m_logSender.info("platform_login_step", "set_config_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_REQ_LOGIN:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (sendPlatformFrame(kCmdLoginReq, buildLoginRequestBody())) {
                m_logSender.info("platform_login_step", "login_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_DONE:
        checkRemoteCtrlAckTimeout(now);
        if (now - m_lastHeartbeat >= std::chrono::seconds(m_config.tcpHeartbeatSec)) {
            if (!sendPlatformFrame(kCmdHeartbeat, buildHeartbeatBody())) {
                closePlatformTcp();
            } else {
                m_lastHeartbeat = now;
            }
        }
        if (now - m_lastChargeInfoReport >= std::chrono::seconds(10)) {
            reportChargeInfoPeriodic();
            m_lastChargeInfoReport = now;
        }
        break;
    default:
        break;
    }
}

void CommProcess::maintainPlatformTcp()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (!m_platformConnected.load()) {
        if (m_lastTcpConnectTry.time_since_epoch().count() == 0 ||
            now - m_lastTcpConnectTry >= std::chrono::seconds(m_config.tcpReconnectSec)) {
            m_lastTcpConnectTry = now;
            connectPlatformTcp();
        }
        return;
    }

    char buf[512];
    const ssize_t n = ::recv(m_tcpFd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0) {
        closePlatformTcp();
        return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        closePlatformTcp();
        return;
    }
    if (n > 0) {
        handlePlatformRxData(buf, static_cast<size_t>(n));
    }

    driveLoginStateMachine(now);
}

void CommProcess::handlePlatformRxData(const char* data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX] len=" << len
                  << " hex=" << toHex(reinterpret_cast<const uint8_t*>(data), len) << std::endl;
    }
    m_tcpRxCache.insert(m_tcpRxCache.end(), data, data + len);

    while (m_tcpRxCache.size() >= 17) {
        // BY ZF: 同步头 F5 E2 F3 06
        if (!(m_tcpRxCache[0] == 0xF5 && m_tcpRxCache[1] == 0xE2 &&
              m_tcpRxCache[2] == 0xF3 && m_tcpRxCache[3] == 0x06)) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        const uint16_t totalLen = readU16BE(&m_tcpRxCache[13]);
        const size_t frameLen = static_cast<size_t>(4 + 1 + 8 + 2 + totalLen);
        if (frameLen < 19) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        if (m_tcpRxCache.size() < frameLen) {
            break;
        }

        processPlatformPacket(m_tcpRxCache.data(), frameLen);
        m_tcpRxCache.erase(m_tcpRxCache.begin(), m_tcpRxCache.begin() + static_cast<long>(frameLen));
    }
}

bool CommProcess::extractSetConfigData(uint8_t cmd, const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x10 最小长度：结果1 + 桩ID4 + 桩类型1 + 6个周期参数 + 在线标志1 + 时段数1
    if (bodyLen < 14) {
        return false;
    }

    gun = 0;
    size_t pos = 0;

    const uint8_t feedbackResult = body[pos++];
    const uint32_t cdzId = readU32BE(body + pos);
    pos += 4;
    (void)body[pos++]; // chargerType
    pos += 6;          // heartbeatInterval/heartbeatTimeoutCount/chargeInfoInterval/chargeInfoTimeoutCount/chargeVehicleInterval/chargeVehicleTimeoutCount
    pos += 1;          // onlineMode
    const uint8_t numOfTimePeriods = body[pos++];

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "cdzId", static_cast<double>(cdzId));
    // BY ZF: setConfig 增加本机配置的 MAC 地址，供内部模块透传使用。
    cJSON_AddStringToObject(data, "macAddr", m_config.macAddr.c_str());
    cJSON* feeModel = cJSON_CreateObject();
    cJSON_AddNumberToObject(feeModel, "timeNum", numOfTimePeriods);
    cJSON* timeSeg = cJSON_CreateArray();
    cJSON* chargeFee = cJSON_CreateArray();
    cJSON* serviceFee = cJSON_CreateArray();
    for (uint8_t i = 0; i < numOfTimePeriods; ++i) {
        const uint8_t startHour = body[pos++];
        (void)body[pos++]; // endHour
        const uint16_t chargeFeeCent = readU16BE(body + pos);
        pos += 2;
        const uint16_t serviceFeeCent = readU16BE(body + pos);
        pos += 2;
        cJSON_AddItemToArray(timeSeg, cJSON_CreateString(bcdHourToHhmm(startHour).c_str()));
        cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(static_cast<double>(chargeFeeCent) / 100.0));
        cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(static_cast<double>(serviceFeeCent) / 100.0));
    }
    cJSON_AddItemToObject(feeModel, "timeSeg", timeSeg);
    cJSON_AddItemToObject(feeModel, "chargeFee", chargeFee);
    cJSON_AddItemToObject(feeModel, "serviceFee", serviceFee);
    cJSON_AddItemToObject(data, "feeModel", feeModel);

    if (pos + 1 > bodyLen) {
        cJSON_Delete(data);
        return false;
    }
    const uint8_t gunCount = body[pos++];
    cJSON_AddNumberToObject(data, "gunCount", gunCount);
    cJSON* guns = cJSON_CreateArray();
    std::vector<uint32_t> gunIdList;
    std::vector<uint8_t> gunTypeList;
    gunIdList.reserve(gunCount);
    gunTypeList.reserve(gunCount);

    const size_t gunsLen = static_cast<size_t>(gunCount) * 5U;
    if (pos + gunsLen > bodyLen) {
        cJSON_Delete(data);
        return false;
    }

    for (uint8_t i = 0; i < gunCount; ++i) {
        const uint32_t gunIdVal = readU32BE(body + pos);
        pos += 4;
        const uint8_t gunTypeVal = body[pos++];

        gunIdList.push_back(gunIdVal);
        gunTypeList.push_back(gunTypeVal);
        cJSON* gi = cJSON_CreateObject();
        cJSON_AddNumberToObject(gi, "gun", i);
        cJSON_AddNumberToObject(gi, "gunId", static_cast<double>(gunIdVal));
        cJSON_AddNumberToObject(gi, "gunType", gunTypeVal);
        cJSON_AddItemToArray(guns, gi);
    }
    cJSON_AddItemToObject(data, "guns", guns);

    // BY ZF: 运营商ID（9字节ASCII）位于枪配置之后；包长不足时置空字符串。
    std::string operatorId;
    if (pos + 9 <= bodyLen) {
        operatorId.reserve(9);
        for (size_t i = 0; i < 9; ++i) {
            const uint8_t b = body[pos + i];
            if (b == 0x00) {
                break;
            }
            operatorId.push_back(static_cast<char>(b));
        }
    }
    cJSON_AddStringToObject(data, "operatorId", operatorId.c_str());

    // BY ZF: 配置确认成功后，同步运行参数（用于后续登录/心跳上报）。
    if (feedbackResult == 0x00) {
        m_config.cdzNo = std::to_string(static_cast<unsigned long long>(cdzId));
        if (gunCount > 0) {
            m_config.gunCount = gunCount;
            m_config.gunIdList = gunIdList;
            m_config.gunTypeList = gunTypeList;
            m_gunStatus.assign(gunCount, 0x00);
            m_vehicleConnectStatus.assign(gunCount, 0x00);
            m_gunRuntimeData.assign(gunCount, GunRuntimeData());
            m_feeModelByGun.assign(gunCount, FeeModel());

            // BY ZF: setConfig 下发费率后，同步更新按枪模型缓存。
            FeeModel model;
            model.feeModelId = std::string("CFG_") + std::to_string(static_cast<unsigned long long>(cdzId));
            model.timeNum = numOfTimePeriods;
            for (uint8_t i = 0; i < numOfTimePeriods; ++i) {
                cJSON* seg = cJSON_GetArrayItem(timeSeg, i);
                cJSON* cf = cJSON_GetArrayItem(chargeFee, i);
                cJSON* sf = cJSON_GetArrayItem(serviceFee, i);
                if (cJSON_IsString(seg) && seg->valuestring) {
                    model.timeSeg.push_back(seg->valuestring);
                } else {
                    model.timeSeg.push_back("0000");
                }
                model.segFlag.push_back(0);
                const double cfYuan = cJSON_IsNumber(cf) ? cf->valuedouble : 0.0;
                const double sfYuan = cJSON_IsNumber(sf) ? sf->valuedouble : 0.0;
                model.chargeFee.push_back(static_cast<unsigned int>(std::max(0.0, cfYuan * 1000.0)));
                model.serviceFee.push_back(static_cast<unsigned int>(std::max(0.0, sfYuan * 1000.0)));
            }
            for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
                m_feeModelByGun[i] = model;
            }
        }
    }

    *outData = data;
    return true;
}

void CommProcess::processPlatformPacket(const uint8_t* frame, size_t frameLen)
{
    if (!frame || frameLen < 19) {
        return;
    }
    const uint16_t recvCrc = readU16BE(frame + frameLen - 2);
    const uint16_t calcCrc = calcCrc16Modbus(frame, frameLen - 2);
    if (recvCrc != calcCrc) {
        return;
    }
    const uint16_t totalLen = readU16BE(frame + 13);
    if (frameLen != static_cast<size_t>(4 + 1 + 8 + 2 + totalLen)) {
        return;
    }
    const uint8_t cmd = frame[16];
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frameLen << std::endl;
    }
    const size_t bodyLen = static_cast<size_t>(totalLen - 1 - 1 - 2);
    const uint8_t* body = (bodyLen > 0) ? (frame + 17) : nullptr;

    if (cmd == kCmdSetConfigAck) {
        if (m_loginState == LOGIN_REQ_SET_CONFIG) {
            const uint8_t feedbackResult = body[0];
            if (feedbackResult != 0x00 ) {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "set_config_ack_reject");
                return;
            }
            uint8_t gun = 0;
            cJSON* dataObj = nullptr;
            if (extractSetConfigData(cmd, body, bodyLen, gun, &dataObj)) {
                publishSetConfig(gun, dataObj);
                cJSON_Delete(dataObj);
                m_loginState = LOGIN_REQ_LOGIN;
                m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
                m_logSender.info("platform_login_step", "set_config_ack_ok");
            } else {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "set_config_ack_parse_fail");
            }
        }
        return;
    }

    if (cmd == kCmdLoginAck) {
        if (m_loginState == LOGIN_REQ_LOGIN) {
            if (!body || bodyLen < 1) {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "login_ack_invalid");
                return;
            }

            const uint8_t feedbackResult = body[0];
            if (feedbackResult != 0x00 || bodyLen < 11) {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "login_ack_invalid");
                return;
            }

            // BY ZF: 反馈成功时解析对时时间（BCD：YYYYMMDDHHmmssFF）。
            const uint8_t* bcdTime = body + 3;
            const int year = (((bcdTime[0] >> 4) & 0x0F) * 1000) +
                             ((bcdTime[0] & 0x0F) * 100) +
                             (((bcdTime[1] >> 4) & 0x0F) * 10) +
                             (bcdTime[1] & 0x0F);
            const int month = (((bcdTime[2] >> 4) & 0x0F) * 10) + (bcdTime[2] & 0x0F);
            const int day = (((bcdTime[3] >> 4) & 0x0F) * 10) + (bcdTime[3] & 0x0F);
            const int hour = (((bcdTime[4] >> 4) & 0x0F) * 10) + (bcdTime[4] & 0x0F);
            const int minute = (((bcdTime[5] >> 4) & 0x0F) * 10) + (bcdTime[5] & 0x0F);
            const int second = (((bcdTime[6] >> 4) & 0x0F) * 10) + (bcdTime[6] & 0x0F);

            char syncTime[32] = {0};
            std::snprintf(syncTime, sizeof(syncTime), "%04d-%02d-%02d %02d:%02d:%02d",
                          year, month, day, hour, minute, second);

            syncSystemTime(year, month, day, hour, minute, second);

            m_loginState = LOGIN_DONE;
            m_logSender.info("platform_login_step", std::string("login_ack_ok,time=") + syncTime);
        }
        return;
    }

    if (cmd == kCmdRemoteStart) {
        uint8_t gun = 0;
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        if (parseRemoteStart014(body, bodyLen, gun, &startData, parsedFeeModel)) {
            if (gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = parsedFeeModel;
            }
            if (!parsedFeeModel.feeModelId.empty()) {
                m_logSender.saveFeeModel(parsedFeeModel);
            }
            if (gun < m_gunRuntimeData.size()) {
                // BY ZF: 记录待回执启动控制命令，待状态流转触发0x04。
                m_gunRuntimeData[gun].pendingCtrlCmd = 0x00;
                m_gunRuntimeData[gun].pendingCtrlSince = std::chrono::steady_clock::now();
            }
            publishPlatCommand(gun, "start_charge", startData);
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteStop) {
        uint8_t gun = 0;
        cJSON* stopData = nullptr;
        if (parseRemoteStop015(body, bodyLen, gun, &stopData)) {
            if (gun < m_gunRuntimeData.size()) {
                // BY ZF: 记录待回执停止控制命令，待状态流转触发0x05。
                m_gunRuntimeData[gun].pendingCtrlCmd = 0x01;
                m_gunRuntimeData[gun].pendingCtrlSince = std::chrono::steady_clock::now();
            }
            publishPlatCommand(gun, "stop_charge", stopData);
        }
        if (stopData) {
            cJSON_Delete(stopData);
        }
        return;
    }

    if (cmd == kCmdRecordConfirm) {
        uint8_t gun = 0;
        cJSON* cfmData = nullptr;
        if (parseRecordConfirm070(body, bodyLen, gun, &cfmData)) {
            publishPlatCommand(gun, "record_cfm", cfmData);
        }
        if (cfmData) {
            cJSON_Delete(cfmData);
        }
        return;
    }

    // BY ZF: 未识别平台帧仅记录调试，不向MQTT发布。
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] ignored cmd=0x"
                  << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    }
}

bool CommProcess::parseRemoteStart014(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x14 最小长度：枪ID4 + 订单10 + 用户5 + 用户状态1 + 余额4 + 计费标志1 + 时段数1
    if (bodyLen < 26) {
        return false;
    }

    const uint32_t gunId = readU32BE(body);
    const uint8_t* orderBcd = body + 4;
    const uint8_t* userBcd = body + 14;
    const uint8_t userStatus = body[19];
    const uint32_t energyBlock = readU32BE(body + 20);  //预充金额
    const uint8_t billingFlag = body[24];
    const uint8_t periodCount = body[25];

    int gunIndex = -1;
    for (size_t i = 0; i < m_config.gunIdList.size(); ++i) {
        if (m_config.gunIdList[i] == gunId) {
            gunIndex = static_cast<int>(i);
            break;
        }
    }
    if (gunIndex < 0) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    const std::string orderNo = bcdToDigitString(orderBcd, 10);
    const std::string chargeUserNo = bcdToDigitString(userBcd, 5);

    feeModel.feeModelId = std::string("PLAT_") + orderNo.substr(0, orderNo.size() > 17 ? 17 : orderNo.size());
    feeModel.timeNum = periodCount;
    feeModel.timeSeg.clear();
    feeModel.segFlag.clear();
    feeModel.chargeFee.clear();
    feeModel.serviceFee.clear();
    feeModel.timeSeg.reserve(periodCount);
    feeModel.segFlag.reserve(periodCount);
    feeModel.chargeFee.reserve(periodCount);
    feeModel.serviceFee.reserve(periodCount);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "chargeUserNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
    cJSON_AddNumberToObject(data, "chargeMode", 2);
    cJSON_AddNumberToObject(data, "prechargeAmount", static_cast<double>(energyBlock) / 100.0);
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "billingFlag", billingFlag);
    cJSON_AddNumberToObject(data, "userStatus", userStatus);
    cJSON_AddNumberToObject(data, "timeNum", periodCount);

    cJSON* timeSeg = cJSON_CreateArray();
    cJSON* chargeFee = cJSON_CreateArray();
    cJSON* serviceFee = cJSON_CreateArray();

    const uint8_t* p = body + 26;
    for (uint8_t i = 0; i < periodCount; ++i) {
        const std::string startSeg = bcdHourToHhmm(p[0]);
        const std::string endSeg = bcdHourToHhmm(p[1]);
        const uint16_t chargeFeeCent = readU16BE(p + 2);
        const uint16_t serviceFeeCent = readU16BE(p + 4);

        feeModel.timeSeg.push_back(startSeg);
        feeModel.segFlag.push_back(0);
        feeModel.chargeFee.push_back(static_cast<unsigned int>(chargeFeeCent) * 10U);
        feeModel.serviceFee.push_back(static_cast<unsigned int>(serviceFeeCent) * 10U);

        cJSON_AddItemToArray(timeSeg, cJSON_CreateString(startSeg.c_str()));
        cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(static_cast<double>(chargeFeeCent) / 100.0));
        cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(static_cast<double>(serviceFeeCent) / 100.0));
        p += 6;

        (void)endSeg;
    }

    cJSON_AddItemToObject(data, "timeSeg", timeSeg);
    cJSON_AddItemToObject(data, "chargeFee", chargeFee);
    cJSON_AddItemToObject(data, "serviceFee", serviceFee);

    // BY ZF: 保存启动命令解析结果到每枪运行态缓存，供后续平台上送复用。
    if (gun < m_gunRuntimeData.size()) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        //本地时间转BCD码
        const std::time_t nowSec = std::time(nullptr);
        std::tm* tmv = std::localtime(&nowSec);
        const int year = tmv ? (tmv->tm_year + 1900) : 2026;
        rd.startTimeBcd[0] = bcdByte((year / 100) % 100);
        rd.startTimeBcd[1] = bcdByte(year % 100);
        rd.startTimeBcd[2] = bcdByte((tmv ? tmv->tm_mon + 1 : 1));
        rd.startTimeBcd[3] = bcdByte((tmv ? tmv->tm_mday : 1));
        rd.startTimeBcd[4] = bcdByte((tmv ? tmv->tm_hour : 0));
        rd.startTimeBcd[5] = bcdByte((tmv ? tmv->tm_min : 0));
        rd.startTimeBcd[6] = bcdByte((tmv ? tmv->tm_sec : 0));
        rd.startTimeBcd[7] = 0xFF;

        rd.chargeUserNo = chargeUserNo;
        rd.orderNo = orderNo;
        rd.chargeMode = 2;
        rd.prechargeAmount = static_cast<double>(energyBlock) / 100.0;
        rd.userStatus = static_cast<int>(userStatus);
        rd.billingFlag = billingFlag;
        rd.feeModelId = feeModel.feeModelId;
        rd.feeTimeNum = static_cast<int>(periodCount);
        rd.feeSegments.clear();
        rd.feeSegments.reserve(periodCount);
        for (uint8_t i = 0; i < periodCount; ++i) {
            FeeSegmentData seg;
            seg.startTs = feeModel.timeSeg[i];
            seg.endTs = (i + 1 < periodCount) ? feeModel.timeSeg[i + 1] : "2400";
            seg.energyKwh = 0.0;
            seg.electricAmount = 0.0;
            seg.serviceAmount = 0.0;
            rd.feeSegments.push_back(seg);
        }
    }

    *outData = data;
    return true;
}

bool CommProcess::parseRemoteStop015(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x15 最小长度：枪ID4 + 订单10 + 操作类型1
    if (bodyLen < 15) {
        return false;
    }

    const uint32_t gunId = readU32BE(body);
    const uint8_t* orderBcd = body + 4;
    const uint8_t operationType = body[14];
    if (operationType != 0x01) {
        return false;
    }

    int gunIndex = -1;
    for (size_t i = 0; i < m_config.gunIdList.size(); ++i) {
        if (m_config.gunIdList[i] == gunId) {
            gunIndex = static_cast<int>(i);
            break;
        }
    }
    if (gunIndex < 0) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    const std::string orderNo = bcdToDigitString(orderBcd, 10);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
    cJSON_AddNumberToObject(data, "stopReason", 0x01);  // BY ZF: 按要求固定停机原因 0x01
    cJSON_AddNumberToObject(data, "tcuStopCode", 0);
    *outData = data;
    return true;
}

bool CommProcess::parseRecordConfirm070(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x70 最小长度：枪ID4 + 反馈结果1
    if (bodyLen < 5) {
        return false;
    }

    const uint32_t gunId = readU32BE(body);
    const uint8_t feedbackResult = body[4];
    const bool ackOk = (feedbackResult == 0x00 || feedbackResult == 0x04);

    int gunIndex = -1;
    for (size_t i = 0; i < m_config.gunIdList.size(); ++i) {
        if (m_config.gunIdList[i] == gunId) {
            gunIndex = static_cast<int>(i);
            break;
        }
    }
    if (gunIndex < 0) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    // BY ZF: 0x70协议不带订单号，使用按枪缓存的最近上送tradeNo。
    std::string tradeNo;
    if (gun < m_gunRuntimeData.size()) {
        tradeNo = m_gunRuntimeData[gun].pendingRecordTradeNo;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "gunId", static_cast<double>(gunId));
    cJSON_AddNumberToObject(data, "confirmFlag", ackOk ? 1 : 0);
    cJSON_AddNumberToObject(data, "result", feedbackResult);
    if (!tradeNo.empty()) {
        cJSON_AddStringToObject(data, "tradeNo", tradeNo.c_str());
    }
    *outData = data;

    // BY ZF: 直接完成交易记录确认
    if (ackOk) {
        m_logSender.confirmTradeRecord(tradeNo, 1);
    }

    // BY ZF: 成功应答后清理缓存，避免后续回包误关联旧记录。
    if (ackOk && gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo.clear();
    }
    return true;
}

bool CommProcess::publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj)
{
    if (!cmd || !dataObj) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddItemToObject(root, "data", cJSON_Duplicate(dataObj, 1));

    char* out = cJSON_PrintUnformatted(root);
    std::string payload;
    if (out) {
        payload = out;
        free(out);
    }
    cJSON_Delete(root);
    if (payload.empty()) {
        return false;
    }
    return publishPlatCmd(gun, payload);
}

bool CommProcess::publishSetConfig(uint8_t gun, cJSON* data)
{
    if (!data) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "type", "setConfig");
    cJSON_AddItemToObject(root, "data", cJSON_Duplicate(data, 1));

    char* out = cJSON_PrintUnformatted(root);
    std::string payload;
    if (out) {
        payload = out;
        free(out);
    }
    cJSON_Delete(root);
    if (payload.empty()) {
        return false;
    }
    const std::string topic = buildTopic("plat", gun, "event");
    return m_mqtt.publish(topic, payload, 1, true);
}

bool CommProcess::isHexString(const std::string& s, size_t needLen) const
{
    if (s.size() != needLen) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}
