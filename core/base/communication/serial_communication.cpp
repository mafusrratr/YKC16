/**
 * 串口通信基类实现（RS485/RS232等）
 * BY ZF
 * 
 * 日志实现：使用 std::cerr 输出到标准错误流
 * 接收方式：非阻塞模式
 * - receive() 方法立即返回，有数据返回字节数，无数据返回-1
 * - 调用者需要自己实现循环和延时来控制接收频率
 */

#include "serial_communication.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iostream>

SerialCommunication::SerialCommunication()
    : m_fd(-1)
    , m_isOpen(false)
{
}

SerialCommunication::~SerialCommunication()
{
    close();
}

bool SerialCommunication::open(const std::string& devicePath, const SerialParams& params)
{
    if (m_isOpen) {
        std::cerr << "[Serial] Device already open: " << m_devicePath << std::endl;
        return false;
    }
    
    // 打开串口设备（非阻塞模式）
    m_fd = ::open(devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        std::cerr << "[Serial] Failed to open device: " << devicePath 
                  << ", error: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 配置串口参数
    struct termios tty;
    if (tcgetattr(m_fd, &tty) != 0) {
        std::cerr << "[Serial] Failed to get terminal attributes: " << strerror(errno) << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    
    // 设置波特率
    speed_t speed = B115200;
    // BY ZF: 扩展低速波特率支持（电表/工控设备常用 1200/2400/4800）
    switch (params.baudrate) {

        case 300: speed = B300; break;
        case 1200: speed = B1200; break;
        case 2400: speed = B2400; break;
        case 4800: speed = B4800; break;
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:
            std::cerr << "[Serial] Unsupported baudrate: " << params.baudrate << std::endl;
            ::close(m_fd);
            m_fd = -1;
            return false;
    }
    
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    
    // 设置数据位
    tty.c_cflag &= ~CSIZE;
    switch (params.dataBits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            std::cerr << "[Serial] Unsupported data bits: " << (int)params.dataBits << std::endl;
            ::close(m_fd);
            m_fd = -1;
            return false;
    }
    
    // 设置停止位
    if (params.stopBits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }
    
    // 设置校验位
    tty.c_cflag &= ~PARENB;  // 清除校验位
    tty.c_iflag &= ~INPCK;   // 禁用输入校验
    if (params.parity == 'E' || params.parity == 'e') {
        tty.c_cflag |= PARENB;  // 启用校验
        tty.c_cflag &= ~PARODD; // 偶校验
        tty.c_iflag |= INPCK;   // 启用输入校验
    } else if (params.parity == 'O' || params.parity == 'o') {
        tty.c_cflag |= PARENB;  // 启用校验
        tty.c_cflag |= PARODD;  // 奇校验
        tty.c_iflag |= INPCK;   // 启用输入校验
    }
    
    // 设置流控
    if (params.flowControl) {
        tty.c_cflag |= CRTSCTS;
    } else {
        tty.c_cflag &= ~CRTSCTS;
    }
    
    // 原始模式（不进行任何处理）
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~OPOST;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR);
    
    // 设置读取参数（非阻塞模式）
    tty.c_cc[VMIN] = 0;   // 非阻塞读取
    tty.c_cc[VTIME] = 0;  // 无超时
    
    // 应用配置
    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        std::cerr << "[Serial] Failed to set terminal attributes: " << strerror(errno) << std::endl;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    
    // 清空输入输出缓冲区
    tcflush(m_fd, TCIOFLUSH);
    
    m_devicePath = devicePath;
    m_params = params;
    m_isOpen = true;
    
    std::cout << "[Serial] Successfully opened " << devicePath 
              << " (baudrate: " << params.baudrate 
              << ", data: " << (int)params.dataBits
              << ", stop: " << (int)params.stopBits
              << ", parity: " << params.parity << ")" << std::endl;
    return true;
}

void SerialCommunication::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_isOpen = false;
    m_devicePath.clear();
}

int SerialCommunication::send(const uint8_t* data, size_t dataLen)
{
    if (!m_isOpen || m_fd < 0) {
        std::cerr << "[Serial] Device not open" << std::endl;
        return -1;
    }
    
    ssize_t nbytes = write(m_fd, data, dataLen);
    if (nbytes < 0) {
        std::cerr << "[Serial] Send failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    // 确保数据发送完成
    tcdrain(m_fd);
    
    return static_cast<int>(nbytes);
}

int SerialCommunication::receive(uint8_t* data, size_t maxLen)
{
    if (!m_isOpen || m_fd < 0) {
        std::cerr << "[Serial] Device not open" << std::endl;
        return -1;
    }
    
    // 非阻塞接收（串口已设置为O_NONBLOCK）
    ssize_t nbytes = read(m_fd, data, maxLen);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 无数据，非阻塞返回
            return -1;
        }
        std::cerr << "[Serial] Receive failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return static_cast<int>(nbytes);
}

int SerialCommunication::flush()
{
    if (!m_isOpen || m_fd < 0) {
        std::cerr << "[Serial] Device not open" << std::endl;
        return -1;
    }
    
    if (tcflush(m_fd, TCIOFLUSH) < 0) {
        std::cerr << "[Serial] Flush failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

int SerialCommunication::setParams(const SerialParams& params)
{
    if (!m_isOpen || m_fd < 0) {
        std::cerr << "[Serial] Device not open" << std::endl;
        return -1;
    }
    
    // 关闭后重新打开以应用新参数
    close();
    return open(m_devicePath, params) ? 0 : -1;
}

int SerialCommunication::getStatus(bool& dcd, bool& dsr, bool& cts)
{
    if (!m_isOpen || m_fd < 0) {
        std::cerr << "[Serial] Device not open" << std::endl;
        return -1;
    }
    
    int status;
    if (ioctl(m_fd, TIOCMGET, &status) < 0) {
        std::cerr << "[Serial] Failed to get status: " << strerror(errno) << std::endl;
        return -1;
    }
    
    dcd = (status & TIOCM_CAR) != 0;
    dsr = (status & TIOCM_DSR) != 0;
    cts = (status & TIOCM_CTS) != 0;
    
    return 0;
}
