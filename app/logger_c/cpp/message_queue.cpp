// BY ZF: 消息队列实现（复制自 core/base/message_queue.cpp，供独立打包使用）
#include "message_queue.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

MessageQueue::MessageQueue(key_t msgKey)
    : m_msgKey(msgKey)
    , m_msgId(-1)
    , m_isCreator(false) {
}

MessageQueue::~MessageQueue() {
    // BY ZF: 队列生命周期由系统管理，此处不删除
}

bool MessageQueue::create() {
    m_msgId = msgget(m_msgKey, IPC_CREAT | IPC_EXCL | 0666);
    if (m_msgId == -1) {
        if (errno == EEXIST) {
            return open();
        }
        printf("Create message queue failed, errno=%d\n", errno);
        return false;
    }
    m_isCreator = true;
    return true;
}

bool MessageQueue::open() {
    m_msgId = msgget(m_msgKey, 0);
    if (m_msgId == -1) {
        printf("Open message queue failed, errno=%d\n", errno);
        return false;
    }
    m_isCreator = false;
    return true;
}

int MessageQueue::send(long msgType, const void* msgData, int dataSize) {
    if (m_msgId == -1) {
        return -1;
    }

    const int MAX_MSG_SIZE = 8192;

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
            printf("Message queue is full, message dropped (errno=EAGAIN)\n");
        } else {
            printf("Send message failed, errno=%d\n", errno);
        }
        return -1;
    }
    return 0;
}

int MessageQueue::receive(long msgType, void* msgData, int bufferSize, int* actualSize) {
    if (m_msgId == -1) {
        return -1;
    }

    const int MAX_MSG_SIZE = 8192;

    struct {
        long msgType;
        char data[MAX_MSG_SIZE];
    } msg;

    int ret = msgrcv(m_msgId, &msg, sizeof(msg.data), msgType, IPC_NOWAIT);
    if (ret == -1) {
        if (errno == ENOMSG) {
            return -1;
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

int MessageQueue::receiveWait(long msgType, void* msgData, int bufferSize, int* actualSize) {
    if (m_msgId == -1) {
        return -1;
    }

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

void MessageQueue::remove() {
    if (m_isCreator && m_msgId != -1) {
        msgctl(m_msgId, IPC_RMID, NULL);
        m_msgId = -1;
    }
}


