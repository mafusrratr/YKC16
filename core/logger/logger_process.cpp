/**
 * 日志进程实现（继承IProcess接口）
 * BY ZF
 */

#include "logger_process.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#include "../../libv2gshm/libcshm/v2gshm.h"
#include "../base/cjson/include/cjson/cJSON.h"

namespace {

bool ensureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!parent.empty() && !ensureDirectoryRecursive(parent)) {
            return false;
        }
    }
    
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        std::cerr << "[Logger][Backup] Failed to create directory: " << path 
                  << ", errno=" << errno << std::endl;
        return false;
    }
    return true;
}

// BY ZF: 仅识别 tcu.db 的备份文件
bool isManagedBackupFile(const std::string& fileName) {
    return fileName.size() > 7 &&
           fileName.compare(0, 4, "tcu_") == 0 &&
           fileName.compare(fileName.size() - 3, 3, ".db") == 0;
}

std::chrono::system_clock::time_point getLatestBackupTime(const std::string& backupDir) {
    DIR* dir = opendir(backupDir.c_str());
    if (!dir) {
        return std::chrono::system_clock::time_point{};
    }
    
    std::chrono::system_clock::time_point latest;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string fileName = entry->d_name;
        if (!isManagedBackupFile(fileName)) {
            continue;
        }
        
        std::string fullPath = backupDir + "/" + fileName;
        struct stat st{};
        if (stat(fullPath.c_str(), &st) == 0) {
            auto fileTime = std::chrono::system_clock::from_time_t(st.st_mtime);
            if (latest.time_since_epoch().count() == 0 || fileTime > latest) {
                latest = fileTime;
            }
        }
    }
    closedir(dir);
    return latest;
}

uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t minuteStartMs(uint64_t tsMs) {
    return (tsMs / 60000ULL) * 60000ULL;
}

std::string localTimeStringFromMs(uint64_t tsMs) {
    std::time_t sec = static_cast<std::time_t>(tsMs / 1000ULL);
    std::tm* tm = std::localtime(&sec);
    char buf[32] = {0};
    if (!tm || std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm) == 0) {
        return "";
    }
    return std::string(buf);
}

int parseTopicGunNo(const std::string& topic, const std::string& prefix, const std::string& category) {
    const std::string head = prefix + "/" + category + "/";
    if (topic.compare(0, head.size(), head) != 0) {
        return -1;
    }
    const size_t start = head.size();
    const size_t end = topic.find('/', start);
    if (end == std::string::npos || end == start) {
        return -1;
    }
    return std::atoi(topic.substr(start, end - start).c_str());
}

uint64_t jsonNumberU64(cJSON* obj, const char* key, uint64_t defaultValue) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(item)) {
        return defaultValue;
    }
    return static_cast<uint64_t>(item->valuedouble);
}

double jsonNumberDouble(cJSON* obj, const char* key, double defaultValue) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(item)) {
        return defaultValue;
    }
    return item->valuedouble;
}

bool jsonNumberInt(cJSON* obj, const char* key, int& value) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    value = item->valueint;
    return true;
}

bool jsonWorkStatus(cJSON* data, int& workStatus) {
    // BY ZF: 兼容协议文档中的 workStatus 和现场可能出现的 workstatus。
    return jsonNumberInt(data, "workStatus", workStatus) ||
           jsonNumberInt(data, "workstatus", workStatus);
}

} // namespace

LoggerProcess::LoggerProcess()
    : m_status(PROC_STATUS_INIT)
    , m_running(false)
    , m_config(ConfigManagerLite::getInstance())
    , m_dbManager(nullptr)
    , m_logQueue(nullptr)
    , m_watchdogQueue(nullptr)  // BY ZF: 看门狗队列初始化
    , m_maxBufferSize(LOGGER_MAX_BUFFER_SIZE)  // BY ZF: 使用宏定义
    , m_flushInterval(std::chrono::seconds(LOGGER_FLUSH_INTERVAL_SECONDS))  // BY ZF: 使用宏定义
    , m_logLevel(LOG_INFO)
    , m_shm(nullptr)
    , m_mqttReady(false)
    , m_mqttHost("127.0.0.1")
    , m_mqttPort(1883)
    , m_mqttKeepalive(60)
    , m_mqttClientId("tcu_logger")
    , m_mqttTopicPrefix("tcu")
    , m_mqttBiasNo(0)
    , m_mqttSeq(0)
    , m_backupEnabled(false)
    , m_backupDir()
    , m_backupIntervalMinutes(0)
    , m_backupCheckIntervalMinutes(0)
    , m_backupMaxSizeBytes(0)  // BY ZF: 默认无限制
    , m_nextBackupTime(std::chrono::system_clock::now())
    , m_nextBackupCheckTime(std::chrono::system_clock::now())
    , m_nextTelemetryCleanupTime(std::chrono::steady_clock::now())
{
    // BY ZF: 减少无关控制台输出
}

LoggerProcess::~LoggerProcess()
{
    stop();
}

// BY ZF: 初始化 Logger 进程，加载配置、数据库、共享内存以及备份计划
bool LoggerProcess::initialize(const char* config)
{
    // BY ZF: 仅在失败时输出
    std::string configFile = std::string(config);
    if (!m_config.loadConfig(configFile)) {
        std::cerr << "Failed to load logger config: " << configFile << std::endl;
        return false;
    }

    // BY ZF: MQTT 发布配置（用于未确认交易记录回放）。
    m_mqttHost = m_config.getString("MQTT", "mqtt_host", "127.0.0.1");
    m_mqttPort = m_config.getInt("MQTT", "mqtt_port", 1883);
    m_mqttKeepalive = m_config.getInt("MQTT", "mqtt_keepalive", 60);
    m_mqttClientId = m_config.getString("MQTT", "mqtt_client_id", "tcu_logger");
    m_mqttTopicPrefix = m_config.getString("MQTT", "mqtt_topic_prefix", "tcu");
    m_mqttBiasNo = m_config.getInt("MQTT", "bias_no", 0);
    // 初始化数据库管理器
    m_dbManager = &DatabaseManager::getInstance();
    m_mainDbPath = m_config.getString("Database", "main_db_path", "/usr/app/data/tcu.db");
    m_chargeDbPath = m_config.getString("Database", "charge_db_path", "/usr/app/data/chargerecords.db");
    m_feeDbPath = m_config.getString("Database", "fee_db_path", "/usr/app/data/feemodel.db");
    m_errorDbPath = m_config.getString("Database", "error_db_path", "/usr/app/data/error.db");
    m_telemetryDbPath = m_config.getString("Database", "telemetry_db_path", "/usr/app/data/telemetry.db");
    
    // BY ZF: 备份配置
    m_backupDir = m_config.getString("Backup", "backup_dir", "");
    m_backupIntervalMinutes = m_config.getInt("Backup", "backup_interval_minutes", 0);
    m_backupCheckIntervalMinutes = m_config.getInt("Backup", "backup_check_minutes", 60);
    if (m_backupCheckIntervalMinutes <= 0) {
        m_backupCheckIntervalMinutes = 60;
    }
    // BY ZF: 读取备份大小限制（MB），转换为字节
    int maxSizeMB = m_config.getInt("Backup", "backup_max_size_mb", 0);
    m_backupMaxSizeBytes = (maxSizeMB > 0) ? (static_cast<int64_t>(maxSizeMB) * 1024 * 1024) : 0;
    m_backupEnabled = !m_backupDir.empty() && m_backupIntervalMinutes > 0;
    
    if (m_backupEnabled) {
        if (!ensureDirectoryRecursive(m_backupDir)) {
            std::cerr << "[Logger][Backup] Failed to prepare backup directory: " 
                      << m_backupDir << std::endl;
            m_backupEnabled = false;
        }
    }
    
    if (!m_dbManager->initialize(m_mainDbPath, m_chargeDbPath, m_feeDbPath, m_errorDbPath, m_telemetryDbPath)) {
        std::cerr << "Failed to initialize database manager" << std::endl;
        return false;
    }
    maybeCleanupTelemetry();
    if (!initMqttPublisher()) {
        std::cerr << "Failed to initialize logger mqtt publisher" << std::endl;
    }
    
    if (m_backupEnabled) {
        auto latestBackup = getLatestBackupTime(m_backupDir);
        auto nowSys = std::chrono::system_clock::now();
        auto interval = std::chrono::minutes(m_backupIntervalMinutes);
        if (latestBackup.time_since_epoch().count() > 0) {
            m_nextBackupTime = latestBackup + interval;
            if (m_nextBackupTime < nowSys) {
                m_nextBackupTime = nowSys;
            }
        } else {
            m_nextBackupTime = nowSys + interval;
        }
        m_nextBackupCheckTime = nowSys;
        std::cout << "[Logger][Backup] Enabled. Directory: " << m_backupDir
                  << ", interval: " << m_backupIntervalMinutes << " minutes"
                  << ", check every " << m_backupCheckIntervalMinutes << " minutes";
        if (m_backupMaxSizeBytes > 0) {
            std::cout << ", max size: " << (m_backupMaxSizeBytes / (1024 * 1024)) << " MB";
        }
        std::cout << std::endl;
    } else {
        std::cout << "[Logger][Backup] Disabled." << std::endl;
    }
    
    // BY ZF: 初始化共享内存
    // BY ZF: CShm 构造函数内部已经完成 init，这里不重复初始化，避免共享内存重复创建/附着。
    CShm* cshm = new CShm();
    m_shm = cshm;
    
    // BY ZF: 加载最新计费模型到共享内存
    if (DatabaseManager::loadFeeModelToShm(m_feeDbPath, m_shm)) {
        std::cout << "Fee model loaded to shared memory successfully" << std::endl;
    } else {
        std::cout << "No fee model found in database or failed to load" << std::endl;
    }
    
    // BY ZF: 初始化消息队列
    m_logQueue = new MessageQueue(MSG_KEY_LOG);
    if (!m_logQueue->create()) {
        std::cerr << "Failed to create log message queue" << std::endl;
        return false;
    }
    
    // BY ZF: 初始化看门狗队列（用于发送看门狗心跳到守护进程）
    // 注意：看门狗队列由守护进程创建，这里只打开（open），不创建（create）
    m_watchdogQueue = new MessageQueue(MSG_KEY_WATCHDOG);
    if (!m_watchdogQueue->open()) {
        std::cerr << "[Logger] Warning: Failed to open watchdog message queue. "
                  << "Make sure daemon process is running first." << std::endl;
        // BY ZF: 看门狗队列打开失败不影响日志功能，但看门狗心跳将无法发送
        // 守护进程可能会误判为进程无响应而重启
    } else {
        std::cout << "[Logger] Watchdog queue opened successfully" << std::endl;
    }
    
    // BY ZF: 使用宏定义设置缓冲区配置（硬编码，不可配置）
    m_maxBufferSize = LOGGER_MAX_BUFFER_SIZE;
    m_flushInterval = std::chrono::seconds(LOGGER_FLUSH_INTERVAL_SECONDS);
    m_status.store(PROC_STATUS_RUNNING);
    return true;
}

