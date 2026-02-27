/**
 * ChargeLogic process
 * BY ZF
 */

#include "charge_logic_process.h"
#include "../base/common/message_queue.h"
#include "../base/logger/log_sender.h"
#include "../base/cjson/include/cjson/cJSON.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

namespace {
    bool parseTopicGun(const std::string& topic, const std::string& prefix, uint8_t& gunOut, std::string& tailOut) {
        if (topic.find(prefix) != 0) {
            return false;
        }
        std::string rest = topic.substr(prefix.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            return false;
        }
        std::string gunStr = rest.substr(0, slash);
        tailOut = rest.substr(slash + 1);
        gunOut = static_cast<uint8_t>(std::stoi(gunStr));
        return true;
    }

    const char* getString(cJSON* obj, const char* key) {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsString(v)) {
            return v->valuestring;
        }
        return "";
    }

    bool getNumber(cJSON* obj, const char* key, double& out) {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v)) {
            out = v->valuedouble;
            return true;
        }
        return false;
    }

    std::string minuteToHHMM(int minuteOfDay) {
        if (minuteOfDay < 0) {
            minuteOfDay = 0;
        }
        if (minuteOfDay > 1440) {
            minuteOfDay = 1440;
        }
        if (minuteOfDay == 1440) {
            return "2400";
        }
        int hh = minuteOfDay / 60;
        int mm = minuteOfDay % 60;
        char buf[5];
        std::snprintf(buf, sizeof(buf), "%02d%02d", hh, mm);
        return std::string(buf);
    }

    // BY ZF: feeData 数值统一保留 5 位小数，抑制浮点尾差
    double roundTo5(double v) {
        return std::round(v * 100000.0) / 100000.0;
    }

    // BY ZF: Unix 秒时间戳转 YYYYMMDDHHMMSS 数字，便于交易表直接检索
    uint64_t toYmdHmsNumber(std::time_t tsSec) {
        std::tm tmv;
        localtime_r(&tsSec, &tmv);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return static_cast<uint64_t>(std::strtoull(buf, nullptr, 10));
    }

    int readGunCountFromIni(const std::string& path) {
        std::ifstream in(path.c_str());
        if (!in.is_open()) {
            return -1;
        }
        std::string line;
        std::string section;
        while (std::getline(in, line)) {
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                continue;
            }
            if (trimmed.front() == '[' && trimmed.back() == ']') {
                section = trimmed.substr(1, trimmed.size() - 2);
                continue;
            }
            if (section != "PileController") {
                continue;
            }
            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = trimmed.substr(0, eq);
            std::string val = trimmed.substr(eq + 1);
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            if (key == "gun_count") {
                try {
                    return std::stoi(val);
                } catch (...) {
                    return -1;
                }
            }
        }
        return -1;
    }

    // BY ZF: 将上层启动数据映射为桩侧启动帧参数
    cJSON* buildPileStartData(cJSON* src)
    {
        cJSON* out = cJSON_CreateObject();
        // BY ZF: 默认值与 CAN 启动帧保持一致
        uint8_t loadControlSwitch = 0x02;
        uint8_t plugAndChargeFlag = 0x01;
        uint8_t auxPowerVoltage = 0x0C;
        uint8_t mergeChargeFlag = 0x00; // 0: 非合并充电, 1: 合并充电（后续由 pile 侧实现）

        if (src) {
            cJSON* v = cJSON_GetObjectItem(src, "loadControlSwitch");
            if (cJSON_IsNumber(v)) loadControlSwitch = static_cast<uint8_t>(v->valueint);
            v = cJSON_GetObjectItem(src, "plugAndChargeFlag");
            if (cJSON_IsNumber(v)) plugAndChargeFlag = static_cast<uint8_t>(v->valueint);
            v = cJSON_GetObjectItem(src, "auxPowerVoltage");
            if (cJSON_IsNumber(v)) auxPowerVoltage = static_cast<uint8_t>(v->valueint);

            // BY ZF: 合并充电标志兼容多个命名
            v = cJSON_GetObjectItem(src, "mergeChargeFlag");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(src, "mergedChargeFlag");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(src, "combineChargeFlag");
            if (cJSON_IsNumber(v)) mergeChargeFlag = static_cast<uint8_t>(v->valueint);
        }

        cJSON_AddNumberToObject(out, "loadControlSwitch", loadControlSwitch);
        cJSON_AddNumberToObject(out, "plugAndChargeFlag", plugAndChargeFlag);
        cJSON_AddNumberToObject(out, "auxPowerVoltage", auxPowerVoltage);
        cJSON_AddNumberToObject(out, "mergeChargeFlag", mergeChargeFlag);
        return out;
    }
}

