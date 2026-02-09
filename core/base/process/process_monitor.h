/**
 * 守护进程（Daemon Process）
 * 负责启动、监控、管理所有业务进程
 * BY ZF
 */

#ifndef DAEMON_PROCESS_H
#define DAEMON_PROCESS_H

#include "../../interfaces/iprocess.h"
#include "../common/config_manager_lite.h"
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
    bool healthCheck() override;
    
    /**
     * 添加业务进程
     */
    void addBusinessProcess(std::shared_ptr<IProcess> process);
    
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
    
protected:
    // 守护进程特有的初始化
    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;
    
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
    
    // 业务进程列表
    std::vector<std::shared_ptr<IProcess>> m_businessProcesses;
    mutable std::mutex m_processesMutex;
    
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
    
    // 统计信息
    std::atomic<int> m_totalChecks;
    std::atomic<int> m_failedChecks;
    std::atomic<int> m_restartCount;
    std::map<std::string, int> m_processRestartCount;  // 每个进程的重启次数
};

#endif // DAEMON_PROCESS_H
