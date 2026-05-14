/**
 * YuanyangV2 tcu_comm process implementation
 * BY ZF
 */

#include "comm_process.h"
#include "../../base/common/message_queue.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

namespace {
    static const uint8_t kCmdTimeSync = 0x02;
    static const uint8_t kCmdTotalCall = 0x03;
    static const uint8_t kCmdExtPulse = 0x11;
    static const uint8_t kCmdFault = 0x12;
    static const uint8_t kCmdRemoteControl = 0x41;
    static const uint8_t kCmdTradeRecord = 0x42;
    static const uint8_t kCmdPowerControl = 0x2E;
    static const uint8_t kCmdFeeModelV2 = 0x51;
    static const uint8_t kCmdExtAllTelesignal = 0x25;
    static const uint8_t kCmdExtChangedTelesignal = 0x26;
    static const uint8_t kCmdExtAllTelemetry = 0x27;
    static const uint8_t kCmdChangedTelemetry4 = 0x5A;
    static const uint8_t kCmdRequestRsaPublicKey = 0x81;
    static const uint8_t kCmdDeviceAuth = 0x82;
    static const uint8_t kCmdResetKey = 0x83;

    static const uint32_t kControlHeartbeatReq = 0x00000043U;
    static const uint32_t kControlHeartbeatAck = 0x00000083U;
    static const uint32_t kControlDefault = 0x00000000U;
    static const uint16_t kMasterAddr = 0x0000U;
    static const uint16_t kBroadcastDeviceAddr = 0xFFFFU;
    static const uint16_t kCotBurst = 0x0001U;
    static const uint16_t kCotActive = 0x0003U;
    static const uint16_t kCotConfirm = 0x0004U;
    static const uint16_t kCotTotalCall = 0x0005U;
    static const uint16_t kCotReject = 0x0006U;
    static const int kYxPointsPerGun = 10;
    static const int kYcPointsPerGun = 14;
    static const int kYmPointsPerGun = 5;

    void feedDaemonWatchdog()
    {
        // BY ZF: Feed monitor watchdog from tcu_comm.
        static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
        static int queueReady = -1;
        if (queueReady == -1) {
            queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
        }
        if (queueReady == 1) {
            const char* processName = "tcu_comm";
            watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        }
    }

    std::vector<std::string> split(const std::string& s, char ch)
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

