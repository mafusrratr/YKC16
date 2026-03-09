/**
 * 数据库管理类实现
 * BY ZF
 */

#include "database_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <tuple>
#include "../../libv2gshm/libcshm/v2gshm.h"

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::~DatabaseManager() {
    cleanup();
}

// BY ZF: 确保目录存在的辅助函数
static bool ensureDirectoryExists(const std::string& filePath) {
    size_t lastSlash = filePath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return true; // 当前目录，无需创建
    }
    std::string dirPath = filePath.substr(0, lastSlash);
    if (dirPath.empty() || dirPath == "/") {
        return true; // 根目录，无需创建
    }
    // 检查目录是否存在
    struct stat st;
    if (stat(dirPath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true; // 目录已存在
        }
    }
    // 递归创建目录
    size_t pos = 0;
    while ((pos = dirPath.find('/', pos + 1)) != std::string::npos) {
        std::string subDir = dirPath.substr(0, pos);
        if (subDir.empty()) continue;
        if (stat(subDir.c_str(), &st) != 0) {
            if (mkdir(subDir.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "Failed to create directory: " << subDir << " (" << strerror(errno) << ")" << std::endl;
                return false;
            }
        }
    }
    // 创建最后一级目录
    if (stat(dirPath.c_str(), &st) != 0) {
        if (mkdir(dirPath.c_str(), 0755) != 0 && errno != EEXIST) {
            std::cerr << "Failed to create directory: " << dirPath << " (" << strerror(errno) << ")" << std::endl;
            return false;
        }
    }
    return true;
}

// BY ZF: SQL字符串转义函数（单引号转义为两个单引号）
static std::string escapeSqlString(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size() * 2);
    for (char c : str) {
        if (c == '\'') {
            escaped += "''"; // SQL中单引号需要转义为两个单引号
        } else {
            escaped += c;
        }
    }
    return escaped;
}

// BY ZF: 获取当前本地时间戳字符串（格式：YYYY-MM-DD HH:MM:SS）
static std::string getCurrentLocalTimeString() {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char timestampStr[32];
    std::strftime(timestampStr, sizeof(timestampStr), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(timestampStr);
}

// BY ZF: 解析逗号分隔浮点文本（如 "1.23,4.56"）
static std::vector<double> parseDoubleCsv(const char* text) {
    std::vector<double> out;
    if (!text || !text[0]) {
        return out;
    }
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        char* endptr = nullptr;
        const double v = std::strtod(token.c_str(), &endptr);
        if (endptr != token.c_str()) {
            out.push_back(v);
        }
    }
    return out;
}

// BY ZF: 文件复制的辅助函数
static bool copyFileBinary(const std::string& srcPath, const std::string& dstPath) {
    std::ifstream src(srcPath, std::ios::binary);
    std::ofstream dst(dstPath, std::ios::binary);
    if (!src || !dst) {
        std::cerr << "Failed to copy file from " << srcPath
                  << " to " << dstPath << std::endl;
        return false;
    }
    dst << src.rdbuf();
    return static_cast<bool>(dst);
}

bool DatabaseManager::initialize(const std::string& mainDbPath, 
                                const std::string& chargeDbPath,
                                const std::string& feeDbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        std::cout << "DatabaseManager already initialized" << std::endl;
        return true;
    }
    
    std::cout << "Initializing DatabaseManager..." << std::endl;
    
    // BY ZF: 显示数据库文件路径（便于调试）
    std::cout << "Main database: " << mainDbPath << std::endl;
    std::cout << "Charge database: " << chargeDbPath << std::endl;
    std::cout << "Fee database: " << feeDbPath << std::endl;
    
    // BY ZF: 确保数据库文件所在目录存在
    if (!ensureDirectoryExists(mainDbPath) || 
        !ensureDirectoryExists(chargeDbPath) || 
        !ensureDirectoryExists(feeDbPath)) {
        std::cerr << "Failed to create database directories" << std::endl;
        return false;
    }
    
    // 存储数据库路径
    m_dbPaths[DB_MAIN] = mainDbPath;
    m_dbPaths[DB_CHARGE] = chargeDbPath;
    m_dbPaths[DB_FEE] = feeDbPath;
    
    // 初始化各个数据库
    int result;
    
    // 主运行数据库
    result = sqlite3_open(mainDbPath.c_str(), &m_databases[DB_MAIN]);
    if (result != SQLITE_OK) {
        std::cout << "Failed to open main database: " << sqlite3_errmsg(m_databases[DB_MAIN]) << std::endl;
        return false;
    }
    
    // 充电记录数据库
    result = sqlite3_open(chargeDbPath.c_str(), &m_databases[DB_CHARGE]);
    if (result != SQLITE_OK) {
        std::cout << "Failed to open charge database: " << sqlite3_errmsg(m_databases[DB_CHARGE]) << std::endl;
        return false;
    }
    
    // 计费模型数据库
    result = sqlite3_open(feeDbPath.c_str(), &m_databases[DB_FEE]);
    if (result != SQLITE_OK) {
        std::cout << "Failed to open fee database: " << sqlite3_errmsg(m_databases[DB_FEE]) << std::endl;
        return false;
    }
    
    // 创建表结构
    if (!createMainTables() || !createChargeTables() || !createFeeTables()) {
        std::cout << "Failed to create database tables" << std::endl;
        cleanup();
        return false;
    }
    
    m_initialized = true;
    std::cout << "DatabaseManager initialized successfully" << std::endl;
    return true;
}

