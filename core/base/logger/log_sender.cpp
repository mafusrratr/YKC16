/**
 * 日志发送器实现
 * BY ZF
 */

#include "log_sender.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include "../common/logger_types.h"

LogSender::LogSender(const std::string& moduleName)
    : m_moduleName(moduleName)
    , m_running(true)
    , m_logQueue(MSG_KEY_LOG)
{
    // 打开消息队列
    if (!m_logQueue.open()) {
        std::cerr << "LogSender: Failed to open log message queue" << std::endl;
    }
    
    // 启动刷新线程
    m_flushThread = std::thread(&LogSender::flushThread, this);
}

LogSender::~LogSender() {
    stop();
}

void LogSender::debug(const std::string& message, const std::string& details) {
    addLog(LOG_DEBUG, message, details);
}

void LogSender::info(const std::string& message, const std::string& details) {
    addLog(LOG_INFO, message, details);
}

void LogSender::warn(const std::string& message, const std::string& details) {
    addLog(LOG_WARN, message, details);
}

void LogSender::error(const std::string& message, const std::string& details) {
    addLog(LOG_ERROR, message, details);
}

void LogSender::fatal(const std::string& message, const std::string& details) {
    addLog(LOG_FATAL, message, details);
}

// BY ZF: 内部刷新函数（不加锁，供已持有锁的代码调用）
void LogSender::flushInternal() {
    if (m_buffer.empty()) {
        return;
    }
    
    // BY ZF: 批量发送日志，队列满时保留未发送的日志
    std::vector<LogEntry> unsentEntries;
    
    for (const auto& entry : m_buffer) {
        std::string jsonData = logEntryToJson(entry);
        if (sendToQueue(jsonData)) {
            // 发送成功，继续下一个
        } else {
            // BY ZF: 队列满时保留未发送的日志，避免日志丢失
            unsentEntries.push_back(entry);
        }
    }
    
    // BY ZF: 如果缓冲区过大（超过最大值的2倍），丢弃最旧的日志，避免内存无限增长
    if (unsentEntries.size() > MAX_BUFFER_SIZE * 2) {
        // 保留最新的 MAX_BUFFER_SIZE 条日志
        size_t droppedCount = unsentEntries.size() - MAX_BUFFER_SIZE;
        std::vector<LogEntry> keptEntries(
            unsentEntries.end() - MAX_BUFFER_SIZE,
            unsentEntries.end()
        );
        unsentEntries = keptEntries;
        std::cerr << "[LogSender] Warning: Buffer overflow, dropped " 
                  << droppedCount 
                  << " oldest log entries" << std::endl;
    }
    
    // 保留未发送的日志，清空已发送的
    m_buffer = unsentEntries;
}

void LogSender::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    flushInternal();
}

void LogSender::stop() {
    if (m_running.load()) {
        m_running = false;
        
        // 刷新剩余日志
        flush();  // BY ZF: 这里调用公共接口，会自动加锁
        
        // 等待刷新线程结束
        if (m_flushThread.joinable()) {
            m_flushThread.join();
        }
    }
}

void LogSender::addLog(LogLevel level, const std::string& message, const std::string& details) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 创建日志条目
    LogEntry entry(level, m_moduleName, message, details);
    
    // 填充毫秒时间戳
    auto now = std::chrono::system_clock::now();
    entry.timestamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // 添加到缓冲区
    m_buffer.push_back(entry);
    
    // BY ZF: 如果缓冲区满了，立即刷新（调用内部函数，避免重复加锁）
    // 注意：flushInternal 会保留未发送的日志，不会全部清空
    if (m_buffer.size() >= MAX_BUFFER_SIZE) {
        flushInternal();
    }
}

std::string LogSender::logEntryToJson(const LogEntry& entry) {
    std::ostringstream json;
    json << "{"
         << "\"timestamp\":" << entry.timestamp << ","
         << "\"level\":" << static_cast<int>(entry.level) << ","
         << "\"module\":\"" << entry.module << "\","
         << "\"message\":\"" << entry.message << "\","
         << "\"details\":\"" << entry.details << "\""
         << "}";
    return json.str();
}

void LogSender::flushThread() {
    while (m_running.load()) {
        std::this_thread::sleep_for(FLUSH_INTERVAL);
        
        if (m_running.load()) {
            flush();  // BY ZF: 这里调用公共接口，会自动加锁
        }
    }
}

// BY ZF: 检查消息队列使用率
double LogSender::getQueueUsage() {
    unsigned long currentMsgs = 0;
    unsigned long maxMsgs = 0;
    
    if (!m_logQueue.getQueueStatus(&currentMsgs, &maxMsgs)) {
        return -1.0;  // 获取失败
    }
    
    if (maxMsgs == 0) {
        return -1.0;  // 无效的最大消息数
    }
    
    return static_cast<double>(currentMsgs) / static_cast<double>(maxMsgs);
}