const char* ChargeLogicProcess::stateToString(ChargeLogicProcess::ChargeState s)
{
    switch (s) {
    case ChargeLogicProcess::STATE_IDLE: return "IDLE";
    case ChargeLogicProcess::STATE_PREPARE: return "PREPARE";
    case ChargeLogicProcess::STATE_STARTING: return "STARTING";
    case ChargeLogicProcess::STATE_CHARGING: return "CHARGING";
    case ChargeLogicProcess::STATE_STOPPING: return "STOPPING";
    case ChargeLogicProcess::STATE_STOPPED: return "STOPPED";
    case ChargeLogicProcess::STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

ChargeLogicProcess::ChargeLogicProcess()
    : BaseProcess(PROC_TCU_LOGIC, "tcu_logic")
    , m_logSender("tcu_logic")
    , m_seq(0)
{
}

ChargeLogicProcess::~ChargeLogicProcess()
{
    doCleanup();
}

bool ChargeLogicProcess::doInitialize()
{
    // BY ZF: load config
    if (!loadConfig()) {
        return false;
    }

    // BY ZF: init mqtt
    if (!initMqtt()) {
        return false;
    }

    m_gunStates.clear();
    m_gunStates.resize(m_config.gunCount);
    // BY ZF: 初始化完成后写入一条 info 日志，便于联调确认进程已就绪
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void ChargeLogicProcess::doRun()
{
    m_running = true;
    m_mainThread = std::thread([this]() {
        while (m_running.load()) {
            // BY ZF: STOPPING 定时重发与结算窗口
            auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < m_gunStates.size(); i++) {
                GunState& gs = m_gunStates[i];
                if (gs.state == STATE_CHARGING) {
                    maybeTriggerTcuStopByPrecharge(static_cast<uint8_t>(i));
                }
                if (gs.state == STATE_STARTING) {
                    if (gs.startingEnterTime.time_since_epoch().count() != 0) {
                        const auto elapsed = now - gs.startingEnterTime;
                        // BY ZF: STARTING 持续 30s 未完成，重发一次启动命令
                        if (!gs.startingRetrySent && elapsed >= std::chrono::seconds(30)) {
                            cJSON* retryData = nullptr;
                            if (!gs.lastStartCmdData.empty()) {
                                retryData = cJSON_Parse(gs.lastStartCmdData.c_str());
                            }
                            if (retryData) {
                                publishPileCmd(static_cast<uint8_t>(i), "start_charge", retryData);
                                cJSON_Delete(retryData);
                            } else {
                                cJSON* pileStartData = buildPileStartData(nullptr);
                                publishPileCmd(static_cast<uint8_t>(i), "start_charge", pileStartData);
                                cJSON_Delete(pileStartData);
                            }
                            gs.startingRetrySent = true;
                        }
                        // BY ZF: STARTING 持续 60s 未完成，进入停机流程
                        if (elapsed >= std::chrono::seconds(60)) {
                            handleEvent(static_cast<uint8_t>(i), EVT_STOP_CMD, "starting_timeout_60s");
                            continue;
                        }
                    }
                }
                if (gs.state == STATE_STOPPING) {
                    // BY ZF: 收到 stop_complete 后停止重发 stop_charge
                    if (!gs.stopCompleteSeen) {
                        if (gs.lastStopCmdTime.time_since_epoch().count() == 0 ||
                            now - gs.lastStopCmdTime >= std::chrono::seconds(2)) {
                            publishPileCmd(static_cast<uint8_t>(i), "stop_charge", nullptr);
                            gs.lastStopCmdTime = now;
                        }
                    }
                    // BY ZF: 进入 STOPPING 后固定等待 15s，或电表连续3次无变化提前收敛
                    if (gs.hasMeterValue && gs.meterStableCount >= 3) {
                        handleEvent(static_cast<uint8_t>(i), EVT_METER_STALE, "meter_stable_3x");
                        continue;
                    }
                    if (gs.stoppingEnterTime.time_since_epoch().count() != 0 &&
                        now - gs.stoppingEnterTime >= std::chrono::seconds(15)) {
                        handleEvent(static_cast<uint8_t>(i), EVT_METER_STALE, "stopping_settle_15s");
                        continue;
                    }
                    // BY ZF: 兜底保护，避免 STOPPING 长时间滞留
                    if (gs.stoppingEnterTime.time_since_epoch().count() != 0 &&
                        now - gs.stoppingEnterTime >= std::chrono::seconds(30)) {
                        handleEvent(static_cast<uint8_t>(i), EVT_METER_STALE, "stopping_force_timeout_30s");
                    }
                }
            }
            // BY ZF: doRun 主循环节流 10ms
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    m_mainThread.join();
}

void ChargeLogicProcess::doCleanup()
{
    m_running = false;
    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

bool ChargeLogicProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    m_config.mqttHost = cfg.getString("ChargeLogic", "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt("ChargeLogic", "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt("ChargeLogic", "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString("ChargeLogic", "mqtt_client_id", "tcu_logic");
    m_config.mqttTopicPrefix = cfg.getString("ChargeLogic", "mqtt_topic_prefix", "tcu");
    m_config.gunCount = static_cast<uint8_t>(cfg.getInt("ChargeLogic", "gun_count", 1));
    m_config.pileConfigPath = cfg.getString("ChargeLogic", "pile_config_path", "");
    m_config.prechargeStopMargin = std::atof(
        cfg.getString("ChargeLogic", "precharge_stop_margin", "1.0").c_str());

    if (!m_config.pileConfigPath.empty()) {
        int configuredGunCount = readGunCountFromIni(m_config.pileConfigPath);
        if (configuredGunCount > 0) {
            m_config.gunCount = static_cast<uint8_t>(configuredGunCount);
        }
    }

    return true;
}

bool ChargeLogicProcess::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        return false;
    }

    m_mqtt.setConnectHandler([this](int rc) {
        if (rc != 0) {
            return;
        }
        for (uint8_t gun = 0; gun < m_config.gunCount; gun++) {
            std::ostringstream t1, t2, t3, t4, t5;
            t1 << m_config.mqttTopicPrefix << "/logic/" << static_cast<int>(gun) << "/cmd";
            t2 << m_config.mqttTopicPrefix << "/plat/" << static_cast<int>(gun) << "/cmd";
            t3 << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gun) << "/event";
            t4 << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gun) << "/data";
            t5 << m_config.mqttTopicPrefix << "/meter/" << static_cast<int>(gun) << "/data";
            m_mqtt.subscribe(t1.str(), 1);
            m_mqtt.subscribe(t2.str(), 1);
            m_mqtt.subscribe(t3.str(), 2);
            m_mqtt.subscribe(t4.str(), 0);
            m_mqtt.subscribe(t5.str(), 0);
        }
    });

    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        return false;
    }

    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    });

    return m_mqtt.loopStart();
}

void ChargeLogicProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    uint8_t gun = 0;
    std::string tail;

    std::string prefixLogic = m_config.mqttTopicPrefix + "/logic/";
    std::string prefixPlat = m_config.mqttTopicPrefix + "/plat/";
    std::string prefixPile = m_config.mqttTopicPrefix + "/pile/";

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    if (parseTopicGun(topic, prefixLogic, gun, tail) && tail == "cmd") {
        const char* cmd = getString(root, "cmd");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handleLogicCmd(gun, cmd, data);
    } else if (parseTopicGun(topic, prefixPlat, gun, tail) && tail == "cmd") {
        const char* cmd = getString(root, "cmd");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handlePlatCmd(gun, cmd, data);
    } else if (parseTopicGun(topic, prefixPile, gun, tail)) {
        if (tail == "event") {
            const char* type = getString(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handlePileEvent(gun, type, data);
        } else if (tail == "data") {
            const char* type = getString(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handlePileData(gun, type, data);
        }
    } else if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/meter/", gun, tail) && tail == "data") {
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handleMeterData(gun, data);
    }

    cJSON_Delete(root);
}

void ChargeLogicProcess::handleLogicCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    if (cmd == "start_charge") {
        // BY ZF: 测试阶段默认鉴权通过，收到 start_charge 即进入启动流程
        GunState& gs = m_gunStates[gun];
        if (gs.state == STATE_PREPARE) {
            updateAuthBasis(gun, data, "hmi");
            gs.pendingStart = true;
            gs.pendingStartData.clear();
            // BY ZF: 缓存“桩侧启动帧参数”，避免透传上层业务字段到 pile
            cJSON* pileStartData = buildPileStartData(data);
            if (pileStartData) {
                char* out = cJSON_PrintUnformatted(pileStartData);
                if (out) {
                    gs.pendingStartData = out;
                    cJSON_free(out);
                }
                cJSON_Delete(pileStartData);
            }
            handleEvent(gun, EVT_START_CMD, "start_cmd");
            handleEvent(gun, EVT_AUTH_OK, "hmi_start_auto_auth_ok");
        } else {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", "start_charge");
            cJSON_AddStringToObject(evt, "state", stateToString(gs.state));
            cJSON_AddStringToObject(evt, "reason", "start_only_allowed_in_prepare");
            publishLogicEvent(gun, "cmd_reject", evt);
            cJSON_Delete(evt);
        }
        return;
    }

    if (cmd == "stop_charge") {
        handleEvent(gun, EVT_STOP_CMD, "stop_cmd");
        return;
    }

    if (cmd == "reset_error") {
        handleEvent(gun, EVT_RESET_ERROR, "reset_error");
        return;
    }
}

