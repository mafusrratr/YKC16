/**
 * 日志发送器类
 * 供其他进程使用，提供本地缓冲和异步发送
 * BY ZF
 */

#ifndef LOG_SENDER_H
#define LOG_SENDER_H

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include "../common/message_queue.h"
#include "../common/logger_types.h"

// BY ZF: 使用 logger_types.h 中的类型定义（LogLevel, LogEntry, TradeRecord, FeeModel）

/**
 * 日志发送器类
 */
class LogSender {
public:
    /**
     * 构造函数
     * @param moduleName 模块名称
     */
    explicit LogSender(const std::string& moduleName);
    
    /**
     * 析构函数
     */
    ~LogSender();
    
    /**
     * 记录调试日志
     */
    void debug(const std::string& message, const std::string& details = "");
    
    /**
     * 记录信息日志
     */
    void info(const std::string& message, const std::string& details = "");
    
    /**
     * 记录警告日志
     */
    void warn(const std::string& message, const std::string& details = "");
    
    /**
     * 记录错误日志
     */
    void error(const std::string& message, const std::string& details = "");
    
    /**
     * 记录致命错误日志
     */
    void fatal(const std::string& message, const std::string& details = "");
    
    /**
     * 手动刷新缓冲区
     */
    void flush();
    
    /**
     * 停止日志发送器
     */
    void stop();

    /**
     * @brief 发送充电交易记录到logger进程，进行入库前处理。
     * @param rec 充电交易记录结构体，包含所有时段电量、费用等。
     * @note 仅做业务结构组装，内部会转逗号分隔字符串。
     */
    void logTradeRecord(const TradeRecord& rec);
    
    /**
     * @brief BY ZF: 保存计费模型到数据库（通过消息队列异步处理）
     * @param model 计费模型结构体
     */
    void saveFeeModel(const FeeModel& model);

private:
    /**
     * 添加日志条目到缓冲区
     */
    void addLog(LogLevel level, const std::string& message, const std::string& details);
    
    /**
     * 将日志条目转换为JSON字符串
     */
    std::string logEntryToJson(const LogEntry& entry);

    /**
     * @brief trade_record结构体序列化为json文本（所有vector转为纯逗号分隔数值字符串）。
     * @param rec 充电交易记录
     * @return json字符串
     */
    std::string tradeRecordToJson(const TradeRecord& rec);
    
    /**
     * @brief BY ZF: fee_model结构体序列化为json文本（所有vector转为分号分隔字符串）。
     * @param model 计费模型
     * @return json字符串
     */
    std::string feeModelToJson(const FeeModel& model);
    
    /**
     * 刷新线程函数
     */
    void flushThread();
    
    /**
     * BY ZF: 内部刷新函数（不加锁，供已持有锁的代码调用）
     */
    void flushInternal();
    
    /**
     * 发送日志到消息队列
     * BY ZF: 会检查队列使用率，超过95%时放弃写入
     */
    bool sendToQueue(const std::string& jsonData);
    
    /**
     * BY ZF: 检查消息队列使用率
     * @return 使用率（0.0-1.0），-1.0表示获取失败
     */
    double getQueueUsage();

private:
    std::string m_moduleName;
    std::vector<LogEntry> m_buffer;
    std::mutex m_mutex;
    std::atomic<bool> m_running;
    std::thread m_flushThread;
    
    // 配置参数
    static constexpr size_t MAX_BUFFER_SIZE = 100;           // 最大缓冲区大小
    static constexpr std::chrono::seconds FLUSH_INTERVAL{2}; // 刷新间隔2秒
    static constexpr double QUEUE_USAGE_THRESHOLD = 0.95;    // BY ZF: 队列使用率阈值（95%）
    
    // 消息队列
    MessageQueue m_logQueue;
};

#endif // LOG_SENDER_H