void DatabaseManager::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& pair : m_databases) {
        if (pair.second) {
            sqlite3_close(pair.second);
            pair.second = nullptr;
        }
    }
    
    m_initialized = false;
    std::cout << "DatabaseManager cleaned up" << std::endl;
}

bool DatabaseManager::createMainTables() {
    std::cout << "Creating main database tables..." << std::endl;
    
    // 运行日志表
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS operation_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            level INTEGER NOT NULL,
            module TEXT NOT NULL,
            message TEXT NOT NULL,
            details TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    if (!executeSQL(DB_MAIN, sql)) {
        return false;
    }
    
    // 创建索引
    if (!executeSQL(DB_MAIN, "CREATE INDEX IF NOT EXISTS idx_timestamp ON operation_logs(timestamp)")) {
        return false;
    }
    if (!executeSQL(DB_MAIN, "CREATE INDEX IF NOT EXISTS idx_level ON operation_logs(level)")) {
        return false;
    }
    if (!executeSQL(DB_MAIN, "CREATE INDEX IF NOT EXISTS idx_module ON operation_logs(module)")) {
        return false;
    }
    
    // BY ZF 性能监控日志表（SQLite不支持在表定义内创建INDEX，拆分为独立语句）
    sql = R"(
        CREATE TABLE IF NOT EXISTS performance_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            cpu_usage REAL,
            memory_usage REAL,
            disk_usage REAL,
            network_usage REAL
        )
    )";
    
    if (!executeSQL(DB_MAIN, sql)) {
        return false;
    }
    // BY ZF 独立创建索引
    if (!executeSQL(DB_MAIN, "CREATE INDEX IF NOT EXISTS idx_perf_timestamp ON performance_logs(timestamp)")) {
        return false;
    }
    
    std::cout << "Main database tables created successfully" << std::endl;
    return true;
}

bool DatabaseManager::createChargeTables() {
    std::cout << "Creating charge database tables..." << std::endl;

    // 仅保留充电交易明细表
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS charge_trade_info (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            gun_no INTEGER NOT NULL,
            pre_trade_no TEXT,
            trade_no TEXT UNIQUE NOT NULL,
            vin_code TEXT,
            time_div_type INTEGER,
            start_type INTEGER,
            charge_start_time INTEGER,
            charge_end_time INTEGER,
            start_soc REAL,
            end_soc REAL,
            reason INTEGER,
            fee_model_id TEXT,
            sum_start REAL,
            sum_end REAL,
            total_elect REAL,
            total_power_cost REAL,
            total_serv_cost REAL,
            total_cost REAL,
            time_num INTEGER,
            part_elect_text TEXT,
            charge_fee_text TEXT,
            service_fee_text TEXT,
            start_point INTEGER,
            cross_points INTEGER,
            points_elect_text TEXT,
            card_number TEXT,
            platform_confirm_flag INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";

    if (!executeSQL(DB_CHARGE, sql)) {
        return false;
    }

    if (!executeSQL(DB_CHARGE, "CREATE INDEX IF NOT EXISTS idx_trade_no ON charge_trade_info(trade_no)")) {
        return false;
    }
    if (!executeSQL(DB_CHARGE, "CREATE INDEX IF NOT EXISTS idx_pre_trade_no ON charge_trade_info(pre_trade_no)")) {
        return false;
    }
    if (!executeSQL(DB_CHARGE, "CREATE INDEX IF NOT EXISTS idx_gun_no ON charge_trade_info(gun_no)")) {
        return false;
    }
    // BY ZF: 兼容历史库，补齐平台确认标志字段（已存在时忽略）。
    sqlite3* chargeDb = getConnection(DB_CHARGE);
    if (chargeDb) {
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(
            chargeDb,
            "ALTER TABLE charge_trade_info ADD COLUMN platform_confirm_flag INTEGER DEFAULT 0",
            nullptr,
            nullptr,
            &errMsg
        );
        if (rc != SQLITE_OK) {
            std::string err = errMsg ? errMsg : "";
            if (errMsg) {
                sqlite3_free(errMsg);
            }
            if (err.find("duplicate column name") == std::string::npos) {
                std::cout << "Failed to ensure platform_confirm_flag column: " << err << std::endl;
                return false;
            }
        }
    }

    std::cout << "Charge database tables created successfully" << std::endl;
    return true;
}

bool DatabaseManager::createFeeTables() {
    std::cout << "Creating fee database tables..." << std::endl;
    
    // BY ZF: 计费模型详细表（tbFeeModel），对应共享内存结构 _evs_service_issue_feeModel
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS tbFeeModel (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            feeModelId TEXT UNIQUE NOT NULL,
            timeNum INTEGER NOT NULL,
            timeSeg TEXT NOT NULL,
            segFlag TEXT,
            chargeFee TEXT NOT NULL,
            serviceFee TEXT NOT NULL,
            timeStamp DATETIME NOT NULL
        )
    )";
    
    if (!executeSQL(DB_FEE, sql)) {
        return false;
    }
    
    // BY ZF: 创建索引
    if (!executeSQL(DB_FEE, "CREATE INDEX IF NOT EXISTS idx_tbFeeModel_id ON tbFeeModel(feeModelId)")) {
        return false;
    }
    if (!executeSQL(DB_FEE, "CREATE INDEX IF NOT EXISTS idx_tbFeeModel_timestamp ON tbFeeModel(timeStamp)")) {
        return false;
    }
    
    std::cout << "Fee database tables created successfully" << std::endl;
    return true;
}

