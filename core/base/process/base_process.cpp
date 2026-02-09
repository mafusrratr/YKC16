/**
 * 进程基类实现（统一监控版本）
 * BY ZF
 */

#include "base_process.h"
#include <iostream>
#include <cstdarg>
#include <cstdio>
#include <signal.h>
#include <unistd.h>

BaseProcess::BaseProcess(ProcessType type, const char* name)
    : m_type(type)
    , m_name(name)
    , m_status(PROC_STATUS_INIT)
    , m_running(false)
    , m_lastFeedTime(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()))
    , m_watchdogTimeout(30)  // 默认30秒超时
{
    std::cout << "BaseProcess created: " << m_name << std::endl;
}

BaseProcess::~BaseProcess()
{
    stop();
    std::cout << "BaseProcess destroyed: " << m_name << std::endl;
}

bool BaseProcess::initialize(const char* config)
{
    std::cout << "BaseProcess::initialize " << m_name << " config: " << config << std::endl;
    
    // BY ZF: 加载配置（使用轻量级配置管理器）
    if (!getConfig().loadConfig(std::string(config))) {
        std::cout << "Failed to load config: " << config << std::endl;
        return false;
    }
    
    // 调用子类初始化
    if (!doInitialize()) {
        std::cout << "Subclass initialization failed: " << m_name << std::endl;
        return false;
    }
    
    // 初始化喂狗时间
    feedWatchdog();
    
    setStatus(PROC_STATUS_RUNNING);
    std::cout << "BaseProcess initialized successfully: " << m_name << std::endl;
    return true;
}

int BaseProcess::start()
{
    std::cout << "BaseProcess::start " << m_name << std::endl;
    
    if (m_status.load() != PROC_STATUS_RUNNING) {
        std::cout << "Process not in running status: " << m_name << std::endl;
        return -1;
    }
    
    m_running = true;
    
    // 启动主循环（单线程）
    run();
    
    return 0;
}

void BaseProcess::stop()
{
    std::cout << "BaseProcess::stop " << m_name << std::endl;
    
    m_running = false;
    
    // 调用子类清理
    doCleanup();
    
    setStatus(PROC_STATUS_STOPPED);
    std::cout << "BaseProcess stopped: " << m_name << std::endl;
}

void BaseProcess::run()
{
    std::cout << "BaseProcess::run starting " << m_name << std::endl;
    
    while (m_running.load()) {
        try {
            // 调用子类主循环
            doRun();
            
            // 短暂休眠，避免CPU占用过高
            usleep(100000);  // 100ms
            
        } catch (const std::exception& e) {
            std::cout << "Exception in process " << m_name << ": " << e.what() << std::endl;
            setStatus(PROC_STATUS_ERROR);
            break;
        }
    }
    
    std::cout << "BaseProcess::run finished " << m_name << std::endl;
}

void BaseProcess::setStatus(ProcessStatus status)
{
    ProcessStatus oldStatus = m_status.load();
    if (oldStatus != status) {
        m_status.store(status);
        std::cout << "Process status changed: " << m_name 
                  << " from " << oldStatus << " to " << status << std::endl;
    }
}

void BaseProcess::logMessage(int level, const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // 简单的日志输出，后续会通过消息队列发送给日志进程
    const char* levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (level >= 0 && level < 5) {
        std::cout << "[" << levelStr[level] << "] " << m_name << ": " << buffer << std::endl;
    }
}

void BaseProcess::feedWatchdog()
{
    std::lock_guard<std::mutex> lock(m_watchdogMutex);
    m_lastFeedTime.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()));
    std::cout << "Watchdog fed for process: " << m_name << std::endl;
}

bool BaseProcess::isWatchdogAlive()
{
    std::lock_guard<std::mutex> lock(m_watchdogMutex);
    auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        currentTime - m_lastFeedTime.load()).count();
    
    return elapsed < m_watchdogTimeout;
}