// BY ZF: 启动 Logger 主线程并进入日志处理循环
int LoggerProcess::start()
{
    if (m_status.load() != PROC_STATUS_RUNNING) {
        std::cerr << "LoggerProcess not in running status" << std::endl;
        return -1;
    }
    m_running = true;
    m_mainThread = std::thread(&LoggerProcess::mainLoop, this);
    return 0;
}

// BY ZF: 停止 Logger 主线程、释放队列与数据库资源
void LoggerProcess::stop()
{
    m_running = false;
    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }
    // BY ZF: 清理消息队列
    if (m_logQueue) {
        delete m_logQueue;
        m_logQueue = nullptr;
    }
    if (m_watchdogQueue) {
        delete m_watchdogQueue;
        m_watchdogQueue = nullptr;
    }
    flushAllTelemetryCaches();
    if (m_dbManager) {
        m_dbManager->cleanup();
    }
    if (m_mqttReady) {
        m_mqtt.loopStop(true);
        m_mqtt.disconnect();
        m_mqttReady = false;
    }
    // BY ZF: 清理共享内存
    if (m_shm) {
        CShm* cshm = static_cast<CShm*>(m_shm);
        delete cshm;
        m_shm = nullptr;
    }
    m_status.store(PROC_STATUS_STOPPED);
}

// BY ZF: IProcess 接口空实现，实际逻辑在独立线程中执行
void LoggerProcess::run()
{
    // 主循环由线程处理，这里不需要实现
}