bool DatabaseManager::executeSQL(DatabaseType dbType, const std::string& sql) {
    sqlite3* db = getConnection(dbType);
    if (!db) {
        return false;
    }
    
    char* errMsg = nullptr;
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (result != SQLITE_OK) {
        std::cout << "SQL execution failed: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

sqlite3* DatabaseManager::getConnection(DatabaseType dbType) {
    auto it = m_databases.find(dbType);
    if (it != m_databases.end()) {
        return it->second;
    }
    return nullptr;
}

bool DatabaseManager::logOperation(LogLevel level, const std::string& module, 
                                  const std::string& message, const std::string& details) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // BY ZF: 使用本地时间戳，避免时区问题
    std::string timestampStr = getCurrentLocalTimeString();
    
    // BY ZF: 转义字符串中的单引号，防止SQL注入
    std::string safeModule = escapeSqlString(module);
    std::string safeMessage = escapeSqlString(message);
    std::string safeDetails = escapeSqlString(details);
    
    std::stringstream sql;
    sql << "INSERT INTO operation_logs (timestamp, level, module, message, details, created_at) VALUES ('"
        << timestampStr << "', "
        << static_cast<int>(level) << ", '"
        << safeModule << "', '"
        << safeMessage << "', '"
        << safeDetails << "', '"
        << timestampStr << "')"; // BY ZF: created_at 也使用本地时间
    
    return executeSQL(DB_MAIN, sql.str());
}

// BY ZF: 批量记录运行日志（使用事务批量提交，显著提高性能）
bool DatabaseManager::logOperationsBatch(const std::vector<std::tuple<LogLevel, std::string, std::string, std::string>>& entries) {
    if (entries.empty()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    sqlite3* db = getConnection(DB_MAIN);
    if (!db) {
        return false;
    }
    
    // BY ZF: 使用本地时间戳，避免时区问题
    std::string timestampStr = getCurrentLocalTimeString();
    
    // BY ZF: 开始事务
    char* errMsg = nullptr;
    int result = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);
    if (result != SQLITE_OK) {
        std::cerr << "[DatabaseManager] Failed to begin transaction: " << (errMsg ? errMsg : "unknown") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    
    // BY ZF: 批量执行 INSERT 语句
    bool success = true;
    for (const auto& entry : entries) {
        LogLevel level = std::get<0>(entry);
        const std::string& module = std::get<1>(entry);
        const std::string& message = std::get<2>(entry);
        const std::string& details = std::get<3>(entry);
        
        // BY ZF: 转义字符串中的单引号，防止SQL注入
        std::string safeModule = escapeSqlString(module);
        std::string safeMessage = escapeSqlString(message);
        std::string safeDetails = escapeSqlString(details);
        
        std::stringstream sql;
        sql << "INSERT INTO operation_logs (timestamp, level, module, message, details, created_at) VALUES ('"
            << timestampStr << "', "
            << static_cast<int>(level) << ", '"
            << safeModule << "', '"
            << safeMessage << "', '"
            << safeDetails << "', '"
            << timestampStr << "')";
        
        result = sqlite3_exec(db, sql.str().c_str(), nullptr, nullptr, &errMsg);
        if (result != SQLITE_OK) {
            std::cerr << "[DatabaseManager] Failed to insert log in batch: " << (errMsg ? errMsg : "unknown") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
            success = false;
            break;
        }
    }
    
    // BY ZF: 提交或回滚事务
    if (success) {
        result = sqlite3_exec(db, "COMMIT", nullptr, nullptr, &errMsg);
        if (result != SQLITE_OK) {
            std::cerr << "[DatabaseManager] Failed to commit transaction: " << (errMsg ? errMsg : "unknown") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
            success = false;
        }
    } else {
        // BY ZF: 回滚事务
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    
    return success;
}

bool DatabaseManager::logChargeSessionStart(const std::string& sessionId, int gunId, 
                                          const std::string& userId, const std::string& startTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "INSERT INTO charge_sessions (session_id, gun_id, user_id, start_time) VALUES ('"
        << sessionId << "', "
        << gunId << ", '"
        << userId << "', '"
        << startTime << "')";
    
    return executeSQL(DB_CHARGE, sql.str());
}

bool DatabaseManager::logChargeSessionEnd(const std::string& sessionId, const std::string& endTime,
                                        double totalEnergy, double totalCost) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "UPDATE charge_sessions SET end_time='" << endTime 
        << "', total_energy=" << totalEnergy 
        << ", total_cost=" << totalCost 
        << ", status='completed' WHERE session_id='" << sessionId << "'";
    
    return executeSQL(DB_CHARGE, sql.str());
}

bool DatabaseManager::logChargeTransaction(const std::string& transactionId, 
                                        const std::string& sessionId,
                                        double amount, const std::string& paymentMethod) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "INSERT INTO charge_transactions (transaction_id, session_id, amount, payment_method) VALUES ('"
        << transactionId << "', '"
        << sessionId << "', "
        << amount << ", '"
        << paymentMethod << "')";
    
    return executeSQL(DB_CHARGE, sql.str());
}

bool DatabaseManager::logChargeTradeInfo(
        int gunNo,
        const std::string& preTradeNo,
        const std::string& tradeNo,
        const std::string& vinCode,
        int timeDivType,
        int startType,
        uint64_t chargeStartTime,
        uint64_t chargeEndTime,
        double startSoc,
        double endSoc,
        unsigned int reason,
        const std::string& feeModelId,
        double sumStart,
        double sumEnd,
        double totalElect,
        double totalPowerCost,
        double totalServCost,
        double totalCost,
        int timeNum,
        const std::string& partElectJson,
        const std::string& chargeFeeJson,
        const std::string& serviceFeeJson,
        int startPoint,
        int crossPoints,
        const std::string& pointsElectJson,
        const std::string& cardNumber)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // BY ZF: 使用本地时间戳，避免时区问题
    std::string createdTimeStr = getCurrentLocalTimeString();
    
    // BY ZF: 转义所有字符串字段，防止SQL注入
    std::string safePreTradeNo = escapeSqlString(preTradeNo);
    std::string safeTradeNo = escapeSqlString(tradeNo);
    std::string safeVinCode = escapeSqlString(vinCode);
    std::string safeFeeModelId = escapeSqlString(feeModelId);
    std::string safePartElect = escapeSqlString(partElectJson);
    std::string safeChargeFee = escapeSqlString(chargeFeeJson);
    std::string safeServiceFee = escapeSqlString(serviceFeeJson);
    std::string safePointsElect = escapeSqlString(pointsElectJson);
    std::string safeCardNumber = escapeSqlString(cardNumber);

    std::stringstream sql;
    sql << "INSERT INTO charge_trade_info ("
        << "gun_no, pre_trade_no, trade_no, vin_code, time_div_type, start_type, "
        << "charge_start_time, charge_end_time, start_soc, end_soc, reason, fee_model_id, "
        << "sum_start, sum_end, total_elect, total_power_cost, total_serv_cost, total_cost, time_num, "
        << "part_elect_text, charge_fee_text, service_fee_text, start_point, cross_points, points_elect_text, card_number, created_at"
        << ") VALUES ("
        << gunNo << ", '"
        << safePreTradeNo << "', '"
        << safeTradeNo << "', '"
        << safeVinCode << "', "
        << timeDivType << ", "
        << startType << ", "
        << chargeStartTime << ", "
        << chargeEndTime << ", "
        << startSoc << ", "
        << endSoc << ", "
        << reason << ", '"
        << safeFeeModelId << "', "
        << sumStart << ", "
        << sumEnd << ", "
        << totalElect << ", "
        << totalPowerCost << ", "
        << totalServCost << ", "
        << totalCost << ", "
        << timeNum << ", '"
        << safePartElect << "', '"
        << safeChargeFee << "', '"
        << safeServiceFee << "', "
        << startPoint << ", "
        << crossPoints << ", '"
        << safePointsElect << "', '"
        << safeCardNumber << "', '"
        << createdTimeStr << "'" // BY ZF: created_at 使用本地时间
        << ")";

    return executeSQL(DB_CHARGE, sql.str());
}

