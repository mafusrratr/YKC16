/**
 * 日志进程实现（继承IProcess接口）
 * BY ZF
 */

#ifndef LOGGER_PROCESS_H
#define LOGGER_PROCESS_H

#include "../interfaces/iprocess.h"
#include "../base/common/logger_types.h"
#include "../base/common/config_manager_lite.h"
#include "../base/common/message_queue.h"
#include "../base/mqtt/mqtt_client.h"
#include "database_manager.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>

// BY ZF: 日志缓冲区配置宏定义（硬编码，不可配置）
#define LOGGER_MAX_BUFFER_SIZE 1000      // 日志缓冲区最大大小（条数）
#define LOGGER_FLUSH_INTERVAL_SECONDS 1  // 日志刷新间隔（秒）

/**
 * 日志进程
 * BY ZF: 继承IProcess接口，直接实现日志功能（不再实现ILogger接口）
 */
class LoggerProcess : public IProcess {
public:
    LoggerProcess();
    virtual ~LoggerProcess();
    
    // IProcess接口实现
    ProcessType getType() override { return PROC_LOGGER; }
    const char* getName() override { return "LoggerProcess"; }
    bool initialize(const char* config) override;
    int start() override;
    void stop() override;
    void run() override;
    ProcessStatus getStatus() override { return m_status.load(); }
    
    // BY ZF: 日志功能方法（不再继承 ILogger，直接实现）
    void debug(const std::string& module, const std::string& message, 
              const std::string& details = "");
    void info(const std::string& module, const std::string& message, 
             const std::string& details = "");
    void warn(const std::string& module, const std::string& message, 
             const std::string& details = "");
    void error(const std::string& module, const std::string& message, 
              const std::string& details = "");
    void fatal(const std::string& module, const std::string& message, 
              const std::string& details = "");
    
    // BY ZF: 直接存储充电交易记录
    void logTradeRecord(const TradeRecord& rec);
    
    // BY ZF: 保存计费模型
    void saveFeeModel(const FeeModel& model);
    
    void setLogLevel(LogLevel level) { m_logLevel = level; }
    LogLevel getLogLevel() const { return m_logLevel; }
    void flush();
    uint32_t cleanupOldLogs(int days);
    
    std::string queryOperationLogs(uint64_t startTime, uint64_t endTime,
                                 int level = -1, 
                                 const std::string& module = "");
    
    void setDatabasePaths(const std::string& mainDbPath,
                        const std::string& chargeDbPath,
                        const std::string& feeDbPath);
    void setBufferConfig(size_t maxSize, int flushInterval);

    // BY ZF: 看门狗喂狗函数（供外部驱动主循环时调用）
    void feedWatchdog();

    // BY ZF: 发布未确认交易记录到 tcu_logic（QoS2）。
    bool publishUnconfirmedRecordToLogic(const TradeRecord& rec);

    // BY ZF: 组装 update_record 事件 payload。
    std::string buildUpdateRecordPayload(const TradeRecord& rec);

private:
    /**
     * 处理日志消息队列
     */
    void processLogMessage();
    
    /**
     * 处理周期性备份任务
     */
    void maybePerformBackup();
    
    /**
     * 处理看门狗消息
     */
    void handleWatchdogMessage();
    
    /**
     * 刷新日志缓冲区到数据库
     */
    void flushLogBufferInternal();
    
    /**
     * 简单的JSON解析函数
     */
    void parseAndLogMessage(const std::string& jsonData);
    bool initMqttPublisher();
    
    /**
     * 主循环线程函数
     */
    void mainLoop();
    
    /**
     * 添加日志条目到缓冲区
     */
    void addLogEntry(const LogEntry& entry);
    // 进程状态
    std::atomic<ProcessStatus> m_status;
    std::atomic<bool> m_running;
    
    // 配置管理
    ConfigManagerLite& m_config;
    
    // 数据库管理
    DatabaseManager* m_dbManager;
    
    // 消息队列
    MessageQueue* m_logQueue;
    MessageQueue* m_watchdogQueue;  // BY ZF: 看门狗队列（用于发送看门狗心跳）
    
    // 主循环线程
    std::thread m_mainThread;
    
    // 日志缓冲区
    std::vector<LogEntry> m_logBuffer;
    std::mutex m_bufferMutex;
    size_t m_maxBufferSize;
    std::chrono::seconds m_flushInterval;
    
    // 日志级别
    LogLevel m_logLevel;
    
    // 配置参数
    std::string m_mainDbPath;
    std::string m_chargeDbPath;
    std::string m_feeDbPath;
    
    // BY ZF: 共享内存指针
    void* m_shm;

    // BY ZF: MQTT 发布器（用于未确认记录回放到 logic）
    MqttClient m_mqtt;
    bool m_mqttReady;
    std::string m_mqttHost;
    int m_mqttPort;
    int m_mqttKeepalive;
    std::string m_mqttClientId;
    std::string m_mqttTopicPrefix;
    std::atomic<uint64_t> m_mqttSeq;
    
    // BY ZF: 备份相关
    bool m_backupEnabled;
    std::string m_backupDir;
    int m_backupIntervalMinutes;
    int m_backupCheckIntervalMinutes;
    int64_t m_backupMaxSizeBytes;  // BY ZF: 备份目录最大大小限制（字节）
    std::chrono::system_clock::time_point m_nextBackupTime;
    std::chrono::system_clock::time_point m_nextBackupCheckTime;
};

#endif // LOGGER_PROCESS_H
