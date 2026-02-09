// BY ZF: 消息队列封装（复制自 core/base/message_queue.h，供独立打包使用）
#pragma once

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <stdint.h>

// BY ZF: 消息队列Key定义（可按需调整）
#define MSG_KEY_CMD      0x1234
#define MSG_KEY_LOG      0x1235
#define MSG_KEY_EVENT    0x1236
#define MSG_KEY_SYSTEM   0x1237
#define MSG_KEY_WATCHDOG 0x1238

// BY ZF: 消息类型定义
#define MSG_TYPE_CMD     1
#define MSG_TYPE_EVENT   2
#define MSG_TYPE_LOG     3
#define MSG_TYPE_CTRL    4
#define MSG_WATCHDOG_FEED 5

// BY ZF: 进程标识枚举（仅用于日志记录，可保留原值）
enum ProcessID {
    PROC_ID_DAEMON = 1,
    PROC_ID_CHARGE,
    PROC_ID_METER,
    PROC_ID_PILE_CTRL,
    PROC_ID_COMM,
    PROC_ID_LOGGER,
    PROC_ID_DISPLAY
};

// BY ZF: 通用消息头结构
struct MessageHeader {
    long msgType;
    int fromPid;
    int toPid;
    uint32_t msgId;
    uint32_t msgSize;
    int32_t result;
};

// BY ZF: 消息队列封装类
class MessageQueue {
public:
    explicit MessageQueue(key_t msgKey);
    ~MessageQueue();

    bool create();
    bool open();
    int send(long msgType, const void* msgData, int dataSize);
    int receive(long msgType, void* msgData, int bufferSize, int* actualSize);
    int receiveWait(long msgType, void* msgData, int bufferSize, int* actualSize);
    void remove();

private:
    key_t m_msgKey;
    int m_msgId;
    bool m_isCreator;
};


