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
    void feedDaemonWatchdog()
    {
        // BY ZF: 通过守护进程看门狗消息队列上报 tcu_logic 存活状态。
        static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
        static int queueReady = -1;
        if (queueReady == -1) {
            queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
        }
        if (queueReady == 1) {
            const char* processName = "tcu_logic";
            watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        }
    }

    bool parseTopicGun(const std::string& topic, const std::string& prefix, int biasNo, uint8_t& gunOut, std::string& tailOut) {
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
        const int externalGun = std::stoi(gunStr);
        const int localGun = externalGun - biasNo;
        if (localGun < 0 || localGun > 255) {
            return false;
        }
        gunOut = static_cast<uint8_t>(localGun);
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

    bool jsonGetInt(cJSON* obj, const char* key, int& out) {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v)) {
            out = v->valueint;
            return true;
        }
        return false;
    }

    uint8_t getPlugAndChargeFlag(cJSON* obj) {
        int value = 0x01;
        if (obj) {
            jsonGetInt(obj, "plugAndChargeFlag", value);
        }
        return static_cast<uint8_t>(value & 0xFF);
    }

    uint8_t getMergeChargeFlag(cJSON* obj) {
        int value = 0x00;
        if (obj) {
            if (!jsonGetInt(obj, "mergeChargeFlag", value) &&
                !jsonGetInt(obj, "mergedChargeFlag", value) &&
                !jsonGetInt(obj, "mergeChargedFlag", value)) {
                jsonGetInt(obj, "combineChargeFlag", value);
            }
        }
        return static_cast<uint8_t>(value & 0xFF);
    }

    std::string sanitizeVinString(const char* vin) {
        std::string out;
        if (!vin) {
            return out;
        }
        out.reserve(17);
        for (size_t i = 0; vin[i] != '\0' && i < 17; ++i) {
            unsigned char ch = static_cast<unsigned char>(vin[i]);
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<unsigned char>(ch - 'a' + 'A');
            }
            out.push_back(static_cast<char>(ch));
        }
        return out;
    }

    bool isValidVinString(const std::string& vin) {
        if (vin.size() != 17) {
            return false;
        }
        for (size_t i = 0; i < vin.size(); ++i) {
            const char ch = vin[i];
            const bool isDigit = (ch >= '0' && ch <= '9');
            const bool isUpper = (ch >= 'A' && ch <= 'Z');
            if ((!isDigit && !isUpper) || ch == 'I' || ch == 'O' || ch == 'Q') {
                return false;
            }
        }
        return true;
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
    double roundNearest(double v) {
        return (v >= 0.0) ? std::floor(v + 0.5) : std::ceil(v - 0.5);
    }

    long long roundNearestLongLong(double v) {
        return static_cast<long long>((v >= 0.0) ? std::floor(v + 0.5) : std::ceil(v - 0.5));
    }

    double roundTo5(double v) {
        return roundNearest(v * 100000.0) / 100000.0;
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

    // BY ZF: Unix 秒时间戳转 YYYYMMDDHHMMSS 字符串，用于本地故障持久化事件。
    std::string toYmdHmsString(std::time_t tsSec) {
        std::tm tmv;
        localtime_r(&tsSec, &tmv);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return std::string(buf);
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
        uint8_t v2g = 0x00; // BY ZF: 0=充电模式,1=放电模式

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
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(src, "mergeChargedFlag");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(src, "combineChargeFlag");
            if (cJSON_IsNumber(v)) mergeChargeFlag = static_cast<uint8_t>(v->valueint);

            // BY ZF: V2G 放电标志，透传给 pile_controller。
            v = cJSON_GetObjectItem(src, "v2g");
            if (cJSON_IsNumber(v) && v->valueint != 0) {
                v2g = 0x01;
            }
        }

        cJSON_AddNumberToObject(out, "loadControlSwitch", loadControlSwitch);
        cJSON_AddNumberToObject(out, "plugAndChargeFlag", plugAndChargeFlag);
        cJSON_AddNumberToObject(out, "auxPowerVoltage", auxPowerVoltage);
        cJSON_AddNumberToObject(out, "mergeChargeFlag", mergeChargeFlag);
        cJSON_AddNumberToObject(out, "v2g", v2g);
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
    , m_lastReplayTime(std::chrono::steady_clock::now())
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
    m_unconfirmedRecordBuffer.clear();
    m_unconfirmedRecordBuffer.resize(m_config.gunCount);
    m_lastReplayTime = std::chrono::steady_clock::now();
    m_lastUnconfirmedQueryTime = m_lastReplayTime;
    // BY ZF: 初始化时向 logger 请求未确认交易记录，由 logger 通过 MQTT 回放。
    m_logSender.requestUnconfirmedTradeRecords(100);
    // BY ZF: 初始化完成后写入一条 info 日志，便于联调确认进程已就绪
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    // BY ZF: 初始化完成后主动上报一次当前枪状态，避免订阅方在未发生状态迁移前无法获知 logic 当前处于 IDLE。
    for (size_t gun = 0; gun < m_gunStates.size(); ++gun) {
        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "from", "NULL");
        cJSON_AddStringToObject(data, "to", stateToString(m_gunStates[gun].state));
        cJSON_AddStringToObject(data, "reason", "logic_init");
        publishLogicEvent(static_cast<uint8_t>(gun), "state_change", data);
        cJSON_Delete(data);
    }
    return true;
}

