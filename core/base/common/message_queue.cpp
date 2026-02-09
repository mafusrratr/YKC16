/**
 * 消息队列实现
 * BY ZF
 */

#include "message_queue.h"
#include <stdio.h>
#include <errno.h>
#include <chrono>

// BY ZF: 错误输出频率控制静态变量初始化
std::chrono::steady_clock::time_point MessageQueue::s_lastErrorTime = std::chrono::steady_clock::now();
int MessageQueue::s_errorCount = 0;

MessageQueue::MessageQueue(key_t msgKey)
    : m_msgKey(msgKey)
    , m_msgId(-1)
    , m_isCreator(false)
{
}

MessageQueue::~MessageQueue()
{
    // 注意：不在这里删除队列，因为队列是全局的
}

bool MessageQueue::create()
{
    // 创建消息队列
    m_msgId = msgget(m_msgKey, IPC_CREAT | IPC_EXCL | 0666);
    if (m_msgId == -1) {
        if (errno == EEXIST) {
            // 队列已存在，尝试打开
            return open();
        }
        printf("Create message queue failed, errno=%d\n", errno);
        return false;
    }
    
    // BY ZF: 设置消息队列总容量为 1MB（可容纳约 5000 条消息，应对爆发式错误日志）
    // 注意：需要系统允许设置大于 msgmnb 的值，通常系统会允许
    struct msqid_ds buf;
    if (msgctl(m_msgId, IPC_STAT, &buf) == 0) {
        // 保存原始值
        unsigned long original_qbytes = buf.msg_qbytes;
        
        // BY ZF: 尝试设置为 1MB（优先选择，应对高负载和突发流量）
        buf.msg_qbytes = 1024 * 1024;
        if (msgctl(m_msgId, IPC_SET, &buf) == -1) {
            // 设置失败，可能是系统限制，尝试较小的值
            if (errno == EINVAL) {
                // 值太大，尝试 512KB
                buf.msg_qbytes = 512 * 1024;
                if (msgctl(m_msgId, IPC_SET, &buf) == -1) {
                    // 仍然失败，尝试 256KB
                    buf.msg_qbytes = 256 * 1024;
                    if (msgctl(m_msgId, IPC_SET, &buf) == -1) {
                        // 所有尝试都失败，使用原始值
                        printf("Warning: Failed to set queue size, using default (errno=%d)\n", errno);
                    } else {
                        printf("Message queue size set to 256KB (1MB and 512KB not allowed)\n");
                    }
                } else {
                    printf("Message queue size set to 512KB (1MB not allowed, original: %lu bytes)\n", original_qbytes);
                }
            } else {
                printf("Warning: Failed to set queue size, using default (errno=%d)\n", errno);
            }
        } else {
            printf("Message queue size set to 1MB (original: %lu bytes)\n", original_qbytes);
        }
    } else {
        printf("Warning: Failed to get queue status (errno=%d)\n", errno);
    }
    
    m_isCreator = true;
    return true;
}

bool MessageQueue::open()
{
    m_msgId = msgget(m_msgKey, 0);
    if (m_msgId == -1) {
        printf("Open message queue failed, errno=%d\n", errno);
        return false;
    }
    m_isCreator = false;
    return true;
}

int MessageQueue::send(long msgType, const void* msgData, int dataSize)
{
    if (m_msgId == -1) {
        return -1;
    }
    
    // BY ZF: 增大消息缓冲区以支持大型JSON（如96时段的计费模型）
    // 消息队列最大支持 8192 字节（大多数系统的默认限制）
    const int MAX_MSG_SIZE = 8192;
    
    // 构造完整的消息结构（包含msgType）
    struct {
        long msgType;
        char data[MAX_MSG_SIZE];
    } msg;
    
    msg.msgType = msgType;
    if (dataSize > 0 && msgData != NULL) {
        if (dataSize > MAX_MSG_SIZE) {
            printf("Message too large: %d bytes (max: %d)\n", dataSize, MAX_MSG_SIZE);
            return -1;
        }
        memcpy(msg.data, msgData, dataSize);
    }
    
    // BY ZF: 使用 IPC_NOWAIT 非阻塞模式，避免队列满时阻塞
    // 当队列满时立即返回错误，而不是阻塞等待
    int ret = msgsnd(m_msgId, &msg, dataSize, IPC_NOWAIT);
    if (ret == -1) {
        if (errno == EAGAIN) {
            // BY ZF: 消息队列已满，非阻塞模式下立即返回
            // 限制错误输出频率，避免刷屏（最多每1秒或每10次错误输出一次）
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - s_lastErrorTime).count();
            s_errorCount++;
            
            if (elapsed >= MAX_ERROR_INTERVAL_MS || s_errorCount >= MAX_ERROR_COUNT) {
                printf("Message queue is full, message dropped (errno=EAGAIN, count=%d)\n", s_errorCount);
                s_lastErrorTime = now;
                s_errorCount = 0;
            }
        } else {
            printf("Send message failed, errno=%d\n", errno);
        }
        return -1;
    }
    return 0;
}