bool DatabaseManager::updateTradeConfirmFlag(const std::string& tradeNo, int confirmFlag)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string safeTradeNo = escapeSqlString(tradeNo);
    std::stringstream sql;
    sql << "UPDATE charge_trade_info SET platform_confirm_flag=" << confirmFlag
        << " WHERE trade_no='" << safeTradeNo << "'";
    return executeSQL(DB_CHARGE, sql.str());
}

bool DatabaseManager::loadUnconfirmedTradeRecords(std::vector<TradeRecord>& outRecords, int limit)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    outRecords.clear();

    sqlite3* db = getConnection(DB_CHARGE);
    if (!db) {
        return false;
    }

    std::stringstream sql;
    sql << "SELECT "
        << "gun_no, pre_trade_no, trade_no, vin_code, time_div_type, start_type, "
        << "charge_start_time, charge_end_time, start_soc, end_soc, reason, fee_model_id, "
        << "sum_start, sum_end, total_elect, total_power_cost, total_serv_cost, total_cost, "
        << "time_num, part_elect_text, charge_fee_text, service_fee_text, "
        << "start_point, cross_points, points_elect_text, card_number "
        << "FROM charge_trade_info WHERE platform_confirm_flag=0 "
        << "ORDER BY id ASC";
    if (limit > 0) {
        sql << " LIMIT " << limit;
    }

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare unconfirmed query: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TradeRecord rec;
        rec.gunNo = sqlite3_column_int(stmt, 0);
        const unsigned char* s = sqlite3_column_text(stmt, 1);
        rec.preTradeNo = s ? reinterpret_cast<const char*>(s) : "";
        s = sqlite3_column_text(stmt, 2);
        rec.tradeNo = s ? reinterpret_cast<const char*>(s) : "";
        s = sqlite3_column_text(stmt, 3);
        rec.vinCode = s ? reinterpret_cast<const char*>(s) : "";
        rec.timeDivType = sqlite3_column_int(stmt, 4);
        rec.startType = sqlite3_column_int(stmt, 5);
        rec.chargeStartTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
        rec.chargeEndTime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
        rec.startSoc = sqlite3_column_double(stmt, 8);
        rec.endSoc = sqlite3_column_double(stmt, 9);
        rec.reason = static_cast<unsigned int>(sqlite3_column_int(stmt, 10));
        s = sqlite3_column_text(stmt, 11);
        rec.feeModelId = s ? reinterpret_cast<const char*>(s) : "";
        rec.sumStart = sqlite3_column_double(stmt, 12);
        rec.sumEnd = sqlite3_column_double(stmt, 13);
        rec.totalElect = sqlite3_column_double(stmt, 14);
        rec.totalPowerCost = sqlite3_column_double(stmt, 15);
        rec.totalServCost = sqlite3_column_double(stmt, 16);
        rec.totalCost = sqlite3_column_double(stmt, 17);
        rec.timeNum = sqlite3_column_int(stmt, 18);
        rec.startPoint = sqlite3_column_int(stmt, 22);
        rec.crossPoints = sqlite3_column_int(stmt, 23);
        s = sqlite3_column_text(stmt, 25);
        rec.cardNumber = s ? reinterpret_cast<const char*>(s) : "";

        rec.partElect = parseDoubleCsv(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19)));
        rec.chargeFee = parseDoubleCsv(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 20)));
        rec.serviceFee = parseDoubleCsv(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 21)));
        rec.pointsElect = parseDoubleCsv(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 24)));
        outRecords.push_back(rec);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::getFeeModel(const std::string& modelId, std::string& modelData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    sqlite3* db = getConnection(DB_FEE);
    if (!db) {
        return false;
    }
    
    std::stringstream sql;
    sql << "SELECT model_data FROM fee_models WHERE model_id='" << modelId << "' AND is_active=1";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    
    if (result != SQLITE_OK) {
        std::cout << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (data) {
            modelData = data;
            found = true;
        }
    }
    
    sqlite3_finalize(stmt);
    return found;
}

