/**
 * 数据库管理类
 * 管理多个SQLite数据库：主运行数据库、充电记录数据库、计费模型数据库
 * BY ZF
 */

#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <sqlite3.h>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <cstdint>
#include "../base/common/logger_types.h"

// 数据库类型枚举
enum DatabaseType {
    DB_MAIN = 0,        // 主运行数据库
    DB_CHARGE = 1,       // 充电记录数据库
    DB_FEE = 2,         // 计费模型数据库
    DB_ERROR = 3        // 故障记录数据库
};

/**
 * 数据库管理类
 */
class DatabaseManager {
public:
    /**
     * 获取单例实例
     */
    static DatabaseManager& getInstance();
    
    /**
     * 初始化数据库
     * @param mainDbPath 主数据库路径
     * @param chargeDbPath 充电记录数据库路径
     * @param feeDbPath 计费模型数据库路径
     * @return true成功
     */
    bool initialize(const std::string& mainDbPath, 
                   const std::string& chargeDbPath,
                   const std::string& feeDbPath,
                   const std::string& errorDbPath);
    
    /**
     * 关闭所有数据库连接
     */
    void cleanup();
    
    // ========== 运行日志相关 ==========
    
    /**
     * 记录运行日志
     * @param level 日志级别
     * @param module 模块名称
     * @param message 日志消息
     * @param details 详细信息（可选）
     * @return true成功
     */
    bool logOperation(LogLevel level, const std::string& module, 
                     const std::string& message, const std::string& details = "");
    
    /**
     * BY ZF: 批量记录运行日志（使用事务批量提交，提高性能）
     * @param entries 日志条目列表，每个条目包含 (level, module, message, details)
     * @return true成功
     */
    bool logOperationsBatch(const std::vector<std::tuple<LogLevel, std::string, std::string, std::string>>& entries);
    
    // ========== 充电记录相关 ==========
    
    /**
     * 记录充电会话开始
     * @param sessionId 会话ID
     * @param gunId 枪号
     * @param userId 用户ID
     * @param startTime 开始时间
     * @return true成功
     */
    bool logChargeSessionStart(const std::string& sessionId, int gunId, 
                              const std::string& userId, const std::string& startTime);
    
    /**
     * 记录充电会话结束
     * @param sessionId 会话ID
     * @param endTime 结束时间
     * @param totalEnergy 总电量
     * @param totalCost 总费用
     * @return true成功
     */
    bool logChargeSessionEnd(const std::string& sessionId, const std::string& endTime,
                           double totalEnergy, double totalCost);
    
    /**
     * 记录充电交易
     * @param transactionId 交易ID
     * @param sessionId 会话ID
     * @param amount 金额
     * @param paymentMethod 支付方式
     * @return true成功
     */
    bool logChargeTransaction(const std::string& transactionId, 
                            const std::string& sessionId,
                            double amount, const std::string& paymentMethod);
    
    // 记录充电交易明细（参考 _evs_event_tradeInfo），数组字段以JSON字符串形式存储
    // 必填：tradeNo 建议唯一；可选：preTradeNo、vinCode、feeModelId、cardNumber
    /**
     * @brief 存储充电交易详细信息，所有时段/跨点字段均为逗号拼接数值文本
     * @param partElectText 时段电量，逗号分隔文本，长度= timeNum
     * @param chargeFeeText 时段电费，同上
     * @param serviceFeeText 时段服务费，同上
     * @param pointsElectText 跨越点电量，逗号分隔文本，长度= crossPoints
     * 其余参数见TradeRecord注释
     */
    bool logChargeTradeInfo(
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
        const std::string& partElectText,    // 逗号分隔电量
        const std::string& chargeFeeText,    // 逗号分隔电费
        const std::string& serviceFeeText,   // 逗号分隔服务费
        int startPoint,
        int crossPoints,
        const std::string& pointsElectText,  // 逗号分隔跨越点电量
        const std::string& cardNumber
    );

    /**
     * @brief 更新交易记录的平台确认标志。
     * @param tradeNo 设备交易流水号（唯一键）
     * @param confirmFlag 确认标志（0未确认，1已确认）
     * @return true 成功
     */
    bool updateTradeConfirmFlag(const std::string& tradeNo, int confirmFlag);

    /**
     * @brief 加载未确认交易记录（platform_confirm_flag=0）。
     * @param outRecords 输出记录列表
     * @param limit 最大条数（<=0 表示不限制）
     * @return true 成功
     */
    bool loadUnconfirmedTradeRecords(std::vector<TradeRecord>& outRecords, int limit = 100);

    // ========== 故障记录相关 ==========
    // BY ZF: 保存通过 MQTT 下发的故障记录事件。
    bool logFaultRecord(int gun,
                        const std::string& type,
                        const std::string& occurTime,
                        const std::string& pointKey,
                        const std::string& faultMessage,
                        unsigned int rawValue);
    
    // ========== 计费模型相关 ==========
    
    /**
     * 获取计费模型
     * @param modelId 模型ID
     * @param modelData 输出：模型数据
     * @return true成功
     */
    bool getFeeModel(const std::string& modelId, std::string& modelData);
    
    /**
     * 更新计费模型
     * @param modelId 模型ID
     * @param modelData 模型数据
     * @return true成功
     */
    bool updateFeeModel(const std::string& modelId, const std::string& modelData);
    