// BY ZF: 日志功能实现（不再继承 ILogger，直接实现）
void LoggerProcess::debug(const std::string& module, const std::string& message, const std::string& details)
{
    LogEntry entry(LOG_DEBUG, module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::info(const std::string& module, const std::string& message, const std::string& details)
{
    LogEntry entry(LOG_INFO, module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::warn(const std::string& module, const std::string& message, const std::string& details)
{
    LogEntry entry(LOG_WARN, module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::error(const std::string& module, const std::string& message, const std::string& details)
{
    LogEntry entry(LOG_ERROR, module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::fatal(const std::string& module, const std::string& message, const std::string& details)
{
    LogEntry entry(LOG_FATAL, module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::flush()
{
    flushLogBufferInternal();
}

uint32_t LoggerProcess::cleanupOldLogs(int days)
{
    // TODO: 实现日志清理功能
    return 0;
}

// BY ZF: 查询运行日志数据库，返回指定时间范围的 JSON 结果
std::string LoggerProcess::queryOperationLogs(uint64_t startTime, uint64_t endTime, int level, const std::string& module)
{
    if (m_dbManager) {
        return m_dbManager->queryOperationLogs(
            std::to_string(startTime), 
            std::to_string(endTime), 
            level, 
            module
        );
    }
    return "{}";
}

void LoggerProcess::setDatabasePaths(const std::string& mainDbPath,
                                     const std::string& chargeDbPath,
                                     const std::string& feeDbPath,
                                     const std::string& errorDbPath)
{
    m_mainDbPath = mainDbPath;
    m_chargeDbPath = chargeDbPath;
    m_feeDbPath = feeDbPath;
    m_errorDbPath = errorDbPath;
}

void LoggerProcess::setBufferConfig(size_t maxSize, int flushInterval)
{
    m_maxBufferSize = maxSize;
    m_flushInterval = std::chrono::seconds(flushInterval);
}

// 私有方法实现
void LoggerProcess::addLogEntry(const LogEntry& entry)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
    // 生成时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    LogEntry entryWithTime = entry;
    entryWithTime.timestamp = timestamp;
    
    m_logBuffer.push_back(entryWithTime);
    
    // 如果缓冲区满了，立即刷新
    if (m_logBuffer.size() >= m_maxBufferSize) {
        flushLogBufferInternal();
    }
}

/**
 * @brief Logger进程主循环，持续监听消息队列，处理日志、看门狗消息并批量flush入库。
 */
void LoggerProcess::mainLoop()
{
    auto lastFlushTime = std::chrono::steady_clock::now();
    while (m_running.load()) {
        processLogMessage();
        handleWatchdogMessage();
        auto now = std::chrono::steady_clock::now();
        if (now - lastFlushTime >= m_flushInterval) {
            flushLogBufferInternal();
            lastFlushTime = now;
        }
        maybePerformBackup();
        flushExpiredTelemetryCaches();
        maybeCleanupTelemetry();
        
        // BY ZF: 动态调整sleep时间：如果队列快满了，不sleep或减少sleep时间
        unsigned long currentMsgs = 0, maxMsgs = 0;
        bool queueStatusOk = m_logQueue->getQueueStatus(&currentMsgs, &maxMsgs);
        if (queueStatusOk && maxMsgs > 0) {
            double queueUsage = (double)currentMsgs / maxMsgs;
            if (queueUsage > 0.8) {
                // BY ZF: 队列使用率超过80%，不sleep，立即处理下一批
                // 不sleep
            } else if (queueUsage > 0.5) {
                // BY ZF: 队列使用率超过50%，减少sleep时间
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                // BY ZF: 队列使用率正常，正常sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        } else {
            // BY ZF: 无法获取队列状态，使用默认sleep时间
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    flushLogBufferInternal();
    flushAllTelemetryCaches();
}

/*
以下是trade_record功能测试用例样例：
// 合法
parseAndLogMessage(R"({\"type\":\"trade_record\",\"gun_no\":1,\"trade_no\":\"TNO1\",\"time_num\":3,\"part_elect_text\":\"10,20,30\",\"charge_fee_text\":\"1,2,3\",\"service_fee_text\":\"4,5,6\",\"cross_points\":1,\"points_elect_text\":\"99\"})");
// 不合法
parseAndLogMessage(R"({\"type\":\"trade_record\",\"gun_no\":1,\"trade_no\":\"TNO_ERR\",\"time_num\":4,\"part_elect_text\":\"10,20,30\",\"charge_fee_text\":\"1,2,3\",\"service_fee_text\":\"4,5,6\",\"cross_points\":2,\"points_elect_text\":\"77\"})");
// 可根据运行日志/DB结果观察测试效果
*/
// BY ZF: 从消息队列读取日志 JSON，并交由解析逻辑处理（批量接收以提高处理效率）
void LoggerProcess::processLogMessage()
{
    if (!m_logQueue) return;
    
    // BY ZF: 检查消息队列状态，如果快满了则提高处理优先级
    unsigned long currentMsgs = 0, maxMsgs = 0;
    bool queueStatusOk = m_logQueue->getQueueStatus(&currentMsgs, &maxMsgs);
    bool queueNearFull = false;
    if (queueStatusOk && maxMsgs > 0) {
        double queueUsage = (double)currentMsgs / maxMsgs;
        queueNearFull = (queueUsage > 0.7);  // BY ZF: 队列使用率超过70%认为快满了
    }
    
    // BY ZF: 增大接收缓冲区以支持大型JSON（如96时段的计费模型）
    const int MAX_BUFFER_SIZE = 8192;
    char buffer[MAX_BUFFER_SIZE];
    int actualSize;
    
    // BY ZF: 批量接收消息，直到队列为空或达到批量限制（避免单次处理过多）
    // 如果队列快满了，增加批量大小以提高处理速度
    int MAX_BATCH_SIZE = queueNearFull ? 200 : 100;  // BY ZF: 队列快满时增加批量大小
    int processedCount = 0;
    
    while (processedCount < MAX_BATCH_SIZE) {
    int result = m_logQueue->receive(MSG_TYPE_LOG, buffer, sizeof(buffer), &actualSize);
    if (result == 0 && actualSize > 0) {
            if (actualSize >= MAX_BUFFER_SIZE - 1) {
                // BY ZF: 消息被截断，记录警告
                warn("LoggerProcess", "Received log message may be truncated", 
                     "Message size: " + std::to_string(actualSize));
            }
        buffer[actualSize] = '\0';
        parseAndLogMessage(std::string(buffer));
            processedCount++;
        } else if (result == -1) {
            // BY ZF: 队列为空，退出循环
            break;
        } else if (result == -2) {
            // BY ZF: 接收失败（可能是消息太大），已在 message_queue 中打印错误
            // 这里不再重复打印，避免刷屏
            break;
        }
    }
    
    // BY ZF: 如果队列快满了或处理了大量消息，主动flush缓冲区（即使没到阈值）
    // 这样可以避免缓冲区满导致的阻塞，提高处理速度
    if (queueNearFull || processedCount > 50) {
        // BY ZF: 检查缓冲区大小（需要获取锁）
        std::vector<std::tuple<LogLevel, std::string, std::string, std::string>> batchEntries;
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            // BY ZF: 如果缓冲区超过80%容量，立即flush
            if (m_logBuffer.size() > (m_maxBufferSize * 0.8)) {
                // BY ZF: 准备批量写入数据
                batchEntries.reserve(m_logBuffer.size());
                
                for (const auto& entry : m_logBuffer) {
                    batchEntries.emplace_back(
                        static_cast<LogLevel>(entry.level),
                        entry.module,
                        entry.message,
                        entry.details
                    );
                }
                
                // BY ZF: 清空缓冲区（在锁内）
                m_logBuffer.clear();
            }
        }
        
        // BY ZF: 在锁外写入数据库，避免长时间持有锁
        if (m_dbManager && !batchEntries.empty()) {
            m_dbManager->logOperationsBatch(batchEntries);
        }
    }
}

// BY ZF: 定期检查并按计划触发数据库备份
void LoggerProcess::maybePerformBackup() {
    if (!m_backupEnabled || !m_dbManager) {
        return;
    }
    
    auto nowSys = std::chrono::system_clock::now();
    if (nowSys < m_nextBackupCheckTime) {
        return;
    }
    m_nextBackupCheckTime = nowSys + std::chrono::minutes(m_backupCheckIntervalMinutes);
    
    // BY ZF: 每次检查时都检查备份目录大小，如果超过限制则清理旧备份
    if (m_backupMaxSizeBytes > 0) {
        int64_t currentSize = DatabaseManager::getBackupDirSize(m_backupDir);
        if (currentSize < 0) {
            std::cerr << "[Logger][Backup] Failed to get backup directory size: " 
                      << m_backupDir << std::endl;
        } else {
            // BY ZF: 输出当前大小和限制，便于调试
            std::cout << "[Logger][Backup] Current backup size: " << currentSize 
                      << " bytes (" << (currentSize / 1024 / 1024) << " MB), "
                      << "limit: " << m_backupMaxSizeBytes 
                      << " bytes (" << (m_backupMaxSizeBytes / 1024 / 1024) << " MB)" << std::endl;
            
            if (currentSize > m_backupMaxSizeBytes) {
                std::cout << "[Logger][Backup] Backup size exceeds limit, cleaning up old backups..." << std::endl;
                int deletedCount = DatabaseManager::cleanupOldBackups(m_backupDir, m_backupMaxSizeBytes);
                if (deletedCount > 0) {
                    std::cout << "[Logger][Backup] Cleanup completed, deleted " << deletedCount << " file(s)" << std::endl;
                } else {
                    std::cerr << "[Logger][Backup] Warning: Cleanup was called but no files were deleted" << std::endl;
                }
            }
        }
    }
    
    if (nowSys < m_nextBackupTime) {
        return;
    }
    
    if (m_dbManager->backupDatabases(m_backupDir)) {
        std::cout << "[Logger][Backup] Main database backed up successfully to "
                  << m_backupDir << std::endl;
        m_nextBackupTime = nowSys + std::chrono::minutes(m_backupIntervalMinutes);
        
        // BY ZF: 备份后再次检查大小，确保不超过限制
        if (m_backupMaxSizeBytes > 0) {
            int64_t currentSize = DatabaseManager::getBackupDirSize(m_backupDir);
            if (currentSize > 0 && currentSize > m_backupMaxSizeBytes) {
                std::cout << "[Logger][Backup] After backup, size exceeds limit, cleaning up..." << std::endl;
                int deletedCount = DatabaseManager::cleanupOldBackups(m_backupDir, m_backupMaxSizeBytes);
                if (deletedCount > 0) {
                    std::cout << "[Logger][Backup] Cleanup completed, deleted " << deletedCount << " file(s)" << std::endl;
                }
            }
        }
    } else {
        std::cerr << "[Logger][Backup] Failed to backup databases to "
                  << m_backupDir << std::endl;
        // 下次尝试延迟到下一次检查时间
        m_nextBackupTime = nowSys + std::chrono::minutes(m_backupCheckIntervalMinutes);
    }
}

// BY ZF: 响应守护进程的看门狗心跳，避免被误判为无响应
void LoggerProcess::handleWatchdogMessage()
{
    if (!m_logQueue) return;
    
    char buffer[256];
    int actualSize;
    
    int result = m_logQueue->receive(MSG_WATCHDOG_FEED, buffer, sizeof(buffer), &actualSize);
    if (result == 0 && actualSize > 0) {
        buffer[actualSize] = '\0';
        // BY ZF: 省略喂狗打印，避免刷屏
    }
}

// BY ZF: 将内存中的日志缓冲批量写入 operation_logs（使用事务批量提交，提高性能）
void LoggerProcess::flushLogBufferInternal()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
    if (m_logBuffer.empty() || !m_dbManager) {
        return;
    }
    
    // BY ZF: 准备批量写入数据（使用事务批量提交，显著提高性能）
    std::vector<std::tuple<LogLevel, std::string, std::string, std::string>> batchEntries;
    batchEntries.reserve(m_logBuffer.size());
    
    for (const auto& entry : m_logBuffer) {
        batchEntries.emplace_back(
            static_cast<LogLevel>(entry.level),
            entry.module,
            entry.message,
            entry.details
        );
    }
    
    // BY ZF: 批量写入数据库（使用事务，一次提交多条记录）
    if (!m_dbManager->logOperationsBatch(batchEntries)) {
        std::cerr << "[LoggerProcess] Failed to batch write logs to database" << std::endl;
    }
    
    // 清空缓冲区
    m_logBuffer.clear();
}

/**
 * @brief 解析并落库 MQ 收到的 JSON 日志数据。
 * 
 * BY ZF: 消息类型区分机制：
 * 1. type="fee_model"    -> 计费模型数据，保存到 tbFeeModel 表
 * 2. type="trade_record" -> 充电交易记录，保存到 charge_trade_info 表
 * 3. 无type字段          -> 普通运行日志，保存到 operation_logs 表（有level/module/message/details字段）
 * 
 * @param jsonData MQ消息字符串
 */
// BY ZF: 解析日志 JSON 并根据 type 分派到对应处理逻辑
void LoggerProcess::parseAndLogMessage(const std::string& jsonData)
{
    // BY ZF: 统一提取 type 字段进行消息类型识别
    auto getType = [](const std::string& json) -> std::string {
        size_t pos = json.find("\"type\":\"");
        if (pos == std::string::npos) return "";  // 无type字段，为普通日志
        pos += 8;  // 跳过 "type":
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };
    
    std::string msgType = getType(jsonData);
    
    // BY ZF: 处理计费模型类型
    if (msgType == "fee_model") {
        auto getInt = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":");
            if (p == std::string::npos) return 0;
            p += key.size() + 3;
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) ++p;
            long long val = 0; bool hasDigit = false;
            while (p < j.size() && j[p] >= '0' && j[p] <= '9') { val = val * 10 + (j[p]-'0'); hasDigit = true; ++p; }
            return hasDigit ? (int)val : 0;
        };
        auto getStr = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":\"");
            if (p == std::string::npos) return std::string{};
            p += key.size() + 4;
            size_t start = p;
            if (start < j.size() && j[start] == '"') ++start;
            size_t end = j.find('"', start);
            if (end == std::string::npos) return std::string{};
            return j.substr(start, end-start);
        };
        
        std::string feeModelId = getStr(jsonData, "fee_model_id");
        int timeNum = getInt(jsonData, "time_num");
        std::string timeSegText = getStr(jsonData, "time_seg_text");
        std::string segFlagText = getStr(jsonData, "seg_flag_text");
        std::string chargeFeeText = getStr(jsonData, "charge_fee_text");
        std::string serviceFeeText = getStr(jsonData, "service_fee_text");
        
        // BY ZF: 解析分号分隔的字符串数组
        auto text2intArray = [](const std::string& text) -> std::vector<unsigned int> {
            std::vector<unsigned int> arr;
            if (text.empty()) return arr;
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    // BY ZF: 添加异常处理，防止无效数字字符串导致崩溃
                    try {
                        // 检查 token 是否只包含数字（可选的前导空格）
                        bool isValid = false;
                        size_t start = 0;
                        while (start < token.size() && (token[start] == ' ' || token[start] == '\t')) {
                            ++start;
                        }
                        if (start < token.size()) {
                            isValid = true;
                            for (size_t i = start; i < token.size(); ++i) {
                                if (token[i] < '0' || token[i] > '9') {
                                    isValid = false;
                                    break;
                                }
                            }
                        }
                        if (isValid) {
                            arr.push_back(static_cast<unsigned int>(std::stoul(token.substr(start))));
                        }
                        // 如果不是有效数字，跳过该 token（不添加到数组）
                    } catch (const std::exception& e) {
                        // BY ZF: 解析失败时跳过该 token，避免崩溃
                        // 可以在这里记录日志，但为避免刷屏，暂时静默处理
                    }
                }
            }
            return arr;
        };
        
        auto text2timeSegArray = [](const std::string& text) -> std::vector<std::string> {
            std::vector<std::string> arr;
            if (text.empty()) return arr;
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    std::string seg = token.substr(0, 4);
                    while (seg.size() < 4) seg = "0" + seg;
                    arr.push_back(seg);
                }
            }
            return arr;
        };
        
        std::vector<std::string> timeSeg = text2timeSegArray(timeSegText);
        std::vector<unsigned int> segFlag = text2intArray(segFlagText);
        std::vector<unsigned int> chargeFee = text2intArray(chargeFeeText);
        std::vector<unsigned int> serviceFee = text2intArray(serviceFeeText);
        
        // BY ZF: 校验数组长度与 timeNum 一致（segFlag 也必须与 timeNum 一致）
        bool ok = (timeNum > 0 && timeNum <= 96 && 
                   (int)timeSeg.size() == timeNum && 
                   (int)chargeFee.size() == timeNum && 
                   (int)serviceFee.size() == timeNum &&
                   (int)segFlag.size() == timeNum);
        if (!ok) {
            std::ostringstream msg;
            msg << "Invalid fee_model array sizes: timeSeg:" << timeSeg.size()
                << ", chargeFee:" << chargeFee.size() << ", serviceFee:" << serviceFee.size()
                << ", segFlag:" << segFlag.size() << ", time_num:" << timeNum
                << ", feeModelId:" << feeModelId;
            std::cerr << "[Logger][FeeModel] " << msg.str() << std::endl;
            this->warn("fee_model_check", msg.str());
            return;
        }
        
        // BY ZF: 打印接收成功信息
        std::cout << "[Logger][FeeModel] Received valid fee_model: " << feeModelId 
                  << ", timeNum=" << timeNum << std::endl;
        
        if (!m_dbManager) return;
        
        // BY ZF: 保存到数据库
        bool saved = m_dbManager->saveFeeModelToDb(feeModelId, static_cast<unsigned char>(timeNum), 
                                                    timeSeg, segFlag, chargeFee, serviceFee);
        
        // BY ZF: 如果保存成功，立即更新共享内存
        if (saved && m_shm) {
            if (DatabaseManager::loadFeeModelToShm(m_feeDbPath, m_shm)) {
                std::cout << "[Logger][FeeModel] Updated in shared memory: " << feeModelId << std::endl;
            } else {
                std::cerr << "[Logger][FeeModel] Failed to update shared memory" << std::endl;
            }
        }
        
        return;
    }
    
    // BY ZF: 处理充电交易记录类型
    if (msgType == "trade_record") {
        // BY ZF: 稳健的整型解析（裸数字优先）
        auto getInt = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":");
            if (p == std::string::npos) return 0;
            p += key.size() + 3; // BY ZF: "key": 紧随其后的位置
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) ++p;
            long long val = 0; bool hasDigit = false;
            while (p < j.size() && j[p] >= '0' && j[p] <= '9') { val = val * 10 + (j[p]-'0'); hasDigit = true; ++p; }
            return hasDigit ? (int)val : 0;
        };
        auto getLL = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":");
            if (p == std::string::npos) return 0LL;
            p += key.size() + 3; // BY ZF
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) ++p;
            long long val = 0; bool hasDigit = false;
            while (p < j.size() && j[p] >= '0' && j[p] <= '9') { val = val * 10 + (j[p]-'0'); hasDigit = true; ++p; }
            return hasDigit ? val : 0LL;
        };
        auto getStr = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":\"");
            if (p == std::string::npos) return std::string{};
            p += key.size() + 4; // BY ZF: 直接指向 value 首字符（可为空串）
            size_t start = p;
            size_t end = j.find('"', start);
            if (end == std::string::npos) return std::string{};
            return j.substr(start, end-start);
        };
        auto getDouble = [&](const std::string& key) -> double {
            std::string s = getStr(jsonData, key);
            if (!s.empty()) {
                try { return std::stod(s); } catch (...) { return 0.0; }
            }
            size_t p = jsonData.find("\"" + key + "\":");
            if (p == std::string::npos) return 0.0;
            p += key.size() + 3;
            while (p < jsonData.size() &&
                   (jsonData[p] == ' ' || jsonData[p] == '\t' || jsonData[p] == '\n' || jsonData[p] == '\r')) {
                ++p;
            }
            size_t start = p;
            if (p < jsonData.size() && (jsonData[p] == '-' || jsonData[p] == '+')) {
                ++p;
            }
            bool hasDigit = false;
            while (p < jsonData.size()) {
                char c = jsonData[p];
                if ((c >= '0' && c <= '9')) {
                    hasDigit = true;
                    ++p;
                    continue;
                }
                if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                    ++p;
                    continue;
                }
                break;
            }
            if (!hasDigit) return 0.0;
            try { return std::stod(jsonData.substr(start, p - start)); } catch (...) { return 0.0; }
        };
        auto parseIntMulti = [&](const std::string& key)->int {
            size_t p = jsonData.find("\"" + key + "\":");
            if (p == std::string::npos) {
                return 0;
            }
            p += key.size() + 3;
            while (p < jsonData.size() &&
                   (jsonData[p] == ' ' || jsonData[p] == '\t' || jsonData[p] == '\n' || jsonData[p] == '\r')) {
                ++p;
            }
            bool quoted = false;
            if (p < jsonData.size() && jsonData[p] == '"') {
                quoted = true;
                ++p;
            }
            long long val = 0;
            bool hasDigit = false;
            while (p < jsonData.size() && jsonData[p] >= '0' && jsonData[p] <= '9') {
                val = val * 10 + (jsonData[p] - '0');
                hasDigit = true;
                ++p;
            }
            if (quoted && p < jsonData.size() && jsonData[p] == '"') {
                ++p;
            }
            return hasDigit ? static_cast<int>(val) : 0;
        };
        // 基本字段解析
        int gunNo = getInt(jsonData, "gun_no");
        std::string preTradeNo = getStr(jsonData, "pre_trade_no");
        std::string tradeNo = getStr(jsonData, "trade_no");
        std::string vinCode = getStr(jsonData, "vin_code");
        int timeDivType = getInt(jsonData, "time_div_type");
        int startType = getInt(jsonData, "start_type");
        long long chargeStartTimeRaw = getLL(jsonData, "charge_start_time");
        long long chargeEndTimeRaw = getLL(jsonData, "charge_end_time");
        uint64_t chargeStartTime = chargeStartTimeRaw > 0 ? static_cast<uint64_t>(chargeStartTimeRaw) : 0ULL;
        uint64_t chargeEndTime = chargeEndTimeRaw > 0 ? static_cast<uint64_t>(chargeEndTimeRaw) : 0ULL;
        double startSoc = getDouble("start_soc");
        double endSoc = getDouble("end_soc");
        unsigned int reason = static_cast<unsigned int>(getInt(jsonData, "reason"));
        std::string feeModelId = getStr(jsonData, "fee_model_id");
        double sumStart = getDouble("sum_start");
        double sumEnd = getDouble("sum_end");
        double totalElect = getDouble("total_elect");
        double totalPowerCost = getDouble("total_power_cost");
        double totalServCost = getDouble("total_serv_cost");
        double totalCost = getDouble("total_cost");
        int timeNum = parseIntMulti("time_num"); // BY ZF
        int startPoint = getInt(jsonData, "start_point");
        int crossPoints = parseIntMulti("cross_points"); // BY ZF
        std::string partElectText = getStr(jsonData, "part_elect_text");
        std::string chargeFeeText = getStr(jsonData, "charge_fee_text");
        std::string serviceFeeText = getStr(jsonData, "service_fee_text");
        std::string pointsElectText = getStr(jsonData, "points_elect_text");
        std::string cardNumber = getStr(jsonData, "card_number");
        
        // BY ZF: 恢复严格校验，不合法拒绝入库
        auto count_nonempty = [](const std::string& s) -> int {
            if (s.empty()) return 0;
            return (int)std::count(s.begin(), s.end(), ',') + 1;
        };
        int v1 = count_nonempty(partElectText);
        int v2 = count_nonempty(chargeFeeText);
        int v3 = count_nonempty(serviceFeeText);
        bool ok = (timeNum == 0 && v1 == 0 && v2 == 0 && v3 == 0)
                || (timeNum > 0 && v1 == timeNum && v2 == timeNum && v3 == timeNum);
        int v4 = count_nonempty(pointsElectText);
        ok = ok && ((crossPoints == 0 && v4 == 0) || (crossPoints > 0 && v4 == crossPoints));
        if (!ok) {
            std::ostringstream msg;
            msg << "Invalid trade_record array size: partElect:" << v1
                << ", chargeFee:" << v2 << ", serviceFee:" << v3
                << ", pointsElect:" << v4 << ", time_num:" << timeNum
                << ", crossPoints:" << crossPoints << ", tradeNo:" << tradeNo;
            std::cerr << "[Logger][TradeRecord] " << msg.str() << std::endl; // BY ZF
            this->warn("trade_record_check", msg.str()); // BY ZF
            return; // BY ZF: 拒绝入库
        }
        m_dbManager->logChargeTradeInfo(
            gunNo, preTradeNo, tradeNo, vinCode, timeDivType, startType,
            chargeStartTime, chargeEndTime, startSoc, endSoc, reason, feeModelId,
            sumStart, sumEnd, totalElect, totalPowerCost, totalServCost, totalCost, timeNum,
            partElectText, chargeFeeText, serviceFeeText, startPoint, crossPoints, pointsElectText, cardNumber
        );
        return;
    }

    // BY ZF: 处理平台确认类型（根据 trade_no 更新确认标志）
    if (msgType == "record_cfm") {
        auto getInt = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":");
            if (p == std::string::npos) return 0;
            p += key.size() + 3;
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) ++p;
            long long val = 0; bool hasDigit = false;
            while (p < j.size() && j[p] >= '0' && j[p] <= '9') { val = val * 10 + (j[p]-'0'); hasDigit = true; ++p; }
            return hasDigit ? static_cast<int>(val) : 0;
        };
        auto getStr = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":\"");
            if (p == std::string::npos) return std::string{};
            p += key.size() + 4;
            size_t end = j.find('"', p);
            if (end == std::string::npos) return std::string{};
            return j.substr(p, end - p);
        };

        const std::string tradeNo = getStr(jsonData, "trade_no");
        int confirmFlag = getInt(jsonData, "confirm_flag");
        if (confirmFlag != 0) {
            confirmFlag = 1;
        }
        if (tradeNo.empty() || !m_dbManager) {
            return;
        }
        if (!m_dbManager->updateTradeConfirmFlag(tradeNo, confirmFlag)) {
            this->warn("record_cfm_update", std::string("update_failed tradeNo=") + tradeNo);
        }
        return;
    }

    // BY ZF: 处理未确认交易记录请求（查询DB并以MQTT QoS2发布给 logic）。
    if (msgType == "get_unconfirmed_record") {
        auto getInt = [](const std::string &j, const std::string &key) {
            size_t p = j.find("\"" + key + "\":");
            if (p == std::string::npos) return 0;
            p += key.size() + 3;
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) ++p;
            long long val = 0; bool hasDigit = false;
            while (p < j.size() && j[p] >= '0' && j[p] <= '9') { val = val * 10 + (j[p]-'0'); hasDigit = true; ++p; }
            return hasDigit ? static_cast<int>(val) : 0;
        };

        int limit = getInt(jsonData, "limit");
        if (limit <= 0) {
            limit = 100;
        }
        std::vector<TradeRecord> records;
        if (!m_dbManager || !m_dbManager->loadUnconfirmedTradeRecords(records, limit)) {
            this->warn("unconfirmed_record_load", "query_failed");
            return;
        }
        for (size_t i = 0; i < records.size(); ++i) {
            publishUnconfirmedRecordToLogic(records[i]);
        }
        std::ostringstream oss;
        oss << "count=" << records.size();
        this->info("unconfirmed_record_load", oss.str());
        return;
    }
    
    // BY ZF: 普通运行日志处理（无type字段或type不是已知类型）
    // 期望格式：{"timestamp":...,"level":1,"module":"daemon","message":"test","details":"extra"}
    
    int level = LOG_INFO;
    std::string module = "unknown";
    std::string message = jsonData;
    std::string details = "";
    
    // 查找level字段
    size_t levelPos = jsonData.find("\"level\":");
    if (levelPos != std::string::npos) {
        levelPos += 8; // 跳过 "level":
        size_t endPos = jsonData.find_first_of(",}", levelPos);
        if (endPos != std::string::npos) {
            std::string levelStr = jsonData.substr(levelPos, endPos - levelPos);
            try {
                level = std::stoi(levelStr);
            } catch (...) {
                level = LOG_INFO;
            }
        }
    }
    
    // 查找module字段
    size_t modulePos = jsonData.find("\"module\":");
    if (modulePos != std::string::npos) {
        modulePos += 9; // 跳过 "module":
        size_t startQuote = jsonData.find("\"", modulePos);
        if (startQuote != std::string::npos) {
            startQuote++;
            size_t endQuote = jsonData.find("\"", startQuote);
            if (endQuote != std::string::npos) {
                module = jsonData.substr(startQuote, endQuote - startQuote);
            }
        }
    }
    
    // 查找message字段
    size_t messagePos = jsonData.find("\"message\":");
    if (messagePos != std::string::npos) {
        messagePos += 10; // 跳过 "message":
        size_t startQuote = jsonData.find("\"", messagePos);
        if (startQuote != std::string::npos) {
            startQuote++;
            size_t endQuote = jsonData.find("\"", startQuote);
            if (endQuote != std::string::npos) {
                message = jsonData.substr(startQuote, endQuote - startQuote);
            }
        }
    }
    
    // 查找details字段
    size_t detailsPos = jsonData.find("\"details\":");
    if (detailsPos != std::string::npos) {
        detailsPos += 10; // 跳过 "details":
        size_t startQuote = jsonData.find("\"", detailsPos);
        if (startQuote != std::string::npos) {
            startQuote++;
            size_t endQuote = jsonData.find("\"", startQuote);
            if (endQuote != std::string::npos) {
                details = jsonData.substr(startQuote, endQuote - startQuote);
            }
        }
    }
    
    // 创建日志条目并添加到缓冲区
    LogEntry entry(static_cast<LogLevel>(level), module, message, details);
    addLogEntry(entry);
}