int MessageQueue::receive(long msgType, void* msgData, int bufferSize, int* actualSize)
{
    if (m_msgId == -1) {
        return -1;
    }
    
    // BY ZF: 增大消息缓冲区以支持大型JSON（如96时段的计费模型）
    const int MAX_MSG_SIZE = 8192;
    
    struct {
        long msgType;
        char data[MAX_MSG_SIZE];
    } msg;
    
    int ret = msgrcv(m_msgId, &msg, sizeof(msg.data), msgType, IPC_NOWAIT);
    if (ret == -1) {
        if (errno == ENOMSG) {
            return -1;  // 无消息
        }
        if (errno == E2BIG) {
            printf("Receive message failed: message too large (errno=%d)\n", errno);
        } else {
            printf("Receive message failed, errno=%d\n", errno);
        }
        return -2;
    }
    
    if (msgData != NULL && bufferSize > 0) {
        int copySize = (ret > bufferSize) ? bufferSize : ret;
        memcpy(msgData, msg.data, copySize);
        if (actualSize) {
            *actualSize = ret;
        }
    } else if (actualSize) {
        *actualSize = ret;
    }
    
    return 0;
}

int MessageQueue::receiveWait(long msgType, void* msgData, int bufferSize, int* actualSize)
{
    if (m_msgId == -1) {
        return -1;
    }
    
    // BY ZF: 增大消息缓冲区以支持大型JSON（如96时段的计费模型）
    const int MAX_MSG_SIZE = 8192;
    
    struct {
        long msgType;
        char data[MAX_MSG_SIZE];
    } msg;
    
    int ret = msgrcv(m_msgId, &msg, sizeof(msg.data), msgType, 0);
    if (ret == -1) {
        if (errno == E2BIG) {
            printf("Receive message failed: message too large (errno=%d)\n", errno);
        } else {
            printf("Receive message failed, errno=%d\n", errno);
        }
        return -1;
    }
    
    if (msgData != NULL && bufferSize > 0) {
        int copySize = (ret > bufferSize) ? bufferSize : ret;
        memcpy(msgData, msg.data, copySize);
        if (actualSize) {
            *actualSize = ret;
        }
    } else if (actualSize) {
        *actualSize = ret;
    }
    
    return 0;
}

void MessageQueue::remove()
{
    if (m_isCreator && m_msgId != -1) {
        msgctl(m_msgId, IPC_RMID, NULL);
        m_msgId = -1;
    }
}

// BY ZF: 获取消息队列状态
bool MessageQueue::getQueueStatus(unsigned long* currentMsgs, unsigned long* maxMsgs)
{
    if (m_msgId == -1) {
        return false;
    }
    
    struct msqid_ds buf;
    if (msgctl(m_msgId, IPC_STAT, &buf) == 0) {
        if (currentMsgs) {
            *currentMsgs = buf.msg_qnum;  // 当前消息数量
        }
        if (maxMsgs) {
            // BY ZF: 估算最大消息数（假设平均每条消息200字节）
            // msg_qbytes 是总字节数，除以平均消息大小得到最大消息数
            unsigned long avgMsgSize = 200;  // 平均每条消息200字节
            *maxMsgs = buf.msg_qbytes / avgMsgSize;
        }
        return true;
    }
    return false;
}