void ChargeLogicProcess::doRun()
{
    // BY ZF: doRun 只执行一次循环迭代，避免在信号 stop() 时被 join 阻塞导致需要二次 Ctrl+C。
    static std::chrono::steady_clock::time_point lastFeedTime = std::chrono::steady_clock::now()
        - std::chrono::seconds(5);
    auto now = std::chrono::steady_clock::now();
    if (now - lastFeedTime >= std::chrono::seconds(5)) {
        feedWatchdog();
        feedDaemonWatchdog();
        lastFeedTime = now;
    }
    // BY ZF: 运行期间每小时主动向 logger 再查询一次未确认交易记录，补偿 comm/平台侧漏确认场景。
    if (now - m_lastUnconfirmedQueryTime >= std::chrono::hours(1)) {
        m_logSender.requestUnconfirmedTradeRecords(100);
        m_lastUnconfirmedQueryTime = now;
    }
    for (size_t i = 0; i < m_gunStates.size(); i++) {
        GunState& gs = m_gunStates[i];
        if (maybeConfirmMeterOfflineFault(static_cast<uint8_t>(i), now)) {
            continue;
        }
        if (maybeConfirmPlatformOfflineFault(static_cast<uint8_t>(i), now)) {
            continue;
        }
        if (maybeConfirmPileOfflineFault(static_cast<uint8_t>(i), now)) {
            continue;
        }
        if (gs.state == STATE_CHARGING) {
            maybeTriggerTcuStopByPrecharge(static_cast<uint8_t>(i));
            if (maybeTriggerMeteringAbnormalByLowVoltage(static_cast<uint8_t>(i), now)) {
                continue;
            }
            // BY ZF: feeData 保底机制，充电中即便电量未变化也至少 15 秒发布一次。
            if (gs.lastFeeDataPublishTime.time_since_epoch().count() == 0 ||
                (now - gs.lastFeeDataPublishTime) >= std::chrono::seconds(15)) {
                publishFeeData(static_cast<uint8_t>(i));
            }
        }
        if (maybeHandlePlugAndChargeAuthTimeout(static_cast<uint8_t>(i), now)) {
            continue;
        }
        if (gs.state == STATE_STARTING) {
            if (gs.startingEnterTime.time_since_epoch().count() != 0) {
                const auto elapsed = now - gs.startingEnterTime;
                // BY ZF: STARTING 持续 30s 未完成，重发一次启动命令
                if (!gs.plugAndChargeActive &&
                    !gs.startingRetrySent &&
                    elapsed >= std::chrono::seconds(30)) {
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
                    const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0xF001U));
                    if (result.valid) {
                        gs.stopReason = result.reason;
                    }
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
            // BY ZF: 兜底保护，避免 STOPPING 长时间滞留
            if (gs.stoppingEnterTime.time_since_epoch().count() != 0 &&
                now - gs.stoppingEnterTime >= std::chrono::seconds(30)) {
                handleEvent(static_cast<uint8_t>(i), EVT_METER_STALE, "stopping_force_timeout_30s");
            }
        }
    }
    replayBufferedUnconfirmedRecords();
    // BY ZF: doRun 主循环节流 10ms
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void ChargeLogicProcess::doCleanup()
{
    m_running = false;
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
    m_config.biasNo = cfg.getInt("ChargeLogic", "bias_no", 0);
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
            std::ostringstream t1, t2, t2e, t3, t4, t5, t6, t7;
            const int topicGun = static_cast<int>(gun) + m_config.biasNo;
            t1 << m_config.mqttTopicPrefix << "/logic/" << topicGun << "/cmd";
            t2 << m_config.mqttTopicPrefix << "/plat/" << topicGun << "/cmd";
            t2e << m_config.mqttTopicPrefix << "/plat/" << topicGun << "/event";
            t3 << m_config.mqttTopicPrefix << "/pile/" << topicGun << "/event";
            t4 << m_config.mqttTopicPrefix << "/pile/" << topicGun << "/data";
            t5 << m_config.mqttTopicPrefix << "/meter/" << topicGun << "/data";
            t6 << m_config.mqttTopicPrefix << "/logger/" << topicGun << "/event";
            t7 << m_config.mqttTopicPrefix << "/meter/" << topicGun << "/event";
            m_mqtt.subscribe(t1.str(), 1);
            m_mqtt.subscribe(t2.str(), 1);
            m_mqtt.subscribe(t2e.str(), 1);
            m_mqtt.subscribe(t3.str(), 2);
            m_mqtt.subscribe(t4.str(), 0);
            m_mqtt.subscribe(t5.str(), 0);
            m_mqtt.subscribe(t6.str(), 2);
            m_mqtt.subscribe(t7.str(), 1);
        }
        m_mqtt.subscribe(getCardEventTopic(), 1);
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
    std::string prefixLogger = m_config.mqttTopicPrefix + "/logger/";

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    if (parseTopicGun(topic, prefixLogic, m_config.biasNo, gun, tail) && tail == "cmd") {
        const char* cmd = getString(root, "cmd");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handleLogicCmd(gun, cmd, data);
    } else if (parseTopicGun(topic, prefixPlat, m_config.biasNo, gun, tail) && tail == "cmd") {
        const char* cmd = getString(root, "cmd");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handlePlatCmd(gun, cmd, data);
    } else if (parseTopicGun(topic, prefixPlat, m_config.biasNo, gun, tail) && tail == "event") {
        const char* event = getString(root, "event");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handlePlatEvent(gun, event ? event : "", data);
    } else if (parseTopicGun(topic, prefixPile, m_config.biasNo, gun, tail)) {
        if (tail == "event") {
            const char* type = getString(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handlePileEvent(gun, type, data);
        } else if (tail == "data") {
            const char* type = getString(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handlePileData(gun, type, data);
        }
    } else if (parseTopicGun(topic, prefixLogger, m_config.biasNo, gun, tail) && tail == "event") {
        const char* event = getString(root, "event");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        if (event && std::strcmp(event, "unconfirmed_record") == 0) {
            TradeRecord rec;
            if (parseTradeRecordFromJson(data, rec)) {
                std::lock_guard<std::mutex> lock(m_unconfirmedMutex);
                if (gun < m_unconfirmedRecordBuffer.size()) {
                    // BY ZF: 限制缓冲上限，避免异常消息导致内存膨胀。
                    if (m_unconfirmedRecordBuffer[gun].size() >= 200) {
                        m_unconfirmedRecordBuffer[gun].pop_front();
                    }
                    m_unconfirmedRecordBuffer[gun].push_back(rec);
                }
            }
        }
    } else if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/meter/", m_config.biasNo, gun, tail)) {
        if (tail == "data") {
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handleMeterData(gun, data);
        } else if (tail == "event") {
            const char* event = getString(root, "event");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            handleMeterEvent(gun, event ? event : "", data);
        }
    } else if (topic == getCardEventTopic()) {
        const char* event = getString(root, "event");
        cJSON* data = cJSON_GetObjectItem(root, "data");
        handleCardEvent(event ? event : "", data);
    }

    cJSON_Delete(root);
}

void ChargeLogicProcess::handleLogicCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    if (cmd == "vin_req" || cmd == "start_charge") {
        // BY ZF: HMI 即插即充入口兼容 vin_req；保留 start_charge 兼容旧流程。
        GunState& gs = m_gunStates[gun];
        if (gs.state == STATE_PREPARE) {
            const bool mergePlugAndCharge = (getPlugAndChargeFlag(data) == 0x02 && getMergeChargeFlag(data) != 0x00);
            if (mergePlugAndCharge) {
                const int peer = getMergePeerGun(gun);
                if (peer < 0) {
                    cJSON* evt = cJSON_CreateObject();
                    cJSON_AddStringToObject(evt, "cmd", cmd.c_str());
                    cJSON_AddStringToObject(evt, "reason", "merge_charge_peer_not_found");
                    publishLogicEvent(gun, "cmd_reject", evt);
                    cJSON_Delete(evt);
                    return;
                }
                GunState& peerGs = m_gunStates[peer];
                if (peerGs.state != STATE_PREPARE) {
                    cJSON* evt = cJSON_CreateObject();
                    cJSON_AddStringToObject(evt, "cmd", cmd.c_str());
                    cJSON_AddStringToObject(evt, "reason", "merge_charge_peer_not_prepare");
                    cJSON_AddNumberToObject(evt, "peerGun", peer);
                    cJSON_AddStringToObject(evt, "peerState", stateToString(peerGs.state));
                    publishLogicEvent(gun, "cmd_reject", evt);
                    cJSON_Delete(evt);
                    return;
                }

                const char* source = (cmd == "vin_req") ? "hmi_vin_req" : "hmi";
                if (!armPendingStart(gun, data, source) || !armPendingStart(static_cast<uint8_t>(peer), data, source)) {
                    cJSON* evt = cJSON_CreateObject();
                    cJSON_AddStringToObject(evt, "cmd", cmd.c_str());
                    cJSON_AddStringToObject(evt, "reason", "merge_charge_arm_failed");
                    cJSON_AddNumberToObject(evt, "peerGun", peer);
                    publishLogicEvent(gun, "cmd_reject", evt);
                    cJSON_Delete(evt);
                    return;
                }

                const char* startReason = (cmd == "vin_req") ? "merge_vin_req" : "merge_start_cmd";
                const char* authReason = (cmd == "vin_req") ? "merge_vin_req_auto_auth_ok" : "merge_hmi_start_auto_auth_ok";
                dispatchArmedStart(gun, startReason, authReason);
                dispatchArmedStart(static_cast<uint8_t>(peer), startReason, authReason);
            } else {
                armPendingStart(gun, data, (cmd == "vin_req") ? "hmi_vin_req" : "hmi");
                dispatchArmedStart(gun,
                                   (cmd == "vin_req") ? "vin_req" : "start_cmd",
                                   (cmd == "vin_req") ? "hmi_vin_req_auto_auth_ok" : "hmi_start_auto_auth_ok");
            }
        } else {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", cmd.c_str());
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

    if (cmd == "request_card_start") {
        std::string mode = "offline";
        const char* modeStr = getString(data, "mode");
        if (modeStr && modeStr[0] != '\0') {
            mode = modeStr;
        }
        if (mode != "offline") {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", "request_card_start");
            cJSON_AddStringToObject(evt, "reason", "card_auth_mode_not_supported");
            cJSON_AddStringToObject(evt, "mode", mode.c_str());
            publishLogicEvent(gun, "cmd_reject", evt);
            cJSON_Delete(evt);
            return;
        }
        startOfflineCardFlow(gun, data);
        return;
    }
}

void ChargeLogicProcess::handlePlatCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    if (cmd == "power_ctrl") {
        // BY ZF: 平台功率控制命令直接透传给 pile_controller 执行。
        double maxChargePowerKw = 0.0;
        bool hasPower = false;
        if (cJSON_IsNumber(data)) {
            maxChargePowerKw = data->valuedouble;
            hasPower = true;
        } else if (cJSON_IsObject(data)) {
            cJSON* v = cJSON_GetObjectItem(data, "maxChargePowerKw");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(data, "powerLimitKw");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(data, "powerKw");
            if (!cJSON_IsNumber(v)) v = cJSON_GetObjectItem(data, "kw");
            if (cJSON_IsNumber(v)) {
                maxChargePowerKw = v->valuedouble;
                hasPower = true;
            }
        }
        if (!hasPower) {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", "power_ctrl");
            cJSON_AddStringToObject(evt, "reason", "missing_max_charge_power_kw");
            publishLogicEvent(gun, "cmd_reject", evt);
            cJSON_Delete(evt);
            return;
        }

        cJSON* pileData = cJSON_CreateObject();
        cJSON_AddNumberToObject(pileData, "maxChargePowerKw", maxChargePowerKw);
        publishPileCmd(gun, "power_ctrl", pileData);
        cJSON_Delete(pileData);

        cJSON* evt = cJSON_CreateObject();
        cJSON_AddNumberToObject(evt, "maxChargePowerKw", maxChargePowerKw);
        publishLogicEvent(gun, "power_ctrl_forwarded", evt);
        cJSON_Delete(evt);
        return;
    }

    if (cmd == "start_charge") {
        if (gs.plugAndChargeActive && gs.state == STATE_STARTING && gs.plugAndChargeVehicleIdConfirmed) {
            handlePlugAndChargeAuthResult(gun, data, "start_charge");
            return;
        }
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
        if (gs.plugAndChargeActive && gs.state == STATE_STARTING && gs.plugAndChargeVehicleIdConfirmed) {
            handlePlugAndChargeAuthResult(gun, data, "auth_result");
            return;
        }
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

    if (cmd == "plug_and_charge_auth_result") {
        handlePlugAndChargeAuthResult(gun, data, "plug_and_charge_auth_result");
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
    GunState& gs = m_gunStates[gun];

    if (type == "start_complete") {
        bool ok = true;
        unsigned int chargeFailReason = 0;
        if (data) {
            cJSON* v = cJSON_GetObjectItem(data, "successFlag");
            if (v && cJSON_IsNumber(v) && v->valueint != 0) {
                ok = false;
            }
            v = cJSON_GetObjectItem(data, "chargeFailReason");
            if (v && cJSON_IsNumber(v) && v->valueint >= 0) {
                chargeFailReason = static_cast<unsigned int>(v->valueint) & 0xFFFFU;
            }
            cJSON* soc = cJSON_GetObjectItem(data, "soc");
            if (soc && cJSON_IsNumber(soc)) {
                m_gunStates[gun].startSoc = soc->valuedouble / 1.0;
            }
            const char* vin = getString(data, "vin");
            if (vin && vin[0]) {
                m_gunStates[gun].vinCode = vin;
                m_gunStates[gun].hasVinCode = true;
            }
        }
        if (ok) {
            handleEvent(gun, EVT_START_COMPLETE_OK, "start_complete_ok");
            m_gunStates[gun].startSuccessFlag = true;
        } else {
            const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(chargeFailReason));
            if (result.valid) {
                m_gunStates[gun].stopReason = result.reason;
            }
            handleEvent(gun, EVT_START_COMPLETE_FAIL, "start_complete_fail");
            m_gunStates[gun].startSuccessFlag = false;
        }
        return;
    }

    if (type == "stop_complete") {
        unsigned int stopReasonRaw = 0;
        if (data) {
            cJSON* r = cJSON_GetObjectItem(data, "stopReason");
            if (r && cJSON_IsNumber(r) && r->valueint >= 0) {
                stopReasonRaw = static_cast<unsigned int>(r->valueint) & 0xFFFFU;
                const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(stopReasonRaw));
                // BY ZF: 已进入充电中后才允许 stop_complete 更新原因；若当前已存在非0故障原因，则正常停机原因不能覆盖它。
                if (m_gunStates[gun].startSuccessFlag && result.valid) {
                    if (result.reason != 0U || m_gunStates[gun].stopReason == 0U) {
                        m_gunStates[gun].stopReason = result.reason;
                    }
                }
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

    if (type == "vehicle_id") {
        handlePlugAndChargeVehicleId(gun, data);
        return;
    }

    if (type == "vehicle_auth_ack") {
        if (data) {
            cJSON* evt = cJSON_CreateObject();
            cJSON* v = cJSON_GetObjectItem(data, "successFlag");
            if (v && cJSON_IsNumber(v)) {
                gs.plugAndChargeAuthAckReceived = true;
                cJSON_AddNumberToObject(evt, "successFlag", v->valueint);
            }
            v = cJSON_GetObjectItem(data, "failReason");
            if (v && cJSON_IsNumber(v)) {
                cJSON_AddNumberToObject(evt, "failReason", v->valueint);
            }
            publishLogicEvent(gun, "plug_and_charge_auth_ack", evt);
            cJSON_Delete(evt);
        }
        return;
    }

    if (type == "stop_response") {
        return;
    }
    if (type == "pile_online") {
        gs.pileOfflineFaultActive = false;
        gs.pileOfflineEventLatched = false;
        gs.pileOfflinePendingTime = std::chrono::steady_clock::time_point();
        if (gs.state == STATE_ERROR && !gs.meterOfflineFaultActive && !gs.platformOfflineFaultActive) {
            if (gs.hasVehicleConnectStatus && gs.lastVehicleConnectStatus != 0) {
                transitionTo(gun, STATE_PREPARE, "pile_online");
            } else {
                transitionTo(gun, STATE_IDLE, "pile_online");
            }
        }
        return;
    }
    if (type == "pile_offline") {
        // BY ZF: 主控离线同样走 30 秒待确认窗口，避免短时抖动立即触发停机。
        if (!gs.pileOfflineEventLatched && gs.pileOfflinePendingTime.time_since_epoch().count() == 0) {
            gs.pileOfflinePendingTime = std::chrono::steady_clock::now();
        }
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
        cJSON* of = cJSON_GetObjectItem(data, "otherFault");
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
        if (of && cJSON_IsNumber(of) && of->valueint >= 0) {
            const unsigned int otherFault = static_cast<unsigned int>(of->valueint) & 0xFFFFU;
            if (!gs.hasOtherFault || gs.lastOtherFault != otherFault) {
                gs.hasOtherFault = true;
                gs.lastOtherFault = otherFault;
                // BY ZF: 常态故障仅按状态阶段判断，不能再附加 workStatus==0 过滤，否则双枪公共故障可能漏记右枪。
                if (otherFault != 0U &&
                    (gs.state == STATE_IDLE || gs.state == STATE_PREPARE || gs.state == STATE_ERROR || gs.state == STATE_STOPPED)) {
                    const FaultJudgeResult result = JudgeStandbyFaultPoint(MakeStandbyPointKey(otherFault));
                    publishSaveErrorEvent(gun, result, otherFault, "pile_yx");
                }
            }
        }

        if (gs.hasWorkStatus && gs.lastWorkStatus == 0 && gs.state == STATE_CHARGING) {
            handleEvent(gun, EVT_WORKSTATUS_ZERO, "workStatus_zero");
        }

        if (gs.hasVehicleConnectStatus) {
            if (gs.state == STATE_IDLE && gs.lastVehicleConnectStatus != 0 && (!gs.hasTotalFault || gs.lastTotalFault == 0)) {
                handleEvent(gun, EVT_VEHICLE_CONNECTED, "vehicle_connected");
            } else if (gs.state == STATE_PREPARE && gs.lastVehicleConnectStatus == 0) {
                handleEvent(gun, EVT_VEHICLE_DISCONNECTED, "vehicle_disconnected");
            } else if (gs.state == STATE_STOPPING && gs.lastVehicleConnectStatus == 0) {
                // BY ZF: STOPPING 阶段若已断枪，记录标志，待进入 STOPPED 后直接回到 IDLE。
                gs.vehicleDisconnectedDuringStopping = true;
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
    double reverseEnergyKwh = 0.0;
    bool hasForwardEnergy = false;
    bool hasReverseEnergy = false;
    if (getNumber(data, "energy", totalEnergyKwh) ||
        getNumber(data, "totalEnergy", totalEnergyKwh) ||
        getNumber(data, "total_kwh", totalEnergyKwh)) {
        hasForwardEnergy = true;
    }
    if (getNumber(data, "ReverseEnergy", reverseEnergyKwh) ||
        getNumber(data, "reverseEnergy", reverseEnergyKwh) ||
        getNumber(data, "reverse_kwh", reverseEnergyKwh)) {
        hasReverseEnergy = true;
    }

    const bool useReverseEnergy = gs.v2gMode && hasReverseEnergy;
    const bool hasEnergy = useReverseEnergy || hasForwardEnergy;
    const double meterEnergyKwh = useReverseEnergy ? reverseEnergyKwh : totalEnergyKwh;
    if (hasEnergy) {
        if (!gs.hasMeterValue) {
            gs.lastMeterValue = meterEnergyKwh;
            gs.lastMeterValueTime = now;
            gs.hasMeterValue = true;
            gs.meterStableCount = 0;
        } else if (meterEnergyKwh != gs.lastMeterValue) {
            gs.lastMeterValue = meterEnergyKwh;
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
        if (gs.state == STATE_CHARGING && voltage >= 10.0) {
            gs.chargingMeterVoltageNormalSeen = true;
        }
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
            gs.feeEnergyBaseKwh = meterEnergyKwh;
            gs.feeLastEnergyKwh = meterEnergyKwh;
            gs.feeHasEnergyBase = true;
        } else {
            double deltaKwh = meterEnergyKwh - gs.feeLastEnergyKwh;
            if (deltaKwh > 0.0) {
                applyEnergyDeltaToFee(gun, deltaKwh);
            }
            gs.feeLastEnergyKwh = meterEnergyKwh;
        }

        // BY ZF: 计费数据变化即送（对比最近一次已发布快照）。
        const double currEnergy = roundTo5(gs.feeTotalEnergyKwh);
        const double currElectric = roundTo5(gs.feeTotalElectricAmount);
        const double currService = roundTo5(gs.feeTotalServiceAmount);
        if (currEnergy != gs.feeLastPublishedTotalEnergy ||
            currElectric != gs.feeLastPublishedElectricAmount ||
            currService != gs.feeLastPublishedServiceAmount) {
            publishFeeData(gun);
        }
    }
}

void ChargeLogicProcess::handleMeterEvent(uint8_t gun, const std::string& event, cJSON* data)
{
    (void)data;
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (event == "meter_online") {
        // BY ZF: 电表恢复在线后清除离线故障态；若当前处于 ERROR，则按插枪状态恢复到 IDLE/PREPARE。
        gs.meterOfflineFaultActive = false;
        gs.meterOfflineEventLatched = false;
        gs.meterOfflinePendingTime = std::chrono::steady_clock::time_point();
        if (gs.state == STATE_ERROR && !gs.platformOfflineFaultActive && !gs.pileOfflineFaultActive) {
            if (gs.hasVehicleConnectStatus && gs.lastVehicleConnectStatus != 0) {
                transitionTo(gun, STATE_PREPARE, "meter_online");
            } else {
                transitionTo(gun, STATE_IDLE, "meter_online");
            }
        }
        return;
    }
    if (event != "meter_offline") {
        return;
    }

    // BY ZF: 电表离线增加 30 秒待确认窗口，避免串口瞬断误报码。
    if (!gs.meterOfflineEventLatched && gs.meterOfflinePendingTime.time_since_epoch().count() == 0) {
        gs.meterOfflinePendingTime = std::chrono::steady_clock::now();
    }
}

void ChargeLogicProcess::handlePlatEvent(uint8_t gun, const std::string& event, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    (void)data;
    GunState& gs = m_gunStates[gun];
    if (event == "platform_online") {
        // BY ZF: 平台恢复在线后清除平台离线故障态；若当前处于 ERROR，则按链路状态恢复到 IDLE/PREPARE。
        gs.platformOfflineFaultActive = false;
        gs.platformOfflineEventLatched = false;
        gs.platformOfflinePendingTime = std::chrono::steady_clock::time_point();
        if (gs.state == STATE_ERROR && !gs.meterOfflineFaultActive && !gs.pileOfflineFaultActive) {
            if (gs.hasVehicleConnectStatus && gs.lastVehicleConnectStatus != 0) {
                transitionTo(gun, STATE_PREPARE, "platform_online");
            } else {
                transitionTo(gun, STATE_IDLE, "platform_online");
            }
        }
        return;
    }
    if (event != "platform_offline") {
        return;
    }

    // BY ZF: 平台离线增加 30 秒待确认窗口，避免链路短抖误报码。
    if (!gs.platformOfflineEventLatched && gs.platformOfflinePendingTime.time_since_epoch().count() == 0) {
        gs.platformOfflinePendingTime = std::chrono::steady_clock::now();
    }
}

void ChargeLogicProcess::startOfflineCardFlow(uint8_t gun, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (gs.state != STATE_PREPARE) {
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "cmd", "request_card_start");
        cJSON_AddStringToObject(evt, "state", stateToString(gs.state));
        cJSON_AddStringToObject(evt, "reason", "card_start_only_allowed_in_prepare");
        publishLogicEvent(gun, "cmd_reject", evt);
        cJSON_Delete(evt);
        return;
    }
    if (isCardReaderBusy()) {
        // BY ZF: 记录刷卡启动被占用拒绝，便于排查多枪共享读卡器时序问题。
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",phase=" << static_cast<int>(m_cardReaderState.phase)
            << ",activeGun=" << static_cast<int>(m_cardReaderState.gun);
        m_logSender.info("card_flow_busy", oss.str());
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "cmd", "request_card_start");
        cJSON_AddStringToObject(evt, "reason", "card_reader_busy");
        cJSON_AddNumberToObject(evt, "activeGun", static_cast<double>(m_cardReaderState.gun));
        publishLogicEvent(gun, "cmd_reject", evt);
        cJSON_Delete(evt);
        return;
    }

    gs.pendingCardStartAuthData.clear();
    if (data) {
        char* out = cJSON_PrintUnformatted(data);
        if (out) {
            gs.pendingCardStartAuthData = out;
            cJSON_free(out);
        }
    }
    m_cardReaderState.gun = gun;
    m_cardReaderState.phase = CARD_PHASE_WAIT_START_CARD;
    {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",rfOpened=" << (m_cardReaderState.rfOpened ? 1 : 0);
        m_logSender.info("card_flow_start", oss.str());
    }
    if (!m_cardReaderState.rfOpened) {
        // BY ZF: 共享读卡器首次进入离线刷卡时打开射频，后续存在刷卡会话时保持开启。
        m_cardReaderState.rfOpened = true;
        publishCardCmd("open_rf", NULL, false, 0U);
    }

    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "mode", "offline");
    cJSON_AddNumberToObject(evt, "rfOpened", m_cardReaderState.rfOpened ? 1 : 0);
    publishLogicEvent(gun, "card_waiting", evt);
    cJSON_Delete(evt);
}

void ChargeLogicProcess::beginOfflineCardCharge(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    GunState& gs = m_gunStates[gun];
    gs.offlineCardChargeActive = true;
    gs.offlineCardStopBySwipeTriggered = false;
    gs.offlineCardSettlementPending = false;

    cJSON* data = NULL;
    if (!gs.pendingCardStartAuthData.empty()) {
        data = cJSON_Parse(gs.pendingCardStartAuthData.c_str());
    }
    if (!data) {
        data = cJSON_CreateObject();
    }
    cJSON_DeleteItemFromObject(data, "orderNo");
    cJSON_DeleteItemFromObject(data, "prechargeAmount");
    cJSON_ReplaceItemInObject(data, "chargeUserNo", cJSON_CreateString(gs.offlineCardNoHex.c_str()));
    cJSON_ReplaceItemInObject(data, "cardNumber", cJSON_CreateString(gs.offlineCardNoHex.c_str()));
    if (!cJSON_GetObjectItem(data, "chargeMode")) {
        cJSON_AddNumberToObject(data, "chargeMode", 2);
    }
    cJSON_ReplaceItemInObject(data, "prechargeAmount",
                              cJSON_CreateNumber(static_cast<double>(gs.offlineCardStartBalance) / 100.0));

    // BY ZF: HMI 若未携带计费模型，则沿用当前枪已同步的本地计费模型。
    const bool hasFeeModelPayload = cJSON_GetObjectItem(data, "timeNum") &&
                                    cJSON_GetObjectItem(data, "timeSeg") &&
                                    cJSON_GetObjectItem(data, "chargeFee") &&
                                    cJSON_GetObjectItem(data, "serviceFee");
    if (!hasFeeModelPayload && gs.feeInitialized) {
        cJSON_ReplaceItemInObject(data, "feeModelNo", cJSON_CreateNumber(gs.feeModelNo));
        cJSON_ReplaceItemInObject(data, "feeModelId", cJSON_CreateString(gs.feeModelId.c_str()));
        cJSON_ReplaceItemInObject(data, "timeNum", cJSON_CreateNumber(static_cast<double>(gs.feeTimeNum)));
        cJSON* timeSegArr = cJSON_CreateArray();
        cJSON* chargeFeeArr = cJSON_CreateArray();
        cJSON* serviceFeeArr = cJSON_CreateArray();
        for (size_t i = 0; i < gs.feeTimeSegMinutes.size(); ++i) {
            cJSON_AddItemToArray(timeSegArr, cJSON_CreateString(minuteToHHMM(gs.feeTimeSegMinutes[i]).c_str()));
            cJSON_AddItemToArray(chargeFeeArr, cJSON_CreateNumber(gs.feeChargePricePerKwh[i]));
            cJSON_AddItemToArray(serviceFeeArr, cJSON_CreateNumber(gs.feeServicePricePerKwh[i]));
        }
        cJSON_ReplaceItemInObject(data, "timeSeg", timeSegArr);
        cJSON_ReplaceItemInObject(data, "chargeFee", chargeFeeArr);
        cJSON_ReplaceItemInObject(data, "serviceFee", serviceFeeArr);
    }

    if (!hasFeeModelPayload && !gs.feeInitialized) {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",feeModelNo=" << gs.feeModelNo
            << ",feeModelId=" << gs.feeModelId;
        m_logSender.warn("card_flow_fee_model_missing", oss.str());
    }

    updateAuthBasis(gun, data, "card_offline");
    gs.pendingStart = true;
    gs.pendingStartData.clear();
    cJSON* pileStartData = buildPileStartData(NULL);
    if (pileStartData) {
        char* out = cJSON_PrintUnformatted(pileStartData);
        if (out) {
            gs.pendingStartData = out;
            cJSON_free(out);
        }
        cJSON_Delete(pileStartData);
    }
    cJSON_Delete(data);
    gs.pendingCardStartAuthData.clear();

    {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",cardNoHex=" << gs.offlineCardNoHex
            << ",startBalance=" << gs.offlineCardStartBalance;
        m_logSender.info("card_flow_begin_charge", oss.str());
    }
    resetCardReaderState();
    handleEvent(gun, EVT_START_CMD, "card_start_cmd");
    handleEvent(gun, EVT_AUTH_OK, "card_auth_ok");
    maybeStartPendingOfflineCardSettlement();
}

void ChargeLogicProcess::prepareOfflineCardSettlement(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (!gs.offlineCardChargeActive && !gs.offlineCardSettlementPending) {
        return;
    }

    const long long totalFeeCent = roundNearestLongLong(std::max(0.0, gs.feeTotalElectricAmount + gs.feeTotalServiceAmount) * 100.0);
    const long long remain = static_cast<long long>(gs.offlineCardStartBalance) - totalFeeCent;
    gs.offlineCardFinalBalance = static_cast<uint32_t>(remain > 0 ? remain : 0);
    gs.offlineCardChargeActive = false;
    gs.offlineCardSettlementPending = true;
    {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",startBalance=" << gs.offlineCardStartBalance
            << ",totalFeeCent=" << totalFeeCent
            << ",finalBalance=" << gs.offlineCardFinalBalance;
        m_logSender.info("card_flow_prepare_settlement", oss.str());
    }

    maybeStartPendingOfflineCardSettlement();
}

void ChargeLogicProcess::beginOfflineCardSettlement(uint8_t gun)
{
    if (gun >= m_gunStates.size() || isCardReaderBusy()) {
        return;
    }

    GunState& gs = m_gunStates[gun];
    if (!gs.offlineCardSettlementPending) {
        return;
    }

    m_cardReaderState.gun = gun;
    m_cardReaderState.phase = CARD_PHASE_WAIT_SETTLEMENT_WRITE;
    {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",cardNoHex=" << gs.offlineCardNoHex
            << ",finalBalance=" << gs.offlineCardFinalBalance;
        m_logSender.info("card_flow_begin_settlement", oss.str());
    }
    publishCardCmd("card_write", gs.offlineCardNoHex.c_str(), true, gs.offlineCardFinalBalance);

    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
    cJSON_AddNumberToObject(evt, "startBalance", static_cast<double>(gs.offlineCardStartBalance));
    cJSON_AddNumberToObject(evt, "finalBalance", static_cast<double>(gs.offlineCardFinalBalance));
    publishLogicEvent(gun, "card_settlement_begin", evt);
    cJSON_Delete(evt);
}

void ChargeLogicProcess::finishOfflineCardSettlement(uint8_t gun, bool closeRfCompleted)
{
    if (gun >= m_gunStates.size()) {
        resetCardReaderState();
        return;
    }

    GunState& gs = m_gunStates[gun];
    {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",cardNoHex=" << gs.offlineCardNoHex
            << ",finalBalance=" << gs.offlineCardFinalBalance
            << ",closeRfCompleted=" << (closeRfCompleted ? 1 : 0);
        m_logSender.info("card_flow_finish_settlement", oss.str());
    }
    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
    cJSON_AddNumberToObject(evt, "cardBalance", static_cast<double>(gs.offlineCardFinalBalance));
    publishLogicEvent(gun, "card_session_closed", evt);
    cJSON_Delete(evt);

    resetGunOfflineCardState(gun);
    if (closeRfCompleted) {
        m_cardReaderState.rfOpened = false;
    }
    resetCardReaderState();
    maybeStartPendingOfflineCardSettlement();
}

void ChargeLogicProcess::resetCardReaderState()
{
    const bool rfOpened = m_cardReaderState.rfOpened;
    m_cardReaderState = CardReaderState();
    m_cardReaderState.rfOpened = rfOpened;
}

void ChargeLogicProcess::resetGunOfflineCardState(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    GunState& gs = m_gunStates[gun];
    gs.offlineCardChargeActive = false;
    gs.offlineCardStopBySwipeTriggered = false;
    gs.offlineCardSettlementPending = false;
    gs.offlineCardNoHex.clear();
    gs.offlineCardStartBalance = 0;
    gs.offlineCardLatestBalance = 0;
    gs.offlineCardFinalBalance = 0;
    gs.pendingCardStartAuthData.clear();
}

bool ChargeLogicProcess::hasOfflineCardChargingSession() const
{
    for (size_t i = 0; i < m_gunStates.size(); ++i) {
        if (m_gunStates[i].offlineCardChargeActive) {
            return true;
        }
    }
    return false;
}

bool ChargeLogicProcess::hasPendingCardSettlement() const
{
    for (size_t i = 0; i < m_gunStates.size(); ++i) {
        if (m_gunStates[i].offlineCardSettlementPending) {
            return true;
        }
    }
    return false;
}

bool ChargeLogicProcess::isCardReaderBusy() const
{
    return m_cardReaderState.phase != CARD_PHASE_IDLE;
}

void ChargeLogicProcess::maybeStartPendingOfflineCardSettlement()
{
    if (isCardReaderBusy()) {
        return;
    }
    for (uint8_t gun = 0; gun < m_gunStates.size(); ++gun) {
        if (m_gunStates[gun].offlineCardSettlementPending) {
            beginOfflineCardSettlement(gun);
            return;
        }
    }
}

void ChargeLogicProcess::handleCardEvent(const std::string& event, cJSON* data)
{
    {
        std::ostringstream oss;
        oss << "event=" << event
            << ",phase=" << static_cast<int>(m_cardReaderState.phase)
            << ",gun=" << static_cast<int>(m_cardReaderState.gun);
        m_logSender.info("card_event_rx", oss.str());
    }
    if (event == "rf_opened") {
        m_cardReaderState.rfOpened = true;
        return;
    }

    if (event == "rf_closed") {
        if (m_cardReaderState.phase == CARD_PHASE_WAIT_RF_CLOSE) {
            finishOfflineCardSettlement(m_cardReaderState.gun, true);
        } else {
            m_cardReaderState.rfOpened = false;
        }
        return;
    }

    if (event == "card_info") {
        const char* cardNoHex = getString(data, "cardNoHex");
        int cardBalance = 0;
        jsonGetInt(data, "cardBalance", cardBalance);
        const bool locked = cJSON_IsTrue(cJSON_GetObjectItem(data, "locked"));
        const std::string cardNo = (cardNoHex && cardNoHex[0] != '\0') ? std::string(cardNoHex) : std::string();

        if (m_cardReaderState.phase == CARD_PHASE_WAIT_START_CARD &&
            m_cardReaderState.gun < m_gunStates.size()) {
            GunState& gs = m_gunStates[m_cardReaderState.gun];
            if (!cardNo.empty()) {
                gs.offlineCardNoHex = cardNo;
            }
            if (cardBalance >= 0) {
                gs.offlineCardLatestBalance = static_cast<uint32_t>(cardBalance);
            }
            if (locked) {
                {
                    std::ostringstream oss;
                    oss << "gun=" << static_cast<int>(m_cardReaderState.gun)
                        << ",cardNoHex=" << gs.offlineCardNoHex
                        << ",cardBalance=" << gs.offlineCardLatestBalance;
                    m_logSender.warn("card_auth_locked_reject", oss.str());
                }
                cJSON* evt = cJSON_CreateObject();
                cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
                cJSON_AddNumberToObject(evt, "cardBalance", static_cast<double>(gs.offlineCardLatestBalance));
                cJSON_AddStringToObject(evt, "reason", "card_locked");
                publishLogicEvent(m_cardReaderState.gun, "card_auth_reject", evt);
                cJSON_Delete(evt);
            } else if (cardBalance > 100) {
                gs.offlineCardStartBalance = static_cast<uint32_t>(cardBalance);
                m_cardReaderState.phase = CARD_PHASE_WAIT_START_LOCK;
                {
                    std::ostringstream oss;
                    oss << "gun=" << static_cast<int>(m_cardReaderState.gun)
                        << ",cardNoHex=" << gs.offlineCardNoHex
                        << ",cardBalance=" << gs.offlineCardStartBalance;
                    m_logSender.info("card_auth_pass", oss.str());
                }
                cJSON* evt = cJSON_CreateObject();
                cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
                cJSON_AddNumberToObject(evt, "cardBalance", static_cast<double>(gs.offlineCardStartBalance));
                publishLogicEvent(m_cardReaderState.gun, "card_auth_ok", evt);
                cJSON_Delete(evt);
                publishCardCmd("card_lock", gs.offlineCardNoHex.c_str(), false, 0U);
            } else {
                {
                    std::ostringstream oss;
                    oss << "gun=" << static_cast<int>(m_cardReaderState.gun)
                        << ",cardNoHex=" << gs.offlineCardNoHex
                        << ",cardBalance=" << gs.offlineCardLatestBalance;
                    m_logSender.info("card_auth_reject", oss.str());
                }
                cJSON* evt = cJSON_CreateObject();
                cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
                cJSON_AddNumberToObject(evt, "cardBalance", static_cast<double>(gs.offlineCardLatestBalance));
                cJSON_AddStringToObject(evt, "reason", "card_balance_not_enough");
                publishLogicEvent(m_cardReaderState.gun, "card_auth_reject", evt);
                cJSON_Delete(evt);
            }
            return;
        }

        if (m_cardReaderState.phase == CARD_PHASE_WAIT_SETTLEMENT_VERIFY &&
            m_cardReaderState.gun < m_gunStates.size()) {
            if (!locked) {
                bool hasOtherOfflineSession = false;
                for (uint8_t gun = 0; gun < m_gunStates.size(); ++gun) {
                    if (gun == m_cardReaderState.gun) {
                        continue;
                    }
                    if (m_gunStates[gun].offlineCardChargeActive || m_gunStates[gun].offlineCardSettlementPending) {
                        hasOtherOfflineSession = true;
                        break;
                    }
                }
                const bool needCloseRf = !hasOtherOfflineSession;
                if (needCloseRf) {
                    m_cardReaderState.phase = CARD_PHASE_WAIT_RF_CLOSE;
                    publishCardCmd("close_rf", NULL, false, 0U);
                } else {
                    finishOfflineCardSettlement(m_cardReaderState.gun, false);
                }
            }
            return;
        }

        for (uint8_t gun = 0; gun < m_gunStates.size(); ++gun) {
            GunState& gs = m_gunStates[gun];
            if (gs.state != STATE_CHARGING || !gs.offlineCardChargeActive || gs.offlineCardStopBySwipeTriggered) {
                continue;
            }
            if (!cardNo.empty() && !gs.offlineCardNoHex.empty() && gs.offlineCardNoHex != cardNo) {
                continue;
            }
            if (cardBalance >= 0) {
                gs.offlineCardLatestBalance = static_cast<uint32_t>(cardBalance);
            }
            const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(0x0105U));
            if (result.valid) {
                gs.stopReason = result.reason;
            }
            gs.offlineCardStopBySwipeTriggered = true;
            {
                std::ostringstream oss;
                oss << "gun=" << static_cast<int>(gun)
                    << ",cardNoHex=" << gs.offlineCardNoHex
                    << ",cardBalance=" << gs.offlineCardLatestBalance;
                m_logSender.info("card_stop_trigger", oss.str());
            }
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cardNoHex", gs.offlineCardNoHex.c_str());
            cJSON_AddNumberToObject(evt, "cardBalance", static_cast<double>(gs.offlineCardLatestBalance));
            publishLogicEvent(gun, "card_stop_request", evt);
            cJSON_Delete(evt);
            handleEvent(gun, EVT_STOP_CMD, "card_swipe_stop");
            return;
        }
        return;
    }

    if (event == "card_locked") {
        if (m_cardReaderState.phase == CARD_PHASE_WAIT_START_LOCK &&
            m_cardReaderState.gun < m_gunStates.size()) {
            beginOfflineCardCharge(m_cardReaderState.gun);
        }
        return;
    }

    if (event == "card_written") {
        if (m_cardReaderState.phase == CARD_PHASE_WAIT_SETTLEMENT_WRITE &&
            m_cardReaderState.gun < m_gunStates.size()) {
            m_cardReaderState.phase = CARD_PHASE_WAIT_SETTLEMENT_UNLOCK;
            publishCardCmd("card_unlock", m_gunStates[m_cardReaderState.gun].offlineCardNoHex.c_str(), false, 0U);
        }
        return;
    }

    if (event == "card_unlocked") {
        if (m_cardReaderState.phase == CARD_PHASE_WAIT_SETTLEMENT_UNLOCK) {
            m_cardReaderState.phase = CARD_PHASE_WAIT_SETTLEMENT_VERIFY;
        }
        return;
    }

    if (event == "op_failed") {
        const char* op = getString(data, "op");
        const char* reason = getString(data, "reason");
        const uint8_t gun = (m_cardReaderState.gun < m_gunStates.size()) ? m_cardReaderState.gun : 0U;
        {
            std::ostringstream oss;
            oss << "gun=" << static_cast<int>(gun)
                << ",op=" << (op ? op : "")
                << ",reason=" << (reason ? reason : "");
            m_logSender.warn("card_op_failed", oss.str());
        }
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "op", op ? op : "");
        cJSON_AddStringToObject(evt, "reason", reason ? reason : "");
        if (gun < m_gunStates.size()) {
            cJSON_AddStringToObject(evt, "cardNoHex", m_gunStates[gun].offlineCardNoHex.c_str());
        }
        publishLogicEvent(gun, "card_op_failed", evt);
        cJSON_Delete(evt);

        if (op && std::strcmp(op, "open_rf") == 0) {
            m_cardReaderState.rfOpened = false;
            resetCardReaderState();
        } else if (op && std::strcmp(op, "card_lock") == 0) {
            m_cardReaderState.phase = CARD_PHASE_WAIT_START_CARD;
        } else if (op && std::strcmp(op, "card_write") == 0) {
            resetCardReaderState();
        } else if (op && std::strcmp(op, "card_unlock") == 0) {
            resetCardReaderState();
        } else if (op && std::strcmp(op, "close_rf") == 0) {
            resetCardReaderState();
        }
        return;
    }
}

bool ChargeLogicProcess::maybeConfirmMeterOfflineFault(uint8_t gun, const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }

    GunState& gs = m_gunStates[gun];
    if (gs.meterOfflineEventLatched ||
        gs.meterOfflinePendingTime.time_since_epoch().count() == 0 ||
        now - gs.meterOfflinePendingTime < std::chrono::seconds(10)) {
        return false;
    }

    gs.meterOfflinePendingTime = std::chrono::steady_clock::time_point();
    gs.meterOfflineFaultActive = true;
    gs.meterOfflineEventLatched = true;

    if (gs.state == STATE_STARTING) {
        const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x0101U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "meter_offline_confirmed_10s");
        return true;
    }
    if (gs.state == STATE_CHARGING) {
        const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(0x0101U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "meter_offline_confirmed_10s");
        return true;
    }
    if (gs.state == STATE_STOPPING) {
        return false;
    }

    const FaultJudgeResult result = JudgeStandbyFaultPoint(MakeStandbyPointKey(0x0101U));
    publishSaveErrorEvent(gun, result, 0x0101U, "meter");
    if (gs.state != STATE_ERROR) {
        handleEvent(gun, EVT_DEVICE_ERR, "meter_offline_confirmed_10s");
        return true;
    }
    return false;
}