void LoggerProcess::handleMqttMessage(const std::string& topic, const std::string& payload)
{
    const std::string savePrefix = m_mqttTopicPrefix + "/save/";
    const std::string meterPrefix = m_mqttTopicPrefix + "/meter/";
    const std::string pilePrefix = m_mqttTopicPrefix + "/pile/";

    if (topic.compare(0, savePrefix.size(), savePrefix) == 0) {
        if (!parseAndSaveErrorEvent(topic, payload)) {
            std::cerr << "[Logger][MQTT] Ignore unsupported payload, topic=" << topic << std::endl;
        }
        return;
    }
    if (topic.compare(0, meterPrefix.size(), meterPrefix) == 0) {
        if (!parseAndCacheMeterData(topic, payload)) {
            std::cerr << "[Logger][Telemetry] Ignore meter payload, topic=" << topic << std::endl;
        }
        return;
    }
    if (topic.compare(0, pilePrefix.size(), pilePrefix) == 0) {
        if (!parseAndCacheBmsData(topic, payload)) {
            std::cerr << "[Logger][Telemetry] Ignore pile payload, topic=" << topic << std::endl;
        }
        return;
    }

    {
        std::cerr << "[Logger][MQTT] Ignore unsupported payload, topic=" << topic << std::endl;
    }
}

