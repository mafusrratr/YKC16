/**
 * 串口通信基类（RS485/RS232等）
 * 提供通用的串口通信功能，供多个模块复用
 * BY ZF
 */

#ifndef SERIAL_COMMUNICATION_H
#define SERIAL_COMMUNICATION_H

#include <string>
#include <stdint.h>

/**
 * 串口参数
 */
struct SerialParams {
    uint32_t baudrate;         // 波特率（如 9600, 115200 等）
    uint8_t dataBits;          // 数据位（5, 6, 7, 8）
    uint8_t stopBits;          // 停止位（1, 2）
    char parity;               // 校验位（'N'无校验, 'E'偶校验, 'O'奇校验）
    bool flowControl;          // 流控（true启用, false禁用）
    
    SerialParams() 
        : baudrate(115200)
        , dataBits(8)
        , stopBits(1)
        , parity('N')
        , flowControl(false)
    {}
};

/**
 * 串口通信基类
 * 封装串口的基本操作（打开、关闭、发送、接收）
 * 
 * 使用方式：
 * 1. 继承此类实现具体的串口协议（如Modbus、DL698等）
 * 2. 在子类中实现协议解析和封装
 * 
 * 示例：
 * class ModbusRTUProtocol : public SerialCommunication {
 *     // 实现Modbus RTU协议逻辑
 * };
 */
class SerialCommunication {
public:
    SerialCommunication();
    virtual ~SerialCommunication();
    
    /**
     * 打开串口设备
     * @param devicePath 设备路径（如 "/dev/ttyUSB0", "/dev/ttyS0" 等）
     * @param params 串口参数
     * @return true成功, false失败
     */
    virtual bool open(const std::string& devicePath, const SerialParams& params);
    
    /**
     * 关闭串口设备
     */
    virtual void close();
    
    /**
     * 检查串口是否已打开
     * @return true已打开, false未打开
     */
    bool isOpen() const { return m_fd >= 0; }
    
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
     * 清空接收缓冲区
     * @return 0成功, 其他失败
     */
    virtual int flush();
    
    /**
     * 设置串口参数（动态修改）
     * @param params 串口参数
     * @return 0成功, 其他失败
     */
    virtual int setParams(const SerialParams& params);
    
    /**
     * 获取串口参数
     * @param params 输出：串口参数
     */
    void getParams(SerialParams& params) const { params = m_params; }
    
    /**
     * 获取串口状态
     * @param dcd 输出：DCD信号（数据载波检测）
     * @param dsr 输出：DSR信号（数据设备就绪）
     * @param cts 输出：CTS信号（清除发送）
     * @return 0成功, 其他失败
     */
    virtual int getStatus(bool& dcd, bool& dsr, bool& cts);

protected:
    int m_fd;                      // 串口设备文件描述符
    std::string m_devicePath;      // 设备路径
    SerialParams m_params;          // 串口参数
    bool m_isOpen;                 // 是否已打开
};

#endif // SERIAL_COMMUNICATION_H