bool LogSender::sendToQueue(const std::string& jsonData) {
    // BY ZF: 检查队列使用率，如果超过95%则放弃写入
    double usage = getQueueUsage();
    if (usage >= 0.0 && usage >= QUEUE_USAGE_THRESHOLD) {
        // BY ZF: 队列使用率过高，放弃写入并打印提示
        unsigned long currentMsgs = 0;
        unsigned long maxMsgs = 0;
        m_logQueue.getQueueStatus(&currentMsgs, &maxMsgs);
        std::cerr << "[LogSender] Warning: Message queue usage is " 
                  << (usage * 100.0) << "% (" << currentMsgs << "/" << maxMsgs 
                  << "), dropping log message to prevent queue overflow" << std::endl;
        return false;
    }
    
    return m_logQueue.send(MSG_TYPE_LOG, jsonData.c_str(), jsonData.length()) == 0;
}

void LogSender::logTradeRecord(const TradeRecord& rec) {
    std::string json = tradeRecordToJson(rec);
    sendToQueue(json);
}

void LogSender::saveFeeModel(const FeeModel& model) {
    std::string json = feeModelToJson(model);
    sendToQueue(json);
}

void LogSender::confirmTradeRecord(const std::string& tradeNo, int confirmFlag) {
    std::ostringstream json;
    json << "{"
         << "\"type\":\"record_cfm\","
         << "\"trade_no\":\"" << tradeNo << "\","
         << "\"confirm_flag\":" << confirmFlag
         << "}";
    sendToQueue(json.str());
}

std::string LogSender::tradeRecordToJson(const TradeRecord& rec) {
    auto vec2text = [](const std::vector<double>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ',';
            oss << std::fixed << std::setprecision(5) << arr[i];
        }
        return oss.str();
    };

    std::ostringstream json;
    json << "{"
         << "\"type\":\"trade_record\"," // 用于识别类型
         << "\"gun_no\":" << rec.gunNo << ","
         << "\"pre_trade_no\":\"" << rec.preTradeNo << "\","
         << "\"trade_no\":\"" << rec.tradeNo << "\","
         << "\"vin_code\":\"" << rec.vinCode << "\","
         << "\"time_div_type\":" << rec.timeDivType << ","
         << "\"start_type\":" << rec.startType << ","
         << "\"charge_start_time\":" << rec.chargeStartTime << ","
         << "\"charge_end_time\":" << rec.chargeEndTime << ","
         << "\"start_soc\":" << rec.startSoc << ","
         << "\"end_soc\":" << rec.endSoc << ","
         << "\"reason\":" << rec.reason << ","
         << "\"fee_model_id\":\"" << rec.feeModelId << "\","
         << "\"sum_start\":" << rec.sumStart << ","
         << "\"sum_end\":" << rec.sumEnd << ","
         << "\"total_elect\":" << rec.totalElect << ","
         << "\"total_power_cost\":" << rec.totalPowerCost << ","
         << "\"total_serv_cost\":" << rec.totalServCost << ","
         << "\"total_cost\":" << rec.totalCost << ","
         << "\"time_num\":" << rec.timeNum << ","
         << "\"part_elect_text\":\"" << vec2text(rec.partElect) << "\"," 
         << "\"charge_fee_text\":\"" << vec2text(rec.chargeFee) << "\"," 
         << "\"service_fee_text\":\"" << vec2text(rec.serviceFee) << "\"," 
         << "\"start_point\":" << rec.startPoint << ","
         << "\"cross_points\":" << rec.crossPoints << ","
         << "\"points_elect_text\":\"" << vec2text(rec.pointsElect) << "\"," 
         << "\"card_number\":\"" << rec.cardNumber << "\"}";
    return json.str();
}

std::string LogSender::feeModelToJson(const FeeModel& model) {
    // BY ZF: 将数组转换为分号分隔的字符串
    auto intArray2text = [](const std::vector<unsigned int>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';
            oss << arr[i];
        }
        return oss.str();
    };
    
    // BY ZF: timeSeg 转换为分号分隔字符串（每个4字符，HHMM格式）
    auto timeSegArray2text = [](const std::vector<std::string>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';
            // 只取前4个字符（HHMM格式）
            std::string seg = arr[i].substr(0, 4);
            while (seg.size() < 4) seg = "0" + seg;  // 不足4位前面补0
            oss << seg;
        }
        return oss.str();
    };
    
    std::ostringstream json;
    json << "{"
         << "\"type\":\"fee_model\","  // BY ZF: 用于识别类型
         << "\"fee_model_id\":\"" << model.feeModelId << "\","
         << "\"time_num\":" << static_cast<int>(model.timeNum) << ","
         << "\"time_seg_text\":\"" << timeSegArray2text(model.timeSeg) << "\","
         << "\"seg_flag_text\":\"" << (model.segFlag.empty() ? "" : intArray2text(model.segFlag)) << "\","
         << "\"charge_fee_text\":\"" << intArray2text(model.chargeFee) << "\","
         << "\"service_fee_text\":\"" << intArray2text(model.serviceFee) << "\"}";
    return json.str();
}
