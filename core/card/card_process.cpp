/**
 * Card process implementation
 * BY ZF
 */

#include "card_process.h"
#include "card_protocol.h"
#include "../base/cjson/include/cjson/cJSON.h"
#include <sstream>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <cstring>

namespace {
    uint64_t nowMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::string getJsonString(cJSON* obj, const char* key)
    {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
        if (!item || !cJSON_IsString(item) || !item->valuestring) {
            return "";
        }
        return item->valuestring;
    }

    std::vector<uint8_t> defaultM1Key()
    {
        return std::vector<uint8_t>(6, 0xFF);
    }

    const int kCardLoopIntervalMs = 50;
    const int kCardMaxQueueDepth = 32;
    const uint8_t kSector0BlockNo = 0x01;
    const uint8_t kSector4BlockNo = 0x10;
    const uint8_t kSector8BlockNo = 0x20;
    const uint8_t kLockedFlagValue = 0xAA;

    std::string cardOpToString(CardBusinessOp op)
    {
        switch (op) {
            case CARD_BIZ_OPEN_RF: return "open_rf";
            case CARD_BIZ_CLOSE_RF: return "close_rf";
            case CARD_BIZ_READ: return "card_read";
            case CARD_BIZ_WRITE: return "card_write";
            case CARD_BIZ_LOCK: return "card_lock";
            case CARD_BIZ_UNLOCK: return "card_unlock";
            case CARD_BIZ_NONE:
            default:
                return "unknown";
        }
    }

    uint32_t parseLe32(const std::vector<uint8_t>& data, size_t offset)
    {
        return static_cast<uint32_t>(data[offset])
             | (static_cast<uint32_t>(data[offset + 1]) << 8)
             | (static_cast<uint32_t>(data[offset + 2]) << 16)
             | (static_cast<uint32_t>(data[offset + 3]) << 24);
    }

    void storeLe32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
    {
        data[offset] = static_cast<uint8_t>(value & 0xFF);
        data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    void appendReaderResult(cJSON* data, const ReaderResult* result)
    {
        if (!result) {
            return;
        }
        cJSON_AddNumberToObject(data, "resultCode", static_cast<double>(result->resultCode));
        cJSON_AddNumberToObject(data, "readerStatus", static_cast<double>(result->deviceStatus));
        cJSON_AddStringToObject(data, "payloadHex", CardReaderProtocol::hexDump(result->data).c_str());
    }
}

CardProcess::CardProcess()
    : BaseProcess(PROC_CARD, "tcu_card")
    , m_seq(0)
    , m_deviceOnline(false)
    , m_lastDaemonWatchdogFeed(std::chrono::steady_clock::now() - std::chrono::seconds(5))
    , m_lastProcessWatchdogFeed(std::chrono::steady_clock::now() - std::chrono::seconds(5))
{
}

CardProcess::~CardProcess()
{
}

bool CardProcess::doInitialize()
{
    if (!loadConfig()) {
        std::cerr << "[Card] loadConfig failed" << std::endl;
        return false;
    }
    if (!initMqtt()) {
        std::cerr << "[Card] initMqtt failed" << std::endl;
        return false;
    }
    if (!initReader()) {
        std::cerr << "[Card] initReader failed" << std::endl;
        return false;
    }
    publishDeviceOnline(true, "reader_ready");
    return true;
}

void CardProcess::doRun()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - m_lastDaemonWatchdogFeed >= std::chrono::seconds(5)) {
        feedDaemonWatchdog();
        m_lastDaemonWatchdogFeed = now;
    }
    if (now - m_lastProcessWatchdogFeed >= std::chrono::seconds(5)) {
        feedWatchdog();
        m_lastProcessWatchdogFeed = now;
    }

    processOneCommand();
    std::this_thread::sleep_for(std::chrono::milliseconds(m_cfg.loopIntervalMs));
}