bool LoggerProcess::parseAndSaveErrorEvent(const std::string& topic, const std::string& payload)
{
    if (!m_dbManager) {
        return false;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        std::cerr << "[Logger][MQTT] Failed to parse payload, topic=" << topic << std::endl;
        return false;
    }

    bool ok = false;
    do {
        cJSON* typeItem = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(typeItem) || !typeItem->valuestring) {
            break;
        }
        if (std::string(typeItem->valuestring) != "Error") {
            break;
        }

        cJSON* data = cJSON_GetObjectItem(root, "data");
        if (!cJSON_IsObject(data)) {
            break;
        }

        cJSON* gunItem = cJSON_GetObjectItem(root, "gun");
        cJSON* occurTimeItem = cJSON_GetObjectItem(data, "occurTime");
        cJSON* pointKeyItem = cJSON_GetObjectItem(data, "pointKey");
        cJSON* faultMessageItem = cJSON_GetObjectItem(data, "faultMessage");
        cJSON* rawValueItem = cJSON_GetObjectItem(data, "rawValue");

        const int gun = cJSON_IsNumber(gunItem) ? gunItem->valueint : -1;
        const std::string occurTime = (cJSON_IsString(occurTimeItem) && occurTimeItem->valuestring) ? occurTimeItem->valuestring : "";
        const std::string pointKey = (cJSON_IsString(pointKeyItem) && pointKeyItem->valuestring) ? pointKeyItem->valuestring : "";
        const std::string faultMessage = (cJSON_IsString(faultMessageItem) && faultMessageItem->valuestring) ? faultMessageItem->valuestring : "";
        const unsigned int rawValue = cJSON_IsNumber(rawValueItem) ? static_cast<unsigned int>(rawValueItem->valueint) : 0U;

        if (gun < 0 || pointKey.empty()) {
            break;
        }

        ok = m_dbManager->logFaultRecord(gun, "Error", occurTime, pointKey, faultMessage, rawValue);
    } while (false);

    cJSON_Delete(root);
    return ok;
}

