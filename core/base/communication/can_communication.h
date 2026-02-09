/**
 * CAN总线通信基类（基于SocketCAN）
 * 提供通用的CAN通信功能，供多个模块复用
 * BY ZF
 * 
 * 使用SocketCAN API（Linux内核CAN驱动接口）
 * 设备路径格式：can0, can1 等（不是 /dev/can0）
 * 
 * 注意：本实现仅支持扩展帧（29位ID），不支持标准帧（11位ID）
 */

#ifndef CAN_COMMUNICATION_H
#define CAN_COMMUNICATION_H

#include <string>
#include <stdint.h>
#include <sys/socket.h>  // BY ZF: 提供 sa_family_t 类型定义
#include <linux/can.h>
#include <linux/can/raw.h>

/**
 * CAN通信基类
 * 封装SocketCAN的基本操作（打开、关闭、发送、接收）
 * 
 * 使用方式：
 * 1. 继承此类实现具体的CAN协议
 * 2. 在子类中实现协议解析和封装
 * 
 * 示例：
 * class CustomCANProtocol : public CANCommunication {
 *     // 实现具体的协议逻辑
 * };
 */
class CANCommunication {
public:
    CANCommunication();
    virtual ~CANCommunication();
    
    /**
     * 打开CAN设备（SocketCAN）
     * @param deviceName CAN设备名称（如 "can0", "can1" 等，不是 /dev/can0）
     * @param baudrate 波特率（如 250000, 500000 等，仅用于记录，实际由内核驱动配置）
     * @return true成功, false失败
     * 
     * 注意：SocketCAN的波特率需要在系统启动时通过ip命令配置，例如：
     *   ip link set can0 type can bitrate 250000
     *   ip link set up can0
     */
    virtual bool open(const std::string& deviceName, uint32_t baudrate);
    
    /**
     * 关闭CAN设备
     */
    virtual void close();
    
    /**
     * 检查CAN设备是否已打开
     * @return true已打开, false未打开
     */
    bool isOpen() const { return m_socket >= 0; }
    
    /**
     * 发送CAN帧（扩展帧，29位ID）
     * @param canId CAN ID（29位扩展帧ID）
     * @param data 数据（最多8字节）
     * @param dataLen 数据长度（1-8）
     * @return 0成功, 其他失败
     */
    virtual int send(uint32_t canId, const uint8_t* data, uint8_t dataLen);
    
    /**
     * 接收CAN帧（非阻塞，扩展帧，29位ID）
     * @param canId 输出：CAN ID（29位扩展帧ID）
     * @param data 输出：数据缓冲区（至少8字节）
     * @param dataLen 输出：实际接收的数据长度
     * @return 0成功（有数据）, -1无数据或失败（非阻塞，立即返回）
     * 
     * 注意：本方法使用非阻塞模式，如果没有数据立即返回-1
     * 调用者需要自己实现循环和延时来控制接收频率
     */
    virtual int receive(uint32_t& canId, uint8_t* data, uint8_t& dataLen);
    
    /**
     * 设置CAN过滤器（扩展帧，29位ID）
     * @param canId 要过滤的CAN ID（29位扩展帧ID）
     * @param mask CAN ID掩码（1表示必须匹配，0表示不关心）
     * @return 0成功, 其他失败
     * 
     * 示例：
     *   setFilter(0x12345678, 0xFFFFFFFF)  // 只接收ID=0x12345678
     *   setFilter(0x12345678, 0xFFFFFFF0)  // 接收ID范围0x12345670-0x1234567F
     */
    virtual int setFilter(uint32_t canId, uint32_t mask);
    
    /**
     * 清除所有CAN过滤器
     * @return 0成功, 其他失败
     */
    virtual int clearFilter();
    
    /**
     * 设置错误帧接收（SocketCAN特性）
     * @param enable true启用错误帧接收, false禁用
     * @return 0成功, 其他失败
     */
    virtual int setErrorFrameFilter(bool enable);
    
    /**
     * 获取错误统计（通过SocketCAN接口）
     * @param txErrors 输出：发送错误计数
     * @param rxErrors 输出：接收错误计数
     * @return 0成功, 其他失败
     */
    virtual int getErrorCount(uint32_t& txErrors, uint32_t& rxErrors);
    
    /**
     * 获取CAN设备状态（通过ioctl）
     * @param state 输出：设备状态（CAN_STATE_*）
     * @return 0成功, 其他失败
     */
    virtual int getDeviceState(uint8_t& state);

protected:
    int m_socket;                  // SocketCAN socket文件描述符
    std::string m_deviceName;      // 设备名称（如 "can0"）
    uint32_t m_baudrate;           // 波特率（仅用于记录）
    bool m_isOpen;                 // 是否已打开
    struct sockaddr_can m_addr;    // SocketCAN地址结构
};

#endif // CAN_COMMUNICATION_H

