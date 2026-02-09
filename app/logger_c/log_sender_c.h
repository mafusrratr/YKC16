// BY ZF: 纯C业务使用的日志接口头文件
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// BY ZF: 前向声明C++对象句柄
typedef struct LogSender LogSender;

// BY ZF: 统一句柄类型，方便业务代码阅读
typedef LogSender* LogSenderHandle;

// BY ZF: 创建日志发送句柄
LogSenderHandle log_sender_create(const char* module_name);

// BY ZF: 销毁日志发送句柄
void log_sender_destroy(LogSenderHandle handle);

// BY ZF: 发送调试日志
void log_sender_debug(LogSenderHandle handle, const char* message, const char* details);

// BY ZF: 发送信息日志
void log_sender_info(LogSenderHandle handle, const char* message, const char* details);

// BY ZF: 发送警告日志
void log_sender_warn(LogSenderHandle handle, const char* message, const char* details);

// BY ZF: 发送错误日志
void log_sender_error(LogSenderHandle handle, const char* message, const char* details);

// BY ZF: 发送致命日志
void log_sender_fatal(LogSenderHandle handle, const char* message, const char* details);

// BY ZF: 手动刷新缓冲区
void log_sender_flush(LogSenderHandle handle);

#ifdef __cplusplus
}
#endif