bool LoggerProcess::parseAndCacheMeterData(const std::string& topic, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return false;
    }

    bool ok = false;
    MeterTelemetryPoint oldPoint{};
    bool hasOldPoint = false;
    do {
        cJSON* data = cJSON_GetObjectItem(root, "data");
        if (!cJSON_IsObject(data)) {
            break;
        }

        const uint64_t recvMs = nowMs();
        const uint64_t sourceTs = jsonNumberU64(root, "ts", recvMs);
        const int topicGunNo = parseTopicGunNo(topic, m_mqttTopicPrefix, "meter");
        cJSON* gunItem = cJSON_GetObjectItem(root, "gun");
        const int gunNo = cJSON_IsNumber(gunItem) ? gunItem->valueint : topicGunNo;
        if (gunNo < 0) {
            break;
        }

        MeterTelemetryPoint point{};
        point.gunNo = gunNo;
        point.minuteStartMs = minuteStartMs(sourceTs);
        point.createdAt = localTimeStringFromMs(point.minuteStartMs);
        point.totalEnergy = jsonNumberDouble(data, "totalEnergy", 0.0);
        point.reverseEnergy = jsonNumberDouble(data, "ReverseEnergy", 0.0);
        point.voltage = jsonNumberDouble(data, "voltage", 0.0);
        point.current = jsonNumberDouble(data, "current", 0.0);

        {
            std::lock_guard<std::mutex> lock(m_telemetryMutex);
            auto it = m_meterTelemetryCache.find(gunNo);
            if (it != m_meterTelemetryCache.end() && it->second.minuteStartMs != point.minuteStartMs) {
                oldPoint = it->second;
                hasOldPoint = true;
            }
            m_meterTelemetryCache[gunNo] = point; // BY ZF: 同一分钟后到数据覆盖前值。
        }
        ok = true;
    } while (false);

    cJSON_Delete(root);
    if (hasOldPoint && m_dbManager) {
        m_dbManager->saveMeterMinutePoint(oldPoint.gunNo, oldPoint.createdAt,
                                          oldPoint.totalEnergy,
                                          oldPoint.reverseEnergy, oldPoint.voltage,
                                          oldPoint.current);
    }
    return ok;
}

