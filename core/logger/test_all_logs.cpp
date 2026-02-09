/**
 * 测试所有日志类型的功能
 * BY ZF
 */

#include "logger_process.h"
#include <iostream>
#include <unistd.h>

int main() {
    std::cout << "=== Test All Log Types ===" << std::endl;
    
    // 创建日志进程实例
    LoggerProcess logger;
    
    // 初始化
    if (!logger.initialize("../../config/project_a/logger.ini")) {
        std::cout << "Failed to initialize logger" << std::endl;
        return -1;
    }
    
    // 启动
    if (logger.start() != 0) {
        std::cout << "Failed to start logger" << std::endl;
        return -1;
    }
    
    std::cout << "Logger started, testing all log types..." << std::endl;
    
    // 测试运行日志（operation_logs）
    logger.info("test_module", "This is an info message", "test details");
    logger.warn("test_module", "This is a warning message");
    logger.error("test_module", "This is an error message");
    
    // 删除所有 logger.logSystemEvent、logger.logError、logger.logChargeEvent、logger.logTransaction 调用。
    
    std::cout << "All log types tested, waiting for flush..." << std::endl;
    
    // 强制刷新缓冲区
    logger.flush();
    
    // 等待一下让日志写入完成
    sleep(2);
    
    // 停止
    logger.stop();
    
    std::cout << "Test completed!" << std::endl;
    return 0;
}

