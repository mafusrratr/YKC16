/**
 * TCU log upload process implementation.
 * BY ZF
 */

#include "tcu_uploader_process.h"

#include "../base/cjson/include/cjson/cJSON.h"

#include <curl/curl.h>
#include <openssl/sha.h>
#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace {

// BY ZF: local file/path helpers keep this process independent from Qt.
std::string trim(const std::string& value)
{
    const char* ws = " \t\r\n";
    const size_t start = value.find_first_not_of(ws);
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(ws);
    return value.substr(start, end - start + 1);
}

std::vector<std::string> splitCsv(const std::string& csv)
{
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ensureDirectoryRecursive(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    if (dirExists(path)) {
        return true;
    }

    const size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0) {
        if (!ensureDirectoryRecursive(path.substr(0, pos))) {
            return false;
        }
    }
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

std::string joinPath(const std::string& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }
    if (left[left.size() - 1] == '/') {
        return left + right;
    }
    return left + "/" + right;
}

std::string baseName(const std::string& path)
{
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string shellQuote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

bool removeRecursive(const std::string& path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }
    struct dirent* entry = nullptr;
    bool ok = true;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!removeRecursive(joinPath(path, name))) {
            ok = false;
        }
    }
    closedir(dir);
    return rmdir(path.c_str()) == 0 && ok;
}

std::string currentTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm tmv;
    localtime_r(&now, &tmv);
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tmv);
    return buf;
}

std::string jsonString(cJSON* obj, const char* key)
{
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return "";
    }
    return item->valuestring;
}

std::string sanitizeDeviceId(const std::string& in)
{
    std::string out;
    for (char c : in) {
        const bool ok = (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (ok) {
            out.push_back(c);
        }
        if (out.size() >= 64) {
            break;
        }
    }
    return out.empty() ? "tcu" : out;
}

std::string sha256File(const std::string& path)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return "";
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    unsigned char buf[8192];
    while (true) {
        const size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            SHA256_Update(&ctx, buf, n);
        }
        if (n < sizeof(buf)) {
            if (std::ferror(fp)) {
                std::fclose(fp);
                return "";
            }
            break;
        }
    }
    std::fclose(fp);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    std::ostringstream oss;
    for (unsigned char b : digest) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

size_t curlWriteToString(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    if (!userdata) {
        return size * nmemb;
    }
    std::string* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string readTextFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeTextFile(const std::string& path, const std::string& text)
{
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp);
    if (!out.is_open()) {
        return false;
    }
    out << text;
    out.close();
    if (!out) {
        unlink(tmp.c_str());
        return false;
    }
    return rename(tmp.c_str(), path.c_str()) == 0;
}