void ChargeLogicProcess::handlePlatCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    if (cmd == "start_charge") {
        // BY ZF: platform 启动流程，测试阶段默认鉴权通过
        if (gs.state == STATE_PREPARE) {
            updateAuthBasis(gun, data, "platform");
            gs.pendingStart = true;
            gs.pendingStartData.clear();
            // BY ZF: 缓存“桩侧启动帧参数”，避免透传上层业务字段到 pile
            cJSON* pileStartData = buildPileStartData(data);
            if (pileStartData) {
                char* out = cJSON_PrintUnformatted(pileStartData);
                if (out) {
                    gs.pendingStartData = out;
                    cJSON_free(out);
                }
                cJSON_Delete(pileStartData);
            }
            handleEvent(gun, EVT_START_CMD, "plat_start_cmd");
            handleEvent(gun, EVT_AUTH_OK, "plat_start_auto_auth_ok");
        } else {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", "start_charge");
            cJSON_AddStringToObject(evt, "state", stateToString(gs.state));
            cJSON_AddStringToObject(evt, "reason", "start_only_allowed_in_prepare");
            publishLogicEvent(gun, "cmd_reject", evt);
            cJSON_Delete(evt);
        }
        return;
    }

    if (cmd == "auth_result") {
        int result = 0;
        if (data) {
            cJSON* v = cJSON_GetObjectItem(data, "result");
            if (v && cJSON_IsNumber(v)) {
                result = v->valueint;
            }
        }
        // BY ZF: 鉴权结果约定：1=通过，其他=失败
        if (result == 1) {
            // BY ZF: 使用鉴权回参覆盖启动参数，确保以下发到 pile 的参数为准
            if (data && gs.pendingStart) {
                cJSON* pileStartData = buildPileStartData(data);
                if (pileStartData) {
                    char* out = cJSON_PrintUnformatted(pileStartData);
                    if (out) {
                        gs.pendingStartData = out;
                        cJSON_free(out);
                    }
                    cJSON_Delete(pileStartData);
                }
            }
            handleEvent(gun, EVT_AUTH_OK, "auth_ok");
        } else {
            handleEvent(gun, EVT_AUTH_FAIL, "auth_fail");
        }
        return;
    }

    // BY ZF: fallback to logic cmd
    handleLogicCmd(gun, cmd, data);
}

void ChargeLogicProcess::handlePileEvent(uint8_t gun, const std::string& type, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    if (type == "start_complete") {
        bool ok = true;
        if (data) {
            cJSON* v = cJSON_GetObjectItem(data, "successFlag");
            if (v && cJSON_IsNumber(v) && v->valueint != 0) {
                ok = false;
            }
            cJSON* soc = cJSON_GetObjectItem(data, "soc");
            if (soc && cJSON_IsNumber(soc)) {
                // BY ZF: start_complete.soc 协议量纲为 0.1%，这里统一换算成 %
                m_gunStates[gun].startSoc = soc->valuedouble / 10.0;
            }
            const char* vin = getString(data, "vin");
            if (vin && vin[0]) {
                m_gunStates[gun].vinCode = vin;
                m_gunStates[gun].hasVinCode = true;
            }
        }
        if (ok) {
            handleEvent(gun, EVT_START_COMPLETE_OK, "start_complete_ok");
        } else {
            handleEvent(gun, EVT_START_COMPLETE_FAIL, "start_complete_fail");
        }
        return;
    }

    if (type == "stop_complete") {
        if (data) {
            cJSON* r = cJSON_GetObjectItem(data, "stopReason");
            if (r && cJSON_IsNumber(r)) {
                m_gunStates[gun].stopReason = static_cast<unsigned int>(r->valueint);
            }
            cJSON* soc = cJSON_GetObjectItem(data, "stopSoc");
            if (soc && cJSON_IsNumber(soc)) {
                // BY ZF: stop_complete.stopSoc 协议量纲为 %，直接使用
                m_gunStates[gun].endSoc = soc->valuedouble;
            }
        }
        handleEvent(gun, EVT_STOP_COMPLETE, "stop_complete");
        return;
    }

    if (type == "start_response") {
        if (data) {
            cJSON* v = cJSON_GetObjectItem(data, "confirmFlag");
            if (v && cJSON_IsNumber(v) && v->valueint != 0) {
                handleEvent(gun, EVT_START_RESPONSE_FAIL, "start_response_fail");
                return;
            }
        }
        handleEvent(gun, EVT_START_RESPONSE_OK, "start_response_ok");
        return;
    }

    if (type == "stop_response") {
        return;
    }
    if (type == "deviceErr_on" || type == "deviceErr") {
        handleEvent(gun, EVT_DEVICE_ERR, "deviceErr");
        return;
    }
    if (type == "deviceErr_off") {
        handleEvent(gun, EVT_RESET_ERROR, "deviceErr_off");
        return;
    }
}

void ChargeLogicProcess::handlePileData(uint8_t gun, const std::string& type, cJSON* data)
{
    if (gun >= m_gunStates.size() || !data) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    if (type == "yx") {
        cJSON* ws = cJSON_GetObjectItem(data, "workStatus");
        cJSON* vc = cJSON_GetObjectItem(data, "vehicleConnectStatus");
        cJSON* tf = cJSON_GetObjectItem(data, "totalFault");
        if (ws && cJSON_IsNumber(ws)) {
            gs.lastWorkStatus = static_cast<uint8_t>(ws->valueint);
            gs.hasWorkStatus = true;
        }
        if (vc && cJSON_IsNumber(vc)) {
            gs.lastVehicleConnectStatus = static_cast<uint8_t>(vc->valueint);
            gs.hasVehicleConnectStatus = true;
        }
        if (tf && cJSON_IsNumber(tf)) {
            gs.lastTotalFault = static_cast<uint8_t>(tf->valueint);
            gs.hasTotalFault = true;
        }

        if (gs.hasWorkStatus && gs.lastWorkStatus == 0 && gs.state == STATE_CHARGING) {
            handleEvent(gun, EVT_WORKSTATUS_ZERO, "workStatus_zero");
        }

        if (gs.hasVehicleConnectStatus) {
            if (gs.state == STATE_IDLE && gs.lastVehicleConnectStatus != 0 && (!gs.hasTotalFault || gs.lastTotalFault == 0)) {
                handleEvent(gun, EVT_VEHICLE_CONNECTED, "vehicle_connected");
            } else if (gs.state == STATE_PREPARE && gs.lastVehicleConnectStatus == 0) {
                handleEvent(gun, EVT_VEHICLE_DISCONNECTED, "vehicle_disconnected");
            } else if (gs.state == STATE_STOPPED && gs.lastVehicleConnectStatus == 0) {
                handleEvent(gun, EVT_VEHICLE_DISCONNECTED, "vehicle_disconnected");
            }
        }

        if (gs.hasTotalFault && gs.lastTotalFault != 0) {
            handleEvent(gun, EVT_TOTAL_FAULT, "total_fault");
        }
        return;
    }

}