bool ChargeLogicProcess::maybeConfirmPlatformOfflineFault(uint8_t gun, const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }

    GunState& gs = m_gunStates[gun];
    if (gs.platformOfflineEventLatched ||
        gs.platformOfflinePendingTime.time_since_epoch().count() == 0 ||
        now - gs.platformOfflinePendingTime < std::chrono::seconds(30)) {
        return false;
    }

    gs.platformOfflinePendingTime = std::chrono::steady_clock::time_point();
    gs.platformOfflineFaultActive = true;
    gs.platformOfflineEventLatched = true;

    if (gs.state == STATE_STARTING) {
        const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x0102U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "platform_offline_confirmed_30s");
        return true;
    }
    if (gs.state == STATE_CHARGING) {
        const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(0x0102U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "platform_offline_confirmed_30s");
        return true;
    }
    if (gs.state == STATE_STOPPING) {
        return false;
    }

    const FaultJudgeResult result = JudgeStandbyFaultPoint(MakeStandbyPointKey(0x0102U));
    publishSaveErrorEvent(gun, result, 0x0102U, "platform");
    if (gs.state != STATE_ERROR) {
        handleEvent(gun, EVT_DEVICE_ERR, "platform_offline_confirmed_30s");
        return true;
    }
    return false;
}

bool ChargeLogicProcess::maybeConfirmPileOfflineFault(uint8_t gun, const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }

    GunState& gs = m_gunStates[gun];
    if (gs.pileOfflineEventLatched ||
        gs.pileOfflinePendingTime.time_since_epoch().count() == 0 ||
        now - gs.pileOfflinePendingTime < std::chrono::seconds(30)) {
        return false;
    }

    gs.pileOfflinePendingTime = std::chrono::steady_clock::time_point();
    gs.pileOfflineFaultActive = true;
    gs.pileOfflineEventLatched = true;

    if (gs.state == STATE_STARTING) {
        const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x0104U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "pile_offline_confirmed_30s");
        return true;
    }
    if (gs.state == STATE_CHARGING) {
        const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(0x0104U));
        if (result.valid) {
            gs.stopReason = result.reason;
        }
        handleEvent(gun, EVT_DEVICE_ERR, "pile_offline_confirmed_30s");
        return true;
    }
    if (gs.state == STATE_STOPPING) {
        return false;
    }

    const FaultJudgeResult result = JudgeStandbyFaultPoint(MakeStandbyPointKey(0x0104U));
    publishSaveErrorEvent(gun, result, 0x0104U, "pile");
    if (gs.state != STATE_ERROR) {
        handleEvent(gun, EVT_DEVICE_ERR, "pile_offline_confirmed_30s");
        return true;
    }
    return false;
}

