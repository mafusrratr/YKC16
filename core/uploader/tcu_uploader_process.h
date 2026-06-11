/**
 * TCU log upload process.
 * BY ZF
 */

#ifndef TCU_UPLOADER_PROCESS_H
#define TCU_UPLOADER_PROCESS_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../base/common/config_manager_lite.h"
#include "../base/common/message_queue.h"
#include "../base/logger/log_sender.h"
#include "../base/mqtt/mqtt_client.h"

typedef struct cJSON cJSON;
typedef struct sqlite3 sqlite3;

// BY ZF: uploader runtime configuration loaded from tcu_uploader.ini.
struct UploaderConfig {
    bool enabled;
    std::string uploadUrl;
    std::string token;
    int intervalMinutes;
    int startupDelaySeconds;
    std::string fallbackDeviceId;

    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;

    std::string dataDir;
    std::string workDir;
    std::vector<std::string> includeFiles;

    int maxRetryPerRound;
    int connectTimeoutSeconds;
    int maxTimeSeconds;
    int maxPendingMb;
};

class TcuUploaderProcess {
public:
    explicit TcuUploaderProcess(const std::string& configPath);
    ~TcuUploaderProcess();

    bool initialize();
    int run(bool once);
    void stop();

private:
    struct PendingFile {
        std::string path;
        long long size;
        long long mtime;
    };
    struct Watermark {
        std::string type;
        std::string timeColumn;
        std::string timeValue;
        std::string seqColumn;
        std::string seqValue;
    };

    bool loadConfig();
    bool initMqtt();
    void handleMqttMessage(const std::string& topic, const std::string& payload);
    void handleMqttConnect(int rc);
    void loadDeviceIdCache();
    void saveDeviceIdCache();
    std::string resolveDeviceId();
    void watchdogLoop();

    bool runOnce();
    void uploadPending(int limit);
    bool createPackage(const std::string& deviceId, const std::string& timestamp, std::string& packagePath);
    bool uploadPackage(const std::string& packagePath, const std::string& deviceId, const std::string& timestamp);
    bool exportIncrementalPackage(const std::string& staging,
                                  const std::string& deviceId,
                                  const std::string& timestamp,
                                  cJSON* manifest,
                                  bool& hasRows);
    bool exportTableByWatermark(sqlite3* dstDb,
                                const std::string& dbName,
                                const std::string& dbPath,
                                const std::string& tableName,
                                const std::string& watermarkType,
                                const std::string& timeColumn,
                                const std::string& seqColumn,
                                const std::map<std::string, Watermark>& state,
                                cJSON* manifestTables,
                                bool& hasRows);
    bool loadUploadState(std::map<std::string, Watermark>& state);
    bool saveUploadState(const std::map<std::string, Watermark>& state);
    bool applyManifestToState(const std::string& manifestText);
    bool applyManifestFromPackage(const std::string& packagePath);
    bool extractManifestText(const std::string& packagePath, std::string& manifestText);

    void enforcePendingLimit(const std::string& protectedPath);
    std::vector<PendingFile> listPendingFiles();

    bool ensureWorkDirs();
    std::string pendingDir() const;
    std::string stagingRoot() const;
    std::string cachePath() const;
    std::string statePath() const;

private:
    std::string m_configPath;
    UploaderConfig m_cfg;
    ConfigManagerLite& m_config;
    LogSender m_log;
    MqttClient m_mqtt;
    std::atomic<bool> m_running;
    std::atomic<bool> m_watchdogRunning;
    std::thread m_watchdogThread;
    std::mutex m_idMutex;
    std::string m_cdzNo;
    std::string m_cdzId;
};

#endif // TCU_UPLOADER_PROCESS_H