bool DatabaseManager::updateFeeModel(const std::string& modelId, const std::string& modelData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "INSERT OR REPLACE INTO fee_models (model_id, model_name, model_data, version, updated_time) VALUES ('"
        << modelId << "', '"
        << modelId << "', '"
        << modelData << "', '1.0', datetime('now'))";
    
    return executeSQL(DB_FEE, sql.str());
}

// BY ZF: 重新打开指定数据库并确保表结构
bool DatabaseManager::reopenDatabase(DatabaseType dbType) {
    auto pathIt = m_dbPaths.find(dbType);
    if (pathIt == m_dbPaths.end()) {
        std::cerr << "Database path not found for type: " << dbType << std::endl;
        return false;
    }
    
    sqlite3* newDb = nullptr;
    if (sqlite3_open(pathIt->second.c_str(), &newDb) != SQLITE_OK) {
        std::cerr << "Failed to reopen database: " << pathIt->second
                  << " (" << sqlite3_errmsg(newDb) << ")" << std::endl;
        if (newDb) {
            sqlite3_close(newDb);
        }
        return false;
    }
    
    m_databases[dbType] = newDb;
    
    bool tablesOk = false;
    switch (dbType) {
        case DB_MAIN:   tablesOk = createMainTables(); break;
        case DB_CHARGE: tablesOk = createChargeTables(); break;
        case DB_FEE:    tablesOk = createFeeTables(); break;
        default:        tablesOk = true; break;
    }
    
    if (!tablesOk) {
        std::cerr << "Failed to create tables after reopening database type: "
                  << dbType << std::endl;
        return false;
    }
    return true;
}

// BY ZF: 单库备份与轮转实现
bool DatabaseManager::backupSingleDatabase(DatabaseType dbType,
                                           const std::string& backupDir,
                                           const std::string& timestamp)
{
    auto pathIt = m_dbPaths.find(dbType);
    if (pathIt == m_dbPaths.end()) {
        std::cerr << "Database path not configured for type: " << dbType << std::endl;
        return false;
    }
    
    std::string srcPath = pathIt->second;
    std::string prefix = (dbType == DB_MAIN) ? "tcu"
                        : (dbType == DB_CHARGE) ? "chargerecords"
                        : "feemodel";
    std::string backupFile = backupDir + "/" + prefix + "_" + timestamp + ".db";
    
    if (!ensureDirectoryExists(backupFile)) {
        std::cerr << "Failed to prepare backup directory for " << backupFile << std::endl;
        return false;
    }
    
    // BY ZF: 关闭旧连接，防止文件被占用
    auto dbIt = m_databases.find(dbType);
    if (dbIt != m_databases.end() && dbIt->second) {
        sqlite3_close(dbIt->second);
        dbIt->second = nullptr;
    }
    
    bool moved = (std::rename(srcPath.c_str(), backupFile.c_str()) == 0);
    if (!moved) {
        std::cerr << "Rename failed, fallback to copy for " << srcPath << std::endl;
        if (!copyFileBinary(srcPath, backupFile)) {
            return false;
        }
        if (std::remove(srcPath.c_str()) != 0) {
            std::cerr << "Warning: failed to remove original db after copy: "
                      << srcPath << std::endl;
        }
    }
    
    // BY ZF: 重新创建新的数据库文件用于后续存储
    if (!reopenDatabase(dbType)) {
        std::cerr << "Failed to reopen database after backup: " << srcPath << std::endl;
        return false;
    }
    
    std::cout << "Database rotated: " << srcPath << " -> " << backupFile << std::endl;
    return true;
}

