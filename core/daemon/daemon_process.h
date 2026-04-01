/**
 * 守护进程（Daemon Process）
 * 负责启动、监控、管理所有业务进程
 * BY ZF
 */

#ifndef DAEMON_PROCESS_H
#define DAEMON_PROCESS_H

#include "../interfaces/iprocess.h"
#include "../base/common/config_manager_lite.h"
#include "../base/common/message_queue.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <map>

/**
 * 守护进程
 * 统一管理所有业务进程的生命周期
 */
class DaemonProcess : public IProcess {
public:
    DaemonProcess();
    ~DaemonProcess();
    
    // IProcess接口实现
    ProcessType getType() override { return PROC_DAEMON; }
    const char* getName() override { return "DaemonProcess"; }
    bool initialize(const char* config) override;
    int start() override;
    void stop() override;
    void run() override;
    ProcessStatus getStatus() override { return m_status.load(); }
    
    /**
     * 从配置文件加载进程列表
     */
    bool loadProcessListFromConfig();
    
    /**
     * 添加业务进程（可执行文件路径和参数）
     */
    void addBusinessProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args = std::vector<std::string>());
    
    /**
     * 启动所有业务进程
     */
    bool startAllProcesses();
    
    /**
     * 停止所有业务进程
     */
    void stopAllProcesses();
    
    /**
     * 获取进程状态报告
     */
    std::string getProcessStatusReport() const;
    
    /**
     * 固件升级功能
     */
    bool upgradeFirmware(const std::string& upgradePath);
    
    /**
     * 重新加载配置
     */
    bool reloadConfig();
    
    /**
     * 处理看门狗喂狗消息
     */
    void handleWatchdogMessage();
    
    /**
     * BY ZF: 处理更新请求消息（在主循环doRun中调用）
     */
    void handleUpdateRequest();
    
    /**
     * BY ZF: 执行更新管理流程（停止所有进程并启动更新处理进程）
     * @param updatePackagePath 更新包所在路径（从消息中获取）
     * @return true成功
     */
    bool performUpdate(const std::string& updatePackagePath);
    
    /**
     * BY ZF: 启动更新处理进程（执行更新：update_handler -p <path>）
     * @param updatePackagePath 更新包所在路径（作为参数传递给更新器）
     * @return true成功
     */
    bool startUpdateProcess(const std::string& updatePackagePath);
    
    /**
     * BY ZF: 启动更新处理进程（执行回滚：update_handler -r）
     * @return true成功
     */
    bool startRollbackProcess();
    
    /**
     * BY ZF: 执行回滚操作（调用 update_handler -r）
     * @return true成功
     */
    bool performRollback();
    
    /**
     * BY ZF: 记录更新状态到持久化文件
     * @param status 更新状态（UPDATING/UPDATED/DONE）
     * @param updatePackagePath 更新包路径（可选）
     */
    void recordUpdateStatus(const std::string& status, const std::string& updatePackagePath = "");
    
    /**
     * BY ZF: 读取更新状态
     * @param status 输出：更新状态（UPDATING/UPDATED/DONE）
     * @param lastUpdatePackagePath 输出：最后一次更新的包路径
     * @return true表示文件存在且有有效状态，false表示文件不存在或无效
     */
    bool readUpdateStatus(std::string& status, std::string& lastUpdatePackagePath);
    
    /**
     * 外部进程喂狗（通过消息队列）
     */
    void feedExternalProcessWatchdog(const std::string& processName);
    
    /**
     * 检查外部进程看门狗
     */
    void checkExternalProcessWatchdog(const std::string& processName);
    
    /**
     * 重启外部进程
     */
    void restartExternalProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args = std::vector<std::string>());
    
    /**
     * 检查外部进程状态（executablePath参数保留用于兼容，实际从m_externalProcesses获取）
     */
    void checkExternalProcess(const std::string& processName, const std::string& executablePath);
    
    /**
     * 获取配置管理器
     */
    ConfigManagerLite& getConfig() { return ConfigManagerLite::getInstance(); }
    
protected:
    // 守护进程特有的初始化
    bool doInitialize();
    void doRun();
    void doCleanup();
    