void CardProcess::doCleanup()
{
    m_reader.close();
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

bool CardProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    const std::string section("Card");

    m_cfg.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_cfg.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_cfg.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_cfg.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_card");
    m_cfg.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_cfg.loopIntervalMs = kCardLoopIntervalMs;
    m_cfg.maxQueueDepth = kCardMaxQueueDepth;
    m_cfg.debugProtocol = cfg.getInt(section, "debug_protocol", 0) != 0;

    m_cfg.serial.device = cfg.getString(section, "serial_device", "/dev/ttyS5");
    m_cfg.serial.baudrate = static_cast<uint32_t>(cfg.getInt(section, "serial_baudrate", 57600));
    m_cfg.serial.dataBits = static_cast<uint8_t>(cfg.getInt(section, "serial_data_bits", 8));
    m_cfg.serial.stopBits = static_cast<uint8_t>(cfg.getInt(section, "serial_stop_bits", 1));
    {
        const std::string parity = cfg.getString(section, "serial_parity", "N");
        m_cfg.serial.parity = parity.empty() ? 'N' : parity[0];
    }
    m_cfg.serial.readTimeoutMs = cfg.getInt(section, "read_timeout_ms", 2000);
    if (m_cfg.serial.readTimeoutMs < 100) {
        m_cfg.serial.readTimeoutMs = 100;
    }

    m_cfg.cmdTopic = cfg.getString(section, "cmd_topic", "");
    m_cfg.eventTopic = cfg.getString(section, "event_topic", "");
    if (m_cfg.cmdTopic.empty()) {
        m_cfg.cmdTopic = m_cfg.mqttTopicPrefix + "/card/0/cmd";
    }
    if (m_cfg.eventTopic.empty()) {
        m_cfg.eventTopic = m_cfg.mqttTopicPrefix + "/card/0/event";
    }

    return true;
}

bool CardProcess::initMqtt()
{
    if (!m_mqtt.init(m_cfg.mqttClientId, true)) {
        return false;
    }

    m_mqtt.setConnectHandler([this](int rc) {
        onMqttConnect(rc);
    });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_cfg.mqttHost, m_cfg.mqttPort, m_cfg.mqttKeepalive)) {
        return false;
    }
    return m_mqtt.loopStart();
}

bool CardProcess::initReader()
{
    if (!m_reader.open(m_cfg.serial)) {
        return false;
    }
    m_reader.setDebug(m_cfg.debugProtocol);
    return true;
}

void CardProcess::onMqttConnect(int rc)
{
    if (rc != 0) {
        std::cerr << "[Card] mqtt connect failed rc=" << rc << std::endl;
        return;
    }
    if (!m_mqtt.subscribe(m_cfg.cmdTopic, 1)) {
        std::cerr << "[Card] mqtt subscribe failed: " << m_cfg.cmdTopic << std::endl;
        return;
    }
    std::cout << "[Card] mqtt subscribed: " << m_cfg.cmdTopic << std::endl;
}

void CardProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    if (topic != m_cfg.cmdTopic) {
        return;
    }

    CardBusinessCommand cmd;
    std::string err;
    if (!parseCommandJson(payload, cmd, err)) {
        publishEvent("cmd_rejected", err);
        return;
    }
    if (!enqueueCommand(cmd, err)) {
        publishOpFailed(cmd.op, err);
        return;
    }
}

bool CardProcess::parseCommandJson(const std::string& payload, CardBusinessCommand& out, std::string& err)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        err = "invalid_json";
        return false;
    }

    out = CardBusinessCommand();
    const std::string op = getJsonString(root, "op");
    if (op == "open_rf") {
        out.op = CARD_BIZ_OPEN_RF;
    } else if (op == "close_rf") {
        out.op = CARD_BIZ_CLOSE_RF;
    } else if (op == "card_read") {
        out.op = CARD_BIZ_READ;
    } else if (op == "card_write") {
        out.op = CARD_BIZ_WRITE;

        const std::string cardNoHex = getJsonString(root, "cardNoHex");
        if (!cardNoHex.empty()) {
            if (!CardReaderProtocol::parseHex(cardNoHex, out.cardNo) || out.cardNo.size() != 16) {
                err = "card_write requires cardNoHex(16)";
                cJSON_Delete(root);
                return false;
            }
            out.hasCardNo = true;
        }

        cJSON* balanceItem = cJSON_GetObjectItemCaseSensitive(root, "cardBalance");
        if (balanceItem != NULL) {
            if (!cJSON_IsNumber(balanceItem) || balanceItem->valuedouble < 0) {
                err = "card_write requires cardBalance>=0";
                cJSON_Delete(root);
                return false;
            }
            out.hasCardBalance = true;
            out.cardBalance = static_cast<uint32_t>(balanceItem->valuedouble);
        }

        if (!out.hasCardNo && !out.hasCardBalance) {
            err = "card_write requires cardNoHex or cardBalance";
            cJSON_Delete(root);
            return false;
        }
    } else if (op == "card_lock") {
        out.op = CARD_BIZ_LOCK;
    } else if (op == "card_unlock") {
        out.op = CARD_BIZ_UNLOCK;
    } else {
        err = "unsupported_op";
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);
    return true;
}

