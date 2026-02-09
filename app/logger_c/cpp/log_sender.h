// BY ZF: 日志发送器类（复制自 core/base/log_sender.h，供独立打包使用）
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>

#include "message_queue.h"
#include "logger_types.h"

// BY ZF: 提供给业务进程使用的日志发送器
class LogSender {
public:
    explicit LogSender(const std::string& moduleName);
    ~LogSender();

    void debug(const std::string& message, const std::string& details = "");
    void info(const std::string& message, const std::string& details = "");
    void warn(const std::string& message, const std::string& details = "");
    void error(const std::string& message, const std::string& details = "");
    void fatal(const std::string& message, const std::string& details = "");

    void flush();
    void stop();

    void logTradeRecord(const TradeRecord& rec);
    void saveFeeModel(const FeeModel& model);

private:
    void addLog(LogLevel level, const std::string& message, const std::string& details);
    std::string logEntryToJson(const LogEntry& entry);
    std::string tradeRecordToJson(const TradeRecord& rec);
    std::string feeModelToJson(const FeeModel& model);
    void flushThread();
    bool sendToQueue(const std::string& jsonData);

private:
    std::string m_moduleName;
    std::vector<LogEntry> m_buffer;
    std::mutex m_mutex;
    std::atomic<bool> m_running;
    std::thread m_flushThread;

    static constexpr size_t MAX_BUFFER_SIZE = 100;
    static constexpr std::chrono::seconds FLUSH_INTERVAL{2};

    MessageQueue m_logQueue;
};


