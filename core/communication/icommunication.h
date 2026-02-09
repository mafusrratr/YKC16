/**
 * 平台通信接口定义
 * 支持TCP/UDP/MQTT/485等多种通信方式
 * BY ZF
 * 
 * 注意：此接口用于进程内部多种实现（TCP、UDP、MQTT等），
 * 应该放在进程内部，而不是全局 interfaces/ 目录
 */

#ifndef ICOMMUNICATION_H
#define ICOMMUNICATION_H

#include <stdint.h>

// 通信类型枚举
enum CommType {
    COMM_NONE = 0,
    COMM_TCP,
    COMM_UDP,
    COMM_MQTT,
    COMM_485,
    COMM_HTTP
};

// 消息类型
enum MsgType {
    MSG_UNKNOWN = 0,
    MSG_HEARTBEAT,          // 心跳
    MSG_REGISTER,           // 注册
    MSG_REMOTE_START,       // 远程启动
    MSG_REMOTE_STOP,        // 远程停止
    MSG_FEE_MODEL,          // 计费模型更新
    MSG_TRADE_RECORD,       // 交易记录上传
    MSG_STATUS,             // 状态上报
    MSG_DEVICE_INFO,        // 设备信息
    MSG_CONFIG,             // 配置下发
    MSG_UPGRADE             // 升级命令
};

/**
 * 平台通信接口
 * 统一管理所有与平台的通信方式
 * 
 * 使用方式：
 * CommunicationProcess 根据配置选择实现：
 * - TCPCommunication（实现 ICommunication）
 * - UDPCommunication（实现 ICommunication）
 * - MQTTCommunication（实现 ICommunication）
 * - RS485Communication（实现 ICommunication）
 */
class ICommunication {
public:
    virtual ~ICommunication() {}
    
    /**
     * 初始化通信模块
     * @param commType 通信类型
     * @param config 配置信息（JSON字符串或配置文件路径）
     * @return true成功, false失败
     */
    virtual bool initialize(CommType commType, const char* config) = 0;
    
    /**
     * 反初始化
     */
    virtual void cleanup() = 0;
    
    /**
     * 连接到平台
     * @return 0成功
     */
    virtual int connect() = 0;
    
    /**
     * 断开连接
     */
    virtual void disconnect() = 0;
    
    /**
     * 发送消息
     * @param msgType 消息类型
     * @param data 消息数据
     * @param dataLen 数据长度
     * @return 0成功
     */
    virtual int sendMessage(MsgType msgType, const void* data, int dataLen) = 0;
    
    /**
     * 接收消息回调
     * @param msgType 消息类型
     * @param data 消息数据
     * @param dataLen 数据长度
     */
    virtual void onMessage(MsgType msgType, const void* data, int dataLen) = 0;
    
    /**
     * 检查连接状态
     * @return true已连接, false未连接
     */
    virtual bool isConnected() = 0;
};

#endif // ICOMMUNICATION_H