bool CardProcess::enqueueCommand(const CardBusinessCommand& cmd, std::string& err)
{
    std::lock_guard<std::mutex> lock(m_cmdMutex);
    if (static_cast<int>(m_cmdQueue.size()) >= m_cfg.maxQueueDepth) {
        err = "queue_full";
        return false;
    }
    m_cmdQueue.push_back(cmd);
    return true;
}

bool CardProcess::popNextCommand(CardBusinessCommand& out)
{
    std::lock_guard<std::mutex> lock(m_cmdMutex);
    if (m_cmdQueue.empty()) {
        return false;
    }
    out = m_cmdQueue.front();
    m_cmdQueue.pop_front();
    return true;
}

void CardProcess::processOneCommand()
{
    CardBusinessCommand cmd;
    if (!popNextCommand(cmd)) {
        return;
    }

    std::string err;
    if (!handleBusinessCommand(cmd, err)) {
        publishOpFailed(cmd.op, err);
    }
}

bool CardProcess::handleBusinessCommand(const CardBusinessCommand& cmd, std::string& err)
{
    switch (cmd.op) {
        case CARD_BIZ_OPEN_RF:
            return handleOpenRf(err);
        case CARD_BIZ_CLOSE_RF:
            return handleCloseRf(err);
        case CARD_BIZ_READ:
            return handleCardRead(err);
        case CARD_BIZ_WRITE:
            return handleCardWrite(cmd, err);
        case CARD_BIZ_LOCK:
            return handleCardLock(true, err);
        case CARD_BIZ_UNLOCK:
            return handleCardLock(false, err);
        case CARD_BIZ_NONE:
        default:
            err = "unsupported_op";
            return false;
    }
}

bool CardProcess::handleOpenRf(std::string& err)
{
    debugLog("handle open_rf begin");
    ReaderCommand cmd;
    cmd.op = READER_OP_OPEN_RF;
    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        debugLog(std::string("handle open_rf failed: ") + err);
        return false;
    }
    debugLog("handle open_rf success");
    publishSimpleEvent("rf_opened");
    return true;
}

bool CardProcess::handleCloseRf(std::string& err)
{
    debugLog("handle close_rf begin");
    ReaderCommand cmd;
    cmd.op = READER_OP_CLOSE_RF;
    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        debugLog(std::string("handle close_rf failed: ") + err);
        return false;
    }
    debugLog("handle close_rf success");
    publishSimpleEvent("rf_closed");
    return true;
}

bool CardProcess::handleCardRead(std::string& err)
{
    debugLog("handle card_read begin");
    CardBusinessInfo info;
    if (!readCardInfo(info, err)) {
        debugLog(std::string("handle card_read failed: ") + err);
        return false;
    }
    debugLog(std::string("handle card_read success uid=")
             + CardReaderProtocol::hexDump(info.uid)
             + " cardNo=" + CardReaderProtocol::hexDump(info.cardNo)
             + " cardBalance=" + std::to_string(info.cardBalance)
             + " locked=" + (info.locked ? "true" : "false"));
    publishCardInfo(info);
    triggerBuzzer();
    return true;
}