std::string quoteIdent(const std::string& ident)
{
    std::string out = "\"";
    for (char c : ident) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

std::string stateKey(const std::string& dbName, const std::string& tableName)
{
    return dbName + "." + tableName;
}

bool sqliteExec(sqlite3* db, const std::string& sql)
{
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[tcu_uploader] sqlite exec failed: " << (err ? err : "") << " sql=" << sql << std::endl;
        if (err) {
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

bool sqliteScalarText(sqlite3* db, const std::string& sql, const std::string& bindValue, std::string& value, bool& hasValue)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, bindValue.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    hasValue = false;
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        value = text ? reinterpret_cast<const char*>(text) : "";
        hasValue = true;
    } else if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

bool sqliteScalarPair(sqlite3* db,
                      const std::string& sql,
                      const std::string& bindTime,
                      const std::string& bindSeq,
                      std::string& timeValue,
                      std::string& seqValue,
                      bool& hasValue)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, bindTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bindTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, bindSeq.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    hasValue = false;
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        const unsigned char* s = sqlite3_column_text(stmt, 1);
        timeValue = t ? reinterpret_cast<const char*>(t) : "";
        seqValue = s ? reinterpret_cast<const char*>(s) : "";
        hasValue = !timeValue.empty();
    } else if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

bool dbEnabled(const std::vector<std::string>& includeFiles, const std::string& dbName)
{
    return std::find(includeFiles.begin(), includeFiles.end(), dbName) != includeFiles.end();
}

bool isTimestamp14(const std::string& value)
{
    if (value.size() != 14) {
        return false;
    }
    for (char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

TcuUploaderProcess::TcuUploaderProcess(const std::string& configPath)
    : m_configPath(configPath)
    , m_config(ConfigManagerLite::getInstance())
    , m_log("tcu_uploader")
    , m_running(false)
    , m_watchdogRunning(false)
{
}

TcuUploaderProcess::~TcuUploaderProcess()
{
    stop();
    curl_global_cleanup();
}

bool TcuUploaderProcess::initialize()
{
    if (!loadConfig()) {
        return false;
    }
    if (!ensureWorkDirs()) {
        std::cerr << "[tcu_uploader] create work dirs failed" << std::endl;
        return false;
    }
    loadDeviceIdCache();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    initMqtt();
    return true;
}

int TcuUploaderProcess::run(bool once)
{
    if (!m_cfg.enabled) {
        std::cout << "[tcu_uploader] disabled by config" << std::endl;
        return 0;
    }

    m_running = true;
    if (!m_watchdogRunning.load()) {
        m_watchdogRunning = true;
        m_watchdogThread = std::thread(&TcuUploaderProcess::watchdogLoop, this);
    }

    if (!once && m_cfg.startupDelaySeconds > 0) {
        for (int i = 0; i < m_cfg.startupDelaySeconds && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    do {
        runOnce();
        if (once) {
            break;
        }
        const int interval = std::max(1, m_cfg.intervalMinutes) * 60;
        for (int i = 0; i < interval && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } while (m_running);

    return 0;
}

void TcuUploaderProcess::stop()
{
    m_running = false;
    m_watchdogRunning = false;
    if (m_watchdogThread.joinable()) {
        m_watchdogThread.join();
    }
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
    m_log.stop();
}

bool TcuUploaderProcess::loadConfig()
{
    if (!m_config.loadConfig(m_configPath)) {
        return false;
    }

    m_cfg.enabled = m_config.getBool("Upload", "enabled", true);
    m_cfg.uploadUrl = m_config.getString("Upload", "upload_url", "http://127.0.0.1:8080/api/device/log/upload");
    m_cfg.token = m_config.getString("Upload", "token", "test-token-001");
    m_cfg.intervalMinutes = m_config.getInt("Upload", "interval_minutes", 1440);
    m_cfg.startupDelaySeconds = m_config.getInt("Upload", "startup_delay_seconds", 120);
    m_cfg.fallbackDeviceId = m_config.getString("Upload", "fallback_device_id", "tcu");

    m_cfg.mqttHost = m_config.getString("MQTT", "mqtt_host", "127.0.0.1");
    m_cfg.mqttPort = m_config.getInt("MQTT", "mqtt_port", 1883);
    m_cfg.mqttKeepalive = m_config.getInt("MQTT", "mqtt_keepalive", 60);
    m_cfg.mqttClientId = m_config.getString("MQTT", "mqtt_client_id", "tcu_uploader");
    m_cfg.mqttTopicPrefix = m_config.getString("MQTT", "mqtt_topic_prefix", "tcu");

    m_cfg.dataDir = m_config.getString("Paths", "data_dir", "/mnt/nandflash/data");
    m_cfg.workDir = m_config.getString("Paths", "work_dir", "/mnt/nandflash/data/upload");
    m_cfg.includeFiles = splitCsv(m_config.getString("Paths", "include_files", "tcu.db,error.db,feemodel.db,chargerecords.db,telemetry.db"));

    m_cfg.maxRetryPerRound = m_config.getInt("Retry", "max_retry_per_round", 3);
    m_cfg.connectTimeoutSeconds = m_config.getInt("Retry", "connect_timeout_seconds", 15);
    m_cfg.maxTimeSeconds = m_config.getInt("Retry", "max_time_seconds", 300);
    m_cfg.maxPendingMb = m_config.getInt("Retry", "max_pending_mb", 200);
    return true;
}

bool TcuUploaderProcess::initMqtt()
{
    m_mqtt.setConnectHandler([this](int rc) { handleMqttConnect(rc); });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        handleMqttMessage(topic, payload);
    });

    if (!m_mqtt.init(m_cfg.mqttClientId, true)) {
        std::cerr << "[tcu_uploader] mqtt init failed" << std::endl;
        return false;
    }
    if (!m_mqtt.connect(m_cfg.mqttHost, m_cfg.mqttPort, m_cfg.mqttKeepalive)) {
        std::cerr << "[tcu_uploader] mqtt connect failed" << std::endl;
        return false;
    }
    if (!m_mqtt.loopStart()) {
        std::cerr << "[tcu_uploader] mqtt loop start failed" << std::endl;
        return false;
    }
    return true;
}

void TcuUploaderProcess::handleMqttConnect(int rc)
{
    if (rc != 0) {
        std::cerr << "[tcu_uploader] mqtt connect rc=" << rc << std::endl;
        return;
    }
    const std::string eventTopic = m_cfg.mqttTopicPrefix + "/plat/+/event";
    const std::string setConfigTopic = m_cfg.mqttTopicPrefix + "/plat/+/setConfig";
    m_mqtt.subscribe(eventTopic, 1);
    m_mqtt.subscribe(setConfigTopic, 1);
}

void TcuUploaderProcess::handleMqttMessage(const std::string&, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    const std::string type = jsonString(root, "type");
    const std::string cmd = jsonString(root, "cmd");
    if (type != "setConfig" && cmd != "setConfig") {
        cJSON_Delete(root);
        return;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return;
    }
    const std::string cdzNo = jsonString(data, "cdzNo");
    const std::string cdzId = jsonString(data, "cdzId");
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_idMutex);
        if (!cdzNo.empty() && cdzNo != m_cdzNo) {
            m_cdzNo = cdzNo;
            changed = true;
        }
        if (!cdzId.empty() && cdzId != m_cdzId) {
            m_cdzId = cdzId;
            changed = true;
        }
    }
    cJSON_Delete(root);

    if (changed) {
        saveDeviceIdCache();
    }
}

void TcuUploaderProcess::loadDeviceIdCache()
{
    std::ifstream in(cachePath());
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string val = trim(line.substr(pos + 1));
        if (key == "cdzNo") {
            m_cdzNo = val;
        } else if (key == "cdzId") {
            m_cdzId = val;
        }
    }
}

void TcuUploaderProcess::saveDeviceIdCache()
{
    ensureDirectoryRecursive(m_cfg.workDir);
    std::lock_guard<std::mutex> lock(m_idMutex);
    std::ofstream out(cachePath());
    if (!out.is_open()) {
        return;
    }
    out << "cdzNo=" << m_cdzNo << "\n";
    out << "cdzId=" << m_cdzId << "\n";
}

std::string TcuUploaderProcess::resolveDeviceId()
{
    std::lock_guard<std::mutex> lock(m_idMutex);
    if (!m_cdzNo.empty()) {
        return sanitizeDeviceId(m_cdzNo);
    }
    if (!m_cdzId.empty()) {
        return sanitizeDeviceId(m_cdzId);
    }
    return sanitizeDeviceId(m_cfg.fallbackDeviceId);
}

void TcuUploaderProcess::watchdogLoop()
{
    // BY ZF: Feed daemon independently so long uploads do not trigger restart.
    MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    bool queueReady = watchdogQueue.open() || watchdogQueue.create();
    const char* processName = "tcu_uploader";

    while (m_watchdogRunning.load()) {
        if (!queueReady) {
            queueReady = watchdogQueue.open() || watchdogQueue.create();
        }
        if (queueReady) {
            watchdogQueue.send(MSG_WATCHDOG_FEED, processName, static_cast<int>(std::strlen(processName)));
        }
        for (int i = 0; i < 5 && m_watchdogRunning.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool TcuUploaderProcess::runOnce()
{
    enforcePendingLimit("");
    uploadPending(m_cfg.maxRetryPerRound);
    if (!listPendingFiles().empty()) {
        enforcePendingLimit("");
        return true;
    }

    const std::string deviceId = resolveDeviceId();
    const std::string startTimestamp = currentTimestamp();
    std::string packagePath;
    if (!createPackage(deviceId, startTimestamp, packagePath)) {
        std::cerr << "[tcu_uploader] create package failed" << std::endl;
        enforcePendingLimit("");
        return false;
    }
    if (packagePath.empty()) {
        enforcePendingLimit("");
        return true;
    }

    const bool ok = uploadPackage(packagePath, deviceId, startTimestamp);
    if (ok) {
        if (applyManifestFromPackage(packagePath)) {
            unlink(packagePath.c_str());
        } else {
            std::cerr << "[tcu_uploader] uploaded but failed to update state, keep pending: " << packagePath << std::endl;
            enforcePendingLimit(packagePath);
            return false;
        }
    }
    enforcePendingLimit(ok ? "" : packagePath);
    return ok;
}

void TcuUploaderProcess::uploadPending(int limit)
{
    if (limit <= 0) {
        return;
    }
    std::vector<PendingFile> files = listPendingFiles();
    std::sort(files.begin(), files.end(), [](const PendingFile& a, const PendingFile& b) {
        return a.mtime < b.mtime;
    });

    int count = 0;
    for (const PendingFile& f : files) {
        if (count >= limit) {
            break;
        }
        const std::string name = baseName(f.path);
        const bool isDeltaPackage = name.find("delta_") == 0;
        const bool isDataPackage = name.find("data_") == 0;
        if (!isDeltaPackage && !isDataPackage) {
            continue;
        }
        std::string id = name.substr(isDeltaPackage ? 6 : 5);
        const size_t suffix = id.find(".tar.gz");
        if (suffix != std::string::npos) {
            id = id.substr(0, suffix);
        }
        const size_t endTsPos = id.rfind('_');
        if (endTsPos != std::string::npos && isTimestamp14(id.substr(endTsPos + 1))) {
            std::string withoutEndTs = id.substr(0, endTsPos);
            const size_t startTsPos = withoutEndTs.rfind('_');
            if (startTsPos != std::string::npos && isTimestamp14(withoutEndTs.substr(startTsPos + 1))) {
                id = withoutEndTs.substr(0, startTsPos);
            } else {
                id = withoutEndTs;
            }
        }
        if (uploadPackage(f.path, sanitizeDeviceId(id), currentTimestamp())) {
            if (applyManifestFromPackage(f.path)) {
                unlink(f.path.c_str());
            } else {
                std::cerr << "[tcu_uploader] pending uploaded but state update failed: " << f.path << std::endl;
                break;
            }
        }
        ++count;
    }
}

bool TcuUploaderProcess::createPackage(const std::string& deviceId, const std::string& timestamp, std::string& packagePath)
{
    const std::string staging = joinPath(stagingRoot(), timestamp);
    removeRecursive(staging);
    if (!ensureDirectoryRecursive(staging)) {
        return false;
    }

    cJSON* manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "mode", "incremental");
    cJSON_AddStringToObject(manifest, "device_id", deviceId.c_str());
    cJSON_AddStringToObject(manifest, "created_at", timestamp.c_str());
    bool hasRows = false;
    const bool exportOk = exportIncrementalPackage(staging, deviceId, timestamp, manifest, hasRows);
    if (!exportOk) {
        cJSON_Delete(manifest);
        removeRecursive(staging);
        return false;
    }
    if (!hasRows) {
        cJSON_Delete(manifest);
        removeRecursive(staging);
        packagePath.clear();
        return true;
    }

    char* text = cJSON_Print(manifest);
    cJSON_Delete(manifest);
    if (!text) {
        removeRecursive(staging);
        return false;
    }
    const bool manifestOk = writeTextFile(joinPath(staging, "manifest.json"), std::string(text) + "\n");
    cJSON_free(text);
    if (!manifestOk) {
        removeRecursive(staging);
        return false;
    }

    const std::string endTimestamp = currentTimestamp();
    (void)endTimestamp;
    const std::string finalName = "delta_" + deviceId + "_" + timestamp + ".tar.gz";
    const std::string finalPath = joinPath(pendingDir(), finalName);
    const std::string tmpPath = joinPath(pendingDir(), "." + finalName + ".tmp");
    unlink(tmpPath.c_str());
    const std::string cmd = "tar -czf " + shellQuote(tmpPath) + " -C " + shellQuote(staging) + " delta.db manifest.json";
    const int rc = std::system(cmd.c_str());
    removeRecursive(staging);
    if (rc != 0 || !fileExists(tmpPath)) {
        unlink(tmpPath.c_str());
        return false;
    }
    if (rename(tmpPath.c_str(), finalPath.c_str()) != 0) {
        unlink(tmpPath.c_str());
        return false;
    }
    packagePath = finalPath;
    return true;
}

bool TcuUploaderProcess::exportIncrementalPackage(const std::string& staging,
                                                  const std::string&,
                                                  const std::string&,
                                                  cJSON* manifest,
                                                  bool& hasRows)
{
    hasRows = false;
    std::map<std::string, Watermark> state;
    if (!loadUploadState(state)) {
        return false;
    }

    const std::string deltaPath = joinPath(staging, "delta.db");
    unlink(deltaPath.c_str());
    sqlite3* dstDb = nullptr;
    if (sqlite3_open(deltaPath.c_str(), &dstDb) != SQLITE_OK) {
        if (dstDb) {
            sqlite3_close(dstDb);
        }
        return false;
    }

    cJSON* tables = cJSON_AddArrayToObject(manifest, "tables");
    cJSON* missing = cJSON_AddArrayToObject(manifest, "missing_files");
    bool ok = true;

    auto exportIfDbExists = [&](const std::string& dbName, const std::string& tableName,
                                const std::string& wmType, const std::string& timeColumn,
                                const std::string& seqColumn) {
        if (!ok || !dbEnabled(m_cfg.includeFiles, dbName)) {
            return;
        }
        const std::string dbPath = joinPath(m_cfg.dataDir, dbName);
        if (!fileExists(dbPath)) {
            cJSON_AddItemToArray(missing, cJSON_CreateString(dbName.c_str()));
            return;
        }
        if (!exportTableByWatermark(dstDb, dbName, dbPath, tableName, wmType, timeColumn, seqColumn, state, tables, hasRows)) {
            ok = false;
        }
    };

    exportIfDbExists("tcu.db", "operation_logs", "time_seq", "timestamp", "id");
    exportIfDbExists("tcu.db", "performance_logs", "time_seq", "timestamp", "id");
    exportIfDbExists("error.db", "fault_records", "time_seq", "occur_time", "id");
    exportIfDbExists("chargerecords.db", "charge_trade_info", "time_seq", "created_at", "id");
    exportIfDbExists("telemetry.db", "meter_minute_points", "time_seq", "created_at", "id");
    exportIfDbExists("telemetry.db", "bms_minute_points", "time_seq", "created_at", "id");
    exportIfDbExists("feemodel.db", "tbFeeModel", "time_seq", "timeStamp", "feeModelId");
    sqlite3_close(dstDb);

    if (!ok) {
        unlink(deltaPath.c_str());
        return false;
    }
    if (!hasRows) {
        unlink(deltaPath.c_str());
    }
    return true;
}

bool TcuUploaderProcess::exportTableByWatermark(sqlite3* dstDb,
                                                const std::string& dbName,
                                                const std::string& dbPath,
                                                const std::string& tableName,
                                                const std::string& watermarkType,
                                                const std::string& timeColumn,
                                                const std::string& seqColumn,
                                                const std::map<std::string, Watermark>& state,
                                                cJSON* manifestTables,
                                                bool& hasRows)
{
    const std::string key = stateKey(dbName, tableName);
    std::string fromTime;
    std::string fromSeq;
    auto it = state.find(key);
    if (it != state.end()) {
        if (it->second.type == "time_seq") {
            fromTime = it->second.timeValue;
            fromSeq = it->second.seqValue;
        } else {
            fromSeq = it->second.seqValue.empty() ? "0" : it->second.seqValue;
        }
    }

    sqlite3* srcDb = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &srcDb, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (srcDb) {
            sqlite3_close(srcDb);
        }
        return false;
    }

    const std::string qTable = quoteIdent(tableName);
    const std::string qTimeColumn = quoteIdent(timeColumn);
    const std::string qSeqColumn = quoteIdent(seqColumn);
    std::string whereSql = "(" + qTimeColumn + " > ? OR (" + qTimeColumn + " = ? AND " + qSeqColumn + " > ?))";
    std::string maxSql = "SELECT " + qTimeColumn + ", " + qSeqColumn + " FROM " + qTable +
                         " WHERE " + whereSql + " ORDER BY " + qTimeColumn + " DESC, " +
                         qSeqColumn + " DESC LIMIT 1";
    std::string toTime;
    std::string toSeq;
    bool hasTo = false;
    if (!sqliteScalarPair(srcDb, maxSql, fromTime, fromSeq, toTime, toSeq, hasTo)) {
        sqlite3_close(srcDb);
        return false;
    }
    if (!hasTo || toTime.empty()) {
        sqlite3_close(srcDb);
        return true;
    }

    std::string rangeSql = whereSql + " AND (" + qTimeColumn + " < ? OR (" + qTimeColumn + " = ? AND " + qSeqColumn + " <= ?))";
    std::string countSql = "SELECT COUNT(*) FROM " + qTable + " WHERE " + rangeSql;
    sqlite3_stmt* countStmt = nullptr;
    if (sqlite3_prepare_v2(srcDb, countSql.c_str(), -1, &countStmt, nullptr) != SQLITE_OK) {
        sqlite3_close(srcDb);
        return false;
    }
    sqlite3_bind_text(countStmt, 1, fromTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(countStmt, 2, fromTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(countStmt, 3, fromSeq.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(countStmt, 4, toTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(countStmt, 5, toTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(countStmt, 6, toSeq.c_str(), -1, SQLITE_TRANSIENT);
    int rows = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW) {
        rows = sqlite3_column_int(countStmt, 0);
    } else {
        sqlite3_finalize(countStmt);
        sqlite3_close(srcDb);
        return false;
    }
    sqlite3_finalize(countStmt);
    if (rows <= 0) {
        sqlite3_close(srcDb);
        return true;
    }

    std::string createSql;
    bool hasSchema = false;
    if (!sqliteScalarText(srcDb,
                          "SELECT sql FROM sqlite_master WHERE type='table' AND name=?",
                          tableName,
                          createSql,
                          hasSchema) || !hasSchema || createSql.empty()) {
        sqlite3_close(srcDb);
        return false;
    }
    if (!sqliteExec(dstDb, createSql)) {
        sqlite3_close(srcDb);
        return false;
    }
    sqlite3_close(srcDb);

    char* quotedPath = sqlite3_mprintf("%Q", dbPath.c_str());
    if (!quotedPath) {
        return false;
    }
    const std::string attachSql = "ATTACH DATABASE " + std::string(quotedPath) + " AS srcdb";
    sqlite3_free(quotedPath);
    if (!sqliteExec(dstDb, attachSql)) {
        return false;
    }

    const std::string copySql = "INSERT INTO " + qTable + " SELECT * FROM srcdb." + qTable +
                                " WHERE " + rangeSql + " ORDER BY " + qTimeColumn + ", " + qSeqColumn;
    sqlite3_stmt* copyStmt = nullptr;
    bool copyOk = sqlite3_prepare_v2(dstDb, copySql.c_str(), -1, &copyStmt, nullptr) == SQLITE_OK;
    if (copyOk) {
        sqlite3_bind_text(copyStmt, 1, fromTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(copyStmt, 2, fromTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(copyStmt, 3, fromSeq.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(copyStmt, 4, toTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(copyStmt, 5, toTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(copyStmt, 6, toSeq.c_str(), -1, SQLITE_TRANSIENT);
        const int copyRc = sqlite3_step(copyStmt);
        copyOk = copyRc == SQLITE_DONE;
    }
    if (copyStmt) {
        sqlite3_finalize(copyStmt);
    }
    const bool detachOk = sqliteExec(dstDb, "DETACH DATABASE srcdb");
    if (!copyOk || !detachOk) {
        return false;
    }

    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "db", dbName.c_str());
    cJSON_AddStringToObject(item, "table", tableName.c_str());
    cJSON_AddStringToObject(item, "watermark_type", watermarkType.c_str());
    cJSON_AddStringToObject(item, "time_column", timeColumn.c_str());
    cJSON_AddStringToObject(item, "seq_column", seqColumn.c_str());
    cJSON_AddStringToObject(item, "from_time", fromTime.c_str());
    cJSON_AddStringToObject(item, "from_seq", fromSeq.c_str());
    cJSON_AddStringToObject(item, "to_time", toTime.c_str());
    cJSON_AddStringToObject(item, "to_seq", toSeq.c_str());
    cJSON_AddNumberToObject(item, "rows", rows);
    cJSON_AddItemToArray(manifestTables, item);
    hasRows = true;
    return true;
}

bool TcuUploaderProcess::loadUploadState(std::map<std::string, Watermark>& state)
{
    state.clear();
    if (!fileExists(statePath())) {
        return true;
    }
    const std::string text = readTextFile(statePath());
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) {
        return false;
    }
    cJSON* tables = cJSON_GetObjectItem(root, "tables");
    if (!cJSON_IsObject(tables)) {
        cJSON_Delete(root);
        return true;
    }
    for (cJSON* item = tables->child; item; item = item->next) {
        if (!item->string || !cJSON_IsObject(item)) {
            continue;
        }
        Watermark wm;
        wm.type = jsonString(item, "type");
        if (wm.type == "time_seq") {
            wm.timeColumn = jsonString(item, "time_column");
            wm.timeValue = jsonString(item, "time_value");
            wm.seqColumn = jsonString(item, "seq_column");
            wm.seqValue = jsonString(item, "seq_value");
        } else {
            cJSON* value = cJSON_GetObjectItem(item, "value");
            wm.seqColumn = "id";
            if (cJSON_IsString(value) && value->valuestring) {
                wm.seqValue = value->valuestring;
            } else if (cJSON_IsNumber(value)) {
                std::ostringstream oss;
                oss << static_cast<long long>(value->valuedouble);
                wm.seqValue = oss.str();
            }
        }
        if (!wm.type.empty() && (!wm.timeValue.empty() || !wm.seqValue.empty())) {
            state[item->string] = wm;
        }
    }
    cJSON_Delete(root);
    return true;
}

bool TcuUploaderProcess::saveUploadState(const std::map<std::string, Watermark>& state)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "updated_at", currentTimestamp().c_str());
    cJSON* tables = cJSON_AddObjectToObject(root, "tables");
    for (const auto& pair : state) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", pair.second.type.c_str());
        if (pair.second.type == "time_seq") {
            cJSON_AddStringToObject(item, "time_column", pair.second.timeColumn.c_str());
            cJSON_AddStringToObject(item, "time_value", pair.second.timeValue.c_str());
            cJSON_AddStringToObject(item, "seq_column", pair.second.seqColumn.c_str());
            cJSON_AddStringToObject(item, "seq_value", pair.second.seqValue.c_str());
        } else {
            cJSON_AddStringToObject(item, "value", pair.second.seqValue.c_str());
        }
        cJSON_AddItemToObject(tables, pair.first.c_str(), item);
    }
    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        return false;
    }
    const bool ok = writeTextFile(statePath(), std::string(text) + "\n");
    cJSON_free(text);
    return ok;
}

bool TcuUploaderProcess::applyManifestToState(const std::string& manifestText)
{
    cJSON* root = cJSON_Parse(manifestText.c_str());
    if (!root) {
        return false;
    }
    if (jsonString(root, "mode") != "incremental") {
        cJSON_Delete(root);
        return true;
    }
    std::map<std::string, Watermark> state;
    if (!loadUploadState(state)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON* tables = cJSON_GetObjectItem(root, "tables");
    if (cJSON_IsArray(tables)) {
        for (cJSON* item = tables->child; item; item = item->next) {
            const std::string db = jsonString(item, "db");
            const std::string table = jsonString(item, "table");
            const std::string type = jsonString(item, "watermark_type");
            if (db.empty() || table.empty() || type.empty()) {
                continue;
            }
            Watermark wm;
            wm.type = type;
            if (type == "time_seq") {
                wm.timeColumn = jsonString(item, "time_column");
                wm.timeValue = jsonString(item, "to_time");
                wm.seqColumn = jsonString(item, "seq_column");
                wm.seqValue = jsonString(item, "to_seq");
            } else {
                cJSON* to = cJSON_GetObjectItem(item, "to");
                wm.seqColumn = "id";
                if (cJSON_IsString(to) && to->valuestring) {
                    wm.seqValue = to->valuestring;
                } else if (cJSON_IsNumber(to)) {
                    std::ostringstream oss;
                    oss << static_cast<long long>(to->valuedouble);
                    wm.seqValue = oss.str();
                }
            }
            if (!wm.timeValue.empty() || !wm.seqValue.empty()) {
                state[stateKey(db, table)] = wm;
            }
        }
    }
    cJSON_Delete(root);
    return saveUploadState(state);
}

bool TcuUploaderProcess::applyManifestFromPackage(const std::string& packagePath)
{
    std::string manifestText;
    return extractManifestText(packagePath, manifestText) && applyManifestToState(manifestText);
}

bool TcuUploaderProcess::extractManifestText(const std::string& packagePath, std::string& manifestText)
{
    const std::string tmp = joinPath(stagingRoot(), ".manifest_" + currentTimestamp() + ".json");
    const std::string cmd = "tar -xOf " + shellQuote(packagePath) + " manifest.json > " + shellQuote(tmp);
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !fileExists(tmp)) {
        unlink(tmp.c_str());
        return false;
    }
    manifestText = readTextFile(tmp);
    unlink(tmp.c_str());
    return !manifestText.empty();
}

bool TcuUploaderProcess::uploadPackage(const std::string& packagePath, const std::string& deviceId, const std::string& timestamp)
{
    const std::string checksum = sha256File(packagePath);
    if (checksum.empty()) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = nullptr;

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, packagePath.c_str());
    curl_mime_filename(part, baseName(packagePath).c_str());

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "device_id");
    curl_mime_data(part, deviceId.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "timestamp");
    curl_mime_data(part, timestamp.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "checksum");
    curl_mime_data(part, checksum.c_str(), CURL_ZERO_TERMINATED);

    struct curl_slist* headers = nullptr;
    const std::string auth = "Authorization: Bearer " + m_cfg.token;
    headers = curl_slist_append(headers, auth.c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, m_cfg.uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, m_cfg.connectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_cfg.maxTimeSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || httpCode < 200 || httpCode >= 300) {
        std::cerr << "[tcu_uploader] upload failed curl=" << rc << " http=" << httpCode << std::endl;
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        return false;
    }
    cJSON* code = cJSON_GetObjectItem(root, "code");
    const bool ok = cJSON_IsNumber(code) && code->valueint == 0;
    cJSON_Delete(root);
    return ok;
}

void TcuUploaderProcess::enforcePendingLimit(const std::string& protectedPath)
{
    const long long maxBytes = static_cast<long long>(std::max(1, m_cfg.maxPendingMb)) * 1024LL * 1024LL;
    std::vector<PendingFile> files = listPendingFiles();
    long long total = 0;
    for (const PendingFile& f : files) {
        total += f.size;
    }
    if (total <= maxBytes) {
        return;
    }

    std::sort(files.begin(), files.end(), [](const PendingFile& a, const PendingFile& b) {
        return a.mtime < b.mtime;
    });

    for (const PendingFile& f : files) {
        if (total <= maxBytes) {
            break;
        }
        if (!protectedPath.empty() && f.path == protectedPath) {
            continue;
        }
        if (unlink(f.path.c_str()) == 0) {
            total -= f.size;
        }
    }
}

std::vector<TcuUploaderProcess::PendingFile> TcuUploaderProcess::listPendingFiles()
{
    std::vector<PendingFile> files;
    DIR* dir = opendir(pendingDir().c_str());
    if (!dir) {
        return files;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || name.empty() || name[0] == '.') {
            continue;
        }
        if (name.size() < 7 || name.find(".tar.gz") == std::string::npos) {
            continue;
        }
        const std::string path = joinPath(pendingDir(), name);
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            PendingFile f;
            f.path = path;
            f.size = static_cast<long long>(st.st_size);
            f.mtime = static_cast<long long>(st.st_mtime);
            files.push_back(f);
        }
    }
    closedir(dir);
    return files;
}

bool TcuUploaderProcess::ensureWorkDirs()
{
    return ensureDirectoryRecursive(m_cfg.workDir) &&
           ensureDirectoryRecursive(pendingDir()) &&
           ensureDirectoryRecursive(stagingRoot());
}

std::string TcuUploaderProcess::pendingDir() const
{
    return joinPath(m_cfg.workDir, "pending");
}

std::string TcuUploaderProcess::stagingRoot() const
{
    return joinPath(m_cfg.workDir, "staging");
}

std::string TcuUploaderProcess::cachePath() const
{
    return joinPath(m_cfg.workDir, "device_id.cache");
}

std::string TcuUploaderProcess::statePath() const
{
    return joinPath(m_cfg.workDir, "upload_state.json");
}