void ChargeLogicProcess::handleMeterData(uint8_t gun, cJSON* data)
{
    if (gun >= m_gunStates.size() || !data) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    auto now = std::chrono::steady_clock::now();
    gs.lastMeterMsgTime = now;

    double totalEnergyKwh = 0.0;
    bool hasEnergy = false;
    if (getNumber(data, "energy", totalEnergyKwh) ||
        getNumber(data, "totalEnergy", totalEnergyKwh) ||
        getNumber(data, "total_kwh", totalEnergyKwh)) {
        hasEnergy = true;
        if (!gs.hasMeterValue) {
            gs.lastMeterValue = totalEnergyKwh;
            gs.lastMeterValueTime = now;
            gs.hasMeterValue = true;
            gs.meterStableCount = 0;
        } else if (totalEnergyKwh != gs.lastMeterValue) {
            gs.lastMeterValue = totalEnergyKwh;
            gs.lastMeterValueTime = now;
            gs.meterStableCount = 0;
        } else {
            if (gs.meterStableCount < 255) {
                gs.meterStableCount++;
            }
        }
    }

    // BY ZF: meter 模块仅提供电量/电压/电流，金额由计费逻辑独立产生

    // BY ZF: 电表电压兼容字段解析
    double voltage = 0.0;
    if (getNumber(data, "voltage", voltage) ||
        getNumber(data, "meterVoltage", voltage) ||
        getNumber(data, "outputVoltage", voltage)) {
        gs.lastMeterVoltage = voltage;
        gs.hasMeterVoltage = true;
    }

    // BY ZF: 电表电流兼容字段解析
    double current = 0.0;
    if (getNumber(data, "current", current) ||
        getNumber(data, "meterCurrent", current) ||
        getNumber(data, "outputCurrent", current)) {
        gs.lastMeterCurrent = current;
        gs.hasMeterCurrent = true;
    }

    // BY ZF: 充电中/停止中按电量增量计算计费
    if (hasEnergy && (gs.state == STATE_CHARGING || gs.state == STATE_STOPPING) && gs.feeInitialized) {
        if (!gs.feeHasEnergyBase) {
            gs.feeEnergyBaseKwh = totalEnergyKwh;
            gs.feeLastEnergyKwh = totalEnergyKwh;
            gs.feeHasEnergyBase = true;
        } else {
            double deltaKwh = totalEnergyKwh - gs.feeLastEnergyKwh;
            if (deltaKwh > 0.0) {
                applyEnergyDeltaToFee(gun, deltaKwh);
                publishFeeData(gun);
            }
            gs.feeLastEnergyKwh = totalEnergyKwh;
        }
    }
}

void ChargeLogicProcess::publishPileCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gun) << "/cmd";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_logic");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", cmd.c_str());
    if (data) {
        // BY ZF: 启动命令发送前兜底重映射，禁止透传上层业务字段
        if (cmd == "start_charge") {
            cJSON* mapped = buildPileStartData(data);
            cJSON_AddItemToObject(root, "data", mapped);
        } else {
            cJSON_AddItemToObject(root, "data", cJSON_Duplicate(data, 1));
        }
    }
    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        m_mqtt.publish(topic.str(), out, 2, false);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void ChargeLogicProcess::publishLogicEvent(uint8_t gun, const std::string& event, cJSON* data)
{
    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/logic/" << static_cast<int>(gun) << "/event";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_logic");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "event", event.c_str());
    if (data) {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(data, 1));
    }
    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        m_mqtt.publish(topic.str(), out, 1, false);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void ChargeLogicProcess::publishFeeData(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    const GunState& gs = m_gunStates[gun];
    if (!gs.feeInitialized) {
        return;
    }

    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/logic/" << static_cast<int>(gun) << "/feeData";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_logic");
    cJSON_AddNumberToObject(root, "gun", gun);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "totalEnergy", roundTo5(gs.feeTotalEnergyKwh));
    cJSON_AddNumberToObject(data, "totalAmount", roundTo5(gs.feeTotalElectricAmount + gs.feeTotalServiceAmount));
    cJSON_AddNumberToObject(data, "electicAmount", roundTo5(gs.feeTotalElectricAmount));
    cJSON_AddNumberToObject(data, "serviceAmount", roundTo5(gs.feeTotalServiceAmount));
    cJSON_AddNumberToObject(data, "feeModelNo", gs.feeModelNo);
    cJSON_AddStringToObject(data, "feeModelId", gs.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "sgemtentNum", gs.feeTimeNum);

    cJSON* segArr = cJSON_CreateArray();
    for (size_t i = 0; i < gs.feeTimeSegMinutes.size(); i++) {
        cJSON* seg = cJSON_CreateObject();
        int startMinute = gs.feeTimeSegMinutes[i];
        int endMinute = (i + 1 < gs.feeTimeSegMinutes.size()) ? gs.feeTimeSegMinutes[i + 1] : 1440;
        cJSON_AddStringToObject(seg, "startTs", minuteToHHMM(startMinute).c_str());
        cJSON_AddStringToObject(seg, "endTs", minuteToHHMM(endMinute).c_str());
        cJSON_AddNumberToObject(seg, "energyKwh", roundTo5((i < gs.feeSegEnergyKwh.size()) ? gs.feeSegEnergyKwh[i] : 0.0));
        cJSON_AddNumberToObject(seg, "electicAmount", roundTo5((i < gs.feeSegElectricAmount.size()) ? gs.feeSegElectricAmount[i] : 0.0));
        cJSON_AddNumberToObject(seg, "serviceAmount", roundTo5((i < gs.feeSegServiceAmount.size()) ? gs.feeSegServiceAmount[i] : 0.0));
        cJSON_AddItemToArray(segArr, seg);
    }
    cJSON_AddItemToObject(data, "segmentsAmount", segArr);

    cJSON_AddItemToObject(root, "data", data);

    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        m_mqtt.publish(topic.str(), out, 1, false);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void ChargeLogicProcess::publishStateChange(uint8_t gun, ChargeState from, ChargeState to, const char* reason)
{
    // BY ZF: 统一状态变更事件
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "from", stateToString(from));
    cJSON_AddStringToObject(data, "to", stateToString(to));
    if (reason) {
        cJSON_AddStringToObject(data, "reason", reason);
    }
    publishLogicEvent(gun, "state_change", data);
    cJSON_Delete(data);
}