// BY ZF: 保存计费模型到数据库
bool DatabaseManager::saveFeeModelToDb(const std::string& feeModelId,
                                        unsigned char timeNum,
                                        const std::vector<std::string>& timeSeg,
                                        const std::vector<unsigned int>& segFlag,
                                        const std::vector<unsigned int>& chargeFee,
                                        const std::vector<unsigned int>& serviceFee) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // BY ZF: 将数组转换为分号分隔的字符串
    auto intArray2text = [](const std::vector<unsigned int>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';  // 使用分号分隔
            oss << arr[i];
        }
        return oss.str();
    };
    
    // BY ZF: timeSeg 格式为4位数字（HHMM，如 "0630" 表示6点30分），用分号分隔
    auto timeSegArray2text = [](const std::vector<std::string>& arr) -> std::string {
        std::ostringstream oss;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) oss << ';';  // 使用分号分隔
            // 只取前4个字符（HHMM格式）
            std::string seg = arr[i].substr(0, 4);
            oss << seg;
        }
        return oss.str();
    };
    
    std::string timeSegText = timeSegArray2text(timeSeg);
    std::string chargeFeeText = intArray2text(chargeFee);
    std::string serviceFeeText = intArray2text(serviceFee);
    std::string segFlagText = "";
    if (!segFlag.empty()) {
        segFlagText = intArray2text(segFlag);
    }
    
    // BY ZF: 使用本地时间戳
    std::string timestampStr = getCurrentLocalTimeString();
    
    // BY ZF: 转义字符串，防止SQL注入
    std::string safeFeeModelId = escapeSqlString(feeModelId);
    std::string safeTimeSeg = escapeSqlString(timeSegText);
    std::string safeSegFlag = escapeSqlString(segFlagText);
    std::string safeChargeFee = escapeSqlString(chargeFeeText);
    std::string safeServiceFee = escapeSqlString(serviceFeeText);
    
    std::stringstream sql;
    sql << "INSERT OR REPLACE INTO tbFeeModel (feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee, timeStamp) VALUES ('"
        << safeFeeModelId << "', "
        << static_cast<int>(timeNum) << ", '"
        << safeTimeSeg << "', "
        << (!segFlag.empty() ? "'" + safeSegFlag + "'" : "NULL") << ", '"
        << safeChargeFee << "', '"
        << safeServiceFee << "', '"
        << timestampStr << "')";
    
    return executeSQL(DB_FEE, sql.str());
}

// BY ZF: 从数据库读取计费模型
bool DatabaseManager::loadFeeModelFromDb(const std::string& feeModelId,
                                        std::string& outFeeModelId,
                                        unsigned char& outTimeNum,
                                        std::vector<std::string>& outTimeSeg,
                                        std::vector<unsigned int>& outSegFlag,
                                        std::vector<unsigned int>& outChargeFee,
                                        std::vector<unsigned int>& outServiceFee) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    sqlite3* db = getConnection(DB_FEE);
    if (!db) {
        return false;
    }
    
    std::string safeFeeModelId = escapeSqlString(feeModelId);
    std::stringstream sql;
    sql << "SELECT feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee FROM tbFeeModel WHERE feeModelId='"
        << safeFeeModelId << "' ORDER BY id DESC LIMIT 1";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // BY ZF: 解析分号分隔的字符串数组
        auto text2intArray = [](const char* text, std::vector<unsigned int>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {  // 使用分号分隔
                if (!token.empty()) {
                    arr.push_back(static_cast<unsigned int>(std::stoul(token)));
                }
            }
        };
        
        // BY ZF: timeSeg 格式为4位数字（HHMM），用分号分隔
        auto text2timeSegArray = [](const char* text, std::vector<std::string>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {  // 使用分号分隔
                if (!token.empty()) {
                    // 确保长度为4（HHMM格式）
                    std::string seg = token.substr(0, 4);
                    while (seg.size() < 4) seg = "0" + seg;  // 不足4位前面补0
                    arr.push_back(seg);
                }
            }
        };
        
        // 解析feeModelId
        const char* modelId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (modelId) {
            outFeeModelId = modelId;
        }
        
        // 解析timeNum
        outTimeNum = static_cast<unsigned char>(sqlite3_column_int(stmt, 1));
        
        // 解析timeSeg
        const char* timeSegText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (timeSegText) {
            text2timeSegArray(timeSegText, outTimeSeg);
        }
        
        // 解析segFlag（可选）
        const char* segFlagText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (segFlagText) {
            text2intArray(segFlagText, outSegFlag);
        }
        
        // 解析chargeFee
        const char* chargeFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (chargeFeeText) {
            text2intArray(chargeFeeText, outChargeFee);
        }
        
        // 解析serviceFee
        const char* serviceFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (serviceFeeText) {
            text2intArray(serviceFeeText, outServiceFee);
        }
        
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}

bool DatabaseManager::loadLatestFeeModelFromDb(std::string& outFeeModelId,
                                               unsigned char& outTimeNum,
                                               std::vector<std::string>& outTimeSeg,
                                               std::vector<unsigned int>& outSegFlag,
                                               std::vector<unsigned int>& outChargeFee,
                                               std::vector<unsigned int>& outServiceFee) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    sqlite3* db = getConnection(DB_FEE);
    if (!db) {
        return false;
    }
    
    // BY ZF: 按 timestamp 降序排序，取最新的一条
    std::stringstream sql;
    sql << "SELECT feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee FROM tbFeeModel ORDER BY timestamp DESC LIMIT 1";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // BY ZF: 使用与 loadFeeModelFromDb 相同的解析逻辑
        auto text2intArray = [](const char* text, std::vector<unsigned int>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    try {
                        arr.push_back(static_cast<unsigned int>(std::stoul(token)));
                    } catch (...) {
                        // 忽略无效数字
                    }
                }
            }
        };
        
        auto text2timeSegArray = [](const char* text, std::vector<std::string>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    std::string seg = token.substr(0, 4);
                    while (seg.size() < 4) seg = "0" + seg;
                    arr.push_back(seg);
                }
            }
        };
        
        const char* modelId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (modelId) {
            outFeeModelId = modelId;
        }
        
        outTimeNum = static_cast<unsigned char>(sqlite3_column_int(stmt, 1));
        
        const char* timeSegText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (timeSegText) {
            text2timeSegArray(timeSegText, outTimeSeg);
        }
        
        const char* segFlagText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (segFlagText) {
            text2intArray(segFlagText, outSegFlag);
        }
        
        const char* chargeFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (chargeFeeText) {
            text2intArray(chargeFeeText, outChargeFee);
        }
        
        const char* serviceFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (serviceFeeText) {
            text2intArray(serviceFeeText, outServiceFee);
        }
        
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}