bool CardProcess::handleCardWrite(const CardBusinessCommand& cmd, std::string& err)
{
    debugLog(std::string("handle card_write begin hasCardNo=")
             + (cmd.hasCardNo ? "true" : "false")
             + " hasCardBalance=" + (cmd.hasCardBalance ? "true" : "false"));
    std::vector<uint8_t> uid;
    if (!activateCard(uid, err)) {
        debugLog(std::string("handle card_write activate failed: ") + err);
        return false;
    }

    if (cmd.hasCardNo) {
        debugLog(std::string("handle card_write write cardNo begin cardNo=")
                 + CardReaderProtocol::hexDump(cmd.cardNo));
        if (!authBlock(kSector0BlockNo, uid, true, err)) {
            debugLog(std::string("handle card_write auth sector0 failed: ") + err);
            return false;
        }
        if (!writeBlock(kSector0BlockNo, cmd.cardNo, err)) {
            debugLog(std::string("handle card_write write block1 failed: ") + err);
            return false;
        }
        debugLog("handle card_write write cardNo success");
    }

    if (cmd.hasCardBalance) {
        debugLog(std::string("handle card_write write cardBalance begin cardBalance=")
                 + std::to_string(cmd.cardBalance));
        std::vector<uint8_t> block32(16, 0x00);
        const uint32_t cardBalanceNeg = ~cmd.cardBalance;
        storeLe32(block32, 0, cmd.cardBalance);
        storeLe32(block32, 4, cardBalanceNeg);

        if (!authBlock(kSector8BlockNo, uid, true, err)) {
            debugLog(std::string("handle card_write auth sector8 failed: ") + err);
            return false;
        }
        if (!writeBlock(kSector8BlockNo, block32, err)) {
            debugLog(std::string("handle card_write write block32 failed: ") + err);
            return false;
        }
        debugLog("handle card_write write cardBalance success");
    }

    debugLog("handle card_write success");
    publishCardWritten(cmd);
    triggerBuzzer();
    return true;
}

bool CardProcess::handleCardLock(bool locked, std::string& err)
{
    debugLog(std::string("handle ")
             + (locked ? "card_lock" : "card_unlock")
             + " begin");
    std::vector<uint8_t> uid;
    if (!activateCard(uid, err)) {
        debugLog(std::string("handle ")
                 + (locked ? "card_lock" : "card_unlock")
                 + " activate failed: " + err);
        return false;
    }

    std::vector<uint8_t> block16(16, 0x00);
    block16[8] = locked ? kLockedFlagValue : 0x00;

    if (!authBlock(kSector4BlockNo, uid, true, err)) {
        debugLog(std::string("handle ")
                 + (locked ? "card_lock" : "card_unlock")
                 + " auth sector4 failed: " + err);
        return false;
    }
    if (!writeBlock(kSector4BlockNo, block16, err)) {
        debugLog(std::string("handle ")
                 + (locked ? "card_lock" : "card_unlock")
                 + " write block16 failed: " + err);
        return false;
    }

    debugLog(std::string("handle ")
             + (locked ? "card_lock" : "card_unlock")
             + " write lock flag success");
    publishLockEvent(locked);

    CardBusinessInfo info;
    if (!readCardInfoByUid(uid, info, err)) {
        debugLog(std::string("handle ")
                 + (locked ? "card_lock" : "card_unlock")
                 + " reread card_info failed: " + err);
        return false;
    }
    debugLog(std::string("handle ")
             + (locked ? "card_lock" : "card_unlock")
             + " success uid=" + CardReaderProtocol::hexDump(info.uid)
             + " cardNo=" + CardReaderProtocol::hexDump(info.cardNo)
             + " cardBalance=" + std::to_string(info.cardBalance)
             + " locked=" + (info.locked ? "true" : "false"));
    publishCardInfo(info);
    triggerBuzzer();
    return true;
}

bool CardProcess::executeReaderCommand(const ReaderCommand& cmd, ReaderResult& result, std::string& err)
{
    debugLog(std::string("reader execute begin op=") + CardReaderProtocol::opToString(cmd.op));
    const bool transportOk = m_reader.execute(cmd, result, err);
    if (!transportOk) {
        publishDeviceOnline(false, err);
        debugLog(std::string("reader execute transport failed op=")
                 + CardReaderProtocol::opToString(cmd.op)
                 + " reason=" + err);
        return false;
    }

    publishDeviceOnline(true, "reader_reply_ok");
    if (!result.success) {
        std::ostringstream oss;
        oss << "reader_status_0x" << std::hex << std::uppercase << result.deviceStatus;
        err = oss.str();
        debugLog(std::string("reader execute device failed op=")
                 + CardReaderProtocol::opToString(cmd.op)
                 + " reason=" + err
                 + " payload=" + CardReaderProtocol::hexDump(result.data));
        return false;
    }
    debugLog(std::string("reader execute success op=")
             + CardReaderProtocol::opToString(cmd.op)
             + " payload=" + CardReaderProtocol::hexDump(result.data));
    return true;
}

