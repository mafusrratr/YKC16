/**
 * 进程基类接口定义
 * 所有业务进程都应该继承此接口
 * BY ZF
 */

#ifndef IPROCESS_H
#define IPROCESS_H

#include <stdint.h>

// 进程类型
enum ProcessType {
    PROC_UNKNOWN = 0,
    PROC_DAEMON,
    PROC_TCU_LOGIC,
    PROC_METER,
    PROC_PILE_CONTROLLER,    // 主控通信进程
    PROC_DISPLAY,
    PROC_COMMUNICATION,
    PROC_LOGGER
};

// 进程状态
enum ProcessStatus {
    PROC_STATUS_INIT = 0,
    PROC_STATUS_RUNNING,
    PROC_STATUS_STOPPING,
    PROC_STATUS_STOPPED,
    PROC_STATUS_ERROR
};

/**
 * 进程基类接口
 */
class IProcess {
public:
    virtual ~IProcess() {}
    
    /**
     * 获取进程类型
     */
    virtual ProcessType getType() = 0;
    
    /**
     * 获取进程名称
     */
    virtual const char* getName() = 0;
    
    /**
     * 初始化进程
     * @param config 配置信息
     * @return true成功, false失败
     */
    virtual bool initialize(const char* config) = 0;
    
    /**
     * 启动进程（在主线程中运行）
     * @return 0成功
     */
    virtual int start() = 0;
    
    /**
     * 停止进程
     */
    virtual void stop() = 0;
    
    /**
     * 主循环（可重写）
     */
    virtual void run() = 0;
    
    /**
     * 获取进程状态
     */
    virtual ProcessStatus getStatus() = 0;
    
    /**
     * 关闭进程（用于异常掉电等场景）
     */
    virtual void shutdown() { stop(); }
};

#endif // IPROCESS_H