bool LoggerProcess::parseAndCacheBmsData(const std::string& topic, const std::string& payload)
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return false;
    }

    bool ok = false;
    BmsTelemetryPoint oldPoint{};
    bool hasOldPoint = false;
    do {
        cJSON* typeItem = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(typeItem) || !typeItem->valuestring) {
            ok = true; // BY ZF: 非标准 pile data 不属于遥测采样错误。
            break;
        }

        const std::string type(typeItem->valuestring);
        cJSON* data = cJSON_GetObjectItem(root, "data");
        if (!cJSON_IsObject(data)) {
            break;
        }

        const int topicGunNo = parseTopicGunNo(topic, m_mqttTopicPrefix, "pile");
        cJSON* gunItem = cJSON_GetObjectItem(root, "gun");
        const int gunNo = cJSON_IsNumber(gunItem) ? gunItem->valueint : topicGunNo;
        if (gunNo < 0) {
            break;
        }

        if (type == "yx") {
            int workStatus = 0;
            if (jsonWorkStatus(data, workStatus)) {
                std::lock_guard<std::mutex> lock(m_telemetryMutex);
                m_pileWorkStatusCache[gunNo] = workStatus; // BY ZF: 后续 yc 写库时用最近 yx 判断是否充电中。
            }
            ok = true;
            break;
        }

        if (type != "yc") {
            ok = true; // BY ZF: pile data 中非 yc 不是错误，按设计静默忽略。
            break;
        }

        const uint64_t recvMs = nowMs();
        const uint64_t sourceTs = jsonNumberU64(root, "ts", recvMs);
        bool charging = false;
        {
            std::lock_guard<std::mutex> lock(m_telemetryMutex);
            auto statusIt = m_pileWorkStatusCache.find(gunNo);
            charging = (statusIt != m_pileWorkStatusCache.end() && statusIt->second != 0);
        }

        BmsTelemetryPoint point{};
        point.gunNo = gunNo;
        point.minuteStartMs = minuteStartMs(sourceTs);
        point.createdAt = localTimeStringFromMs(point.minuteStartMs);
        point.bmsReqVoltage = jsonNumberDouble(data, "bmsReqVoltage", 0.0);
        point.bmsReqCurrent = jsonNumberDouble(data, "bmsReqCurrent", 0.0);
        if (!charging) {
            point.bmsReqVoltage = 0.0; // BY ZF: 非充电状态下清零 BMS 需求，避免停机未拔枪残留需求污染分析。
            point.bmsReqCurrent = 0.0; // BY ZF
        }
        point.bmsMeasuredVoltage = jsonNumberDouble(data, "bmsMeasuredVoltage", 0.0);
        point.bmsMeasuredCurrent = jsonNumberDouble(data, "bmsMeasuredCurrent", 0.0);
        point.outputVoltage = jsonNumberDouble(data, "outputVoltage", 0.0);
        point.outputCurrent = jsonNumberDouble(data, "outputCurrent", 0.0);

        {
            std::lock_guard<std::mutex> lock(m_telemetryMutex);
            auto it = m_bmsTelemetryCache.find(gunNo);
            if (it != m_bmsTelemetryCache.end() && it->second.minuteStartMs != point.minuteStartMs) {
                oldPoint = it->second;
                hasOldPoint = true;
            }
            m_bmsTelemetryCache[gunNo] = point; // BY ZF: 同一分钟后到数据覆盖前值。
        }
        ok = true;
    } while (false);

    cJSON_Delete(root);
    if (hasOldPoint && m_dbManager) {
        m_dbManager->saveBmsMinutePoint(oldPoint.gunNo, oldPoint.createdAt,
                                        oldPoint.bmsReqVoltage,
                                        oldPoint.bmsReqCurrent, oldPoint.bmsMeasuredVoltage,
                                        oldPoint.bmsMeasuredCurrent, oldPoint.outputVoltage,
                                        oldPoint.outputCurrent);
    }
    return ok;
}

void LoggerProcess::flushExpiredTelemetryCaches()
{
    if (!m_dbManager) {
        return;
    }

    const uint64_t currentMinute = minuteStartMs(nowMs());
    std::vector<MeterTelemetryPoint> meterPoints;
    std::vector<BmsTelemetryPoint> bmsPoints;
    {
        std::lock_guard<std::mutex> lock(m_telemetryMutex);
        for (auto it = m_meterTelemetryCache.begin(); it != m_meterTelemetryCache.end(); ) {
            if (it->second.minuteStartMs < currentMinute) {
                meterPoints.push_back(it->second);
                m_meterTelemetryCache.erase(it++);
            } else {
                ++it;
            }
        }
        for (auto it = m_bmsTelemetryCache.begin(); it != m_bmsTelemetryCache.end(); ) {
            if (it->second.minuteStartMs < currentMinute) {
                bmsPoints.push_back(it->second);
                m_bmsTelemetryCache.erase(it++);
            } else {
                ++it;
            }
        }
    }

    for (const auto& p : meterPoints) {
        m_dbManager->saveMeterMinutePoint(p.gunNo, p.createdAt, p.totalEnergy,
                                          p.reverseEnergy, p.voltage, p.current);
    }
    for (const auto& p : bmsPoints) {
        m_dbManager->saveBmsMinutePoint(p.gunNo, p.createdAt, p.bmsReqVoltage,
                                        p.bmsReqCurrent, p.bmsMeasuredVoltage,
                                        p.bmsMeasuredCurrent, p.outputVoltage,
                                        p.outputCurrent);
    }
}

void LoggerProcess::flushAllTelemetryCaches()
{
    if (!m_dbManager) {
        return;
    }

    std::vector<MeterTelemetryPoint> meterPoints;
    std::vector<BmsTelemetryPoint> bmsPoints;
    {
        std::lock_guard<std::mutex> lock(m_telemetryMutex);
        for (const auto& pair : m_meterTelemetryCache) {
            meterPoints.push_back(pair.second);
        }
        for (const auto& pair : m_bmsTelemetryCache) {
            bmsPoints.push_back(pair.second);
        }
        m_meterTelemetryCache.clear();
        m_bmsTelemetryCache.clear();
    }

    for (const auto& p : meterPoints) {
        m_dbManager->saveMeterMinutePoint(p.gunNo, p.createdAt, p.totalEnergy,
                                          p.reverseEnergy, p.voltage, p.current);
    }
    for (const auto& p : bmsPoints) {
        m_dbManager->saveBmsMinutePoint(p.gunNo, p.createdAt, p.bmsReqVoltage,
                                        p.bmsReqCurrent, p.bmsMeasuredVoltage,
                                        p.bmsMeasuredCurrent, p.outputVoltage,
                                        p.outputCurrent);
    }
}

void LoggerProcess::maybeCleanupTelemetry()
{
    if (!m_dbManager) {
        return;
    }

    auto nowSteady = std::chrono::steady_clock::now();
    if (nowSteady < m_nextTelemetryCleanupTime) {
        return;
    }
    m_nextTelemetryCleanupTime = nowSteady + std::chrono::minutes(LOGGER_TELEMETRY_CLEANUP_INTERVAL_MINUTES);

    const uint64_t retentionMs = static_cast<uint64_t>(LOGGER_TELEMETRY_RETENTION_DAYS) * 24ULL * 60ULL * 60ULL * 1000ULL;
    const uint64_t currentMs = nowMs();
    if (currentMs <= retentionMs) {
        return;
    }
    const uint64_t cutoffMs = currentMs - retentionMs;
    const std::string cutoffTime = localTimeStringFromMs(cutoffMs);
    if (!m_dbManager->cleanupTelemetryBefore(cutoffTime)) {
        std::cerr << "[Logger][Telemetry] cleanup failed, cutoffMs=" << cutoffMs << std::endl;
    }
}

// BY ZF: 直接接收业务层传入的 TradeRecord，完成长度校验并入库（不经过 MQ）
void LoggerProcess::logTradeRecord(const TradeRecord& rec)
{
    auto vec2text = [](const std::vector<double>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ',';
            oss << std::fixed << std::setprecision(5) << arr[i];
        }
        return oss.str();
    };
    // BY ZF: 严格长度校验
    int v1 = (int)rec.partElect.size();
    int v2 = (int)rec.chargeFee.size();
    int v3 = (int)rec.serviceFee.size();
    int v4 = (int)rec.pointsElect.size();
    bool ok = (rec.timeNum == 0 && v1 == 0 && v2 == 0 && v3 == 0)
           || (rec.timeNum > 0 && v1 == rec.timeNum && v2 == rec.timeNum && v3 == rec.timeNum);
    ok = ok && ((rec.crossPoints == 0 && v4 == 0) || (rec.crossPoints > 0 && v4 == rec.crossPoints));
    if (!ok) {
        std::ostringstream msg; msg << "Invalid TradeRecord sizes: partElect:" << v1 << ", chargeFee:" << v2 << ", serviceFee:" << v3 << ", pointsElect:" << v4 << ", time_num:" << rec.timeNum << ", crossPoints:" << rec.crossPoints << ", tradeNo:" << rec.tradeNo;
        std::cerr << "[Logger][TradeRecord] " << msg.str() << std::endl; // BY ZF
        this->warn("trade_record_check", msg.str()); // BY ZF
        return; // BY ZF: 拒绝入库
    }
    if (!m_dbManager) return;
    m_dbManager->logChargeTradeInfo(
        rec.gunNo, rec.preTradeNo, rec.tradeNo, rec.vinCode, rec.timeDivType, rec.startType,
        rec.chargeStartTime, rec.chargeEndTime, rec.startSoc, rec.endSoc, rec.reason, rec.feeModelId,
        rec.sumStart, rec.sumEnd, rec.totalElect, rec.totalPowerCost, rec.totalServCost, rec.totalCost, rec.timeNum,
        vec2text(rec.partElect), vec2text(rec.chargeFee), vec2text(rec.serviceFee), rec.startPoint, rec.crossPoints, vec2text(rec.pointsElect), rec.cardNumber
    );
}