    bool hexToBytes(const std::string& in, std::vector<uint8_t>& out)
    {
        std::string s;
        for (size_t i = 0; i < in.size(); ++i) {
            if (std::isxdigit(static_cast<unsigned char>(in[i])) != 0) {
                s.push_back(in[i]);
            }
        }
        if (s.empty() || (s.size() % 2U) != 0U) {
            return false;
        }
        out.clear();
        out.reserve(s.size() / 2U);
        for (size_t i = 0; i < s.size(); i += 2U) {
            const char hc = s[i];
            const char lc = s[i + 1U];
            const int hi = (hc >= '0' && hc <= '9') ? (hc - '0') :
                           (hc >= 'A' && hc <= 'F') ? (hc - 'A' + 10) :
                           (hc >= 'a' && hc <= 'f') ? (hc - 'a' + 10) : -1;
            const int lo = (lc >= '0' && lc <= '9') ? (lc - '0') :
                           (lc >= 'A' && lc <= 'F') ? (lc - 'A' + 10) :
                           (lc >= 'a' && lc <= 'f') ? (lc - 'a' + 10) : -1;
            if (hi < 0 || lo < 0) {
                return false;
            }
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return true;
    }

    RSA* loadRsaPublicKey(const std::string& rsaPubKey)
    {
        if (rsaPubKey.empty()) {
            return nullptr;
        }

        BIO* mem = BIO_new_mem_buf(rsaPubKey.data(), static_cast<int>(rsaPubKey.size()));
        if (mem) {
            RSA* rsa = PEM_read_bio_RSAPublicKey(mem, nullptr, nullptr, nullptr);
            BIO_free(mem);
            if (rsa) {
                return rsa;
            }
        }

        mem = BIO_new_mem_buf(rsaPubKey.data(), static_cast<int>(rsaPubKey.size()));
        if (mem) {
            RSA* rsa = PEM_read_bio_RSA_PUBKEY(mem, nullptr, nullptr, nullptr);
            BIO_free(mem);
            if (rsa) {
                return rsa;
            }
        }

        std::vector<uint8_t> raw;
        if (!hexToBytes(rsaPubKey, raw)) {
            return nullptr;
        }

        const unsigned char* p = raw.data();
        RSA* rsa = d2i_RSAPublicKey(nullptr, &p, static_cast<long>(raw.size()));
        if (rsa) {
            return rsa;
        }

        p = raw.data();
        rsa = d2i_RSA_PUBKEY(nullptr, &p, static_cast<long>(raw.size()));
        if (rsa) {
            return rsa;
        }

        if (raw.size() == 128U || raw.size() == 256U) {
            rsa = RSA_new();
            BIGNUM* n = BN_bin2bn(raw.data(), static_cast<int>(raw.size()), nullptr);
            BIGNUM* e = BN_new();
            if (rsa && n && e && BN_set_word(e, RSA_F4) == 1 && RSA_set0_key(rsa, n, e, nullptr) == 1) {
                return rsa;
            }
            if (n) {
                BN_free(n);
            }
            if (e) {
                BN_free(e);
            }
            if (rsa) {
                RSA_free(rsa);
            }
        }
        return nullptr;
    }

    bool rsaPublicEncryptPkcs1(const std::string& rsaPubKey,
                               const std::vector<uint8_t>& plain,
                               std::vector<uint8_t>& out)
    {
        RSA* rsa = loadRsaPublicKey(rsaPubKey);
        if (!rsa) {
            return false;
        }
        const int rsaSize = RSA_size(rsa);
        const int maxBlock = rsaSize - 11;
        if (rsaSize <= 0 || maxBlock <= 0) {
            RSA_free(rsa);
            return false;
        }
        out.clear();
        for (size_t off = 0; off < plain.size(); off += static_cast<size_t>(maxBlock)) {
            const size_t chunk = std::min(static_cast<size_t>(maxBlock), plain.size() - off);
            std::vector<uint8_t> block(static_cast<size_t>(rsaSize), 0U);
            const int n = RSA_public_encrypt(static_cast<int>(chunk),
                                             plain.data() + off,
                                             block.data(),
                                             rsa,
                                             RSA_PKCS1_PADDING);
            if (n <= 0) {
                RSA_free(rsa);
                return false;
            }
            out.insert(out.end(), block.begin(), block.begin() + n);
        }
        RSA_free(rsa);
        return true;
    }

    bool rsaPublicDecryptPkcs1(const std::string& rsaPubKey,
                               const uint8_t* in,
                               size_t inLen,
                               std::vector<uint8_t>& out)
    {
        RSA* rsa = loadRsaPublicKey(rsaPubKey);
        if (!rsa) {
            return false;
        }
        const int rsaSize = RSA_size(rsa);
        if (rsaSize <= 0 || inLen == 0U || (inLen % static_cast<size_t>(rsaSize)) != 0U) {
            RSA_free(rsa);
            return false;
        }
        out.clear();
        for (size_t off = 0; off < inLen; off += static_cast<size_t>(rsaSize)) {
            std::vector<uint8_t> block(static_cast<size_t>(rsaSize), 0U);
            const int n = RSA_public_decrypt(rsaSize, in + off, block.data(), rsa, RSA_PKCS1_PADDING);
            if (n < 0) {
                RSA_free(rsa);
                return false;
            }
            out.insert(out.end(), block.begin(), block.begin() + n);
        }
        RSA_free(rsa);
        return true;
    }

    bool aes128EcbEncryptPkcs5(const uint8_t key[16],
                               const std::vector<uint8_t>& plain,
                               std::vector<uint8_t>& out)
    {
        out.clear();
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        std::vector<uint8_t> buf(plain.size() + 16U, 0U);
        int outLen1 = 0;
        if (EVP_EncryptUpdate(ctx, buf.data(), &outLen1, plain.data(), static_cast<int>(plain.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        int outLen2 = 0;
        const int ok = EVP_EncryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    bool aes128EcbDecryptPkcs5(const uint8_t key[16],
                               const uint8_t* in,
                               size_t inLen,
                               std::vector<uint8_t>& out)
    {
        out.clear();
        if (!in || inLen == 0U) {
            return true;
        }
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        std::vector<uint8_t> buf(inLen + 16U, 0U);
        int outLen1 = 0;
        if (EVP_DecryptUpdate(ctx, buf.data(), &outLen1, in, static_cast<int>(inLen)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        int outLen2 = 0;
        const int ok = EVP_DecryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    const char* cmdName(uint8_t cmd)
    {
        switch (cmd) {
        case kCmdTimeSync: return "time_sync";
        case kCmdTotalCall: return "total_call";
        case kCmdExtPulse: return "ext_pulse";
        case kCmdFault: return "fault";
        case kCmdPowerControl: return "power_control";
        case kCmdRemoteControl: return "remote_control";
        case kCmdTradeRecord: return "trade_record";
        case kCmdFeeModelV2: return "fee_model_v2";
        case kCmdExtAllTelesignal: return "ext_all_telesignal";
        case kCmdExtChangedTelesignal: return "ext_changed_telesignal";
        case kCmdExtAllTelemetry: return "ext_all_telemetry";
        case kCmdChangedTelemetry4: return "changed_telemetry4";
        case kCmdRequestRsaPublicKey: return "request_rsa_public_key";
        case kCmdDeviceAuth: return "device_auth";
        case kCmdResetKey: return "reset_key";
        default: return "unknown";
        }
    }

    float readFloatLEValue(const uint8_t* p)
    {
        const uint32_t raw = static_cast<uint32_t>(p[0]) |
                             (static_cast<uint32_t>(p[1]) << 8) |
                             (static_cast<uint32_t>(p[2]) << 16) |
                             (static_cast<uint32_t>(p[3]) << 24);
        float v = 0.0f;
        std::memcpy(&v, &raw, sizeof(v));
        return v;
    }

    bool parseAddressPart(const std::string& text, uint16_t& out)
    {
        if (text.empty()) {
            return false;
        }
        char* end = nullptr;
        const unsigned long v = std::strtoul(text.c_str(), &end, 0);
        if (!end || *end != '\0' || v > 0xFFFFUL) {
            return false;
        }
        out = static_cast<uint16_t>(v & 0xFFFFU);
        return true;
    }

    bool parseAssetAddressCode(const std::string& text, uint16_t& station, uint16_t& device)
    {
        std::string s;
        for (size_t i = 0; i < text.size(); ++i) {
            if (!std::isspace(static_cast<unsigned char>(text[i]))) {
                s.push_back(text[i]);
            }
        }
        if (s.empty()) {
            return false;
        }

        const size_t sep = s.find_first_of(":,");
        if (sep != std::string::npos) {
            uint16_t st = 0;
            uint16_t dev = 0;
            if (!parseAddressPart(s.substr(0, sep), st) ||
                !parseAddressPart(s.substr(sep + 1U), dev)) {
                return false;
            }
            station = st;
            device = dev;
            return station != 0U && station != 0xFFFFU && device != 0U;
        }

        int base = 10;
        std::string num = s;
        if (num.size() > 2U && num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
            base = 16;
        } else if (num.size() == 8U) {
            bool allHex = true;
            for (size_t i = 0; i < num.size(); ++i) {
                if (std::isxdigit(static_cast<unsigned char>(num[i])) == 0) {
                    allHex = false;
                    break;
                }
            }
            if (allHex) {
                base = 16;
            }
        }
        char* end = nullptr;
        const unsigned long long v = std::strtoull(num.c_str(), &end, base);
        if (!end || *end != '\0') {
            return false;
        }
        const uint32_t addr = static_cast<uint32_t>(v & 0xFFFFFFFFULL);
        station = static_cast<uint16_t>(addr & 0xFFFFU);
        device = static_cast<uint16_t>((addr >> 16) & 0xFFFFU);
        return station != 0U && station != 0xFFFFU && device != 0U;
    }
}

CommProcess::CommProcess()
    : BaseProcess(PROC_COMMUNICATION, "tcu_comm")
    , m_logSender("tcu_comm")
    , m_seq(0)
    , m_platformConnected(false)
    , m_tcpFd(-1)
    , m_loginState(LOGIN_IDLE)
    , m_aesSessionKeyReady(false)
    , m_loginCryptoPrepared(false)
    , m_platformOnlineEventActive(false)
{
    m_aesSessionKey.fill(0);
}

CommProcess::~CommProcess()
{
}

bool CommProcess::doInitialize()
{
    if (!loadConfig()) {
        return false;
    }
    if (m_config.offlineRunMode) {
        m_platformOnlineEventActive = true;
    }
    if (!initMqtt()) {
        return false;
    }
    const size_t gunCount = static_cast<size_t>(m_config.gunCount);
    m_runtimeChangedByGun.assign(gunCount, 1);
    m_lastFaultActiveByGun.assign(gunCount, 0);
    m_lastFaultCodeByGun.assign(gunCount, 0U);
    m_pendingControls.assign(gunCount, PendingControl());
    m_lastRuntimeReport = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    m_lastPeriodicSetConfigPublish = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    m_logSender.info("init_completed", std::string("yuanyang_v2 gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void CommProcess::doRun()
{
    m_running = true;
    std::chrono::steady_clock::time_point lastFeedTime =
            std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (m_running.load()) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedWatchdog();
            feedDaemonWatchdog();
            lastFeedTime = now;
        }
        if (now - m_lastPeriodicSetConfigPublish >= std::chrono::seconds(30)) {
            publishInitialSetConfig();
            m_lastPeriodicSetConfigPublish = now;
        }
        maintainPlatformTcp();
        checkPendingControlTimeouts(now);
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
    m_config.gunCount = static_cast<uint8_t>(std::max(1, std::min(64, cfg.getInt(section, "gun_count", 1))));
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_comm_yuanyang_v2");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.biasNo = cfg.getInt(section, "bias_no", 0);
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.masterHost = cfg.getString(section, "master_host", "127.0.0.1");
    m_config.masterPort = cfg.getInt(section, "master_port", 9000);
    m_config.tcpReconnectSec = cfg.getInt(section, "tcp_reconnect_sec", 3);
    m_config.tcpHeartbeatSec = cfg.getInt(section, "tcp_heartbeat_sec", 20);
    if (m_config.tcpHeartbeatSec <= 0) {
        m_config.tcpHeartbeatSec = 20;
    }
    m_config.loginRetrySec = cfg.getInt(section, "login_retry_sec", 10);
    if (m_config.loginRetrySec <= 0) {
        m_config.loginRetrySec = 10;
    }
    m_config.assetCode = cfg.getString(section, "asset_code", "");
    m_config.stationAddr = static_cast<uint16_t>(cfg.getInt(section, "station_addr", 1) & 0xFFFF);
    if (m_config.stationAddr == 0 || m_config.stationAddr == 0xFFFFU) {
        m_config.stationAddr = 1;
    }
    uint16_t assetStation = 0;
    uint16_t assetDevice = 0;
    const bool hasAssetAddr = parseAssetAddressCode(m_config.assetCode, assetStation, assetDevice);
    if (hasAssetAddr) {
        // BY ZF: YuanyangV2 frame address comes from asset_code, same config role as NYC.
        m_config.stationAddr = assetStation;
    } else if (!m_config.assetCode.empty()) {
        m_logSender.warn("invalid_asset_code", m_config.assetCode);
    }
    m_config.rsaPublicKey = cfg.getString(section, "rsa_public_key", "");
    m_config.offlineRunMode = (cfg.getInt(section, "offline_run_mode", 0) != 0);
    m_config.debugTcp = (cfg.getInt(section, "debug", 0) != 0);

    m_config.deviceAddrList.clear();
    m_config.gunAssetCodeList.clear();
    m_gunRuntimeData.clear();
    m_feeModelByGun.clear();
    m_config.deviceAddrList.reserve(static_cast<size_t>(m_config.gunCount));
    m_config.gunAssetCodeList.reserve(static_cast<size_t>(m_config.gunCount));
    m_gunRuntimeData.reserve(static_cast<size_t>(m_config.gunCount));
    m_feeModelByGun.reserve(static_cast<size_t>(m_config.gunCount));
    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        std::ostringstream key;
        key << "gun" << static_cast<int>(i + 1) << "_device_addr";
        std::ostringstream assetKey;
        assetKey << "gun" << static_cast<int>(i + 1) << "_asset_code";
        const std::string gunAssetCode = cfg.getString(section, assetKey.str(), "");
        uint16_t gunStation = 0;
        uint16_t gunDevice = 0;
        int addr = cfg.getInt(section, key.str(), hasAssetAddr ? static_cast<int>(assetDevice) : static_cast<int>(i + 1));
        if (!gunAssetCode.empty() && parseAssetAddressCode(gunAssetCode, gunStation, gunDevice)) {
            if (gunStation != m_config.stationAddr) {
                m_logSender.warn("gun_asset_station_mismatch", gunAssetCode);
            }
            addr = static_cast<int>(gunDevice);
        } else if (!gunAssetCode.empty()) {
            m_logSender.warn("invalid_gun_asset_code", gunAssetCode);
        }
        if (addr <= 0 || addr > 0xFFFF) {
            addr = hasAssetAddr ? static_cast<int>(assetDevice) : static_cast<int>(i + 1);
        }
        m_config.deviceAddrList.push_back(static_cast<uint16_t>(addr & 0xFFFF));
        m_config.gunAssetCodeList.push_back(gunAssetCode.empty() ? m_config.assetCode : gunAssetCode);
        m_gunRuntimeData.push_back(GunRuntimeData());
        m_feeModelByGun.push_back(FeeModel());
    }
    m_rsaPublicKeyActive = m_config.rsaPublicKey;
    if (m_rsaPublicKeyActive.empty()) {
        m_logSender.warn("rsa_public_key_empty", "will request from platform");
    }
    return true;
}

bool CommProcess::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[YuanyangV2] mqtt init failed" << std::endl;
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
        std::cerr << "[YuanyangV2] mqtt connect failed" << std::endl;
        return false;
    }
    return m_mqtt.loopStart();
}

void CommProcess::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[YuanyangV2] mqtt connect rc=" << rc << std::endl;
        return;
    }
    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 1);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/pile/+/event", 1);
    m_mqtt.subscribe(p + "/meter/+/data", 0);
    publishInitialSetConfig();
    if (m_platformOnlineEventActive || m_config.offlineRunMode) {
        publishPlatformLinkEvent(true, m_config.offlineRunMode ? "offline_mode" : "login_ready");
    }
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
    } else if (module == "logic" && leaf == "feeData") {
        handleLogicFeeForPlatform(gun, payload);
    } else if (module == "pile" && leaf == "data") {
        handlePileDataForPlatform(gun, payload);
    } else if (module == "pile" && leaf == "event") {
        handlePileEventForPlatform(gun, payload);
    } else if (module == "meter" && leaf == "data") {
        handleMeterDataForPlatform(gun, payload);
    }
}

bool CommProcess::parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const
{
    const std::vector<std::string> seg = split(topic, '/');
    if (seg.size() != 4 || seg[0] != m_config.mqttTopicPrefix) {
        return false;
    }
    module = seg[1];
    const int externalGun = std::atoi(seg[2].c_str());
    const int localGun = externalGun - m_config.biasNo;
    if (localGun < 0 || localGun >= static_cast<int>(m_config.gunCount)) {
        return false;
    }
    gun = static_cast<uint8_t>(localGun);
    leaf = seg[3];
    return true;
}

std::string CommProcess::buildTopic(const char* module, uint8_t gun, const char* leaf) const
{
    std::ostringstream oss;
    oss << m_config.mqttTopicPrefix << "/" << module << "/"
        << (static_cast<int>(gun) + m_config.biasNo) << "/" << leaf;
    return oss.str();
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
    if (!cJSON_GetObjectItem(root, "gun")) {
        cJSON_AddNumberToObject(root, "gun", gun);
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string out = text ? text : payload;
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    return out;
}

bool CommProcess::handleLogicEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return false;
    }
    cJSON* evt = cJSON_GetObjectItem(root, "event");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsString(evt) && evt->valuestring && cJSON_IsObject(data)) {
        if (std::strcmp(evt->valuestring, "state_change") == 0) {
            cJSON* to = cJSON_GetObjectItem(data, "to");
            if (cJSON_IsString(to) && to->valuestring) {
                uint8_t status = 0;
                if (std::strcmp(to->valuestring, "IDLE") == 0) status = 0;
                else if (std::strcmp(to->valuestring, "PREPARE") == 0) status = 1;
                else if (std::strcmp(to->valuestring, "STARTING") == 0) status = 2;
                else if (std::strcmp(to->valuestring, "CHARGING") == 0) status = 3;
                else if (std::strcmp(to->valuestring, "STOPPING") == 0) status = 4;
                else if (std::strcmp(to->valuestring, "STOPPED") == 0) status = 5;
                else if (std::strcmp(to->valuestring, "ERROR") == 0) status = 6;
                if (m_gunRuntimeData[gun].gunStatus != status) {
                    m_gunRuntimeData[gun].gunStatus = status;
                    m_runtimeChangedByGun[gun] = 1;
                }
            }
        } else if (std::strcmp(evt->valuestring, "update_record") == 0) {
            std::vector<uint8_t> tail;
            if (buildChargeRecordTail(gun, data, tail)) {
                const std::vector<uint8_t> frame = buildAsduFrame(
                            kCmdTradeRecord, 0x01, kCotActive, 0x0000, tail,
                            kMasterAddr, kMasterAddr,
                            m_config.stationAddr, deviceAddrFromGun(gun));
                sendFrame(frame);
            }
        }
    }
    cJSON_Delete(root);
    return true;
}

bool CommProcess::handleLogicFeeForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        rd.totalEnergy = jsonNumber(data, "totalEnergy", rd.totalEnergy);
        rd.totalAmount = jsonNumber(data, "totalAmount", rd.totalAmount);
        rd.electricAmount = jsonNumber(data, "electricAmount", jsonNumber(data, "electicAmount", rd.electricAmount));
        rd.serviceAmount = jsonNumber(data, "serviceAmount", rd.serviceAmount);
        rd.chargedTime = jsonNumber(data, "chargedTime", jsonNumber(data, "chargeTime", rd.chargedTime));
        const std::string feeModelId = jsonString(data, "feeModelId");
        if (!feeModelId.empty()) {
            rd.feeModelId = feeModelId;
        }
        rd.feeTimeNum = jsonInt(data, "timeNum", jsonInt(data, "sgemtentNum", rd.feeTimeNum));
    }
    cJSON_Delete(root);
    return true;
}