void ChargeLogicProcess::publishUpdateRecordEvent(uint8_t gun, const TradeRecord& rec)
{
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "gunNo", rec.gunNo);
    cJSON_AddStringToObject(data, "preTradeNo", rec.preTradeNo.c_str());
    cJSON_AddStringToObject(data, "tradeNo", rec.tradeNo.c_str());
    cJSON_AddStringToObject(data, "vinCode", rec.vinCode.c_str());
    cJSON_AddNumberToObject(data, "timeDivType", rec.timeDivType);
    cJSON_AddNumberToObject(data, "startType", rec.startType);
    cJSON_AddNumberToObject(data, "chargeStartTime", static_cast<double>(rec.chargeStartTime));
    cJSON_AddNumberToObject(data, "chargeEndTime", static_cast<double>(rec.chargeEndTime));
    cJSON_AddNumberToObject(data, "startSoc", roundTo5(rec.startSoc));
    cJSON_AddNumberToObject(data, "endSoc", roundTo5(rec.endSoc));
    cJSON_AddNumberToObject(data, "reason", rec.reason);
    cJSON_AddStringToObject(data, "feeModelId", rec.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "sumStart", roundTo5(rec.sumStart));
    cJSON_AddNumberToObject(data, "sumEnd", roundTo5(rec.sumEnd));
    cJSON_AddNumberToObject(data, "totalElect", roundTo5(rec.totalElect));
    cJSON_AddNumberToObject(data, "totalPowerCost", roundTo5(rec.totalPowerCost));
    cJSON_AddNumberToObject(data, "totalServCost", roundTo5(rec.totalServCost));
    cJSON_AddNumberToObject(data, "totalCost", roundTo5(rec.totalCost));
    cJSON_AddNumberToObject(data, "timeNum", rec.timeNum);
    cJSON_AddNumberToObject(data, "startPoint", rec.startPoint);
    cJSON_AddNumberToObject(data, "crossPoints", rec.crossPoints);
    cJSON_AddStringToObject(data, "cardNumber", rec.cardNumber.c_str());

    // BY ZF: 上送分时段明细，便于平台侧直接生成账单。
    cJSON* partElectArr = cJSON_CreateArray();
    for (size_t i = 0; i < rec.partElect.size(); ++i) {
        cJSON_AddItemToArray(partElectArr, cJSON_CreateNumber(roundTo5(rec.partElect[i])));
    }
    cJSON_AddItemToObject(data, "partElect", partElectArr);

    cJSON* chargeFeeArr = cJSON_CreateArray();
    for (size_t i = 0; i < rec.chargeFee.size(); ++i) {
        cJSON_AddItemToArray(chargeFeeArr, cJSON_CreateNumber(roundTo5(rec.chargeFee[i])));
    }
    cJSON_AddItemToObject(data, "chargeFee", chargeFeeArr);

    cJSON* serviceFeeArr = cJSON_CreateArray();
    for (size_t i = 0; i < rec.serviceFee.size(); ++i) {
        cJSON_AddItemToArray(serviceFeeArr, cJSON_CreateNumber(roundTo5(rec.serviceFee[i])));
    }
    cJSON_AddItemToObject(data, "serviceFee", serviceFeeArr);

    cJSON* pointsElectArr = cJSON_CreateArray();
    for (size_t i = 0; i < rec.pointsElect.size(); ++i) {
        cJSON_AddItemToArray(pointsElectArr, cJSON_CreateNumber(roundTo5(rec.pointsElect[i])));
    }
    cJSON_AddItemToObject(data, "pointsElect", pointsElectArr);

    publishLogicEvent(gun, "update_record", data);
    cJSON_Delete(data);
}

void ChargeLogicProcess::updateAuthBasis(uint8_t gun, cJSON* data, const char* source)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    uint64_t startTs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::string userNo;
    std::string orderNo;
    std::string preTradeNo;
    int chargeMode = 0;
    double prechargeAmount = 0.0;
    int feeModelNo = 0;

    if (data) {
        cJSON* n = cJSON_GetObjectItem(data, "startTime");
        if (n && cJSON_IsNumber(n) && n->valuedouble > 0) {
            startTs = static_cast<uint64_t>(n->valuedouble);
        }

        const char* u = getString(data, "chargeUserNo");
        if (!u || !u[0]) u = getString(data, "userNo");
        if (!u || !u[0]) u = getString(data, "userId");
        if (u && u[0]) userNo = u;

        const char* o = getString(data, "orderNo");
        if (!o || !o[0]) o = getString(data, "chargeOrderNo");
        if (o && o[0]) orderNo = o;
        const char* pt = getString(data, "preTradeNo");
        if (pt && pt[0]) preTradeNo = pt;

        cJSON* m = cJSON_GetObjectItem(data, "chargeMode");
        if (m && cJSON_IsNumber(m)) {
            chargeMode = m->valueint;
        }

        cJSON* p = cJSON_GetObjectItem(data, "prechargeAmount");
        if (!p) p = cJSON_GetObjectItem(data, "prepaidAmount");
        if (p && cJSON_IsNumber(p)) {
            prechargeAmount = p->valuedouble;
        }

        cJSON* f = cJSON_GetObjectItem(data, "feeModelNo");
        if (f && cJSON_IsNumber(f)) {
            feeModelNo = f->valueint;
        }
    }

    gs.startTimeMs = startTs;
    gs.chargeStartTime = toYmdHmsNumber(static_cast<std::time_t>(startTs / 1000ULL));
    gs.chargeEndTime = 0;
    gs.chargeUserNo = userNo;
    gs.orderNo = orderNo;
    gs.preTradeNo = preTradeNo.empty() ? orderNo : preTradeNo;
    // BY ZF: tradeNo 与 preTradeNo/orderNo 对齐，不再添加前缀。
    const std::string baseTradeNo = gs.preTradeNo.empty() ? gs.orderNo : gs.preTradeNo;
    gs.tradeNo = baseTradeNo.empty()
        ? ("T" + std::to_string(gun) + "_" + std::to_string(gs.chargeStartTime))
        : baseTradeNo;
    gs.startType = (source && std::string(source) == "platform") ? 2 : 1;
    gs.startSoc = 0;
    gs.endSoc = 0;
    gs.stopReason = 0;
    gs.hasVinCode = false;
    gs.vinCode.clear();
    gs.chargeMode = chargeMode;
    gs.prechargeAmount = prechargeAmount;
    gs.feeModelNo = feeModelNo;
    gs.hasAuthBasis = true;
    gs.tcuStopReqSent = false;
    gs.hasTotalAmount = false;
    gs.lastTotalAmount = 0.0;
    gs.feeHasEnergyBase = false;
    gs.feeEnergyBaseKwh = 0.0;
    gs.feeLastEnergyKwh = 0.0;
    gs.feeTotalEnergyKwh = 0.0;
    gs.feeTotalElectricAmount = 0.0;
    gs.feeTotalServiceAmount = 0.0;
    gs.meterStableCount = 0;

    // BY ZF: 从启动命令解析计费模型（HHMM时段 + 分段电费/服务费）
    parseFeeModel(gun, data);

    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "source", source ? source : "");
    cJSON_AddNumberToObject(evt, "startTime", static_cast<double>(gs.startTimeMs));
    cJSON_AddStringToObject(evt, "chargeUserNo", gs.chargeUserNo.c_str());
    cJSON_AddStringToObject(evt, "orderNo", gs.orderNo.c_str());
    cJSON_AddNumberToObject(evt, "chargeMode", gs.chargeMode);
    cJSON_AddNumberToObject(evt, "prechargeAmount", gs.prechargeAmount);
    cJSON_AddNumberToObject(evt, "feeModelNo", gs.feeModelNo);
    publishLogicEvent(gun, "auth_basis", evt);
    cJSON_Delete(evt);
}