void ChargeLogicProcess::publishPileCmd(uint8_t gun, const std::string& cmd, cJSON* data)
{
    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/pile/" << (static_cast<int>(gun) + m_config.biasNo) << "/cmd";

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

void ChargeLogicProcess::publishCardCmd(const std::string& op,
                                        const char* cardNoHex,
                                        bool hasCardBalance,
                                        uint32_t cardBalance)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_logic");
    cJSON_AddStringToObject(root, "op", op.c_str());
    if (cardNoHex && cardNoHex[0] != '\0') {
        cJSON_AddStringToObject(root, "cardNoHex", cardNoHex);
    }
    if (hasCardBalance) {
        cJSON_AddNumberToObject(root, "cardBalance", static_cast<double>(cardBalance));
    }
    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        m_mqtt.publish(getCardCmdTopic(), out, 1, false);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

std::string ChargeLogicProcess::getCardCmdTopic() const
{
    return m_config.mqttTopicPrefix + "/card/0/cmd";
}

std::string ChargeLogicProcess::getCardEventTopic() const
{
    return m_config.mqttTopicPrefix + "/card/0/event";
}

void ChargeLogicProcess::publishLogicEvent(uint8_t gun, const std::string& event, cJSON* data)
{
    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gun) + m_config.biasNo) << "/event";

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
        m_mqtt.publish(topic.str(), out, 2, true);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void ChargeLogicProcess::publishPlugAndChargeAuthRequest(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (!gs.plugAndChargeActive || !gs.plugAndChargeVehicleIdConfirmed || gs.plugAndChargeAuthRequestPublished) {
        return;
    }

    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "vin", gs.vinCode.c_str());
    cJSON_AddStringToObject(evt, "vinCode", gs.vinCode.c_str());
    cJSON_AddNumberToObject(evt, "plugAndChargeFlag", gs.plugAndChargeFlag);
    cJSON_AddNumberToObject(evt, "mergeChargeFlag", gs.mergeChargeFlag);
    cJSON_AddNumberToObject(evt, "chargeStartTime", static_cast<double>(gs.chargeStartTime));
    cJSON_AddNumberToObject(evt, "soc", gs.plugAndChargeBatterySoc);
    cJSON_AddNumberToObject(evt, "currentBatteryVoltage", gs.plugAndChargeCurrentBatteryVoltage);
    cJSON* countArr = cJSON_CreateArray();
    for (int i = 0; i < 3; ++i) {
        cJSON_AddItemToArray(countArr, cJSON_CreateNumber(gs.plugAndChargeBatteryChargeCount[i]));
    }
    cJSON_AddItemToObject(evt, "batteryChargeCount", countArr);
    publishLogicEvent(gun, "plug_and_charge_auth_request", evt);
    cJSON_Delete(evt);

    gs.plugAndChargeAuthRequestPublished = true;
    gs.plugAndChargeAuthRequestTime = std::chrono::steady_clock::now();
}