bool CommProcess::handlePileDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsString(type) && type->valuestring && cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        if (std::strcmp(type->valuestring, "yx") == 0) {
            bool changed = false;
            struct U8Field { const char* key; uint8_t* dst; };
            U8Field fields[] = {
                {"workStatus", &rd.yxWorkStatus},
                {"totalErr", &rd.yxTotalFault},
                {"totalFault", &rd.yxTotalFault},
                {"totalAlarm", &rd.yxTotalAlarm},
                {"emergencyStopFault", &rd.yxEmergencyStopFault},
                {"vehicleConnectStatus", &rd.yxVehicleConnectStatus},
                {"gunSeatStatus", &rd.yxGunSeatStatus},
                {"electronicLockStatus", &rd.yxElectronicLockStatus},
                {"dcContactorStatus", &rd.yxDcContactorStatus}
            };
            for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
                const int v = jsonInt(data, fields[i].key, *fields[i].dst);
                const uint8_t nv = static_cast<uint8_t>(std::max(0, std::min(255, v)));
                if (*fields[i].dst != nv) {
                    *fields[i].dst = nv;
                    changed = true;
                }
            }
            const uint16_t otherFault = static_cast<uint16_t>(jsonInt(data, "otherFault", rd.yxOtherFault) & 0xFFFF);
            if (rd.yxOtherFault != otherFault) {
                rd.yxOtherFault = otherFault;
                changed = true;
            }
            if (changed) {
                m_runtimeChangedByGun[gun] = 1;
            }
        } else if (std::strcmp(type->valuestring, "yc") == 0) {
            rd.voltage = jsonNumber(data, "outputVoltage", rd.voltage);
            rd.current = jsonNumber(data, "outputCurrent", rd.current);
            rd.soc = jsonNumber(data, "soc", rd.soc);
            rd.batteryMinTemp = jsonNumber(data, "batteryMinTemp", rd.batteryMinTemp);
            rd.batteryMaxTemp = jsonNumber(data, "batteryMaxTemp", rd.batteryMaxTemp);
            rd.cellMaxVoltage = jsonNumber(data, "cellMaxVoltage", rd.cellMaxVoltage);
            rd.cellMinVoltage = jsonNumber(data, "cellMinVoltage", rd.cellMinVoltage);
            rd.pileEnvTemp = jsonNumber(data, "pileEnvTemp", rd.pileEnvTemp);
            rd.bmsReqVoltage = jsonNumber(data, "bmsReqVoltage", rd.bmsReqVoltage);
            rd.bmsReqCurrent = jsonNumber(data, "bmsReqCurrent", rd.bmsReqCurrent);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool CommProcess::handlePileEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsString(type) && type->valuestring && cJSON_IsObject(data)) {
        const bool isStart = (std::strcmp(type->valuestring, "start_complete") == 0);
        const bool isStop = (std::strcmp(type->valuestring, "stop_complete") == 0);
        if ((isStart || isStop) && gun < m_pendingControls.size() && m_pendingControls[gun].active) {
            PendingControl pending = m_pendingControls[gun];
            const bool match = (isStart && pending.action == 1U) || (isStop && pending.action == 0U);
            if (match) {
                const int successFlag = jsonInt(data, "successFlag", 0);
                sendControlAck(gun, pending, successFlag == 0 ? kCotConfirm : kCotReject);
                m_pendingControls[gun].active = false;
            }
        }
        if (isStop) {
            m_gunRuntimeData[gun].gunStatus = 5;
            m_runtimeChangedByGun[gun] = 1;
        }
    }
    cJSON_Delete(root);
    return true;
}

bool CommProcess::handleMeterDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        rd.meterEnergy = jsonNumber(data, "totalEnergy", rd.meterEnergy);
        rd.meterVoltage = jsonNumber(data, "voltage", rd.meterVoltage);
        rd.meterCurrent = jsonNumber(data, "current", rd.meterCurrent);
    }
    cJSON_Delete(root);
    return true;
}

bool CommProcess::publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj)
{
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", cmd);
    if (dataObj) {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(dataObj, 1));
    } else {
        cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    }
    char* text = cJSON_PrintUnformatted(root);
    const std::string payload = text ? text : "";
    if (text) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    if (payload.empty()) {
        return false;
    }
    return m_mqtt.publish(buildTopic("plat", gun, "cmd"), payload, 1, false);
}

void CommProcess::publishPlatformLinkEvent(bool online, const char* reason)
{
    const size_t count = static_cast<size_t>(m_config.gunCount);
    for (size_t i = 0; i < count; ++i) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
        cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
        cJSON_AddStringToObject(root, "source", "tcu_comm");
        cJSON_AddNumberToObject(root, "gun", static_cast<int>(i));
        cJSON_AddStringToObject(root, "event", online ? "platform_online" : "platform_offline");
        cJSON* data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "online", online ? 1 : 0);
        cJSON_AddStringToObject(data, "reason", reason ? reason : "");
        cJSON_AddItemToObject(root, "data", data);
        char* text = cJSON_PrintUnformatted(root);
        if (text) {
            m_mqtt.publish(buildTopic("plat", static_cast<uint8_t>(i), "event"), text, 1, true);
            cJSON_free(text);
        }
        cJSON_Delete(root);
    }
}

void CommProcess::publishInitialSetConfig()
{
    for (uint8_t gun = 0; gun < m_config.gunCount; ++gun) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs()));
        cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
        cJSON_AddStringToObject(root, "source", "tcu_comm");
        cJSON_AddNumberToObject(root, "gun", gun);
        cJSON_AddStringToObject(root, "type", "setConfig");
        cJSON* data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "stationAddr", m_config.stationAddr);
        cJSON_AddNumberToObject(data, "deviceAddr", deviceAddrFromGun(gun));
        cJSON_AddNumberToObject(data, "gunCount", m_config.gunCount);
        cJSON_AddStringToObject(data, "protocol", "YuanyangV2");
        cJSON_AddItemToObject(root, "data", data);
        char* text = cJSON_PrintUnformatted(root);
        if (text) {
            m_mqtt.publish(buildTopic("plat", gun, "event"), text, 1, true);
            cJSON_free(text);
        }
        cJSON_Delete(root);
    }
}

bool CommProcess::connectPlatformTcp()
{
    closePlatformTcp();
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::ostringstream portText;
    portText << m_config.masterPort;
    if (::getaddrinfo(m_config.masterHost.c_str(), portText.str().c_str(), &hints, &res) != 0 || !res) {
        m_logSender.warn("platform_tcp", "resolve_failed");
        return false;
    }
    int fd = -1;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        m_logSender.warn("platform_tcp", "connect_failed");
        return false;
    }
    m_tcpFd = fd;
    m_platformConnected.store(true);
    m_tcpRxCache.clear();
    m_loginState = LOGIN_IDLE;
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    resetCryptoSession();
    m_logSender.info("platform_tcp", "connected");
    return true;
}

