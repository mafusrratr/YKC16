/**
 * 消息队列封装类
 * 支持多个消息队列：命令队列、日志队列、事件队列、系统控制队列
 * BY ZF
 */

#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <stdint.h>
#include <chrono>

// 消息队列Key定义
#define MSG_KEY_CMD     0x1234    // 命令队列
#define MSG_KEY_LOG     0x1235    // 日志队列
#define MSG_KEY_EVENT   0x1236    // 事件队列
#define MSG_KEY_SYSTEM  0x1237    // 系统控制队列
#define MSG_KEY_WATCHDOG 0x1238   // 看门狗队列

// 消息类型定义
#define MSG_TYPE_CMD    1         // 命令消息
#define MSG_TYPE_EVENT  2         // 事件消息
#define MSG_TYPE_LOG    3         // 日志消息
#define MSG_TYPE_CTRL   4         // 控制消息
#define MSG_WATCHDOG_FEED 5       // 看门狗喂狗消息
#define MSG_UPDATE_REQUEST 6      // BY ZF: 更新请求消息

// 进程类型（用于消息来源标识）
enum ProcessID {
    PROC_ID_DAEMON = 1,
    PROC_ID_CHARGE,
    PROC_ID_METER,
    PROC_ID_PILE_CTRL,    // 主控通信进程
    PROC_ID_COMM,
    PROC_ID_LOGGER,
    PROC_ID_DISPLAY
};

/**
 * 通用消息结构
 */
struct MessageHeader {
    long msgType;           // 消息类型（IPC要求）
    int fromPid;            // 发送进程ID
    int toPid;              // 目标进程ID（0表示广播）
    uint32_t msgId;         // 消息ID
    uint32_t msgSize;       // 消息体大小
    int32_t result;         // 返回码
};

/**
 * 消息队列管理类
 */
class MessageQueue {
public:
    /**
     * 构造函数
     * @param msgKey 消息队列Key
     */
    explicit MessageQueue(key_t msgKey);
    
    /**
     * 析构函数
     */
    ~MessageQueue();
    
    /**
     * 创建消息队列
     * @return true成功
     */
    bool create();
    
    /**
     * 打开消息队列
     * @return true成功
     */
    bool open();
    
    /**
     * 发送消息
     * @param msgType 消息类型
     * @param msgData 消息数据
     * @param dataSize 数据大小
     * @return 0成功
     */
    int send(long msgType, const void* msgData, int dataSize);
    
    /**
     * 接收消息（非阻塞）
     * @param msgType 消息类型（0表示接收任意类型）
     * @param msgData 输出：消息数据缓冲区
     * @param bufferSize 缓冲区大小
     * @param actualSize 输出：实际接收的数据大小
     * @return 0成功，-1无消息，其他失败
     */
    int receive(long msgType, void* msgData, int bufferSize, int* actualSize);
    
    /**
     * 接收消息（阻塞）
     * @param msgType 消息类型
     * @param msgData 输出：消息数据缓冲区
     * @param bufferSize 缓冲区大小
     * @param actualSize 输出：实际接收的数据大小
     * @return 0成功
     */
    int receiveWait(long msgType, void* msgData, int bufferSize, int* actualSize);
    
    /**
     * 删除消息队列
     */
    void remove();
    
    /**
     * BY ZF: 获取消息队列状态（当前消息数量和总容量）
     * @param currentMsgs 输出：当前消息数量
     * @param maxMsgs 输出：最大消息数量（基于msg_qbytes计算）
     * @return true成功
     */
    bool getQueueStatus(unsigned long* currentMsgs, unsigned long* maxMsgs);
    
private:
    key_t m_msgKey;
    int m_msgId;
    bool m_isCreator;
    
    // BY ZF: 错误输出频率控制（避免刷屏）
    static std::chrono::steady_clock::time_point s_lastErrorTime;
    static int s_errorCount;  // 错误计数
    static const int MAX_ERROR_INTERVAL_MS = 1000;  // 最多每1秒输出一次错误
    static const int MAX_ERROR_COUNT = 10;  // 连续错误超过此数量时输出
};

#endif // MESSAGE_QUEUE_H