void ChargeLogicProcess::publishFeeData(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    const GunState& gs = m_gunStates[gun];

    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gun) + m_config.biasNo) << "/feeData";

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
    // BY ZF: 充电时长（秒），按“进入 CHARGING 到当前发送时刻”计算。
    double chargedTimeSec = 0.0;
    if (gs.chargingEnterTime.time_since_epoch().count() > 0) {
        const auto nowSteady = std::chrono::steady_clock::now();
        if (nowSteady > gs.chargingEnterTime) {
            chargedTimeSec = std::chrono::duration_cast<std::chrono::seconds>(
                nowSteady - gs.chargingEnterTime).count();
        }
    }
    cJSON_AddNumberToObject(data, "chargedTime", chargedTimeSec);
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

    // BY ZF: 记录已发布快照，用于“变化即送”判定。
    GunState& mgs = m_gunStates[gun];
    mgs.feeLastPublishedTotalEnergy = roundTo5(mgs.feeTotalEnergyKwh);
    mgs.feeLastPublishedElectricAmount = roundTo5(mgs.feeTotalElectricAmount);
    mgs.feeLastPublishedServiceAmount = roundTo5(mgs.feeTotalServiceAmount);
    mgs.lastFeeDataPublishTime = std::chrono::steady_clock::now();
}

void ChargeLogicProcess::publishSaveErrorEvent(uint8_t gun, const FaultJudgeResult& result, unsigned int rawValue, const char* faultSource)
{
    if (!result.valid) {
        return;
    }
    if (!result.treatAsFault) {
        return;
    }

    std::ostringstream topic;
    topic << m_config.mqttTopicPrefix << "/save/" << (static_cast<int>(gun) + m_config.biasNo) << "/event";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_logic");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "type", "Error");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "occurTime", toYmdHmsString(std::time(NULL)).c_str());
    cJSON_AddNumberToObject(data, "faultType", result.faultType);
    cJSON_AddStringToObject(data, "pointKey", result.pointKey.c_str());
    cJSON_AddStringToObject(data, "faultSource", faultSource ? faultSource : "");
    cJSON_AddStringToObject(data, "faultMessage", result.message.c_str());
    cJSON_AddNumberToObject(data, "rawValue", rawValue);
    cJSON_AddItemToObject(root, "data", data);

    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        m_mqtt.publish(topic.str(), out, 2, true);
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
    if (gun < m_gunStates.size() && m_gunStates[gun].stopReason != 0U) {
        cJSON_AddNumberToObject(data, "stopReason", static_cast<double>(m_gunStates[gun].stopReason));
        const char* stopReasonText = QueryReasonText(m_gunStates[gun].stopReason);
        if (stopReasonText && stopReasonText[0] != '\0') {
            cJSON_AddStringToObject(data, "stopReasonText", stopReasonText);
        }
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

bool ChargeLogicProcess::parseTradeRecordFromJson(cJSON* data, TradeRecord& rec)
{
    if (!data || !cJSON_IsObject(data)) {
        return false;
    }
    auto getIntField = [](cJSON* obj, const char* key, int& out) -> bool {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v)) {
            out = v->valueint;
            return true;
        }
        return false;
    };
    auto getDoubleField = [](cJSON* obj, const char* key, double& out) -> bool {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v)) {
            out = v->valuedouble;
            return true;
        }
        return false;
    };
    auto getUint64Field = [](cJSON* obj, const char* key, uint64_t& out) -> bool {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v) && v->valuedouble >= 0.0) {
            out = static_cast<uint64_t>(v->valuedouble);
            return true;
        }
        return false;
    };
    auto getStringField = [](cJSON* obj, const char* key, std::string& out) -> bool {
        cJSON* v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsString(v) && v->valuestring) {
            out = v->valuestring;
            return true;
        }
        return false;
    };
    auto getArrayDoubles = [](cJSON* obj, const char* key, std::vector<double>& out) {
        out.clear();
        cJSON* arr = cJSON_GetObjectItem(obj, key);
        if (!arr || !cJSON_IsArray(arr)) {
            return;
        }
        const int size = cJSON_GetArraySize(arr);
        out.reserve(size > 0 ? static_cast<size_t>(size) : 0U);
        for (int i = 0; i < size; ++i) {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (item && cJSON_IsNumber(item)) {
                out.push_back(item->valuedouble);
            }
        }
    };

    getIntField(data, "gunNo", rec.gunNo);
    getStringField(data, "preTradeNo", rec.preTradeNo);
    getStringField(data, "tradeNo", rec.tradeNo);
    getStringField(data, "vinCode", rec.vinCode);
    getIntField(data, "timeDivType", rec.timeDivType);
    getIntField(data, "startType", rec.startType);
    getUint64Field(data, "chargeStartTime", rec.chargeStartTime);
    getUint64Field(data, "chargeEndTime", rec.chargeEndTime);
    getDoubleField(data, "startSoc", rec.startSoc);
    getDoubleField(data, "endSoc", rec.endSoc);
    int reason = 0;
    if (getIntField(data, "reason", reason) && reason >= 0) {
        rec.reason = static_cast<unsigned int>(reason);
    }
    getStringField(data, "feeModelId", rec.feeModelId);
    getDoubleField(data, "sumStart", rec.sumStart);
    getDoubleField(data, "sumEnd", rec.sumEnd);
    getDoubleField(data, "totalElect", rec.totalElect);
    getDoubleField(data, "totalPowerCost", rec.totalPowerCost);
    getDoubleField(data, "totalServCost", rec.totalServCost);
    getDoubleField(data, "totalCost", rec.totalCost);
    getIntField(data, "timeNum", rec.timeNum);
    getIntField(data, "startPoint", rec.startPoint);
    getIntField(data, "crossPoints", rec.crossPoints);
    getStringField(data, "cardNumber", rec.cardNumber);
    getArrayDoubles(data, "partElect", rec.partElect);
    getArrayDoubles(data, "chargeFee", rec.chargeFee);
    getArrayDoubles(data, "serviceFee", rec.serviceFee);
    getArrayDoubles(data, "pointsElect", rec.pointsElect);
    return !rec.tradeNo.empty();
}

void ChargeLogicProcess::replayBufferedUnconfirmedRecords()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastReplayTime < std::chrono::seconds(10)) {
        return;
    }
    m_lastReplayTime = now;

    std::lock_guard<std::mutex> lock(m_unconfirmedMutex);
    for (size_t i = 0; i < m_gunStates.size() && i < m_unconfirmedRecordBuffer.size(); ++i) {
        if (m_gunStates[i].state != STATE_IDLE) {
            continue;
        }
        if (m_unconfirmedRecordBuffer[i].empty()) {
            continue;
        }
        TradeRecord rec = m_unconfirmedRecordBuffer[i].front();
        m_unconfirmedRecordBuffer[i].pop_front();
        rec.gunNo = static_cast<int>(i);
        publishUpdateRecordEvent(static_cast<uint8_t>(i), rec);
    }
}

int ChargeLogicProcess::getMergePeerGun(uint8_t gun) const
{
    if (m_gunStates.size() < 2) {
        return -1;
    }
    const uint8_t peer = (gun % 2 == 0) ? static_cast<uint8_t>(gun + 1) : static_cast<uint8_t>(gun - 1);
    if (peer >= m_gunStates.size()) {
        return -1;
    }
    return static_cast<int>(peer);
}

bool ChargeLogicProcess::armPendingStart(uint8_t gun, cJSON* data, const char* source)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }

    updateAuthBasis(gun, data, source);
    GunState& gs = m_gunStates[gun];
    gs.pendingStart = true;
    gs.pendingStartData.clear();

    cJSON* pileStartData = buildPileStartData(data);
    if (pileStartData) {
        char* out = cJSON_PrintUnformatted(pileStartData);
        if (out) {
            gs.pendingStartData = out;
            cJSON_free(out);
        }
        cJSON_Delete(pileStartData);
    }
    return true;
}

void ChargeLogicProcess::dispatchArmedStart(uint8_t gun, const char* startReason, const char* authReason)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    handleEvent(gun, EVT_START_CMD, startReason);
    handleEvent(gun, EVT_AUTH_OK, authReason);
}