void CommProcess::closePlatformTcp()
{
    if (m_tcpFd >= 0) {
        ::close(m_tcpFd);
        m_tcpFd = -1;
    }
    if (m_platformConnected.exchange(false)) {
        m_logSender.warn("platform_tcp", "closed");
    }
    m_loginState = LOGIN_IDLE;
    resetCryptoSession();
    if (m_platformOnlineEventActive && !m_config.offlineRunMode) {
        m_platformOnlineEventActive = false;
        publishPlatformLinkEvent(false, "tcp_closed");
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

    const int recvTimeoutSec = std::max(60, m_config.tcpHeartbeatSec * 6);
    if (now - m_lastHeartbeatRecv >= std::chrono::seconds(recvTimeoutSec)) {
        m_logSender.warn("platform_tcp", "heartbeat_recv_timeout");
        closePlatformTcp();
        return
    }

    char buf[1024];
    const ssize_t n = ::recv(m_tcpFd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0) {
        closePlatformTcp();
        return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        m_logSender.warn("platform_tcp", std::string("recv_error_") + std::strerror(errno));
        closePlatformTcp();
        return;
    }
    if (n > 0) {
        handlePlatformRxData(buf, static_cast<size_t>(n));
    }
    driveLoginStateMachine(now);
}

void CommProcess::driveLoginStateMachine(const std::chrono::steady_clock::time_point& now)
{
    if (!m_platformConnected.load()) {
        return;
    }
    switch (m_loginState) {
    case LOGIN_IDLE:
        if (m_nextLoginAllowedTime.time_since_epoch().count() != 0 && now < m_nextLoginAllowedTime) {
            break;
        }
        prepareLoginCryptoContext();
        m_loginState = LOGIN_REQ_RSA_PUBLIC_KEY;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        break;
    case LOGIN_REQ_RSA_PUBLIC_KEY:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (!sendRsaPublicKeyRequest()) {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_REQ_AUTH:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (!sendDeviceAuthRequest()) {
                m_logSender.warn("platform_login_step", "device_auth_send_fail");
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_ONLINE:
        if (now - m_lastHeartbeat >= std::chrono::seconds(m_config.tcpHeartbeatSec)) {
            if (!sendHeartbeat()) {
                closePlatformTcp();
            } else {
                m_lastHeartbeat = now;
            }
        }
        reportRuntimePeriodic();
        break;
    default:
        break;
    }
}

void CommProcess::checkPendingControlTimeouts(const std::chrono::steady_clock::time_point& now)
{
    for (uint8_t gun = 0; gun < m_pendingControls.size(); ++gun) {
        PendingControl& pending = m_pendingControls[gun];
        if (pending.active && now - pending.createdAt >= std::chrono::seconds(30)) {
            sendControlAck(gun, pending, kCotReject);
            pending.active = false;
            m_logSender.warn("platform_control", "pending_timeout");
        }
    }
}

std::vector<uint8_t> CommProcess::buildHeartbeatFrame(uint32_t control)
{
    std::vector<uint8_t> frame;
    frame.reserve(32);
    frame.push_back(0x68);
    frame.push_back(0x00);
    frame.push_back(0x00);
    appendU32LE(frame, control);
    appendU16LE(frame, kMasterAddr);
    appendU16LE(frame, kMasterAddr);
    appendU16LE(frame, m_config.stationAddr);
    appendU16LE(frame, deviceAddrFromGun(0));
    const uint16_t len = static_cast<uint16_t>(frame.size() - 3U);
    frame[1] = static_cast<uint8_t>(len & 0xFF);
    frame[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    return frame;
}

std::vector<uint8_t> CommProcess::buildAsduFrame(uint8_t cmd, uint8_t vsq, uint16_t cot, uint16_t commonAddr,
                                                 const std::vector<uint8_t>& tailPlain,
                                                 uint16_t targetStation, uint16_t targetDevice,
                                                 uint16_t sourceStation, uint16_t sourceDevice)
{
    std::vector<uint8_t> tail;
    if (!encryptTail(cmd, tailPlain, tail)) {
        return std::vector<uint8_t>();
    }
    std::vector<uint8_t> frame;
    frame.reserve(32U + tail.size());
    frame.push_back(0x68);
    frame.push_back(0x00);
    frame.push_back(0x00);
    appendU32LE(frame, kControlDefault);
    appendU16LE(frame, targetStation);
    appendU16LE(frame, targetDevice);
    appendU16LE(frame, sourceStation);
    appendU16LE(frame, sourceDevice);
    frame.push_back(cmd);
    frame.push_back(vsq);
    appendU16LE(frame, cot);
    appendU16LE(frame, commonAddr);
    frame.insert(frame.end(), tail.begin(), tail.end());
    if (frame.size() > 0xFFFFU + 3U) {
        return std::vector<uint8_t>();
    }
    const uint16_t len = static_cast<uint16_t>(frame.size() - 3U);
    frame[1] = static_cast<uint8_t>(len & 0xFF);
    frame[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    return frame;
}

bool CommProcess::sendFrame(const std::vector<uint8_t>& frame)
{
    if (m_tcpFd < 0 || frame.empty()) {
        return false;
    }
    if (m_config.debugTcp) {
        std::cout << "[YuanyangV2][TX] len=" << frame.size()
                  << " hex=" << toHex(frame.data(), frame.size()) << std::endl;
    }
    const ssize_t n = ::send(m_tcpFd, frame.data(), frame.size(), 0);
    return n >= 0 && static_cast<size_t>(n) == frame.size();
}

bool CommProcess::sendAsdu(uint8_t cmd, uint8_t vsq, uint16_t cot, uint16_t commonAddr,
                           const std::vector<uint8_t>& tailPlain,
                           uint16_t targetStation, uint16_t targetDevice)
{
    const std::vector<uint8_t> frame = buildAsduFrame(cmd, vsq, cot, commonAddr, tailPlain,
                                                      targetStation, targetDevice,
                                                      m_config.stationAddr, kBroadcastDeviceAddr);
    return sendFrame(frame);
}

bool CommProcess::sendHeartbeat()
{
    return sendFrame(buildHeartbeatFrame(kControlHeartbeatReq));
}

bool CommProcess::sendRsaPublicKeyRequest()
{
    std::vector<uint8_t> tail;
    const bool ok = sendAsdu(kCmdRequestRsaPublicKey, 0x00, kCotActive, 0x0000, tail);
    if (ok) {
        m_logSender.info("platform_login_step", "rsa_pubkey_req_sent");
    }
    return ok;
}

bool CommProcess::sendDeviceAuthRequest()
{
    if (m_rsaPublicKeyActive.empty()) {
        return false;
    }
    std::vector<uint8_t> tail = buildInfoAddrTail(0);
    tail.insert(tail.end(), m_aesSessionKey.begin(), m_aesSessionKey.end());
    const bool ok = sendAsdu(kCmdDeviceAuth, 0x00, kCotActive, 0x0000, tail);
    if (ok) {
        m_logSender.info("platform_login_step", "device_auth_sent");
    }
    return ok;
}

bool CommProcess::sendControlAck(uint8_t gun, const PendingControl& pending, uint16_t cot)
{
    std::vector<uint8_t> tail = pending.tailPlain;
    if (tail.size() >= 3U) {
        tail.push_back(0x00); // BY ZF: result text length, empty.
        for (int i = 0; i < 17; ++i) {
            tail.push_back(0x00); // BY ZF: VIN placeholder for v2.15 ack extension.
        }
    }
    const std::vector<uint8_t> frame = buildAsduFrame(kCmdRemoteControl, pending.vsq, cot, pending.commonAddr, tail,
                                                      kMasterAddr, kMasterAddr,
                                                      m_config.stationAddr, deviceAddrFromGun(gun));
    return sendFrame(frame);
}

bool CommProcess::sendFeeModelAck(const ParsedFrame& pf, bool ok)
{
    const uint16_t sourceDevice = (pf.targetDevice == 0 || pf.targetDevice == kBroadcastDeviceAddr)
            ? kBroadcastDeviceAddr : pf.targetDevice;
    const std::vector<uint8_t> frame = buildAsduFrame(kCmdFeeModelV2, pf.vsq, ok ? kCotConfirm : kCotReject,
                                                      pf.commonAddr, pf.tail,
                                                      kMasterAddr, kMasterAddr,
                                                      m_config.stationAddr, sourceDevice);
    return sendFrame(frame);
}

void CommProcess::handlePlatformRxData(const char* data, size_t len)
{
    if (!data || len == 0U) {
        return;
    }
    if (m_config.debugTcp) {
        std::cout << "[YuanyangV2][RX] len=" << len
                  << " hex=" << toHex(reinterpret_cast<const uint8_t*>(data), len) << std::endl;
    }
    m_tcpRxCache.insert(m_tcpRxCache.end(), data, data + len);
    while (m_tcpRxCache.size() >= 3U) {
        if (static_cast<uint8_t>(m_tcpRxCache[0]) != 0x68U) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        const uint16_t lenField = static_cast<uint16_t>(
                    static_cast<uint8_t>(m_tcpRxCache[1]) |
                    (static_cast<uint16_t>(static_cast<uint8_t>(m_tcpRxCache[2])) << 8));
        const size_t frameLen = static_cast<size_t>(lenField) + 3U;
        if (lenField < 12U) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        if (m_tcpRxCache.size() < frameLen) {
            break;
        }
        processPlatformPacket(reinterpret_cast<const uint8_t*>(m_tcpRxCache.data()), frameLen);
        m_tcpRxCache.erase(m_tcpRxCache.begin(), m_tcpRxCache.begin() + static_cast<long>(frameLen));
    }
}

void CommProcess::processPlatformPacket(const uint8_t* frame, size_t frameLen)
{
    ParsedFrame pf;
    if (!parseFrame(frame, frameLen, pf)) {
        m_logSender.warn("platform_rx", "parse_fail");
        return;
    }
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    if (!pf.hasAsdu) {
        if (pf.control == kControlHeartbeatAck) {
            if (m_loginState == LOGIN_ONLINE && !m_platformOnlineEventActive && !m_config.offlineRunMode) {
                m_platformOnlineEventActive = true;
                publishPlatformLinkEvent(true, "heartbeat_ok");
            }
        }
        return;
    }
    if (m_config.debugTcp) {
        std::cout << "[YuanyangV2][RX_FRAME] cmd=0x" << std::hex << static_cast<int>(pf.cmd) << std::dec
                  << "(" << cmdName(pf.cmd) << ")"
                  << " cot=0x" << std::hex << pf.cot << std::dec
                  << " tailLen=" << pf.tail.size() << std::endl;
    }

    if (pf.cmd == kCmdRequestRsaPublicKey) {
        if (pf.cot == kCotConfirm && tryUpdateRsaPubKeyFromResponse(pf.tail.empty() ? nullptr : pf.tail.data(),
                                                                     pf.tail.size())) {
            m_loginState = LOGIN_REQ_AUTH;
            m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
            m_logSender.info("platform_login_step", "rsa_pubkey_resp_ok");
        } else {
            m_loginState = LOGIN_IDLE;
            m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            m_logSender.warn("platform_login_step", "rsa_pubkey_resp_invalid");
        }
        return;
    }

    if (pf.cmd == kCmdDeviceAuth) {
        if (pf.cot == kCotConfirm) {
            m_aesSessionKeyReady = true;
            m_loginState = LOGIN_ONLINE;
            m_lastHeartbeat = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.tcpHeartbeatSec);
            m_lastHeartbeatRecv = std::chrono::steady_clock::now();
            if (!m_platformOnlineEventActive && !m_config.offlineRunMode) {
                m_platformOnlineEventActive = true;
                publishPlatformLinkEvent(true, "login_ready");
            }
            m_logSender.info("platform_login_step", "device_auth_ok");
        } else {
            m_loginState = LOGIN_IDLE;
            m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            m_logSender.warn("platform_login_step", "device_auth_rejected");
        }
        return;
    }

    if (pf.cmd == kCmdResetKey) {
        resetCryptoSession();
        prepareLoginCryptoContext();
        m_loginState = LOGIN_REQ_RSA_PUBLIC_KEY;
        m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
        m_logSender.warn("platform_login_step", "reset_key_rx");
        return;
    }

    if (pf.cmd == kCmdTotalCall && pf.cot == kCotActive) {
        sendTotalCallResponse();
        return;
    }

    if (pf.cmd == kCmdTimeSync && pf.cot == kCotActive) {
        syncSystemTimeFromTail(pf.tail.empty() ? nullptr : pf.tail.data(), pf.tail.size());
        return;
    }

    if (pf.cmd == kCmdRemoteControl && pf.cot == kCotActive) {
        uint8_t gun = 0;
        uint8_t action = 0;
        cJSON* dataObj = nullptr;
        if (parseRemoteControl041(pf, gun, action, &dataObj)) {
            publishPlatCommand(gun, action == 0 ? "stop_charge" : "start_charge", dataObj);
            PendingControl pending;
            pending.active = true;
            pending.action = (action == 0 ? 0U : 1U);
            pending.targetStation = pf.sourceStation;
            pending.targetDevice = pf.sourceDevice;
            pending.vsq = pf.vsq;
            pending.commonAddr = pf.commonAddr;
            pending.tailPlain = pf.tail;
            pending.createdAt = std::chrono::steady_clock::now();
            m_pendingControls[gun] = pending;
        } else {
            PendingControl pending;
            pending.active = true;
            pending.action = action;
            pending.vsq = pf.vsq;
            pending.commonAddr = pf.commonAddr;
            pending.tailPlain = pf.tail;
            sendControlAck(gun, pending, kCotReject);
        }
        if (dataObj) {
            cJSON_Delete(dataObj);
        }
        return;
    }

    if (pf.cmd == kCmdPowerControl && pf.cot == kCotActive) {
        uint8_t gun = 0;
        cJSON* dataObj = nullptr;
        if (parsePowerControl02E(pf, gun, &dataObj)) {
            publishPlatCommand(gun, "power_ctrl", dataObj);
            const std::vector<uint8_t> ack = buildAsduFrame(kCmdPowerControl, pf.vsq, kCotConfirm, pf.commonAddr, pf.tail,
                                                            kMasterAddr, kMasterAddr,
                                                            m_config.stationAddr, deviceAddrFromGun(gun));
            sendFrame(ack);
        }
        if (dataObj) {
            cJSON_Delete(dataObj);
        }
        return;
    }

    if (pf.cmd == kCmdFeeModelV2 && (pf.cot == kCotConfirm || pf.cot == kCotBurst || pf.cot == kCotActive)) {
        FeeModel feeModel;
        const bool ok = parseFeeModel051(pf, feeModel);
        if (ok) {
            for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
                m_feeModelByGun[i] = feeModel;
            }
            m_logSender.saveFeeModel(feeModel);
            m_logSender.info("platform_fee_model", feeModel.feeModelId);
        } else {
            m_logSender.warn("platform_fee_model", "parse_fail");
        }
        sendFeeModelAck(pf, ok);
        return;
    }

    if (pf.cmd == kCmdTradeRecord && pf.cot == kCotConfirm) {
        uint8_t gun = 0;
        cJSON* dataObj = nullptr;
        if (parseRecordConfirm042(pf, gun, &dataObj)) {
            publishPlatCommand(gun, "record_cfm", dataObj);
            const std::string tradeNo = dataObj ? jsonString(dataObj, "tradeNo") : std::string();
            if (!tradeNo.empty()) {
                m_logSender.confirmTradeRecord(tradeNo, 1);
            }
        }
        if (dataObj) {
            cJSON_Delete(dataObj);
        }
        return;
    }
}

bool CommProcess::parseFrame(const uint8_t* frame, size_t frameLen, ParsedFrame& out)
{
    if (!frame || frameLen < 15U || frame[0] != 0x68U) {
        return false;
    }
    const uint16_t lenField = readU16LE(frame + 1);
    if (frameLen != static_cast<size_t>(lenField) + 3U) {
        return false;
    }
    out.control = readU32LE(frame + 3);
    out.targetStation = readU16LE(frame + 7);
    out.targetDevice = readU16LE(frame + 9);
    out.sourceStation = readU16LE(frame + 11);
    out.sourceDevice = readU16LE(frame + 13);
    if (frameLen == 15U) {
        out.hasAsdu = false;
        return true;
    }
    if (frameLen < 21U) {
        return false;
    }
    out.hasAsdu = true;
    out.cmd = frame[15];
    out.vsq = frame[16];
    out.cot = readU16LE(frame + 17);
    out.commonAddr = readU16LE(frame + 19);
    const size_t rawTailLen = frameLen - 21U;
    const uint8_t* rawTail = rawTailLen > 0U ? frame + 21 : nullptr;
    if (!decryptTail(out.cmd, rawTail, rawTailLen, out.tail)) {
        return false;
    }
    return true;
}

void CommProcess::resetCryptoSession()
{
    m_aesSessionKey.fill(0);
    m_aesSessionKeyReady = false;
    m_loginCryptoPrepared = false;
}

void CommProcess::prepareLoginCryptoContext()
{
    if (m_loginCryptoPrepared) {
        return;
    }
    if (RAND_bytes(m_aesSessionKey.data(), static_cast<int>(m_aesSessionKey.size())) != 1) {
        std::random_device rd;
        for (size_t i = 0; i < m_aesSessionKey.size(); ++i) {
            m_aesSessionKey[i] = static_cast<uint8_t>(rd() & 0xFF);
        }
    }
    m_aesSessionKeyReady = false;
    m_loginCryptoPrepared = true;
}

bool CommProcess::tryUpdateRsaPubKeyFromResponse(const uint8_t* tail, size_t tailLen)
{
    if (!tail || tailLen == 0U) {
        return false;
    }
    size_t off = 0;
    size_t keyLen = tailLen;
    if (tailLen >= 5U) {
        off = 3U;
        const uint16_t lenLE = readU16LE(tail + off);
        if (lenLE > 0U && static_cast<size_t>(lenLE) <= tailLen - off - 2U) {
            off += 2U;
            keyLen = lenLE;
        } else {
            keyLen = tailLen - off;
        }
    }
    std::string key;
    const unsigned char* p = tail + off;
    RSA* rsa = d2i_RSAPublicKey(nullptr, &p, static_cast<long>(keyLen));
    if (!rsa) {
        p = tail + off;
        rsa = d2i_RSA_PUBKEY(nullptr, &p, static_cast<long>(keyLen));
    }
    if (rsa) {
        key = toHex(tail + off, keyLen);
        RSA_free(rsa);
    } else {
        key.assign(reinterpret_cast<const char*>(tail + off), keyLen);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key[0])) != 0) {
            key.erase(key.begin());
        }
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key[key.size() - 1U])) != 0) {
            key.erase(key.end() - 1);
        }
    }
    RSA* test = loadRsaPublicKey(key);
    if (!test) {
        return false;
    }
    RSA_free(test);
    if (key != m_rsaPublicKeyActive) {
        m_rsaPublicKeyActive = key;
        m_config.rsaPublicKey = key;
        persistRsaPubKeyToIni(key);
    }
    return true;
}

