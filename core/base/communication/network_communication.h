/**
 * 网络通信基类（TCP/UDP）
 * 提供通用的网络通信功能，供多个模块复用
 * BY ZF
 */

#ifndef NETWORK_COMMUNICATION_H
#define NETWORK_COMMUNICATION_H

#include <string>
#include <stdint.h>

/**
 * 网络通信类型
 */
enum NetworkType {
    NETWORK_TCP = 0,
    NETWORK_UDP
};

/**
 * 网络通信基类
 * 封装TCP/UDP的基本操作（连接、断开、发送、接收）
 * 
 * 使用方式：
 * 1. 继承此类实现具体的网络协议
 * 2. 在子类中实现协议解析和封装
 * 
 * 示例：
 * class CustomTCPProtocol : public NetworkCommunication {
 *     // 实现具体的协议逻辑
 * };
 */
class NetworkCommunication {
public:
    NetworkCommunication(NetworkType type);
    virtual ~NetworkCommunication();
    
    /**
     * 连接到服务器（TCP）或绑定端口（UDP）
     * @param host 服务器地址（TCP）或本地地址（UDP，空字符串表示任意地址）
     * @param port 端口号
     * @param timeoutMs 连接超时（毫秒，仅TCP有效）
     * @return 0成功, 其他失败
     */
    virtual int connect(const std::string& host, uint16_t port, int timeoutMs = 5000);
    
    /**
     * 断开连接
     */
    virtual void disconnect();
    
    /**
     * 检查是否已连接（TCP）或已绑定（UDP）
     * @return true已连接/已绑定, false未连接/未绑定
     */
    bool isConnected() const { return m_socket >= 0; }
    
    /**
     * 发送数据
     * @param data 数据缓冲区
     * @param dataLen 数据长度
     * @return 实际发送的字节数, -1失败
     */
    virtual int send(const uint8_t* data, size_t dataLen);
    
    /**
     * 接收数据（非阻塞）
     * @param data 数据缓冲区
     * @param maxLen 缓冲区最大长度
     * @return 实际接收的字节数, -1无数据或失败（非阻塞，立即返回）
     * 
     * 注意：本方法使用非阻塞模式，如果没有数据立即返回-1
     * 调用者需要自己实现循环和延时来控制接收频率
     */
    virtual int receive(uint8_t* data, size_t maxLen);
    
    /**
     * 发送数据到指定地址（仅UDP）
     * @param host 目标地址
     * @param port 目标端口
     * @param data 数据缓冲区
     * @param dataLen 数据长度
     * @return 实际发送的字节数, -1失败
     */
    virtual int sendTo(const std::string& host, uint16_t port, const uint8_t* data, size_t dataLen);
    
    /**
     * 从指定地址接收数据（仅UDP，非阻塞）
     * @param data 数据缓冲区
     * @param maxLen 缓冲区最大长度
     * @param host 输出：源地址
     * @param port 输出：源端口
     * @return 实际接收的字节数, -1无数据或失败（非阻塞，立即返回）
     * 
     * 注意：本方法使用非阻塞模式，如果没有数据立即返回-1
     * 调用者需要自己实现循环和延时来控制接收频率
     */
    virtual int receiveFrom(uint8_t* data, size_t maxLen, std::string& host, uint16_t& port);
    
    /**
     * 设置TCP_NODELAY选项（仅TCP）
     * @param enable true启用, false禁用
     * @return 0成功, 其他失败
     */
    virtual int setNoDelay(bool enable);
    
    /**
     * 设置发送超时
     * @param timeoutMs 超时时间（毫秒）
     * @return 0成功, 其他失败
     */
    virtual int setSendTimeout(int timeoutMs);

protected:
    NetworkType m_type;            // 网络类型（TCP/UDP）
    int m_socket;                  // Socket文件描述符
    std::string m_host;            // 服务器地址（TCP）或本地地址（UDP）
    uint16_t m_port;               // 端口号
    bool m_isConnected;            // 是否已连接/已绑定
};

#endif // NETWORK_COMMUNICATION_H