void ChargeLogicProcess::syncMergePrechargeAmount(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];
    if (gs.mergeChargeFlag == 0x00) {
        return;
    }

    const int peer = getMergePeerGun(gun);
    if (peer < 0) {
        return;
    }
    GunState& peerGs = m_gunStates[peer];
    if (!peerGs.hasAuthBasis || peerGs.mergeChargeFlag == 0x00) {
        return;
    }
    if (gs.prechargeAmount <= 0.0 || peerGs.prechargeAmount <= 0.0) {
        return;
    }

    // BY ZF: 合并充总预充值金额应保持一致；若平台分枪下发不一致，统一取最小值。
    const double effective = std::min(gs.prechargeAmount, peerGs.prechargeAmount);
    gs.prechargeAmount = effective;
    peerGs.prechargeAmount = effective;
}

double ChargeLogicProcess::getEffectivePrechargeAmount(uint8_t gun) const
{
    if (gun >= m_gunStates.size()) {
        return 0.0;
    }
    const GunState& gs = m_gunStates[gun];
    double effective = gs.prechargeAmount;
    if (gs.mergeChargeFlag == 0x00) {
        return effective;
    }

    const int peer = getMergePeerGun(gun);
    if (peer < 0) {
        return effective;
    }
    const GunState& peerGs = m_gunStates[peer];
    if (!peerGs.hasAuthBasis || peerGs.mergeChargeFlag == 0x00 || peerGs.prechargeAmount <= 0.0) {
        return effective;
    }
    if (effective <= 0.0) {
        return peerGs.prechargeAmount;
    }
    return std::min(effective, peerGs.prechargeAmount);
}

double ChargeLogicProcess::getEffectiveTotalAmount(uint8_t gun) const
{
    if (gun >= m_gunStates.size()) {
        return 0.0;
    }
    const GunState& gs = m_gunStates[gun];
    double total = gs.hasTotalAmount ? gs.lastTotalAmount : 0.0;
    if (gs.mergeChargeFlag == 0x00) {
        return total;
    }

    const int peer = getMergePeerGun(gun);
    if (peer < 0) {
        return total;
    }
    const GunState& peerGs = m_gunStates[peer];
    if (peerGs.mergeChargeFlag == 0x00) {
        return total;
    }
    if (peerGs.hasTotalAmount) {
        total += peerGs.lastTotalAmount;
    }
    return total;
}

void ChargeLogicProcess::updateAuthBasis(uint8_t gun, cJSON* data, const char* source)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    GunState& gs = m_gunStates[gun];

    // BY ZF: 充电开始时间统一以 logic 接收到启动命令的本机时间为准，不使用平台透传 startTime。
    uint64_t startTs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::string userNo;
    std::string orderNo;
    std::string preTradeNo;
    int chargeMode = 0;
    double prechargeAmount = 0.0;
    int feeModelNo = 0;
    int v2g = 0;

    if (data) {
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

        jsonGetInt(data, "v2g", v2g);
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
    if (source && std::string(source) == "platform") {
        gs.startType = 2;
    } else if (source && std::string(source) == "card_offline") {
        gs.startType = 4;
    } else {
        gs.startType = 1;
    }
    gs.startSoc = 0;
    gs.endSoc = 0;
    gs.stopReason = 0;
    gs.plugAndChargeFlag = getPlugAndChargeFlag(data);
    gs.mergeChargeFlag = getMergeChargeFlag(data);
    gs.plugAndChargeActive = (gs.plugAndChargeFlag == 0x02);
    gs.plugAndChargeVehicleIdReceived = false;
    gs.plugAndChargeVehicleIdConfirmed = false;
    gs.plugAndChargeAuthRequestPublished = false;
    gs.plugAndChargeAuthResultSent = false;
    gs.plugAndChargeAuthAckReceived = false;
    gs.plugAndChargeBatteryChargeCount[0] = 0;
    gs.plugAndChargeBatteryChargeCount[1] = 0;
    gs.plugAndChargeBatteryChargeCount[2] = 0;
    gs.plugAndChargeBatterySoc = 0;
    gs.plugAndChargeCurrentBatteryVoltage = 0;
    gs.plugAndChargeAuthRequestTime = std::chrono::steady_clock::time_point();
    gs.hasVinCode = false;
    gs.vinCode.clear();
    gs.chargeMode = chargeMode;
    gs.prechargeAmount = prechargeAmount;
    gs.v2gMode = (v2g != 0);
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

    // BY ZF: 从启动命令解析计费模型（HHMM时段 + 分段电费/服务费）。
    parseFeeModel(gun, data);
    syncMergePrechargeAmount(gun);

    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "source", source ? source : "");
    cJSON_AddNumberToObject(evt, "startTime", static_cast<double>(gs.startTimeMs));
    cJSON_AddStringToObject(evt, "chargeUserNo", gs.chargeUserNo.c_str());
    cJSON_AddStringToObject(evt, "orderNo", gs.orderNo.c_str());
    cJSON_AddNumberToObject(evt, "chargeMode", gs.chargeMode);
    cJSON_AddNumberToObject(evt, "prechargeAmount", gs.prechargeAmount);
    cJSON_AddNumberToObject(evt, "feeModelNo", gs.feeModelNo);
    cJSON_AddNumberToObject(evt, "plugAndChargeFlag", gs.plugAndChargeFlag);
    cJSON_AddNumberToObject(evt, "mergeChargeFlag", gs.mergeChargeFlag);
    cJSON_AddNumberToObject(evt, "v2g", (v2g != 0) ? 1 : 0);
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

    // BY ZF: 解析成功后直接保存计费模型到 logger/本地数据库。
    if (!gs.feeModelId.empty()) {
        FeeModel feeModel;
        feeModel.feeModelId = gs.feeModelId;
        feeModel.timeNum = gs.feeTimeNum;
        feeModel.timeSeg.reserve(gs.feeTimeSegMinutes.size());
        feeModel.segFlag.reserve(gs.feeTimeSegMinutes.size());
        feeModel.chargeFee.reserve(gs.feeChargePricePerKwh.size());
        feeModel.serviceFee.reserve(gs.feeServicePricePerKwh.size());

        for (size_t i = 0; i < gs.feeTimeSegMinutes.size(); ++i) {
            feeModel.timeSeg.push_back(minuteToHHMM(gs.feeTimeSegMinutes[i]));
            // BY ZF: 启动命令下发的是按时段展开的价格数组，这里按段号(1-based)落库。
            feeModel.segFlag.push_back(static_cast<unsigned int>(i + 1));
            // BY ZF: 统一按 10^-5 元保存单价，避免平台下发 5 位小数时丢精度。
            feeModel.chargeFee.push_back(static_cast<unsigned int>(
                roundNearest(std::max(0.0, gs.feeChargePricePerKwh[i]) * 100000.0)));
            feeModel.serviceFee.push_back(static_cast<unsigned int>(
                roundNearest(std::max(0.0, gs.feeServicePricePerKwh[i]) * 100000.0)));
        }

        m_logSender.saveFeeModel(feeModel);
    }
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
    const bool mergeCharge = (gs.mergeChargeFlag != 0x00);
    const int peer = mergeCharge ? getMergePeerGun(gun) : -1;
    if (mergeCharge && peer >= 0) {
        // BY ZF: 合并充余额停机只由主枪统一判定，避免双枪重复触发。
        const uint8_t primaryGun = (gun < static_cast<uint8_t>(peer)) ? gun : static_cast<uint8_t>(peer);
        if (gun != primaryGun) {
            return;
        }
    }
    if (gs.state != STATE_CHARGING || gs.tcuStopReqSent) {
        return;
    }
    const double effectivePrechargeAmount = getEffectivePrechargeAmount(gun);
    const double effectiveTotalAmount = getEffectiveTotalAmount(gun);
    if (!gs.hasAuthBasis || effectivePrechargeAmount <= 0.0) {
        return;
    }

    if (!gs.hasTotalAmount && !(mergeCharge && peer >= 0 && m_gunStates[peer].hasTotalAmount)) {
        return;
    }

    const double threshold = effectivePrechargeAmount - m_config.prechargeStopMargin;
    if (effectiveTotalAmount >= threshold) {
        const size_t targetCount = (mergeCharge && peer >= 0) ? 2U : 1U;
        for (size_t idx = 0; idx < targetCount; ++idx) {
            const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
            GunState& targetGs = m_gunStates[targetGun];
            if (targetGs.tcuStopReqSent) {
                continue;
            }

            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "reason", "precharge_near_limit");
            cJSON_AddNumberToObject(evt, "totalAmount", effectiveTotalAmount);
            cJSON_AddNumberToObject(evt, "selfAmount", targetGs.hasTotalAmount ? targetGs.lastTotalAmount : 0.0);
            if (mergeCharge && peer >= 0) {
                const uint8_t peerGun = (targetGun == gun) ? static_cast<uint8_t>(peer) : gun;
                const GunState& peerGs = m_gunStates[peerGun];
                cJSON_AddNumberToObject(evt, "peerGun", peerGun);
                cJSON_AddNumberToObject(evt, "peerAmount", peerGs.hasTotalAmount ? peerGs.lastTotalAmount : 0.0);
            }
            cJSON_AddNumberToObject(evt, "prechargeAmount", effectivePrechargeAmount);
            cJSON_AddNumberToObject(evt, "margin", m_config.prechargeStopMargin);
            cJSON_AddStringToObject(evt, "chargeUserNo", targetGs.chargeUserNo.c_str());
            cJSON_AddStringToObject(evt, "orderNo", targetGs.orderNo.c_str());
            cJSON_AddNumberToObject(evt, "feeModelNo", targetGs.feeModelNo);
            publishLogicEvent(targetGun, "tcu_stop_request", evt);
            cJSON_Delete(evt);

            targetGs.tcuStopReqSent = true;
            handleEvent(targetGun, EVT_STOP_CMD, "tcu_precharge_near_limit");
        }
    }
}

bool ChargeLogicProcess::maybeTriggerMeteringAbnormalByLowVoltage(uint8_t gun, const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }
    GunState& gs = m_gunStates[gun];
    if (gs.state != STATE_CHARGING || gs.meteringAbnormalTriggered) {
        return false;
    }
    if (!gs.hasMeterVoltage || gs.chargingEnterTime.time_since_epoch().count() == 0) {
        return false;
    }
    if (gs.chargingMeterVoltageNormalSeen || gs.lastMeterVoltage >= 10.0) {
        return false;
    }
    if (now - gs.chargingEnterTime < std::chrono::seconds(30)) {
        return false;
    }

    const FaultJudgeResult result = JudgeChargingFailPoint(MakeChargingPointKey(0x0101U));
    if (result.valid) {
        gs.stopReason = result.reason;
    }
    gs.meteringAbnormalTriggered = true;
    handleEvent(gun, EVT_DEVICE_ERR, "meter_voltage_below_10v_30s");
    return true;
}

bool ChargeLogicProcess::maybeHandlePlugAndChargeAuthTimeout(
    uint8_t gun,
    const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunStates.size()) {
        return false;
    }

    GunState& gs = m_gunStates[gun];
    const bool mergePlugAndCharge = (gs.plugAndChargeActive && gs.mergeChargeFlag != 0x00);
    const int peer = mergePlugAndCharge ? getMergePeerGun(gun) : -1;
    if (mergePlugAndCharge && peer >= 0) {
        // BY ZF: 合并充电只由主枪执行一次平台鉴权超时收口，避免双枪重复下发 0x19 失败。
        const uint8_t primaryGun = (gun < static_cast<uint8_t>(peer)) ? gun : static_cast<uint8_t>(peer);
        if (gun != primaryGun) {
            return false;
        }
    }
    if (!gs.plugAndChargeActive ||
        gs.state != STATE_STARTING ||
        !gs.plugAndChargeVehicleIdConfirmed ||
        gs.plugAndChargeAuthResultSent ||
        gs.plugAndChargeAuthRequestTime.time_since_epoch().count() == 0 ||
        now - gs.plugAndChargeAuthRequestTime < std::chrono::seconds(30)) {
        return false;
    }

    const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x34U));
    const size_t targetCount = (mergePlugAndCharge && peer >= 0) ? 2U : 1U;
    for (size_t idx = 0; idx < targetCount; ++idx) {
        const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
        GunState& targetGs = m_gunStates[targetGun];

        cJSON* pileData = cJSON_CreateObject();
        cJSON_AddStringToObject(pileData, "vin", targetGs.vinCode.c_str());
        cJSON_AddNumberToObject(pileData, "successFlag", 0x01);
        cJSON_AddNumberToObject(pileData, "failReason", 0x03);
        publishPileCmd(targetGun, "vehicle_auth", pileData);
        cJSON_Delete(pileData);

        targetGs.plugAndChargeAuthResultSent = true;
        if (result.valid) {
            targetGs.stopReason = result.reason;
        }

        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "vin", targetGs.vinCode.c_str());
        cJSON_AddNumberToObject(evt, "failReason", 0x03);
        if (mergePlugAndCharge && peer >= 0) {
            cJSON_AddNumberToObject(evt, "peerGun", (targetGun == gun) ? peer : gun);
        }
        if (result.valid) {
            cJSON_AddNumberToObject(evt, "stopReason", static_cast<double>(targetGs.stopReason));
            if (!result.message.empty()) {
                cJSON_AddStringToObject(evt, "stopReasonText", result.message.c_str());
            }
        }
        publishLogicEvent(targetGun, "plug_and_charge_auth_timeout", evt);
        cJSON_Delete(evt);

        if (targetGs.state == STATE_STARTING) {
            transitionTo(targetGun, STATE_STOPPED, "plug_and_charge_auth_timeout");
        }
    }
    return true;
}

