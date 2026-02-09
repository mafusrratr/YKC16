// BY ZF: 日志发送器实现（复制自 core/base/log_sender.cpp，供独立打包使用）
#include "log_sender.h"
#include <iostream>
#include <sstream>
#include <iomanip>

LogSender::LogSender(const std::string& moduleName)
    : m_moduleName(moduleName)
    , m_running(true)
    , m_logQueue(MSG_KEY_LOG) {
    if (!m_logQueue.open()) {
        std::cerr << "LogSender: Failed to open log message queue" << std::endl;
    }
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

void LogSender::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.empty()) {
        return;
    }
    for (const auto& entry : m_buffer) {
        std::string jsonData = logEntryToJson(entry);
        if (!sendToQueue(jsonData)) {
            std::cerr << "LogSender: Failed to send log entry" << std::endl;
        }
    }
    m_buffer.clear();
}

void LogSender::stop() {
    if (m_running.load()) {
        m_running = false;
        flush();
        if (m_flushThread.joinable()) {
            m_flushThread.join();
        }
    }
}

void LogSender::addLog(LogLevel level, const std::string& message, const std::string& details) {
    std::lock_guard<std::mutex> lock(m_mutex);
    LogEntry entry(level, m_moduleName, message, details);
    auto now = std::chrono::system_clock::now();
    entry.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
    );
    m_buffer.push_back(entry);
    if (m_buffer.size() >= MAX_BUFFER_SIZE) {
        flush();
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
            flush();
        }
    }
}

bool LogSender::sendToQueue(const std::string& jsonData) {
    return m_logQueue.send(MSG_TYPE_LOG, jsonData.c_str(), static_cast<int>(jsonData.length())) == 0;
}

void LogSender::logTradeRecord(const TradeRecord& rec) {
    std::string json = tradeRecordToJson(rec);
    sendToQueue(json);
}

void LogSender::saveFeeModel(const FeeModel& model) {
    std::string json = feeModelToJson(model);
    sendToQueue(json);
}

std::string LogSender::tradeRecordToJson(const TradeRecord& rec) {
    auto vec2text = [](const std::vector<unsigned int>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ',';
            oss << arr[i];
        }
        return oss.str();
    };

    std::ostringstream json;
    json << "{"
         << "\"type\":\"trade_record\","
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
    auto intArray2text = [](const std::vector<unsigned int>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';
            oss << arr[i];
        }
        return oss.str();
    };

    auto timeSegArray2text = [](const std::vector<std::string>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';
            std::string seg = arr[i].substr(0, 4);
            while (seg.size() < 4) seg = "0" + seg;
            oss << seg;
        }
        return oss.str();
    };

    std::ostringstream json;
    json << "{"
         << "\"type\":\"fee_model\","
         << "\"fee_model_id\":\"" << model.feeModelId << "\","
         << "\"time_num\":" << static_cast<int>(model.timeNum) << ","
         << "\"time_seg_text\":\"" << timeSegArray2text(model.timeSeg) << "\","
         << "\"seg_flag_text\":\"" << (model.segFlag.empty() ? "" : intArray2text(model.segFlag)) << "\","
         << "\"charge_fee_text\":\"" << intArray2text(model.chargeFee) << "\","
         << "\"service_fee_text\":\"" << intArray2text(model.serviceFee) << "\"}";
    return json.str();
}