bool ChargeLogicProcess::parseFeeModel(uint8_t gun, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }
    GunState& gs = m_gunStates[gun];
    gs.feeInitialized = false;
    gs.feeModelId.clear();
    gs.feeTimeNum = 0;
    gs.feeTimeSegMinutes.clear();
    gs.feeChargePricePerKwh.clear();
    gs.feeServicePricePerKwh.clear();
    gs.feeSegEnergyKwh.clear();
    gs.feeSegElectricAmount.clear();
    gs.feeSegServiceAmount.clear();

    if (!data) {
        return false;
    }

    const char* modelId = getString(data, "feeModelId");
    if (!modelId || !modelId[0]) {
        modelId = getString(data, "feeModelID");
    }
    if (modelId && modelId[0]) {
        gs.feeModelId = modelId;
    }

    cJSON* timeNumJson = cJSON_GetObjectItem(data, "timeNum");
    cJSON* timeSegJson = cJSON_GetObjectItem(data, "timeSeg");
    cJSON* chargeFeeJson = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFeeJson = cJSON_GetObjectItem(data, "serviceFee");
    if (!timeNumJson || !cJSON_IsNumber(timeNumJson) || !timeSegJson || !chargeFeeJson || !serviceFeeJson) {
        return false;
    }

    int timeNum = timeNumJson->valueint;
    if (timeNum <= 0 || !cJSON_IsArray(timeSegJson) || !cJSON_IsArray(chargeFeeJson) || !cJSON_IsArray(serviceFeeJson)) {
        return false;
    }
    int segCount = std::min(timeNum, cJSON_GetArraySize(timeSegJson));
    segCount = std::min(segCount, cJSON_GetArraySize(chargeFeeJson));
    segCount = std::min(segCount, cJSON_GetArraySize(serviceFeeJson));
    if (segCount <= 0) {
        return false;
    }

    gs.feeTimeNum = static_cast<uint8_t>(segCount);
    gs.feeTimeSegMinutes.resize(segCount);
    gs.feeChargePricePerKwh.resize(segCount);
    gs.feeServicePricePerKwh.resize(segCount);
    gs.feeSegEnergyKwh.assign(segCount, 0.0);
    gs.feeSegElectricAmount.assign(segCount, 0.0);
    gs.feeSegServiceAmount.assign(segCount, 0.0);

    for (int i = 0; i < segCount; i++) {
        cJSON* ts = cJSON_GetArrayItem(timeSegJson, i);
        cJSON* cf = cJSON_GetArrayItem(chargeFeeJson, i);
        cJSON* sf = cJSON_GetArrayItem(serviceFeeJson, i);
        int minute = 0;
        if (ts && cJSON_IsString(ts) && ts->valuestring && std::strlen(ts->valuestring) >= 4) {
            std::string hhmm(ts->valuestring, 4);
            int hh = std::atoi(hhmm.substr(0, 2).c_str());
            int mm = std::atoi(hhmm.substr(2, 2).c_str());
            if (hh < 0) hh = 0;
            if (hh > 23) hh = 23;
            if (mm < 0) mm = 0;
            if (mm > 59) mm = 59;
            minute = hh * 60 + mm;
        }
        gs.feeTimeSegMinutes[i] = minute;
        // BY ZF: chargeFee/serviceFee 输入按“浮点元/度”直接使用
        gs.feeChargePricePerKwh[i] = (cf && cJSON_IsNumber(cf)) ? cf->valuedouble : 0.0;
        gs.feeServicePricePerKwh[i] = (sf && cJSON_IsNumber(sf)) ? sf->valuedouble : 0.0;
    }

    // 保证分段按起始分钟升序
    std::vector<int> idx(segCount);
    for (int i = 0; i < segCount; i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&gs](int a, int b) {
        return gs.feeTimeSegMinutes[a] < gs.feeTimeSegMinutes[b];
    });
    std::vector<int> sortedSeg(segCount);
    std::vector<double> sortedCharge(segCount), sortedService(segCount);
    for (int i = 0; i < segCount; i++) {
        sortedSeg[i] = gs.feeTimeSegMinutes[idx[i]];
        sortedCharge[i] = gs.feeChargePricePerKwh[idx[i]];
        sortedService[i] = gs.feeServicePricePerKwh[idx[i]];
    }
    gs.feeTimeSegMinutes.swap(sortedSeg);
    gs.feeChargePricePerKwh.swap(sortedCharge);
    gs.feeServicePricePerKwh.swap(sortedService);

    gs.feeInitialized = true;
    return true;
}

int ChargeLogicProcess::getCurrentMinuteOfDay() const
{
    std::time_t now = std::time(nullptr);
    std::tm localTm;
#if defined(_WIN32)
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif
    return localTm.tm_hour * 60 + localTm.tm_min;
}

int ChargeLogicProcess::getSegmentIndexByMinute(const GunState& gs, int minuteOfDay) const
{
    if (gs.feeTimeSegMinutes.empty()) {
        return -1;
    }
    int idx = 0;
    for (size_t i = 0; i < gs.feeTimeSegMinutes.size(); i++) {
        if (minuteOfDay >= gs.feeTimeSegMinutes[i]) {
            idx = static_cast<int>(i);
        } else {
            break;
        }
    }
    return idx;
}