bool DatabaseManager::loadLatestFeeModelFromFile(const std::string& dbPath, FeeModel& outModel) {
    // BY ZF: 直接打开数据库文件进行读取
    sqlite3* db = nullptr;
    int result = sqlite3_open(dbPath.c_str(), &db);
    if (result != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }
    
    // BY ZF: 按 timestamp 降序排序，取最新的一条
    const char* sql = "SELECT feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee FROM tbFeeModel ORDER BY timestamp DESC LIMIT 1";
    
    sqlite3_stmt* stmt;
    result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // BY ZF: 解析逻辑与 loadLatestFeeModelFromDb 相同
        auto text2intArray = [](const char* text, std::vector<unsigned int>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    try {
                        arr.push_back(static_cast<unsigned int>(std::stoul(token)));
                    } catch (...) {
                        // 忽略无效数字
                    }
                }
            }
        };
        
        auto text2timeSegArray = [](const char* text, std::vector<std::string>& arr) {
            if (!text) return;
            arr.clear();
            std::string s(text);
            std::istringstream iss(s);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    std::string seg = token.substr(0, 4);
                    while (seg.size() < 4) seg = "0" + seg;
                    arr.push_back(seg);
                }
            }
        };
        
        const char* modelId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (modelId) {
            outModel.feeModelId = modelId;
        }
        
        outModel.timeNum = static_cast<unsigned char>(sqlite3_column_int(stmt, 1));
        
        const char* timeSegText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (timeSegText) {
            text2timeSegArray(timeSegText, outModel.timeSeg);
        }
        
        const char* segFlagText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (segFlagText) {
            text2intArray(segFlagText, outModel.segFlag);
        }
        
        const char* chargeFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (chargeFeeText) {
            text2intArray(chargeFeeText, outModel.chargeFee);
        }
        
        const char* serviceFeeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (serviceFeeText) {
            text2intArray(serviceFeeText, outModel.serviceFee);
        }
        
        found = true;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

bool DatabaseManager::loadFeeModelToShm(const std::string& dbPath, void* shm) {
    // BY ZF: 加载计费模型
    FeeModel model;
    if (!loadLatestFeeModelFromFile(dbPath, model)) {
        std::cerr << "Failed to load fee model from database" << std::endl;
        return false;
    }
    
    // BY ZF: 转换为共享内存结构并写入
    if (shm == nullptr) {
        std::cerr << "Invalid shared memory pointer" << std::endl;
        return false;
    }
    
    // BY ZF: 获取共享内存中的计费模型指针（需要包含 v2gshm.h）
    // 这里假设 shm 是 CShm* 类型
    CShm* cshm = static_cast<CShm*>(shm);
    _evs_service_issue_feeModel* shmModel = cshm->evs_getFeeModel();
    
    if (shmModel == nullptr) {
        std::cerr << "Failed to get fee model pointer from shared memory" << std::endl;
        return false;
    }
    
    // BY ZF: 清空共享内存数据
    memset(shmModel, 0, sizeof(_evs_service_issue_feeModel));
    
    // BY ZF: 复制 feeModelId（最多16字节 + '\0'）
    strncpy(shmModel->feeModelId, model.feeModelId.c_str(), EVS_MAX_MODEL_ID_LEN - 1);
    shmModel->feeModelId[EVS_MAX_MODEL_ID_LEN - 1] = '\0';
    
    // BY ZF: 复制 timeNum
    shmModel->timeNum = model.timeNum;
    
    // BY ZF: 复制 timeSeg（每个时段5字节，格式："HHMM\0"）
    for (size_t i = 0; i < model.timeSeg.size() && i < EVS_MAX_MODEL_DEVSEG; ++i) {
        strncpy(shmModel->timeSeg[i], model.timeSeg[i].c_str(), 4);
        shmModel->timeSeg[i][4] = '\0';
    }
    
    // BY ZF: 复制 chargeFee
    for (size_t i = 0; i < model.chargeFee.size() && i < EVS_MAX_MODEL_DEVSEG; ++i) {
        shmModel->chargeFee[i] = model.chargeFee[i];
    }
    
    // BY ZF: 复制 serviceFee
    for (size_t i = 0; i < model.serviceFee.size() && i < EVS_MAX_MODEL_DEVSEG; ++i) {
        shmModel->serviceFee[i] = model.serviceFee[i];
    }
    
    std::cout << "Successfully loaded and wrote fee model to shared memory: " 
              << model.feeModelId << ", timeNum=" << (int)model.timeNum << std::endl;
    
    return true;
}

std::string DatabaseManager::queryOperationLogs(const std::string& startTime, 
                                               const std::string& endTime,
                                               int level, 
                                               const std::string& module) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "SELECT timestamp, level, module, message, details FROM operation_logs WHERE 1=1";
    
    if (!startTime.empty()) {
        sql << " AND timestamp >= '" << startTime << "'";
    }
    if (!endTime.empty()) {
        sql << " AND timestamp <= '" << endTime << "'";
    }
    if (level >= 0) {
        sql << " AND level = " << level;
    }
    if (!module.empty()) {
        sql << " AND module = '" << module << "'";
    }
    
    sql << " ORDER BY timestamp DESC LIMIT 1000";
    
    // 这里简化实现，实际应该返回JSON格式的查询结果
    return sql.str();
}