bool CardProcess::activateCard(std::vector<uint8_t>& uid, std::string& err)
{
    debugLog("activate card begin");
    ReaderCommand cmd;
    cmd.op = READER_OP_ACTIVE_CARD;
    cmd.activeTimeoutMs = 0;

    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        return false;
    }

    if (result.data.size() >= 6) {
        uid.assign(result.data.begin() + 2, result.data.begin() + 6);
        debugLog(std::string("activate card uid=") + CardReaderProtocol::hexDump(uid));
        return true;
    }
    if (result.data.size() == 4) {
        uid = result.data;
        debugLog(std::string("activate card uid=") + CardReaderProtocol::hexDump(uid));
        return true;
    }

    err = "active_card_uid_missing";
    debugLog(std::string("activate card failed: ") + err);
    return false;
}

bool CardProcess::authBlock(uint8_t blockNo, const std::vector<uint8_t>& uid, bool forWrite, std::string& err)
{
    debugLog(std::string("auth block begin block=")
             + std::to_string(blockNo)
             + " mode=" + (forWrite ? "write" : "read")
             + " uid=" + CardReaderProtocol::hexDump(uid));
    ReaderCommand cmd;
    cmd.op = READER_OP_AUTH_M1;
    cmd.keyType = 0x60;
    cmd.blockNo = blockNo;
    cmd.authForWrite = forWrite;
    cmd.key = defaultM1Key();
    cmd.uid = uid;

    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        return false;
    }
    debugLog(std::string("auth block success block=")
             + std::to_string(blockNo)
             + " mode=" + (forWrite ? "write" : "read"));
    return true;
}

bool CardProcess::readBlock(uint8_t blockNo, std::vector<uint8_t>& data, std::string& err)
{
    debugLog(std::string("read block begin block=") + std::to_string(blockNo));
    ReaderCommand cmd;
    cmd.op = READER_OP_READ_BLOCK;
    cmd.blockNo = blockNo;

    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        return false;
    }
    if (result.data.size() < 16) {
        err = "read_block_payload_short";
        debugLog(std::string("read block failed block=")
                 + std::to_string(blockNo)
                 + " reason=" + err);
        return false;
    }
    data.assign(result.data.begin(), result.data.begin() + 16);
    debugLog(std::string("read block success block=")
             + std::to_string(blockNo)
             + " data=" + CardReaderProtocol::hexDump(data));
    return true;
}

bool CardProcess::writeBlock(uint8_t blockNo, const std::vector<uint8_t>& data, std::string& err)
{
    if (data.size() != 16) {
        err = "write_block_payload_invalid";
        debugLog(std::string("write block failed block=")
                 + std::to_string(blockNo)
                 + " reason=" + err);
        return false;
    }

    debugLog(std::string("write block begin block=")
             + std::to_string(blockNo)
             + " data=" + CardReaderProtocol::hexDump(data));
    ReaderCommand cmd;
    cmd.op = READER_OP_WRITE_BLOCK;
    cmd.blockNo = blockNo;
    cmd.blockData = data;

    ReaderResult result;
    if (!executeReaderCommand(cmd, result, err)) {
        return false;
    }
    debugLog(std::string("write block success block=") + std::to_string(blockNo));
    return true;
}

bool CardProcess::readCardInfo(CardBusinessInfo& info, std::string& err)
{
    debugLog("read card_info begin");
    std::vector<uint8_t> uid;
    if (!activateCard(uid, err)) {
        return false;
    }
    return readCardInfoByUid(uid, info, err);
}

