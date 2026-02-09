// BY ZF: 纯C业务日志接口实现
#include "log_sender_c.h"
#include "cpp/log_sender.h"
#include <new>

extern "C" {

// BY ZF: 创建日志发送句柄
LogSenderHandle log_sender_create(const char* module_name) {
    try {
        return new LogSender(module_name ? module_name : "");
    } catch (...) {
        return nullptr;
    }
}

// BY ZF: 销毁日志发送句柄
void log_sender_destroy(LogSenderHandle handle) {
    delete handle;
}

// BY ZF: 发送调试日志
void log_sender_debug(LogSenderHandle handle, const char* message, const char* details) {
    if (handle && message) {
        handle->debug(message, details ? details : "");
    }
}

// BY ZF: 发送信息日志
void log_sender_info(LogSenderHandle handle, const char* message, const char* details) {
    if (handle && message) {
        handle->info(message, details ? details : "");
    }
}

// BY ZF: 发送警告日志
void log_sender_warn(LogSenderHandle handle, const char* message, const char* details) {
    if (handle && message) {
        handle->warn(message, details ? details : "");
    }
}

// BY ZF: 发送错误日志
void log_sender_error(LogSenderHandle handle, const char* message, const char* details) {
    if (handle && message) {
        handle->error(message, details ? details : "");
    }
}

// BY ZF: 发送致命日志
void log_sender_fatal(LogSenderHandle handle, const char* message, const char* details) {
    if (handle && message) {
        handle->fatal(message, details ? details : "");
    }
}

// BY ZF: 手动刷新缓冲区
void log_sender_flush(LogSenderHandle handle) {
    if (handle) {
        handle->flush();
    }
}

} // extern "C"