void ChargeLogicProcess::applyEnergyDeltaToFee(uint8_t gun, double deltaKwh)
{
    if (gun >= m_gunStates.size() || deltaKwh <= 0.0) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (!gs.feeInitialized) {
        return;
    }
    int minuteOfDay = getCurrentMinuteOfDay();
    int segIdx = getSegmentIndexByMinute(gs, minuteOfDay);
    if (segIdx < 0 || segIdx >= static_cast<int>(gs.feeChargePricePerKwh.size()) ||
        segIdx >= static_cast<int>(gs.feeServicePricePerKwh.size())) {
        return;
    }

    double electric = deltaKwh * gs.feeChargePricePerKwh[segIdx];
    double service = deltaKwh * gs.feeServicePricePerKwh[segIdx];

    gs.feeTotalEnergyKwh += deltaKwh;
    gs.feeTotalElectricAmount += electric;
    gs.feeTotalServiceAmount += service;

    if (segIdx < static_cast<int>(gs.feeSegEnergyKwh.size())) {
        gs.feeSegEnergyKwh[segIdx] += deltaKwh;
        gs.feeSegElectricAmount[segIdx] += electric;
        gs.feeSegServiceAmount[segIdx] += service;
    }

    gs.lastTotalAmount = gs.feeTotalElectricAmount + gs.feeTotalServiceAmount;
    gs.hasTotalAmount = true;
}

// BY ZF: 预充金额接近阈值时，触发停机
void ChargeLogicProcess::maybeTriggerTcuStopByPrecharge(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (gs.state != STATE_CHARGING || gs.tcuStopReqSent) {
        return;
    }
    if (!gs.hasAuthBasis || gs.prechargeAmount <= 0.0 || !gs.hasTotalAmount) {
        return;
    }

    const double threshold = gs.prechargeAmount - m_config.prechargeStopMargin;
    if (gs.lastTotalAmount >= threshold) {
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "reason", "precharge_near_limit");
        cJSON_AddNumberToObject(evt, "totalAmount", gs.lastTotalAmount);
        cJSON_AddNumberToObject(evt, "prechargeAmount", gs.prechargeAmount);
        cJSON_AddNumberToObject(evt, "margin", m_config.prechargeStopMargin);
        cJSON_AddStringToObject(evt, "chargeUserNo", gs.chargeUserNo.c_str());
        cJSON_AddStringToObject(evt, "orderNo", gs.orderNo.c_str());
        cJSON_AddNumberToObject(evt, "feeModelNo", gs.feeModelNo);
        publishLogicEvent(gun, "tcu_stop_request", evt);
        cJSON_Delete(evt);

        gs.tcuStopReqSent = true;
        handleEvent(gun, EVT_STOP_CMD, "tcu_precharge_near_limit");
    }
}

void ChargeLogicProcess::handleEvent(uint8_t gun, EventType evt, const char* reason)
{
    // BY ZF: 集中状态机入口
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    ChargeState prev = gs.state;

    switch (gs.state) {
    case STATE_IDLE:
        if (evt == EVT_VEHICLE_CONNECTED) {
            transitionTo(gun, STATE_PREPARE, reason);
        } else if (evt == EVT_TOTAL_FAULT || evt == EVT_DEVICE_ERR) {
            transitionTo(gun, STATE_ERROR, reason);
        } else if (evt == EVT_RESET_ERROR) {
            transitionTo(gun, STATE_IDLE, reason);
        }
        break;
    case STATE_PREPARE:
        if (evt == EVT_AUTH_OK) {
            // BY ZF: 鉴权通过后下发启动命令
            if (gs.pendingStart) {
                if (!gs.pendingStartData.empty()) {
                    cJSON* startData = cJSON_Parse(gs.pendingStartData.c_str());
                    if (startData) {
                        publishPileCmd(gun, "start_charge", startData);
                        gs.lastStartCmdData = gs.pendingStartData;
                        cJSON_Delete(startData);
                    } else {
                        cJSON* pileStartData = buildPileStartData(nullptr);
                        publishPileCmd(gun, "start_charge", pileStartData);
                        char* out = cJSON_PrintUnformatted(pileStartData);
                        if (out) {
                            gs.lastStartCmdData = out;
                            cJSON_free(out);
                        }
                        cJSON_Delete(pileStartData);
                    }
                } else {
                    cJSON* pileStartData = buildPileStartData(nullptr);
                    publishPileCmd(gun, "start_charge", pileStartData);
                    char* out = cJSON_PrintUnformatted(pileStartData);
                    if (out) {
                        gs.lastStartCmdData = out;
                        cJSON_free(out);
                    }
                    cJSON_Delete(pileStartData);
                }
                gs.pendingStart = false;
                gs.pendingStartData.clear();
                gs.startingRetrySent = false;
            }
            transitionTo(gun, STATE_STARTING, reason);
        } else if (evt == EVT_AUTH_FAIL) {
            gs.pendingStart = false;
            gs.pendingStartData.clear();
            gs.lastStartCmdData.clear();
            transitionTo(gun, STATE_PREPARE, reason);
        } else if (evt == EVT_VEHICLE_DISCONNECTED) {
            gs.pendingStart = false;
            gs.pendingStartData.clear();
            gs.lastStartCmdData.clear();
            transitionTo(gun, STATE_IDLE, reason);
        } else if (evt == EVT_TOTAL_FAULT || evt == EVT_DEVICE_ERR) {
            transitionTo(gun, STATE_ERROR, reason);
        } else if (evt == EVT_RESET_ERROR) {
            gs.pendingStart = false;
            gs.pendingStartData.clear();
            gs.lastStartCmdData.clear();
            transitionTo(gun, STATE_IDLE, reason);
        } else if (evt == EVT_START_CMD) {
            transitionTo(gun, STATE_PREPARE, reason);
        }
        break;
    case STATE_STARTING:
        if (evt == EVT_START_COMPLETE_OK) {
            gs.tcuStopReqSent = false;
            transitionTo(gun, STATE_CHARGING, reason);
        } else if (evt == EVT_START_COMPLETE_FAIL || evt == EVT_TOTAL_FAULT || evt == EVT_DEVICE_ERR || evt == EVT_STOP_CMD) {
            enterStopping(gun, reason);
        } else if (evt == EVT_RESET_ERROR) {
            transitionTo(gun, STATE_IDLE, reason);
        }
        break;
    case STATE_CHARGING:
        if (evt == EVT_STOP_COMPLETE) {
            enterStopping(gun, reason);
            // BY ZF: CHARGING 直接收到 stop_complete 时，保留完成标记，避免在 enterStopping 中被清零
            gs.stopCompleteSeen = true;
        } else if (evt == EVT_STOP_CMD || evt == EVT_WORKSTATUS_ZERO || evt == EVT_TOTAL_FAULT || evt == EVT_DEVICE_ERR) {
            enterStopping(gun, reason);
        }
        break;
    case STATE_STOPPING:
        if (evt == EVT_STOP_COMPLETE) {
            gs.stopCompleteSeen = true;
        } else if (evt == EVT_METER_STALE) {
            transitionTo(gun, STATE_STOPPED, reason);
        }
        break;
    case STATE_STOPPED:
        if (evt == EVT_VEHICLE_DISCONNECTED) {
            transitionTo(gun, STATE_IDLE, reason);
        }
        break;
    case STATE_ERROR:
        if (evt == EVT_RESET_ERROR) {
            transitionTo(gun, STATE_IDLE, reason);
        }
        break;
    default:
        break;
    }

    (void)prev;
}

