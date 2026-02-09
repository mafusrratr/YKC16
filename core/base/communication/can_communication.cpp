/**
 * CAN总线通信基类实现（基于SocketCAN）
 * BY ZF
 * 
 * 日志实现：使用 std::cerr 输出到标准错误流
 * 注意：std::cerr 是输出流，不是异常，不能被 catch 捕获
 * 如果需要异常处理，应该：
 * 1. 使用 try-catch 捕获函数返回的错误码（当前方式）
 * 2. 或者抛出 C++ 异常（throw std::runtime_error）
 * 3. 或者使用 LogSender 统一日志管理（推荐，后续可改进）
 * 
 * 接收方式：非阻塞模式
 * - receive() 方法立即返回，有数据返回0，无数据返回-1
 * - 调用者需要自己实现循环和延时来控制接收频率
 */

#include "can_communication.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <iostream>

CANCommunication::CANCommunication()
    : m_socket(-1)
    , m_baudrate(0)
    , m_isOpen(false)
{
    memset(&m_addr, 0, sizeof(m_addr));
}

CANCommunication::~CANCommunication()
{
    close();
}

bool CANCommunication::open(const std::string& deviceName, uint32_t baudrate)
{
    if (m_isOpen) {
        std::cerr << "[CAN] Device already open: " << m_deviceName << std::endl;
        return false;
    }
    
    // 创建SocketCAN socket
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        std::cerr << "[CAN] Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 获取网络接口索引
    struct ifreq ifr;
    strncpy(ifr.ifr_name, deviceName.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    
    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "[CAN] Failed to get interface index for " << deviceName 
                  << ": " << strerror(errno) << std::endl;
        ::close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // 绑定到CAN接口
    m_addr.can_family = AF_CAN;
    m_addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(m_socket, (struct sockaddr *)&m_addr, sizeof(m_addr)) < 0) {
        std::cerr << "[CAN] Failed to bind to " << deviceName 
                  << ": " << strerror(errno) << std::endl;
        ::close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // 设置为非阻塞模式（所有接收操作都是非阻塞的）
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
    
    m_deviceName = deviceName;
    m_baudrate = baudrate;
    m_isOpen = true;
    
    std::cout << "[CAN] Successfully opened " << deviceName 
              << " (baudrate: " << baudrate << ")" << std::endl;
    return true;
}

void CANCommunication::close()
{
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
    m_isOpen = false;
    m_deviceName.clear();
    m_baudrate = 0;
}

int CANCommunication::send(uint32_t canId, const uint8_t* data, uint8_t dataLen)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    // BY ZF: CAN帧最大数据长度为8字节
    if (dataLen > 8) {
        std::cerr << "[CAN] Data length too large: " << (int)dataLen << std::endl;
        return -1;
    }
    
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    
    // 扩展帧（29位ID）
    frame.can_id = (canId & CAN_EFF_MASK) | CAN_EFF_FLAG;
    frame.can_dlc = dataLen;
    memcpy(frame.data, data, dataLen);
    
    ssize_t nbytes = write(m_socket, &frame, sizeof(frame));
    if (nbytes < 0) {
        std::cerr << "[CAN] Failed to send frame: " << strerror(errno) << std::endl;
        return -1;
    }
    
    if (nbytes != sizeof(frame)) {
        std::cerr << "[CAN] Incomplete send: " << nbytes << " bytes" << std::endl;
        return -1;
    }
    
    return 0;
}

int CANCommunication::receive(uint32_t& canId, uint8_t* data, uint8_t& dataLen)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    struct can_frame frame;
    
    // 非阻塞接收（socket已设置为O_NONBLOCK）
    ssize_t nbytes = read(m_socket, &frame, sizeof(frame));
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 无数据，非阻塞返回
            return -1;
        }
        std::cerr << "[CAN] Failed to receive frame: " << strerror(errno) << std::endl;
        return -1;
    }
    
    if (nbytes != sizeof(frame)) {
        std::cerr << "[CAN] Incomplete receive: " << nbytes << " bytes" << std::endl;
        return -1;
    }
    
    // 解析CAN帧（扩展帧，29位ID）
    canId = frame.can_id & CAN_EFF_MASK;
    dataLen = frame.can_dlc;
    // BY ZF: CAN帧最大数据长度为8字节
    if (dataLen > 8) {
        dataLen = 8;
    }
    memcpy(data, frame.data, dataLen);
    
    return 0;
}

int CANCommunication::setFilter(uint32_t canId, uint32_t mask)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    struct can_filter filter;
    
    // 扩展帧（29位ID）
    filter.can_id = (canId & CAN_EFF_MASK) | CAN_EFF_FLAG;
    filter.can_mask = (mask & CAN_EFF_MASK) | CAN_EFF_FLAG;
    
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
        std::cerr << "[CAN] Failed to set filter: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

int CANCommunication::clearFilter()
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    // 设置空过滤器以清除所有过滤器
    struct can_filter filter[0];
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FILTER, filter, 0) < 0) {
        std::cerr << "[CAN] Failed to clear filter: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

int CANCommunication::setErrorFrameFilter(bool enable)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    int error_mask = enable ? CAN_ERR_MASK : 0;
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &error_mask, sizeof(error_mask)) < 0) {
        std::cerr << "[CAN] Failed to set error frame filter: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

int CANCommunication::getErrorCount(uint32_t& txErrors, uint32_t& rxErrors)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    // BY ZF: SocketCAN的错误统计需要通过/proc/net/can/stats读取
    // 当前简化实现，返回0（表示无错误）
    // TODO: 后续可完善从/proc/net/can/stats读取错误统计
    txErrors = 0;
    rxErrors = 0;
    
    return 0;
}

int CANCommunication::getDeviceState(uint8_t& state)
{
    if (!m_isOpen || m_socket < 0) {
        std::cerr << "[CAN] Device not open" << std::endl;
        return -1;
    }
    
    // BY ZF: SocketCAN的设备状态需要通过ioctl(SIOCGIFSTATS)或/proc/net/can/stats读取
    // 当前简化实现，如果设备已打开则认为状态正常
    // TODO: 后续可完善从/proc/net/can/stats读取设备状态
    state = 0;  // 0 表示正常状态
    
    return 0;
}