bool CommProcess::persistRsaPubKeyToIni(const std::string& pubKey)
{
    ConfigManagerLite& cfg = getConfig();
    cfg.setString("Comm", "rsa_public_key", pubKey);
    return cfg.saveConfig("/usr/app/config/tcu_comm.ini");
}

bool CommProcess::encryptTail(uint8_t cmd, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out)
{
    if (plain.empty()) {
        out.clear();
        return true;
    }
    if (cmd == kCmdRequestRsaPublicKey) {
        out = plain;
        return true;
    }
    if (cmd == kCmdDeviceAuth) {
        return rsaPublicEncryptPkcs1(m_rsaPublicKeyActive, plain, out);
    }
    if (!m_aesSessionKeyReady) {
        return false;
    }
    return aes128EcbEncryptPkcs5(m_aesSessionKey.data(), plain, out);
}

bool CommProcess::decryptTail(uint8_t cmd, const uint8_t* body, size_t bodyLen, std::vector<uint8_t>& out)
{
    if (!body || bodyLen == 0U) {
        out.clear();
        return true;
    }
    if (cmd == kCmdRequestRsaPublicKey) {
        out.assign(body, body + bodyLen);
        return true;
    }
    if (cmd == kCmdDeviceAuth) {
        return rsaPublicDecryptPkcs1(m_rsaPublicKeyActive, body, bodyLen, out);
    }
    if (!m_aesSessionKeyReady) {
        return false;
    }
    return aes128EcbDecryptPkcs5(m_aesSessionKey.data(), body, bodyLen, out);
}