private:
    /**
     * 监控线程主循环
     */
    void monitorThread();
    
    /**
     * 检查单个进程
     */
    void checkProcess(std::shared_ptr<IProcess> process);
    
    /**
     * 重启异常进程
     */
    void restartProcess(std::shared_ptr<IProcess> process);
    
    /**
     * 启动单个进程
     */
    bool startProcess(std::shared_ptr<IProcess> process);
    
    /**
     * 停止单个进程
     */
    void stopProcess(std::shared_ptr<IProcess> process);
    
    /**
     * 启动外部进程（可执行文件及参数）
     */
    bool startExternalProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args = std::vector<std::string>());
    
    /**
     * 停止外部进程
     */
    void stopExternalProcess(const std::string& processName);
    
    // BY ZF: 进程信息结构体，存储路径和参数
    struct ProcessInfo {
        std::string executablePath;      // 可执行文件路径
        std::vector<std::string> args;   // 启动参数列表
    };
    
    // 外部进程列表（可执行文件）
    std::map<std::string, ProcessInfo> m_externalProcesses;  // name -> process info
    std::vector<std::string> m_processStartOrder;            // BY ZF: 按配置process_list保存启动顺序
    std::map<std::string, pid_t> m_externalPids;            // name -> process id
    std::map<std::string, std::chrono::milliseconds> m_externalWatchdogs;  // name -> last feed time
    mutable std::mutex m_externalMutex;
    
    // 消息队列
    MessageQueue m_messageQueue;          // BY ZF: 看门狗队列
    MessageQueue m_systemQueue;           // BY ZF: 系统控制队列（用于接收更新请求）
    
    // 守护进程状态
    std::atomic<ProcessStatus> m_status;
    std::atomic<bool> m_running;
    std::thread m_monitorThread;
    
    // 配置参数
    int m_monitorInterval;      // 监控间隔（秒）
    int m_watchdogTimeout;      // 看门狗超时时间（秒）
    int m_maxRestartCount;      // 最大重启次数
    int m_restartDelay;         // 重启延迟（秒）
    std::string m_upgradeDir;   // 升级文件目录
    std::string m_deployDir;    // 部署目录
    
    // BY ZF: 更新管理相关
    std::string m_updateProcessPath;      // BY ZF: 更新器（更新处理进程）路径（从配置读取）
    std::vector<std::string> m_updateProcessArgs;  // BY ZF: 更新器启动参数（如-qws等）
    std::string m_updatePackagePath;      // BY ZF: 更新包路径（从消息中获取）
    pid_t m_updateProcessPid;             // BY ZF: 更新处理进程PID（-1表示未启动）
    std::atomic<bool> m_isUpdating;       // BY ZF: 是否正在更新中（防止重复处理）
    std::atomic<bool> m_watchdogEnabled;  // BY ZF: 看门狗监控是否启用（更新时暂停）
    
    // BY ZF: 回滚相关
    std::string m_updateStatusFile;       // BY ZF: 更新状态持久化文件路径
    int m_rollbackCheckInterval;          // BY ZF: 回滚检查间隔（秒）
    int m_rollbackThresholdTime;          // BY ZF: 回滚阈值时间（秒，在此时间内检测到心跳超时则回滚）
    std::atomic<bool> m_rollbackCheckEnabled;  // BY ZF: 是否启用回滚检查（状态为UPDATED时启用）
    std::chrono::steady_clock::time_point m_lastNormalTime;  // BY ZF: 上次所有进程正常的时间（用于DONE判断）
    std::chrono::steady_clock::time_point m_firstTimeoutTime;  // BY ZF: 首次检测到心跳超时的时间（用于回滚判断）
    bool m_hasTimeoutInWindow;            // BY ZF: 在阈值时间窗口内是否发生过心跳超时
    
    // 统计信息
    std::atomic<int> m_totalChecks;
    std::atomic<int> m_failedChecks;
    std::atomic<int> m_restartCount;
    std::map<std::string, int> m_processRestartCount;  // 每个进程的重启次数
};

#endif // DAEMON_PROCESS_H