std::string DatabaseManager::queryChargeRecords(const std::string& startTime,
                                              const std::string& endTime,
                                              int gunId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::stringstream sql;
    sql << "SELECT session_id, gun_id, user_id, start_time, end_time, total_energy, total_cost FROM charge_sessions WHERE 1=1";
    
    if (!startTime.empty()) {
        sql << " AND start_time >= '" << startTime << "'";
    }
    if (!endTime.empty()) {
        sql << " AND start_time <= '" << endTime << "'";
    }
    if (gunId >= 0) {
        sql << " AND gun_id = " << gunId;
    }
    
    sql << " ORDER BY start_time DESC LIMIT 1000";
    
    return sql.str();
}

bool DatabaseManager::backupDatabases(const std::string& backupDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 获取当前时间作为备份文件名
    // BY ZF: 使用 strftime 替代 put_time，兼容 C++0x
    std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    
    if (!ensureDirectoryExists(backupDir + "/placeholder")) {
        std::cerr << "Failed to ensure backup directory exists: " << backupDir << std::endl;
        return false;
    }
    
    // BY ZF: 针对每个数据库执行剪切->复制->删除->重建的轮转流程
    for (const auto& pair : m_dbPaths) {
        if (!backupSingleDatabase(pair.first, backupDir, timestamp)) {
            std::cerr << "Backup failed for database type: " << pair.first << std::endl;
            return false;
        }
    }
    
    std::cout << "[Logger][Backup] Databases rotated to " << backupDir
              << " (timestamp=" << timestamp << ")" << std::endl;
    return true;
}

// BY ZF: 获取备份目录总大小（字节）
int64_t DatabaseManager::getBackupDirSize(const std::string& backupDir) {
    int64_t totalSize = 0;
    DIR* dir = opendir(backupDir.c_str());
    if (!dir) {
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string fileName = entry->d_name;
        // BY ZF: 只统计.db文件
        if (fileName.size() < 4 || fileName.find(".db") == std::string::npos) {
            continue;
        }
        
        std::string fullPath = backupDir + "/" + fileName;
        struct stat st{};
        if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            totalSize += st.st_size;
        }
    }
    closedir(dir);
    return totalSize;
}

// BY ZF: 清理旧备份文件，直到目录大小低于限制
int DatabaseManager::cleanupOldBackups(const std::string& backupDir, int64_t maxSizeBytes) {
    if (maxSizeBytes <= 0) {
        return 0; // BY ZF: 未设置大小限制，不清理
    }
    
    // BY ZF: 收集所有备份文件及其信息
    struct BackupFileInfo {
        std::string path;
        std::string name;
        int64_t size;
        time_t mtime;
    };
    
    std::vector<BackupFileInfo> backupFiles;
    DIR* dir = opendir(backupDir.c_str());
    if (!dir) {
        return 0;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string fileName = entry->d_name;
        // BY ZF: 只处理.db文件
        if (fileName.size() < 4 || fileName.find(".db") == std::string::npos) {
            continue;
        }
        
        std::string fullPath = backupDir + "/" + fileName;
        struct stat st{};
        if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            BackupFileInfo info;
            info.path = fullPath;
            info.name = fileName;
            info.size = st.st_size;
            info.mtime = st.st_mtime;
            backupFiles.push_back(info);
        }
    }
    closedir(dir);
    
    if (backupFiles.empty()) {
        return 0;
    }
    
    // BY ZF: 按修改时间排序，最旧的在前
    std::sort(backupFiles.begin(), backupFiles.end(), 
              [](const BackupFileInfo& a, const BackupFileInfo& b) {
                  return a.mtime < b.mtime;
              });
    
    // BY ZF: 计算当前总大小
    int64_t currentSize = 0;
    for (const auto& file : backupFiles) {
        currentSize += file.size;
    }
    
    std::cout << "[Logger][Backup] Found " << backupFiles.size() 
              << " backup file(s), total size: " << currentSize 
              << " bytes (" << (currentSize / 1024 / 1024) << " MB), "
              << "limit: " << maxSizeBytes 
              << " bytes (" << (maxSizeBytes / 1024 / 1024) << " MB)" << std::endl;
    
    // BY ZF: 如果总大小未超过限制，不需要清理
    if (currentSize <= maxSizeBytes) {
        std::cout << "[Logger][Backup] Backup size is within limit, no cleanup needed" << std::endl;
        return 0;
    }
    
    std::cout << "[Logger][Backup] Backup size exceeds limit by " 
              << (currentSize - maxSizeBytes) << " bytes, starting cleanup..." << std::endl;
    
    // BY ZF: 删除最旧的文件，直到总大小低于限制
    int deletedCount = 0;
    for (const auto& file : backupFiles) {
        if (currentSize <= maxSizeBytes) {
            break;
        }
        
        if (unlink(file.path.c_str()) == 0) {
            currentSize -= file.size;
            deletedCount++;
            std::cout << "[Logger][Backup] Deleted old backup: " << file.name 
                      << " (size: " << file.size << " bytes)" << std::endl;
        } else {
            std::cerr << "[Logger][Backup] Failed to delete backup: " << file.name 
                      << ", errno=" << errno << std::endl;
        }
    }
    
    if (deletedCount > 0) {
        std::cout << "[Logger][Backup] Cleaned up " << deletedCount 
                  << " old backup(s), current size: " << currentSize 
                  << " bytes (limit: " << maxSizeBytes << " bytes)" << std::endl;
    }
    
    return deletedCount;
}