void ChargeLogicProcess::transitionTo(uint8_t gun, ChargeState to, const char* reason)
{
    // BY ZF: 集中状态切换
    GunState& gs = m_gunStates[gun];
    if (gs.state == to) {
        return;
    }
    ChargeState prev = gs.state;
    gs.state = to;
    if (to == STATE_STARTING) {
        gs.startingEnterTime = std::chrono::steady_clock::now();
        gs.startingRetrySent = false;
    } else {
        gs.startingEnterTime = std::chrono::steady_clock::time_point();
        gs.startingRetrySent = false;
    }
    publishStateChange(gun, prev, to, reason);
    if (to == STATE_STOPPED && prev == STATE_STOPPING) {
        logTradeRecordOnStopped(gun, reason);
    }
    if (to == STATE_IDLE) {
        // BY ZF: 回到空闲态时清理本次充电流程上下文
        resetChargeSessionState(gun);
    }
}

void ChargeLogicProcess::resetChargeSessionState(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    GunState& gs = m_gunStates[gun];

    gs.pendingStart = false;
    gs.pendingStartData.clear();
    gs.lastStartCmdData.clear();
    gs.startingRetrySent = false;
    gs.stopCompleteSeen = false;
    gs.tcuStopReqSent = false;
    gs.meterStableCount = 0;
    gs.lastStopCmdTime = std::chrono::steady_clock::time_point();
    gs.startingEnterTime = std::chrono::steady_clock::time_point();
    gs.stoppingEnterTime = std::chrono::steady_clock::time_point();
    gs.lastMeterMsgTime = std::chrono::steady_clock::time_point();
    gs.lastMeterValueTime = std::chrono::steady_clock::time_point();

    gs.hasAuthBasis = false;
    gs.startTimeMs = 0;
    gs.startType = 0;
    gs.chargeStartTime = 0;
    gs.chargeEndTime = 0;
    gs.chargeUserNo.clear();
    gs.orderNo.clear();
    gs.preTradeNo.clear();
    gs.tradeNo.clear();
    gs.hasVinCode = false;
    gs.vinCode.clear();
    gs.startSoc = 0;
    gs.endSoc = 0;
    gs.stopReason = 0;
    gs.chargeMode = 0;
    gs.prechargeAmount = 0.0;
    gs.feeModelNo = 0;

    gs.feeInitialized = false;
    gs.feeModelId.clear();
    gs.feeTimeNum = 0;
    gs.feeTimeSegMinutes.clear();
    gs.feeChargePricePerKwh.clear();
    gs.feeServicePricePerKwh.clear();
    gs.feeSegEnergyKwh.clear();
    gs.feeSegElectricAmount.clear();
    gs.feeSegServiceAmount.clear();
    gs.feeHasEnergyBase = false;
    gs.feeEnergyBaseKwh = 0.0;
    gs.feeLastEnergyKwh = 0.0;
    gs.feeTotalEnergyKwh = 0.0;
    gs.feeTotalElectricAmount = 0.0;
    gs.feeTotalServiceAmount = 0.0;

    gs.hasTotalAmount = false;
    gs.lastTotalAmount = 0.0;
}

void ChargeLogicProcess::logTradeRecordOnStopped(uint8_t gun, const char* reason)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    TradeRecord rec;
    rec.gunNo = static_cast<int>(gun);
    rec.preTradeNo = gs.preTradeNo;
    rec.tradeNo = gs.tradeNo.empty() ? ("T" + std::to_string(gun) + "_" + std::to_string(gs.chargeStartTime)) : gs.tradeNo;
    rec.vinCode = gs.hasVinCode ? gs.vinCode : "";
    rec.timeDivType = gs.chargeMode;
    rec.startType = gs.startType;
    rec.chargeStartTime = gs.chargeStartTime;
    rec.chargeEndTime = toYmdHmsNumber(std::time(nullptr));
    gs.chargeEndTime = rec.chargeEndTime;
    rec.startSoc = roundTo5(gs.startSoc);
    rec.endSoc = roundTo5(gs.endSoc);
    rec.reason = gs.stopReason;
    rec.feeModelId = gs.feeModelId;

    const double sumStartKwh = std::max(0.0, gs.feeEnergyBaseKwh);
    const double sumEndKwh = std::max(0.0, gs.feeEnergyBaseKwh + gs.feeTotalEnergyKwh);
    const double totalElectKwh = std::max(0.0, gs.feeTotalEnergyKwh);
    const double totalPowerCostYuan = std::max(0.0, gs.feeTotalElectricAmount);
    const double totalServCostYuan = std::max(0.0, gs.feeTotalServiceAmount);
    const double totalCostYuan = totalPowerCostYuan + totalServCostYuan;
    rec.sumStart = roundTo5(sumStartKwh);
    rec.sumEnd = roundTo5(sumEndKwh);
    rec.totalElect = roundTo5(totalElectKwh);
    rec.totalPowerCost = roundTo5(totalPowerCostYuan);
    rec.totalServCost = roundTo5(totalServCostYuan);
    rec.totalCost = roundTo5(totalCostYuan);

    rec.timeNum = gs.feeTimeNum;
    rec.partElect.reserve(gs.feeSegEnergyKwh.size());
    rec.chargeFee.reserve(gs.feeSegElectricAmount.size());
    rec.serviceFee.reserve(gs.feeSegServiceAmount.size());
    for (size_t i = 0; i < gs.feeSegEnergyKwh.size(); i++) {
        rec.partElect.push_back(roundTo5(std::max(0.0, gs.feeSegEnergyKwh[i])));
        rec.chargeFee.push_back(roundTo5(std::max(0.0, gs.feeSegElectricAmount[i])));
        rec.serviceFee.push_back(roundTo5(std::max(0.0, gs.feeSegServiceAmount[i])));
    }

    rec.startPoint = 0;
    rec.crossPoints = 0;
    rec.pointsElect.clear();
    rec.cardNumber = gs.chargeUserNo;

    m_logSender.logTradeRecord(rec);
    publishUpdateRecordEvent(gun, rec);

    if (reason && reason[0]) {
        m_logSender.info("trade_record_logged", reason);
    } else {
        m_logSender.info("trade_record_logged");
    }
}

void ChargeLogicProcess::enterStopping(uint8_t gun, const char* reason)
{
    // BY ZF: 进入 STOPPING 并下发 stop_charge
    transitionTo(gun, STATE_STOPPING, reason);
    m_gunStates[gun].stopCompleteSeen = false;
    m_gunStates[gun].meterStableCount = 0;
    m_gunStates[gun].stoppingEnterTime = std::chrono::steady_clock::now();
    publishPileCmd(gun, "stop_charge", nullptr);
    m_gunStates[gun].lastStopCmdTime = std::chrono::steady_clock::now();
}