bool CommProcess::parseRemoteControl041(const ParsedFrame& pf, uint8_t& gun, uint8_t& action, cJSON** outData)
{
    if (outData) {
        *outData = nullptr;
    }
    if (pf.tail.size() < 3U + 1U + 1U + 1U + 4U + 4U + 4U + 4U + 16U + 4U + 18U) {
        return false;
    }
    gun = gunFromDeviceAddr(pf.targetDevice);
    if (gun >= m_config.gunCount) {
        return false;
    }
    size_t pos = 3U;
    const uint8_t pointNo = pf.tail[pos++];
    const uint8_t actionRaw = pf.tail[pos++];
    const uint8_t controlInfo = pf.tail[pos++];
    pos += 4U; // schedule time.
    const float maxVoltage = readFloatLEValue(&pf.tail[pos]); pos += 4U;
    const float maxCurrent = readFloatLEValue(&pf.tail[pos]); pos += 4U;
    const float controlData = readFloatLEValue(&pf.tail[pos]); pos += 4U;
    std::string card(reinterpret_cast<const char*>(&pf.tail[pos]), 16U); pos += 16U;
    const float maxAmount = readFloatLEValue(&pf.tail[pos]); pos += 4U;
    std::string order(reinterpret_cast<const char*>(&pf.tail[pos]), 18U);
    action = static_cast<uint8_t>(actionRaw & 0x03U);

    while (!card.empty() && (card[card.size() - 1U] == '\0' || card[card.size() - 1U] == ' ')) {
        card.erase(card.end() - 1);
    }
    while (!order.empty() && (order[order.size() - 1U] == '\0' || order[order.size() - 1U] == ' ')) {
        order.erase(order.end() - 1);
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "pointNo", pointNo);
    cJSON_AddNumberToObject(data, "remoteAction", action);
    cJSON_AddNumberToObject(data, "controlInfo", controlInfo);
    cJSON_AddNumberToObject(data, "stopCondition", controlInfo & 0x1F);
    cJSON_AddNumberToObject(data, "chargeMode", 1);
    cJSON_AddNumberToObject(data, "maxVoltage", maxVoltage);
    cJSON_AddNumberToObject(data, "maxCurrent", maxCurrent);
    cJSON_AddNumberToObject(data, "controlData", controlData);
    cJSON_AddNumberToObject(data, "prechargeAmount", maxAmount > 0.0f ? maxAmount : controlData);
    cJSON_AddStringToObject(data, "chargeUserNo", card.c_str());
    cJSON_AddStringToObject(data, "cardNumber", card.c_str());
    cJSON_AddStringToObject(data, "orderNo", order.c_str());
    cJSON_AddStringToObject(data, "tradeNo", order.c_str());
    cJSON_AddStringToObject(data, "preTradeNo", order.c_str());
    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].chargeUserNo = card;
        m_gunRuntimeData[gun].orderNo = order;
    }
    if (outData) {
        *outData = data;
    } else {
        cJSON_Delete(data);
    }
    return true;
}

bool CommProcess::parsePowerControl02E(const ParsedFrame& pf, uint8_t& gun, cJSON** outData)
{
    if (outData) {
        *outData = nullptr;
    }
    if (pf.tail.size() < 7U) {
        return false;
    }
    gun = gunFromDeviceAddr(pf.targetDevice);
    if (gun >= m_config.gunCount) {
        return false;
    }
    const uint32_t infoAddr = static_cast<uint32_t>(pf.tail[0]) |
                              (static_cast<uint32_t>(pf.tail[1]) << 8) |
                              (static_cast<uint32_t>(pf.tail[2]) << 16);
    const uint32_t value = readU32LE(&pf.tail[3]);
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "ctrlType", infoAddr);
    cJSON_AddNumberToObject(data, "power", value / 100.0);
    cJSON_AddNumberToObject(data, "rawValue", value);
    if (outData) {
        *outData = data;
    } else {
        cJSON_Delete(data);
    }
    return true;
}

bool CommProcess::parseFeeModel051(const ParsedFrame& pf, FeeModel& feeModel)
{
    if (pf.tail.size() < 4U) {
        return false;
    }
    size_t pos = 3U;
    const uint8_t timeNum = pf.tail[pos++];
    if (timeNum == 0U || timeNum > 96U) {
        return false;
    }
    const size_t need = 3U + 1U + static_cast<size_t>(timeNum) * 5U + 8U * 4U;
    if (pf.tail.size() < need) {
        return false;
    }
    struct SegmentDef {
        uint8_t flag;
        uint8_t sh;
        uint8_t sm;
        uint8_t eh;
        uint8_t em;
    };
    std::vector<SegmentDef> segments;
    segments.reserve(timeNum);
    for (uint8_t i = 0; i < timeNum; ++i) {
        SegmentDef seg;
        seg.flag = pf.tail[pos++];
        seg.sh = pf.tail[pos++];
        seg.sm = pf.tail[pos++];
        seg.eh = pf.tail[pos++];
        seg.em = pf.tail[pos++];
        if (seg.flag < 1U || seg.flag > 4U || seg.sh > 23U || seg.sm > 59U) {
            return false;
        }
        segments.push_back(seg);
    }
    uint32_t chargePrice[5] = {0U};
    uint32_t servicePrice[5] = {0U};
    chargePrice[1] = readU32LE(&pf.tail[pos]); pos += 4U; // peak
    chargePrice[2] = readU32LE(&pf.tail[pos]); pos += 4U; // sharp
    chargePrice[3] = readU32LE(&pf.tail[pos]); pos += 4U; // flat
    chargePrice[4] = readU32LE(&pf.tail[pos]); pos += 4U; // valley
    servicePrice[1] = readU32LE(&pf.tail[pos]); pos += 4U;
    servicePrice[2] = readU32LE(&pf.tail[pos]); pos += 4U;
    servicePrice[3] = readU32LE(&pf.tail[pos]); pos += 4U;
    servicePrice[4] = readU32LE(&pf.tail[pos]); pos += 4U;

    std::ostringstream id;
    id << "YYV2_" << static_cast<unsigned long long>(nowMs());
    feeModel = FeeModel();
    feeModel.feeModelId = id.str();
    feeModel.timeNum = timeNum;
    for (size_t i = 0; i < segments.size(); ++i) {
        char hhmm[5] = {0};
        std::snprintf(hhmm, sizeof(hhmm), "%02u%02u",
                      static_cast<unsigned int>(segments[i].sh),
                      static_cast<unsigned int>(segments[i].sm));
        feeModel.timeSeg.push_back(hhmm);
        feeModel.segFlag.push_back(segments[i].flag);
        feeModel.chargeFee.push_back(chargePrice[segments[i].flag]);
        feeModel.serviceFee.push_back(servicePrice[segments[i].flag]);
    }
    return true;
}