void ChargeLogicProcess::handlePlugAndChargeVehicleId(uint8_t gun, cJSON* data)
{
    if (gun >= m_gunStates.size()) {
        return;
    }
    //TODO:流程仍需优化，有点太复杂了
    GunState& gs = m_gunStates[gun];
    std::string vin = sanitizeVinString(data ? getString(data, "vin") : "");
    if (!vin.empty()) {
        gs.vinCode = vin;
        gs.hasVinCode = true;
    }
    gs.plugAndChargeVehicleIdReceived = true;

    if (data) {
        cJSON* arr = cJSON_GetObjectItem(data, "batteryChargeCount");
        if (arr && cJSON_IsArray(arr)) {
            for (int i = 0; i < 3; ++i) {
                cJSON* item = cJSON_GetArrayItem(arr, i);
                if (item && cJSON_IsNumber(item)) {
                    gs.plugAndChargeBatteryChargeCount[i] = static_cast<uint8_t>(item->valueint);
                }
            }
        }
        cJSON* v = cJSON_GetObjectItem(data, "soc");
        if (v && cJSON_IsNumber(v)) {
            gs.plugAndChargeBatterySoc = static_cast<uint16_t>(v->valueint & 0xFFFF);
        }
        v = cJSON_GetObjectItem(data, "currentBatteryVoltage");
        if (v && cJSON_IsNumber(v)) {
            gs.plugAndChargeCurrentBatteryVoltage = static_cast<uint16_t>(v->valueint & 0xFFFF);
        }
    }

    const bool singleGunConfirmOk = gs.plugAndChargeActive && gs.state == STATE_STARTING && isValidVinString(vin);
    const bool mergePlugAndCharge = (gs.plugAndChargeActive && gs.mergeChargeFlag != 0x00);
    const int peer = mergePlugAndCharge ? getMergePeerGun(gun) : -1;

    if (!mergePlugAndCharge || peer < 0) {
        cJSON* pileData = cJSON_CreateObject();
        cJSON_AddNumberToObject(pileData, "successFlag", singleGunConfirmOk ? 0x00 : 0x01);
        cJSON_AddNumberToObject(pileData, "failReason", singleGunConfirmOk ? 0x00 : 0x01);
        publishPileCmd(gun, "vehicle_id_confirm", pileData);
        cJSON_Delete(pileData);

        if (!singleGunConfirmOk) {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "vin", vin.c_str());
            cJSON_AddStringToObject(evt, "reason", gs.plugAndChargeActive ? "invalid_vin" : "unexpected_vehicle_id");
            publishLogicEvent(gun, "plug_and_charge_vehicle_id_reject", evt);
            cJSON_Delete(evt);
            return;
        }

        gs.plugAndChargeVehicleIdConfirmed = true;
        publishPlugAndChargeAuthRequest(gun);

        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "vin", vin.c_str());
        publishLogicEvent(gun, "plug_and_charge_vehicle_id_confirmed", evt);
        cJSON_Delete(evt);
        return;
    }

    GunState& peerGs = m_gunStates[peer];
    if (!singleGunConfirmOk) {
        const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x32U));
        for (size_t idx = 0; idx < 2; ++idx) {
            const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
            GunState& targetGs = m_gunStates[targetGun];
            if (targetGs.plugAndChargeVehicleIdReceived) {
                cJSON* pileData = cJSON_CreateObject();
                cJSON_AddNumberToObject(pileData, "successFlag", 0x01);
                cJSON_AddNumberToObject(pileData, "failReason", 0x01);
                publishPileCmd(targetGun, "vehicle_id_confirm", pileData);
                cJSON_Delete(pileData);
            }
            if (result.valid) {
                targetGs.stopReason = result.reason;
            }
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "vin", targetGs.vinCode.c_str());
            cJSON_AddStringToObject(evt, "reason", "invalid_vin");
            cJSON_AddNumberToObject(evt, "peerGun", (targetGun == gun) ? peer : gun);
            publishLogicEvent(targetGun, "plug_and_charge_vehicle_id_reject", evt);
            cJSON_Delete(evt);
            if (targetGs.state == STATE_STARTING) {
                transitionTo(targetGun, STATE_STOPPED, "merge_invalid_vin");
            }
        }
        return;
    }

    if (!peerGs.plugAndChargeVehicleIdReceived) {
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "vin", vin.c_str());
        cJSON_AddNumberToObject(evt, "peerGun", peer);
        cJSON_AddStringToObject(evt, "reason", "waiting_peer_vehicle_id");
        publishLogicEvent(gun, "plug_and_charge_vehicle_id_wait_peer", evt);
        cJSON_Delete(evt);
        return;
    }

    const bool peerVinValid = isValidVinString(peerGs.vinCode);
    const bool sameVin = (peerVinValid && vin == peerGs.vinCode);
    if (!sameVin) {
        const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(0x36U));
        for (size_t idx = 0; idx < 2; ++idx) {
            const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
            GunState& targetGs = m_gunStates[targetGun];
            // BY ZF: 双枪 VIN 不一致属于合并充即插即充鉴权失败，不按非法 VIN 回 0x18 失败。
            cJSON* confirmData = cJSON_CreateObject();
            cJSON_AddNumberToObject(confirmData, "successFlag", 0x00);
            cJSON_AddNumberToObject(confirmData, "failReason", 0x00);
            publishPileCmd(targetGun, "vehicle_id_confirm", confirmData);
            cJSON_Delete(confirmData);
            targetGs.plugAndChargeVehicleIdConfirmed = true;

            cJSON* authData = cJSON_CreateObject();
            cJSON_AddStringToObject(authData, "vin", targetGs.vinCode.c_str());
            cJSON_AddNumberToObject(authData, "successFlag", 0x01);
            cJSON_AddNumberToObject(authData, "failReason", 0x02);
            publishPileCmd(targetGun, "vehicle_auth", authData);
            cJSON_Delete(authData);
            targetGs.plugAndChargeAuthResultSent = true;

            if (result.valid) {
                targetGs.stopReason = result.reason;
            }
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "vin", targetGs.vinCode.c_str());
            cJSON_AddNumberToObject(evt, "peerGun", (targetGun == gun) ? peer : gun);
            cJSON_AddStringToObject(evt, "peerVin", m_gunStates[(targetGun == gun) ? peer : gun].vinCode.c_str());
            cJSON_AddStringToObject(evt, "reason", "merge_vehicle_id_mismatch");
            cJSON_AddNumberToObject(evt, "failReason", 0x02);
            if (result.valid) {
                cJSON_AddNumberToObject(evt, "stopReason", static_cast<double>(targetGs.stopReason));
                if (!result.message.empty()) {
                    cJSON_AddStringToObject(evt, "stopReasonText", result.message.c_str());
                }
            }
            publishLogicEvent(targetGun, "plug_and_charge_vehicle_id_reject", evt);
            cJSON_Delete(evt);
            if (targetGs.state == STATE_STARTING) {
                transitionTo(targetGun, STATE_STOPPED, "merge_vehicle_id_mismatch");
            }
        }
        return;
    }

    for (size_t idx = 0; idx < 2; ++idx) {
        const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
        GunState& targetGs = m_gunStates[targetGun];
        cJSON* pileData = cJSON_CreateObject();
        cJSON_AddNumberToObject(pileData, "successFlag", 0x00);
        cJSON_AddNumberToObject(pileData, "failReason", 0x00);
        publishPileCmd(targetGun, "vehicle_id_confirm", pileData);
        cJSON_Delete(pileData);
        targetGs.plugAndChargeVehicleIdConfirmed = true;
    }

    const uint8_t primaryGun = (gun < static_cast<uint8_t>(peer)) ? gun : static_cast<uint8_t>(peer);
    publishPlugAndChargeAuthRequest(primaryGun);
    const std::chrono::steady_clock::time_point authReqTime = m_gunStates[primaryGun].plugAndChargeAuthRequestTime;
    m_gunStates[peer].plugAndChargeAuthRequestPublished = true;
    m_gunStates[peer].plugAndChargeAuthRequestTime = authReqTime;

    for (size_t idx = 0; idx < 2; ++idx) {
        const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "vin", m_gunStates[targetGun].vinCode.c_str());
        cJSON_AddNumberToObject(evt, "peerGun", (targetGun == gun) ? peer : gun);
        publishLogicEvent(targetGun, "plug_and_charge_vehicle_id_confirmed", evt);
        cJSON_Delete(evt);
    }
}