    // BY ZF: 计费模型详细表（tbFeeModel）读写接口
    /**
     * 保存计费模型到数据库（从共享内存结构 _evs_service_issue_feeModel 写入）
     * @param feeModelId 计费模型ID
     * @param timeNum 时段数（1-96）
     * @param timeSeg 时段开始时间点数组（每个4字符，如"0630"）
     * @param segFlag 时段标志数组（可选，如无则传nullptr）
     * @param chargeFee 电费数组（单位：0.001元）
     * @param serviceFee 服务费数组（单位：0.001元）
     * @return true成功
     */
    bool saveFeeModelToDb(const std::string& feeModelId,
                          unsigned char timeNum,
                          const std::vector<std::string>& timeSeg,
                          const std::vector<unsigned int>& segFlag,
                          const std::vector<unsigned int>& chargeFee,
                          const std::vector<unsigned int>& serviceFee);
    
    /**
     * 从数据库读取计费模型（读取到共享内存结构）
     * @param feeModelId 计费模型ID
     * @param outFeeModelId 输出：计费模型ID
     * @param outTimeNum 输出：时段数
     * @param outTimeSeg 输出：时段开始时间点数组
     * @param outSegFlag 输出：时段标志数组（可选，如不需要可传nullptr）
     * @param outChargeFee 输出：电费数组
     * @param outServiceFee 输出：服务费数组
     * @return true成功
     */
    bool loadFeeModelFromDb(const std::string& feeModelId,
                            std::string& outFeeModelId,
                            unsigned char& outTimeNum,
                            std::vector<std::string>& outTimeSeg,
                            std::vector<unsigned int>& outSegFlag,
                            std::vector<unsigned int>& outChargeFee,
                            std::vector<unsigned int>& outServiceFee);
    
    /**
     * BY ZF: 从数据库加载最新的计费模型（按 timestamp 降序，取第一条）
     * @param outFeeModelId 输出：计费模型编号
     * @param outTimeNum 输出：时段数
     * @param outTimeSeg 输出：时段开始时间点数组
     * @param outSegFlag 输出：时段标志数组
     * @param outChargeFee 输出：电费数组
     * @param outServiceFee 输出：服务费数组
     * @return true成功
     */
    bool loadLatestFeeModelFromDb(std::string& outFeeModelId,
                                  unsigned char& outTimeNum,
                                  std::vector<std::string>& outTimeSeg,
                                  std::vector<unsigned int>& outSegFlag,
                                  std::vector<unsigned int>& outChargeFee,
                                  std::vector<unsigned int>& outServiceFee);
    
    /**
     * BY ZF: 静态方法，直接从数据库文件加载最新的计费模型（供 LogSender 使用）
     * @param dbPath 数据库文件路径
     * @param outModel 输出：计费模型结构
     * @return true成功
     */
    static bool loadLatestFeeModelFromFile(const std::string& dbPath, FeeModel& outModel);
    
    /**
     * BY ZF: 加载最新计费模型并写入共享内存
     * @param dbPath 数据库文件路径
     * @param shm 共享内存指针
     * @return true成功
     */
    static bool loadFeeModelToShm(const std::string& dbPath, void* shm);
    
    // ========== 查询接口 ==========
    
    /**
     * 查询运行日志
     * @param startTime 开始时间
     * @param endTime 结束时间
     * @param level 日志级别（-1表示所有级别）
     * @param module 模块名称（空字符串表示所有模块）
     * @return 查询结果
     */
    std::string queryOperationLogs(const std::string& startTime, 
                                 const std::string& endTime,
                                 int level = -1, 
                                 const std::string& module = "");
    
    /**
     * 查询充电记录
     * @param startTime 开始时间
     * @param endTime 结束时间
     * @param gunId 枪号（-1表示所有枪）
     * @return 查询结果
     */
    std::string queryChargeRecords(const std::string& startTime,
                                 const std::string& endTime,
                                 int gunId = -1);
    
    /**
     * 备份数据库
     * @param backupDir 备份目录
     * @return true成功
     */
    bool backupDatabases(const std::string& backupDir);
    
    /**
     * BY ZF: 获取备份目录总大小（字节）
     * @param backupDir 备份目录
     * @return 总大小（字节），失败返回-1
     */
    static int64_t getBackupDirSize(const std::string& backupDir);
    
    /**
     * BY ZF: 清理旧备份文件，直到目录大小低于限制
     * @param backupDir 备份目录
     * @param maxSizeBytes 最大大小限制（字节）
     * @return 删除的文件数量
     */
    static int cleanupOldBackups(const std::string& backupDir, int64_t maxSizeBytes);
    
private:
    DatabaseManager() = default;
    ~DatabaseManager();
    
    // 禁用拷贝构造和赋值
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    
    /**
     * 获取数据库连接
     * @param dbType 数据库类型
     * @return 数据库连接
     */
    sqlite3* getConnection(DatabaseType dbType);
    
    /**
     * 创建主数据库表
     * @return true成功
     */
    bool createMainTables();
    
    /**
     * 创建充电记录数据库表
     * @return true成功
     */
    bool createChargeTables();
    
    /**
     * 创建计费模型数据库表
     * @return true成功
     */
    bool createFeeTables();
    bool createErrorTables();
    
    /**
     * 执行SQL语句
     * @param dbType 数据库类型
     * @param sql SQL语句
     * @return true成功
     */
    bool executeSQL(DatabaseType dbType, const std::string& sql);
    
    // BY ZF: 重新打开指定数据库文件并确保表结构
    bool reopenDatabase(DatabaseType dbType);
    
    // BY ZF: 备份单个数据库文件（剪切 -> 复制 -> 删除 -> 重新创建）
    bool backupSingleDatabase(DatabaseType dbType,
                              const std::string& backupDir,
                              const std::string& timestamp);
    
private:
    std::map<DatabaseType, sqlite3*> m_databases;
    std::map<DatabaseType, std::string> m_dbPaths;
    std::mutex m_mutex;
    bool m_initialized = false;
};

#endif // DATABASE_MANAGER_H