bool CardProcess::readCardInfoByUid(const std::vector<uint8_t>& uid, CardBusinessInfo& info, std::string& err)
{
    debugLog(std::string("read card_info by uid begin uid=") + CardReaderProtocol::hexDump(uid));
    std::vector<uint8_t> block1;
    std::vector<uint8_t> block16;
    std::vector<uint8_t> block32;

    if (!authBlock(kSector0BlockNo, uid, false, err)) {
        return false;
    }
    if (!readBlock(kSector0BlockNo, block1, err)) {
        return false;
    }

    if (!authBlock(kSector4BlockNo, uid, false, err)) {
        return false;
    }
    if (!readBlock(kSector4BlockNo, block16, err)) {
        return false;
    }

    if (!authBlock(kSector8BlockNo, uid, false, err)) {
        return false;
    }
    if (!readBlock(kSector8BlockNo, block32, err)) {
        return false;
    }

    const uint32_t cardBalance = parseLe32(block32, 0);
    const uint32_t cardBalanceNeg = parseLe32(block32, 4);
    if ((~cardBalance) != cardBalanceNeg) {
        err = "card_balance_check_err";
        debugLog(std::string("read card_info balance check failed uid=")
                 + CardReaderProtocol::hexDump(uid)
                 + " cardBalance=" + std::to_string(cardBalance)
                 + " cardBalanceNeg=" + std::to_string(cardBalanceNeg));
        return false;
    }

    info.uid = uid;
    info.cardNo = block1;
    info.cardBalance = cardBalance;
    info.locked = (block16[8] == kLockedFlagValue);
    debugLog(std::string("read card_info parsed uid=")
             + CardReaderProtocol::hexDump(info.uid)
             + " cardNo=" + CardReaderProtocol::hexDump(info.cardNo)
             + " cardBalance=" + std::to_string(info.cardBalance)
             + " locked=" + (info.locked ? "true" : "false"));
    return true;
}

void CardProcess::triggerBuzzer()
{
    ReaderCommand cmd;
    cmd.op = READER_OP_BUZZER;
    cmd.buzzerOnMs = 500;
    cmd.buzzerRepeat = 1;

    ReaderResult result;
    std::string err;
    debugLog("trigger buzzer begin");
    if (!executeReaderCommand(cmd, result, err)) {
        debugLog(std::string("trigger buzzer failed: ") + err);
        return;
    }
    debugLog("trigger buzzer success");
}

void CardProcess::debugLog(const std::string& msg) const
{
    if (!m_cfg.debugProtocol) {
        return;
    }
    std::cout << "[Card][Flow] " << msg << std::endl;
}

void CardProcess::publishEvent(const std::string& event, const std::string& reason)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", event.c_str());

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "reason", reason.c_str());
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishSimpleEvent(const std::string& event)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", event.c_str());
    cJSON_AddItemToObject(root, "data", cJSON_CreateObject());

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishCardInfo(const CardBusinessInfo& info)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", "card_info");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "uidHex", CardReaderProtocol::hexDump(info.uid).c_str());
    cJSON_AddStringToObject(data, "cardNoHex", CardReaderProtocol::hexDump(info.cardNo).c_str());
    cJSON_AddNumberToObject(data, "cardBalance", static_cast<double>(info.cardBalance));
    cJSON_AddBoolToObject(data, "locked", info.locked ? 1 : 0);
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishCardWritten(const CardBusinessCommand& cmd)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", "card_written");

    cJSON* data = cJSON_CreateObject();
    if (cmd.hasCardNo) {
        cJSON_AddStringToObject(data, "cardNoHex", CardReaderProtocol::hexDump(cmd.cardNo).c_str());
    }
    if (cmd.hasCardBalance) {
        cJSON_AddNumberToObject(data, "cardBalance", static_cast<double>(cmd.cardBalance));
    }
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishLockEvent(bool locked)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", locked ? "card_locked" : "card_unlocked");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "locked", locked ? 1 : 0);
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishOpFailed(CardBusinessOp op,
                                  const std::string& reason,
                                  const ReaderResult* result)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_card");
    cJSON_AddStringToObject(root, "event", "op_failed");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "op", cardOpToString(op).c_str());
    cJSON_AddStringToObject(data, "reason", reason.c_str());
    appendReaderResult(data, result);
    cJSON_AddItemToObject(root, "data", data);

    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "{}";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);

    m_mqtt.publish(m_cfg.eventTopic, payload, 1, false);
}

void CardProcess::publishDeviceOnline(bool online, const std::string& reason)
{
    if (m_deviceOnline == online) {
        return;
    }
    m_deviceOnline = online;
    publishEvent(online ? "device_online" : "device_offline", reason);
}

void CardProcess::feedDaemonWatchdog()
{
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_card";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
    }
}