void ChargeLogicProcess::handlePlugAndChargeAuthResult(uint8_t gun, cJSON* data, const char* resultSource)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    GunState& gs = m_gunStates[gun];
    const bool mergePlugAndCharge = (gs.plugAndChargeActive && gs.mergeChargeFlag != 0x00);
    const int peer = mergePlugAndCharge ? getMergePeerGun(gun) : -1;
    GunState* peerGs = (mergePlugAndCharge && peer >= 0) ? &m_gunStates[peer] : NULL;
    if (!gs.plugAndChargeActive || gs.state != STATE_STARTING || !gs.plugAndChargeVehicleIdConfirmed) {
        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "cmd", resultSource ? resultSource : "plug_and_charge_auth_result");
        cJSON_AddStringToObject(evt, "reason", "plug_and_charge_auth_not_ready");
        cJSON_AddStringToObject(evt, "state", stateToString(gs.state));
        publishLogicEvent(gun, "cmd_reject", evt);
        cJSON_Delete(evt);
        return;
    }
    if (peerGs) {
        if (!peerGs->plugAndChargeActive ||
            peerGs->state != STATE_STARTING ||
            !peerGs->plugAndChargeVehicleIdConfirmed) {
            cJSON* evt = cJSON_CreateObject();
            cJSON_AddStringToObject(evt, "cmd", resultSource ? resultSource : "plug_and_charge_auth_result");
            cJSON_AddStringToObject(evt, "reason", "merge_peer_not_ready");
            cJSON_AddNumberToObject(evt, "peerGun", peer);
            cJSON_AddStringToObject(evt, "peerState", stateToString(peerGs->state));
            publishLogicEvent(gun, "cmd_reject", evt);
            cJSON_Delete(evt);
            return;
        }
    }
    if (gs.plugAndChargeAuthResultSent || (peerGs && peerGs->plugAndChargeAuthResultSent)) {
        return;
    }

    bool authSuccess = false;
    int result = 0;
    bool hasResult = false;
    bool hasExplicitSuccess = false;
    int successFlag = -1;
    unsigned int failReason = 0x02;
    if (data) {
        hasResult = jsonGetInt(data, "result", result);
        hasExplicitSuccess = jsonGetInt(data, "successFlag", successFlag);
        int failReasonInt = 0;
        if (jsonGetInt(data, "failReason", failReasonInt) ||
            jsonGetInt(data, "reasonCode", failReasonInt) ||
            jsonGetInt(data, "authFailReason", failReasonInt)) {
            failReason = static_cast<unsigned int>(failReasonInt) & 0xFFU;
        }
    }
    if (hasResult) {
        authSuccess = (result == 1);
    } else if (hasExplicitSuccess && successFlag >= 0) {
        authSuccess = (successFlag == 0);
    } else if (resultSource && std::string(resultSource) == "start_charge") {
        authSuccess = true;
    }

    std::string authVin = sanitizeVinString(data ? getString(data, "vin") : "");
    if (authVin.empty()) {
        authVin = sanitizeVinString(data ? getString(data, "vinCode") : "");
    }
    if (authVin.empty()) {
        authVin = gs.vinCode;
    }

    if (authSuccess && !isValidVinString(authVin)) {
        authSuccess = false;
        failReason = 0x01;
    }
    if (authSuccess) {
        failReason = 0x00;
    } else if (failReason == 0x00) {
        failReason = 0x02;
    }

    const size_t targetCount = peerGs ? 2U : 1U;
    for (size_t idx = 0; idx < targetCount; ++idx) {
        const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
        GunState& targetGs = m_gunStates[targetGun];

        if (data) {
            const char* userNo = getString(data, "chargeUserNo");
            if (!userNo || !userNo[0]) userNo = getString(data, "userNo");
            if (!userNo || !userNo[0]) userNo = getString(data, "userId");
            if (userNo && userNo[0]) {
                targetGs.chargeUserNo = userNo;
            }

            const char* orderNo = getString(data, "orderNo");
            if (!orderNo || !orderNo[0]) orderNo = getString(data, "chargeOrderNo");
            if (orderNo && orderNo[0]) {
                targetGs.orderNo = orderNo;
            }

            const char* preTradeNo = getString(data, "preTradeNo");
            if (preTradeNo && preTradeNo[0]) {
                targetGs.preTradeNo = preTradeNo;
            } else if (targetGs.preTradeNo.empty() && !targetGs.orderNo.empty()) {
                targetGs.preTradeNo = targetGs.orderNo;
            }

            const char* tradeNo = getString(data, "tradeNo");
            if (tradeNo && tradeNo[0]) {
                targetGs.tradeNo = tradeNo;
            } else if (targetGs.tradeNo.empty()) {
                const std::string baseTradeNo = targetGs.preTradeNo.empty() ? targetGs.orderNo : targetGs.preTradeNo;
                targetGs.tradeNo = baseTradeNo.empty()
                    ? ("T" + std::to_string(targetGun) + "_" + std::to_string(targetGs.chargeStartTime))
                    : baseTradeNo;
            }

            int chargeMode = 0;
            if (jsonGetInt(data, "chargeMode", chargeMode)) {
                targetGs.chargeMode = chargeMode;
            }

            cJSON* prechargeAmount = cJSON_GetObjectItem(data, "prechargeAmount");
            if (!prechargeAmount) prechargeAmount = cJSON_GetObjectItem(data, "prepaidAmount");
            if (prechargeAmount && cJSON_IsNumber(prechargeAmount)) {
                targetGs.prechargeAmount = prechargeAmount->valuedouble;
            }

            int feeModelNo = 0;
            if (jsonGetInt(data, "feeModelNo", feeModelNo)) {
                targetGs.feeModelNo = feeModelNo;
            }

            const char* feeModelId = getString(data, "feeModelId");
            if (feeModelId && feeModelId[0]) {
                targetGs.feeModelId = feeModelId;
            }

            targetGs.mergeChargeFlag = getMergeChargeFlag(data);

            if (cJSON_GetObjectItem(data, "timeNum")) {
                parseFeeModel(targetGun, data);
            }
        }

        cJSON* pileData = cJSON_CreateObject();
        cJSON_AddStringToObject(pileData, "vin", authVin.c_str());
        cJSON_AddNumberToObject(pileData, "successFlag", authSuccess ? 0x00 : 0x01);
        cJSON_AddNumberToObject(pileData, "failReason", failReason);
        cJSON_AddStringToObject(pileData, "chargeUserNo", targetGs.chargeUserNo.c_str());
        cJSON_AddStringToObject(pileData, "orderNo", targetGs.orderNo.c_str());
        cJSON_AddStringToObject(pileData, "preTradeNo", targetGs.preTradeNo.c_str());
        cJSON_AddStringToObject(pileData, "tradeNo", targetGs.tradeNo.c_str());
        cJSON_AddNumberToObject(pileData, "chargeStartTime", static_cast<double>(targetGs.chargeStartTime));
        cJSON_AddNumberToObject(pileData, "chargeMode", targetGs.chargeMode);
        cJSON_AddNumberToObject(pileData, "prechargeAmount", targetGs.prechargeAmount);
        cJSON_AddNumberToObject(pileData, "feeModelNo", targetGs.feeModelNo);
        cJSON_AddStringToObject(pileData, "feeModelId", targetGs.feeModelId.c_str());
        cJSON_AddNumberToObject(pileData, "plugAndChargeFlag", targetGs.plugAndChargeFlag);
        cJSON_AddNumberToObject(pileData, "mergeChargeFlag", targetGs.mergeChargeFlag);
        cJSON_AddNumberToObject(pileData, "timeNum", targetGs.feeTimeNum);
        cJSON* timeSegArr = cJSON_CreateArray();
        for (size_t i = 0; i < targetGs.feeTimeSegMinutes.size(); ++i) {
            cJSON_AddItemToArray(timeSegArr, cJSON_CreateString(minuteToHHMM(targetGs.feeTimeSegMinutes[i]).c_str()));
        }
        cJSON_AddItemToObject(pileData, "timeSeg", timeSegArr);
        cJSON* chargeFeeArr = cJSON_CreateArray();
        for (size_t i = 0; i < targetGs.feeChargePricePerKwh.size(); ++i) {
            cJSON_AddItemToArray(chargeFeeArr, cJSON_CreateNumber(targetGs.feeChargePricePerKwh[i]));
        }
        cJSON_AddItemToObject(pileData, "chargeFee", chargeFeeArr);
        cJSON* serviceFeeArr = cJSON_CreateArray();
        for (size_t i = 0; i < targetGs.feeServicePricePerKwh.size(); ++i) {
            cJSON_AddItemToArray(serviceFeeArr, cJSON_CreateNumber(targetGs.feeServicePricePerKwh[i]));
        }
        cJSON_AddItemToObject(pileData, "serviceFee", serviceFeeArr);
        publishPileCmd(targetGun, "vehicle_auth", pileData);
        cJSON_Delete(pileData);

        targetGs.plugAndChargeAuthResultSent = true;

        cJSON* evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "sourceCmd", resultSource ? resultSource : "");
        cJSON_AddStringToObject(evt, "vin", authVin.c_str());
        cJSON_AddNumberToObject(evt, "successFlag", authSuccess ? 0x00 : 0x01);
        cJSON_AddNumberToObject(evt, "failReason", failReason);
        if (peerGs) {
            cJSON_AddNumberToObject(evt, "peerGun", (targetGun == gun) ? peer : gun);
        }

        if (!authSuccess) {
            unsigned int startFailPoint = 0xF002U;
            if (failReason == 0x01U) {
                startFailPoint = 0x32U;
            } else if (failReason == 0x03U) {
                startFailPoint = 0x34U;
            }
            const FaultJudgeResult result = JudgeStartFailPoint(MakeStartPointKey(startFailPoint));
            if (result.valid) {
                targetGs.stopReason = result.reason;
                cJSON_AddNumberToObject(evt, "stopReason", static_cast<double>(targetGs.stopReason));
                if (!result.message.empty()) {
                    cJSON_AddStringToObject(evt, "stopReasonText", result.message.c_str());
                }
            }
        }
        publishLogicEvent(targetGun, "plug_and_charge_auth_result_forwarded", evt);
        cJSON_Delete(evt);
    }

    if (!authSuccess) {
        for (size_t idx = 0; idx < targetCount; ++idx) {
            const uint8_t targetGun = (idx == 0) ? gun : static_cast<uint8_t>(peer);
            if (m_gunStates[targetGun].state == STATE_STARTING) {
                transitionTo(targetGun, STATE_STOPPED, "plug_and_charge_auth_failed");
            }
        }
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
            // BY ZF: 电表离线故障是持续状态，未恢复在线前插枪应直接进入故障态，不能进入 PREPARE。
            if (gs.meterOfflineFaultActive || gs.platformOfflineFaultActive || gs.pileOfflineFaultActive) {
                transitionTo(gun, STATE_ERROR, "link_fault_active");
            } else {
                transitionTo(gun, STATE_PREPARE, reason);
            }
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
            // BY ZF: 电表稳定/超时只是 STOPPING->STOPPED 的内部收敛条件，不能覆盖真实停机原因。
            transitionTo(gun, STATE_STOPPED, "");
        }
        break;
    case STATE_STOPPED:
        if (evt == EVT_VEHICLE_DISCONNECTED) {
            transitionTo(gun, STATE_IDLE, reason);
        } else if (evt == EVT_TOTAL_FAULT || evt == EVT_DEVICE_ERR) {
            transitionTo(gun, STATE_ERROR, reason);
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
        gs.startSuccessFlag = false;
    } else {
        gs.startingEnterTime = std::chrono::steady_clock::time_point();
        gs.startingRetrySent = false;
    }
    if (to == STATE_CHARGING) {
        // BY ZF: 进入 CHARGING 时锁定计时起点，feeData.chargedTime 仅使用该起点计算。
        gs.chargingEnterTime = std::chrono::steady_clock::now();
        gs.startSuccessFlag = true;
        gs.chargingMeterVoltageNormalSeen = false;
        gs.meteringAbnormalTriggered = false;
    } else if (to == STATE_IDLE) {
        gs.chargingEnterTime = std::chrono::steady_clock::time_point();
        gs.startSuccessFlag = false;
        gs.vehicleDisconnectedDuringStopping = false;
        gs.chargingMeterVoltageNormalSeen = false;
        gs.meteringAbnormalTriggered = false;
    }
    // BY ZF: 状态切换同时写 logger，便于后续排查状态机流转。
    {
        std::ostringstream details;
        details << "gun=" << static_cast<int>(gun)
                << ",from=" << stateToString(prev)
                << ",to=" << stateToString(to);
        if (reason && reason[0] != '\0') {
            details << ",reason=" << reason;
        }
        m_logSender.info("state_transition", details.str());
    }
    publishStateChange(gun, prev, to, reason);
    if (to == STATE_STOPPED ) {
        logTradeRecordOnStopped(gun, reason);
        if (gs.offlineCardChargeActive || gs.offlineCardSettlementPending) {
            prepareOfflineCardSettlement(gun);
        }
        if (gs.vehicleDisconnectedDuringStopping) {
            handleEvent(gun, EVT_VEHICLE_DISCONNECTED, "vehicle_disconnected_after_stopping");
        }
    }
    if (to == STATE_IDLE) {
        // BY ZF: 回到空闲态时清理本次充电流程上下文
        resetChargeSessionState(gun);
        // BY ZF: 进入 IDLE 时补发一条清零 feeData。
        publishFeeData(gun);
        maybeStartPendingOfflineCardSettlement();
    }
}

void ChargeLogicProcess::resetChargeSessionState(uint8_t gun)
{
    if (gun >= m_gunStates.size()) {
        return;
    }

    GunState& gs = m_gunStates[gun];

    gs.hasOtherFault = false;
    gs.lastOtherFault = 0;
    gs.pendingStart = false;
    gs.pendingStartData.clear();
    gs.lastStartCmdData.clear();
    gs.pendingCardStartAuthData.clear();
    gs.startingRetrySent = false;
    gs.stopCompleteSeen = false;
    gs.tcuStopReqSent = false;
    gs.vehicleDisconnectedDuringStopping = false;
    gs.meterStableCount = 0;
    gs.lastStopCmdTime = std::chrono::steady_clock::time_point();
    gs.startingEnterTime = std::chrono::steady_clock::time_point();
    gs.chargingEnterTime = std::chrono::steady_clock::time_point();
    gs.lastFeeDataPublishTime = std::chrono::steady_clock::time_point();
    gs.plugAndChargeAuthRequestTime = std::chrono::steady_clock::time_point();
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
    gs.plugAndChargeFlag = 0x01;
    gs.mergeChargeFlag = 0x00;
    gs.plugAndChargeActive = false;
    gs.plugAndChargeVehicleIdReceived = false;
    gs.plugAndChargeVehicleIdConfirmed = false;
    gs.plugAndChargeAuthRequestPublished = false;
    gs.plugAndChargeAuthResultSent = false;
    gs.plugAndChargeAuthAckReceived = false;
    gs.plugAndChargeBatteryChargeCount[0] = 0;
    gs.plugAndChargeBatteryChargeCount[1] = 0;
    gs.plugAndChargeBatteryChargeCount[2] = 0;
    gs.plugAndChargeBatterySoc = 0;
    gs.plugAndChargeCurrentBatteryVoltage = 0;
    gs.startSoc = 0;
    gs.endSoc = 0;
    gs.stopReason = 0;
    gs.chargeMode = 0;
    gs.prechargeAmount = 0.0;
    gs.v2gMode = false;
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
    gs.feeLastPublishedTotalEnergy = -1.0;
    gs.feeLastPublishedElectricAmount = -1.0;
    gs.feeLastPublishedServiceAmount = -1.0;
    gs.startSuccessFlag = false;

    gs.hasTotalAmount = false;
    gs.lastTotalAmount = 0.0;
    gs.chargingMeterVoltageNormalSeen = false;
    gs.meteringAbnormalTriggered = false;
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
    m_gunStates[gun].vehicleDisconnectedDuringStopping = false;
    m_gunStates[gun].stopCompleteSeen = false;
    m_gunStates[gun].meterStableCount = 0;
    m_gunStates[gun].stoppingEnterTime = std::chrono::steady_clock::now();
    publishPileCmd(gun, "stop_charge", nullptr);
    m_gunStates[gun].lastStopCmdTime = std::chrono::steady_clock::now();
}