bool CommProcess::parseRecordConfirm042(const ParsedFrame& pf, uint8_t& gun, cJSON** outData)
{
    if (outData) {
        *outData = nullptr;
    }
    gun = gunFromDeviceAddr(pf.targetDevice);
    if (gun >= m_config.gunCount) {
        return false;
    }
    std::string tradeNo = (gun < m_gunRuntimeData.size()) ? m_gunRuntimeData[gun].pendingRecordTradeNo : std::string();
    if (tradeNo.empty() && pf.tail.size() >= 3U + 6U + 6U + 16U + 4U * 14U + 1U + 2U + 4U + 18U) {
        const size_t off = 3U + 6U + 6U + 16U + 4U * 14U + 1U + 2U + 4U;
        tradeNo.assign(reinterpret_cast<const char*>(&pf.tail[off]), 18U);
        while (!tradeNo.empty() && (tradeNo[tradeNo.size() - 1U] == '\0' || tradeNo[tradeNo.size() - 1U] == ' ')) {
            tradeNo.erase(tradeNo.end() - 1);
        }
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tradeNo", tradeNo.c_str());
    cJSON_AddNumberToObject(data, "confirmFlag", 1);
    if (outData) {
        *outData = data;
    } else {
        cJSON_Delete(data);
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildInfoAddrTail(uint32_t infoAddr) const
{
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(infoAddr & 0xFFU));
    out.push_back(static_cast<uint8_t>((infoAddr >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>((infoAddr >> 16) & 0xFFU));
    return out;
}

std::vector<uint8_t> CommProcess::buildAllTelesignalTail() const
{
    std::vector<uint8_t> out = buildInfoAddrTail(0);
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        const GunRuntimeData& rd = m_gunRuntimeData[i];
        out.push_back(rd.gunStatus != 0 ? 1 : 0);
        out.push_back(rd.yxWorkStatus != 0 ? 1 : 0);
        out.push_back(rd.yxTotalFault != 0 ? 1 : 0);
        out.push_back(rd.yxTotalAlarm != 0 ? 1 : 0);
        out.push_back(rd.yxEmergencyStopFault != 0 ? 1 : 0);
        out.push_back(rd.yxVehicleConnectStatus != 0 ? 1 : 0);
        out.push_back(rd.yxGunSeatStatus != 0 ? 1 : 0);
        out.push_back(rd.yxElectronicLockStatus != 0 ? 1 : 0);
        out.push_back(rd.yxDcContactorStatus != 0 ? 1 : 0);
        out.push_back(rd.yxOtherFault != 0 ? 1 : 0);
    }
    return out;
}

std::vector<uint8_t> CommProcess::buildChangedTelesignalTail(uint8_t gun) const
{
    std::vector<uint8_t> out;
    if (gun >= m_gunRuntimeData.size()) {
        return out;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    const uint32_t base = static_cast<uint32_t>(gun) * kYxPointsPerGun;
    uint8_t vals[kYxPointsPerGun] = {
        static_cast<uint8_t>(rd.gunStatus != 0),
        static_cast<uint8_t>(rd.yxWorkStatus != 0),
        static_cast<uint8_t>(rd.yxTotalFault != 0),
        static_cast<uint8_t>(rd.yxTotalAlarm != 0),
        static_cast<uint8_t>(rd.yxEmergencyStopFault != 0),
        static_cast<uint8_t>(rd.yxVehicleConnectStatus != 0),
        static_cast<uint8_t>(rd.yxGunSeatStatus != 0),
        static_cast<uint8_t>(rd.yxElectronicLockStatus != 0),
        static_cast<uint8_t>(rd.yxDcContactorStatus != 0),
        static_cast<uint8_t>(rd.yxOtherFault != 0)
    };
    for (int i = 0; i < kYxPointsPerGun; ++i) {
        const std::vector<uint8_t> addr = buildInfoAddrTail(base + static_cast<uint32_t>(i));
        out.insert(out.end(), addr.begin(), addr.end());
        out.push_back(vals[i]);
    }
    return out;
}

std::vector<uint8_t> CommProcess::buildAllTelemetryTail() const
{
    std::vector<uint8_t> out = buildInfoAddrTail(0);
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        const GunRuntimeData& rd = m_gunRuntimeData[i];
        appendI32LE(out, scaleToI32(rd.voltage, 10.0));
        appendI32LE(out, scaleToI32(rd.current, 10.0));
        appendI32LE(out, scaleToI32(rd.soc, 1.0));
        appendI32LE(out, scaleToI32(rd.batteryMinTemp, 1.0));
        appendI32LE(out, scaleToI32(rd.batteryMaxTemp, 1.0));
        appendI32LE(out, scaleToI32(rd.cellMaxVoltage, 1000.0));
        appendI32LE(out, scaleToI32(rd.cellMinVoltage, 1000.0));
        appendI32LE(out, scaleToI32(rd.pileEnvTemp, 1.0));
        appendI32LE(out, scaleToI32(rd.bmsReqVoltage, 10.0));
        appendI32LE(out, scaleToI32(rd.bmsReqCurrent, 10.0));
        appendI32LE(out, scaleToI32(rd.meterVoltage, 10.0));
        appendI32LE(out, scaleToI32(rd.meterCurrent, 10.0));
        appendI32LE(out, scaleToI32(rd.totalEnergy, 1000.0));
        appendI32LE(out, scaleToI32(rd.totalAmount, 10000.0));
    }
    return out;
}

std::vector<uint8_t> CommProcess::buildChangedTelemetryTail(uint8_t gun) const
{
    std::vector<uint8_t> out;
    if (gun >= m_gunRuntimeData.size()) {
        return out;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    const int32_t vals[kYcPointsPerGun] = {
        scaleToI32(rd.voltage, 10.0),
        scaleToI32(rd.current, 10.0),
        scaleToI32(rd.soc, 1.0),
        scaleToI32(rd.batteryMinTemp, 1.0),
        scaleToI32(rd.batteryMaxTemp, 1.0),
        scaleToI32(rd.cellMaxVoltage, 1000.0),
        scaleToI32(rd.cellMinVoltage, 1000.0),
        scaleToI32(rd.pileEnvTemp, 1.0),
        scaleToI32(rd.bmsReqVoltage, 10.0),
        scaleToI32(rd.bmsReqCurrent, 10.0),
        scaleToI32(rd.meterVoltage, 10.0),
        scaleToI32(rd.meterCurrent, 10.0),
        scaleToI32(rd.totalEnergy, 1000.0),
        scaleToI32(rd.totalAmount, 10000.0)
    };
    const uint32_t base = static_cast<uint32_t>(gun) * kYcPointsPerGun;
    for (int i = 0; i < kYcPointsPerGun; ++i) {
        const std::vector<uint8_t> addr = buildInfoAddrTail(base + static_cast<uint32_t>(i));
        out.insert(out.end(), addr.begin(), addr.end());
        appendI32LE(out, vals[i]);
    }
    return out;
}

std::vector<uint8_t> CommProcess::buildExtPulseTail() const
{
    std::vector<uint8_t> out = buildInfoAddrTail(0);
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        const GunRuntimeData& rd = m_gunRuntimeData[i];
        const uint32_t vals[kYmPointsPerGun] = {
            scaleToU32(rd.meterEnergy, 1000.0),
            scaleToU32(rd.totalEnergy, 1000.0),
            scaleToU32(rd.totalAmount, 10000.0),
            scaleToU32(rd.electricAmount, 10000.0),
            scaleToU32(rd.serviceAmount, 10000.0)
        };
        for (int j = 0; j < kYmPointsPerGun; ++j) {
            appendU32LE(out, vals[j]);
            out.push_back(0x00);
        }
    }
    return out;
}

std::vector<uint8_t> CommProcess::buildFaultTail(uint8_t gun, uint32_t faultCode, bool active) const
{
    (void)gun;
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(faultCode & 0xFFU));
    out.push_back(static_cast<uint8_t>((faultCode >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>((faultCode >> 16) & 0xFFU));
    out.push_back(active ? 0x01 : 0x00);
    return out;
}

bool CommProcess::buildChargeRecordTail(uint8_t gun, cJSON* data, std::vector<uint8_t>& tail)
{
    if (!data || gun >= m_gunRuntimeData.size()) {
        return false;
    }
    tail = buildInfoAddrTail(0);
    const uint64_t startTime = static_cast<uint64_t>(jsonNumber(data, "chargeStartTime", 0.0));
    const uint64_t endTime = static_cast<uint64_t>(jsonNumber(data, "chargeEndTime", 0.0));
    auto appendTime6 = [&tail](uint64_t ymdhms) {
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
        auto d2 = [](char a, char b) -> uint8_t {
            if (a < '0' || a > '9' || b < '0' || b > '9') {
                return 0;
            }
            return static_cast<uint8_t>((a - '0') * 10 + (b - '0'));
        };
        tail.push_back(d2(buf[12], buf[13]));
        tail.push_back(d2(buf[10], buf[11]));
        tail.push_back(d2(buf[8], buf[9]));
        tail.push_back(d2(buf[6], buf[7]));
        tail.push_back(d2(buf[4], buf[5]));
        tail.push_back(d2(buf[2], buf[3]));
    };
    auto appendFixedAscii = [&tail](const std::string& text, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            tail.push_back(i < text.size() ? static_cast<uint8_t>(text[i]) : 0x20U);
        }
    };
    appendTime6(startTime);
    appendTime6(endTime);
    std::string card = jsonString(data, "cardNumber");
    if (card.empty()) {
        card = m_gunRuntimeData[gun].chargeUserNo;
    }
    appendFixedAscii(card, 16U);
    const double sumStart = jsonNumber(data, "sumStart", m_gunRuntimeData[gun].meterEnergy);
    const double sumEnd = jsonNumber(data, "sumEnd", m_gunRuntimeData[gun].meterEnergy);
    const double totalElect = jsonNumber(data, "totalElect", m_gunRuntimeData[gun].totalEnergy);
    const double totalCost = jsonNumber(data, "totalCost", m_gunRuntimeData[gun].totalAmount);
    appendFloatLE(tail, static_cast<float>(sumStart));
    appendFloatLE(tail, static_cast<float>(sumEnd));
    appendFloatLE(tail, static_cast<float>(totalElect));
    appendFloatLE(tail, static_cast<float>(totalCost));
    for (int i = 0; i < 2; ++i) {
        tail.push_back(0xFF);
        tail.push_back(0xFF);
        tail.push_back(0xFF);
        tail.push_back(0xFF);
    }

    double energyByFlag[5] = {0.0};
    double chargeByFlag[5] = {0.0};
    double serviceByFlag[5] = {0.0};
    cJSON* partElect = cJSON_GetObjectItem(data, "partElect");
    cJSON* chargeFee = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFee = cJSON_GetObjectItem(data, "serviceFee");
    const FeeModel* model = (gun < m_feeModelByGun.size()) ? &m_feeModelByGun[gun] : nullptr;
    const int cnt = cJSON_IsArray(partElect) ? cJSON_GetArraySize(partElect) : 0;
    for (int i = 0; i < cnt; ++i) {
        unsigned int flag = 3U;
        if (model && model->segFlag.size() > static_cast<size_t>(i)) {
            flag = model->segFlag[static_cast<size_t>(i)];
        }
        if (flag < 1U || flag > 4U) {
            flag = 3U;
        }
        cJSON* e = cJSON_GetArrayItem(partElect, i);
        cJSON* c = cJSON_IsArray(chargeFee) ? cJSON_GetArrayItem(chargeFee, i) : nullptr;
        cJSON* s = cJSON_IsArray(serviceFee) ? cJSON_GetArrayItem(serviceFee, i) : nullptr;
        if (cJSON_IsNumber(e)) energyByFlag[flag] += e->valuedouble;
        if (cJSON_IsNumber(c)) chargeByFlag[flag] += c->valuedouble;
        if (cJSON_IsNumber(s)) serviceByFlag[flag] += s->valuedouble;
    }
    const int order[4] = {1, 4, 2, 3}; // BY ZF: peak, valley, sharp, flat.
    for (int i = 0; i < 4; ++i) appendFloatLE(tail, static_cast<float>(energyByFlag[order[i]]));
    for (int i = 0; i < 4; ++i) appendFloatLE(tail, static_cast<float>(chargeByFlag[order[i]] + serviceByFlag[order[i]]));
    tail.push_back(static_cast<uint8_t>(std::max(0, std::min(100, jsonInt(data, "endSoc", 0)))));
    appendU16LE(tail, static_cast<uint16_t>(jsonInt(data, "reason", 0) & 0xFFFF));
    appendU32LE(tail, static_cast<uint32_t>(jsonInt(data, "reasonParam", 0)));
    std::string orderNo = jsonString(data, "orderNo");
    if (orderNo.empty()) {
        orderNo = jsonString(data, "tradeNo");
    }
    appendFixedAscii(orderNo, 18U);
    m_gunRuntimeData[gun].pendingRecordTradeNo = jsonString(data, "tradeNo").empty() ? orderNo : jsonString(data, "tradeNo");
    return true;
}

void CommProcess::reportRuntimePeriodic()
{
    if (m_loginState != LOGIN_ONLINE || !m_aesSessionKeyReady) {
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const bool periodic = (now - m_lastRuntimeReport >= std::chrono::seconds(10));
    for (uint8_t gun = 0; gun < m_config.gunCount; ++gun) {
        const bool changed = gun < m_runtimeChangedByGun.size() && m_runtimeChangedByGun[gun] != 0;
        if (changed) {
            const std::vector<uint8_t> yx = buildChangedTelesignalTail(gun);
            sendFrame(buildAsduFrame(kCmdExtChangedTelesignal, kYxPointsPerGun, kCotBurst, 0x0000, yx,
                                     kMasterAddr, kMasterAddr,
                                     m_config.stationAddr, deviceAddrFromGun(gun)));
            const std::vector<uint8_t> yc = buildChangedTelemetryTail(gun);
            sendFrame(buildAsduFrame(kCmdChangedTelemetry4, kYcPointsPerGun, kCotBurst, 0x0000, yc,
                                     kMasterAddr, kMasterAddr,
                                     m_config.stationAddr, deviceAddrFromGun(gun)));
            const uint32_t faultCode = (m_gunRuntimeData[gun].yxOtherFault != 0)
                    ? static_cast<uint32_t>(m_gunRuntimeData[gun].yxOtherFault)
                    : (m_gunRuntimeData[gun].yxTotalFault ? 1U : 0U);
            const bool faultActive = faultCode != 0U;
            if (faultActive != (m_lastFaultActiveByGun[gun] != 0) || faultCode != m_lastFaultCodeByGun[gun]) {
                sendFrame(buildAsduFrame(kCmdFault, 0x01, kCotBurst, 0x0000,
                                         buildFaultTail(gun, faultCode, faultActive),
                                         kMasterAddr, kMasterAddr,
                                         m_config.stationAddr, deviceAddrFromGun(gun)));
                m_lastFaultActiveByGun[gun] = faultActive ? 1 : 0;
                m_lastFaultCodeByGun[gun] = faultCode;
            }
            m_runtimeChangedByGun[gun] = 0;
        }
        if (periodic) {
            const std::vector<uint8_t> yc = buildChangedTelemetryTail(gun);
            sendFrame(buildAsduFrame(kCmdExtAllTelemetry, kYcPointsPerGun, kCotBurst, 0x0000, yc,
                                     kMasterAddr, kMasterAddr,
                                     m_config.stationAddr, deviceAddrFromGun(gun)));
            const std::vector<uint8_t> yx = buildChangedTelesignalTail(gun);
            sendFrame(buildAsduFrame(kCmdExtAllTelesignal, kYxPointsPerGun, kCotBurst, 0x0000, yx,
                                     kMasterAddr, kMasterAddr,
                                     m_config.stationAddr, deviceAddrFromGun(gun)));
        }
    }
    if (periodic) {
        const int pulseCount = static_cast<int>(m_config.gunCount) * kYmPointsPerGun;
        sendFrame(buildAsduFrame(kCmdExtPulse, static_cast<uint8_t>(0x80 | std::min(127, pulseCount)),
                                 kCotBurst, 0x0000, buildExtPulseTail(),
                                 kMasterAddr, kMasterAddr,
                                 m_config.stationAddr, kBroadcastDeviceAddr));
        m_lastRuntimeReport = now;
    }
}

void CommProcess::sendTotalCallResponse()
{
    for (uint8_t gun = 0; gun < m_config.gunCount; ++gun) {
        sendFrame(buildAsduFrame(kCmdExtAllTelesignal, kYxPointsPerGun, kCotTotalCall, 0x0000,
                                 buildChangedTelesignalTail(gun),
                                 kMasterAddr, kMasterAddr,
                                 m_config.stationAddr, deviceAddrFromGun(gun)));
        sendFrame(buildAsduFrame(kCmdExtAllTelemetry, kYcPointsPerGun, kCotTotalCall, 0x0000,
                                 buildChangedTelemetryTail(gun),
                                 kMasterAddr, kMasterAddr,
                                 m_config.stationAddr, deviceAddrFromGun(gun)));
    }
    const int pulseCount = static_cast<int>(m_config.gunCount) * kYmPointsPerGun;
    sendFrame(buildAsduFrame(kCmdExtPulse, static_cast<uint8_t>(0x80 | std::min(127, pulseCount)),
                             kCotTotalCall, 0x0000, buildExtPulseTail(),
                             kMasterAddr, kMasterAddr,
                             m_config.stationAddr, kBroadcastDeviceAddr));
    sendFrame(buildAsduFrame(kCmdFault, 0x00, kCotTotalCall, 0x0000, std::vector<uint8_t>(),
                             kMasterAddr, kMasterAddr,
                             m_config.stationAddr, kBroadcastDeviceAddr));
}

uint8_t CommProcess::gunFromDeviceAddr(uint16_t deviceAddr) const
{
    for (size_t i = 0; i < m_config.deviceAddrList.size(); ++i) {
        if (m_config.deviceAddrList[i] == deviceAddr) {
            return static_cast<uint8_t>(i);
        }
    }
    if (deviceAddr > 0U && deviceAddr <= m_config.gunCount) {
        return static_cast<uint8_t>(deviceAddr - 1U);
    }
    return 0;
}

uint16_t CommProcess::deviceAddrFromGun(uint8_t gun) const
{
    if (gun < m_config.deviceAddrList.size()) {
        return m_config.deviceAddrList[gun];
    }
    return static_cast<uint16_t>(gun + 1U);
}

bool CommProcess::syncSystemTimeFromTail(const uint8_t* tail, size_t tailLen)
{
    if (!tail || tailLen < 10U) {
        return false;
    }
    const uint8_t* t = tail + 3U;
    const uint16_t ms = readU16LE(t);
    const int second = (ms / 1000U) % 60U;
    const int minute = t[2] & 0x3F;
    const int hour = t[3] & 0x1F;
    const int day = t[4] & 0x1F;
    const int month = t[5] & 0x0F;
    const int year = 2000 + (t[6] & 0x7F);
    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "date -s \"%04d-%02d-%02d %02d:%02d:%02d\" >/dev/null 2>&1",
                  year, month, day, hour, minute, second);
    const int rc = std::system(cmd);
    m_logSender.info("platform_time_sync", rc == 0 ? "ok" : "system_date_fail");
    return rc == 0;
}

void CommProcess::appendU16LE(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFFU));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
}

void CommProcess::appendU32LE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFFU));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFU));
}