// BY ZF: 保存计费模型并同步到数据库与共享内存，包含严密校验
void LoggerProcess::saveFeeModel(const FeeModel& model)
{
    // BY ZF: 严格长度校验（segFlag 也必须与 timeNum 一致）
    bool ok = (model.timeNum > 0 && model.timeNum <= 96 && 
               (int)model.timeSeg.size() == model.timeNum && 
               (int)model.chargeFee.size() == model.timeNum && 
               (int)model.serviceFee.size() == model.timeNum &&
               (int)model.segFlag.size() == model.timeNum);
    if (!ok) {
        std::ostringstream msg;
        msg << "Invalid FeeModel sizes: timeSeg:" << model.timeSeg.size()
            << ", chargeFee:" << model.chargeFee.size() << ", serviceFee:" << model.serviceFee.size()
            << ", segFlag:" << model.segFlag.size() << ", time_num:" << (int)model.timeNum
            << ", feeModelId:" << model.feeModelId;
        std::cerr << "[Logger][FeeModel] " << msg.str() << std::endl;
        this->warn("fee_model_check", msg.str());
        return;
    }
    
    if (!m_dbManager) return;
    
    // BY ZF: 保存到数据库
    bool saved = m_dbManager->saveFeeModelToDb(model.feeModelId, model.timeNum, 
                                                model.timeSeg, model.segFlag, 
                                                model.chargeFee, model.serviceFee);
    
    // BY ZF: 如果保存成功，立即更新共享内存
    if (saved && m_shm) {
        if (DatabaseManager::loadFeeModelToShm(m_feeDbPath, m_shm)) {
            std::cout << "Fee model updated in shared memory: " << model.feeModelId << std::endl;
        } else {
            std::cerr << "Failed to update fee model in shared memory" << std::endl;
        }
    }
}

bool LoggerProcess::initMqttPublisher()
{
    if (!m_mqtt.init(m_mqttClientId, true)) {
        return false;
    }
    m_mqtt.setConnectHandler([this](int rc) {
        if (rc != 0) {
            std::cerr << "[Logger][MQTT] connect rc=" << rc << std::endl;
            return;
        }
        const std::string saveTopic = m_mqttTopicPrefix + "/save/+/event";
        const std::string meterTopic = m_mqttTopicPrefix + "/meter/+/data";
        const std::string pileTopic = m_mqttTopicPrefix + "/pile/+/data";
        if (!m_mqtt.subscribe(saveTopic, 2)) {
            std::cerr << "[Logger][MQTT] subscribe failed: " << saveTopic << std::endl;
            return;
        }
        if (!m_mqtt.subscribe(meterTopic, 0)) {
            std::cerr << "[Logger][MQTT] subscribe failed: " << meterTopic << std::endl;
            return;
        }
        if (!m_mqtt.subscribe(pileTopic, 0)) {
            std::cerr << "[Logger][MQTT] subscribe failed: " << pileTopic << std::endl;
            return;
        }
        std::cout << "[Logger][MQTT] subscribed: " << saveTopic << std::endl;
        std::cout << "[Logger][MQTT] subscribed: " << meterTopic << std::endl;
        std::cout << "[Logger][MQTT] subscribed: " << pileTopic << std::endl;
    });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        handleMqttMessage(topic, payload);
    });
    if (!m_mqtt.connect(m_mqttHost, m_mqttPort, m_mqttKeepalive)) {
        return false;
    }
    if (!m_mqtt.loopStart()) {
        return false;
    }
    m_mqttReady = true;
    return true;
}

std::string LoggerProcess::buildUpdateRecordPayload(const TradeRecord& rec)
{
    cJSON* root = cJSON_CreateObject();
    const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(nowMs));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_mqttSeq));
    cJSON_AddStringToObject(root, "source", "tcu_logger");
    cJSON_AddNumberToObject(root, "gun", rec.gunNo);
    cJSON_AddStringToObject(root, "event", "unconfirmed_record");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "gunNo", rec.gunNo);
    cJSON_AddStringToObject(data, "preTradeNo", rec.preTradeNo.c_str());
    cJSON_AddStringToObject(data, "tradeNo", rec.tradeNo.c_str());
    cJSON_AddStringToObject(data, "vinCode", rec.vinCode.c_str());
    cJSON_AddNumberToObject(data, "timeDivType", rec.timeDivType);
    cJSON_AddNumberToObject(data, "startType", rec.startType);
    cJSON_AddNumberToObject(data, "chargeStartTime", static_cast<double>(rec.chargeStartTime));
    cJSON_AddNumberToObject(data, "chargeEndTime", static_cast<double>(rec.chargeEndTime));
    cJSON_AddNumberToObject(data, "startSoc", rec.startSoc);
    cJSON_AddNumberToObject(data, "endSoc", rec.endSoc);
    cJSON_AddNumberToObject(data, "reason", rec.reason);
    cJSON_AddStringToObject(data, "feeModelId", rec.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "sumStart", rec.sumStart);
    cJSON_AddNumberToObject(data, "sumEnd", rec.sumEnd);
    cJSON_AddNumberToObject(data, "totalElect", rec.totalElect);
    cJSON_AddNumberToObject(data, "totalPowerCost", rec.totalPowerCost);
    cJSON_AddNumberToObject(data, "totalServCost", rec.totalServCost);
    cJSON_AddNumberToObject(data, "totalCost", rec.totalCost);
    cJSON_AddNumberToObject(data, "timeNum", rec.timeNum);
    cJSON_AddNumberToObject(data, "startPoint", rec.startPoint);
    cJSON_AddNumberToObject(data, "crossPoints", rec.crossPoints);
    cJSON_AddStringToObject(data, "cardNumber", rec.cardNumber.c_str());

    cJSON* partElect = cJSON_CreateArray();
    for (size_t i = 0; i < rec.partElect.size(); ++i) {
        cJSON_AddItemToArray(partElect, cJSON_CreateNumber(rec.partElect[i]));
    }
    cJSON_AddItemToObject(data, "partElect", partElect);

    cJSON* chargeFee = cJSON_CreateArray();
    for (size_t i = 0; i < rec.chargeFee.size(); ++i) {
        cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(rec.chargeFee[i]));
    }
    cJSON_AddItemToObject(data, "chargeFee", chargeFee);

    cJSON* serviceFee = cJSON_CreateArray();
    for (size_t i = 0; i < rec.serviceFee.size(); ++i) {
        cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(rec.serviceFee[i]));
    }
    cJSON_AddItemToObject(data, "serviceFee", serviceFee);

    cJSON* pointsElect = cJSON_CreateArray();
    for (size_t i = 0; i < rec.pointsElect.size(); ++i) {
        cJSON_AddItemToArray(pointsElect, cJSON_CreateNumber(rec.pointsElect[i]));
    }
    cJSON_AddItemToObject(data, "pointsElect", pointsElect);
    cJSON_AddItemToObject(root, "data", data);

    char* out = cJSON_PrintUnformatted(root);
    std::string payload = out ? out : "";
    if (out) {
        cJSON_free(out);
    }
    cJSON_Delete(root);
    return payload;
}

bool LoggerProcess::publishUnconfirmedRecordToLogic(const TradeRecord& rec)
{
    if (!m_mqttReady) {
        return false;
    }
    std::ostringstream topic;
    topic << m_mqttTopicPrefix << "/logger/" << (rec.gunNo + m_mqttBiasNo) << "/event";
    const std::string payload = buildUpdateRecordPayload(rec);
    if (payload.empty()) {
        return false;
    }
    return m_mqtt.publish(topic.str(), payload, 2, false);
}

void LoggerProcess::feedWatchdog()
{
    // BY ZF: 使用看门狗队列发送心跳消息到守护进程（不是日志队列！）
    if (m_watchdogQueue) {
        std::string processName = "tcu_logger";
        // BY ZF: 看门狗发送失败时输出错误（调试阶段需要）
        if (m_watchdogQueue->send(MSG_WATCHDOG_FEED, (void*)processName.c_str(), processName.length()) != 0) {
            // BY ZF: 错误输出已在 message_queue 中控制频率，这里不再重复输出
            // 如果需要单独统计看门狗失败，可以在这里添加计数
        }
    }
}
