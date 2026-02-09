/**
 * 进程基类实现（统一监控版本）
 * 所有业务进程都应该继承此基类
 * BY ZF
 */

#ifndef BASE_PROCESS_H
#define BASE_PROCESS_H

#include "../../interfaces/iprocess.h"
#include "../common/config_manager_lite.h"
#include <string>
#include <atomic>
#include <chrono>
#include <mutex>

/**
 * 进程基类实现（轻量级版本）
 * 不依赖Qt，仅使用标准C++和系统调用
 * 看门狗和健康检查由monitor统一管理
 */
class BaseProcess : public IProcess {
public:
    explicit BaseProcess(ProcessType type, const char* name);
    virtual ~BaseProcess();
    
    // IProcess接口实现
    ProcessType getType() override { return m_type; }
    const char* getName() override { return m_name.c_str(); }
    bool initialize(const char* config) override;
    int start() override;
    void stop() override;
    void run() override;
    ProcessStatus getStatus() override { return m_status.load(); }
    
protected:
    /**
     * 子类需要重写的虚函数
     */
    virtual bool doInitialize() = 0;  // 子类初始化逻辑
    virtual void doRun() = 0;         // 子类主循环逻辑
    virtual void doCleanup() = 0;    // 子类清理逻辑
    
    /**
     * 工具方法
     */
    void setStatus(ProcessStatus status);
    void logMessage(int level, const char* format, ...);
    
    /**
     * 看门狗机制（由monitor调用）
     */
    void feedWatchdog();  // 喂狗
    bool isWatchdogAlive(); // 检查看门狗是否存活
    std::chrono::milliseconds getLastFeedTime() const { return m_lastFeedTime.load(); }
    
    // BY ZF: 配置管理（使用轻量级配置管理器，不依赖Qt）
    ConfigManagerLite& getConfig() { return ConfigManagerLite::getInstance(); }
    
    // 进程信息
    ProcessType m_type;
    std::string m_name;
    std::atomic<ProcessStatus> m_status;
    
    // 运行控制
    std::atomic<bool> m_running;
    
    // 看门狗机制（线程安全）
    std::atomic<std::chrono::milliseconds> m_lastFeedTime;
    int m_watchdogTimeout;    // 看门狗超时时间（秒）
    mutable std::mutex m_watchdogMutex;
};

#endif // BASE_PROCESS_H