void CommProcess::appendI32LE(std::vector<uint8_t>& out, int32_t v)
{
    appendU32LE(out, static_cast<uint32_t>(v));
}

uint16_t CommProcess::readU16LE(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

uint32_t CommProcess::readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void CommProcess::appendFloatLE(std::vector<uint8_t>& out, float v)
{
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(raw));
    appendU32LE(out, raw);
}

double CommProcess::jsonNumber(cJSON* obj, const char* key, double defValue)
{
    cJSON* n = cJSON_GetObjectItem(obj, key);
    return (n && cJSON_IsNumber(n)) ? n->valuedouble : defValue;
}

int CommProcess::jsonInt(cJSON* obj, const char* key, int defValue)
{
    cJSON* n = cJSON_GetObjectItem(obj, key);
    return (n && cJSON_IsNumber(n)) ? n->valueint : defValue;
}

std::string CommProcess::jsonString(cJSON* obj, const char* key)
{
    cJSON* s = cJSON_GetObjectItem(obj, key);
    return (s && cJSON_IsString(s) && s->valuestring) ? std::string(s->valuestring) : std::string();
}

uint32_t CommProcess::scaleToU32(double v, double scale)
{
    if (v <= 0.0) {
        return 0U;
    }
    return static_cast<uint32_t>(v * scale + 0.5);
}

int32_t CommProcess::scaleToI32(double v, double scale)
{
    return static_cast<int32_t>(v * scale + (v >= 0.0 ? 0.5 : -0.5));
}

std::string CommProcess::toHex(const uint8_t* data, size_t len)
{
    if (!data || len == 0U) {
        return std::string();
    }
    static const char* tbl = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2U);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(tbl[(data[i] >> 4) & 0x0F]);
        out.push_back(tbl[data[i] & 0x0F]);
    }
    return out;
}

bool CommProcess::isHexString(const std::string& s)
{
    if (s.empty()) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (std::isxdigit(static_cast<unsigned char>(s[i])) == 0) {
            return false;
        }
    }
    return true;
}

uint64_t CommProcess::nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
}
