/**
 * 网络通信基类实现（TCP/UDP）
 * BY ZF
 * 
 * 日志实现：使用 std::cerr 输出到标准错误流
 * 接收方式：非阻塞模式
 * - receive() 和 receiveFrom() 方法立即返回，有数据返回字节数，无数据返回-1
 * - 调用者需要自己实现循环和延时来控制接收频率
 */

#include "network_communication.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <iostream>

NetworkCommunication::NetworkCommunication(NetworkType type)
    : m_type(type)
    , m_socket(-1)
    , m_port(0)
    , m_isConnected(false)
{
}

NetworkCommunication::~NetworkCommunication()
{
    disconnect();
}

int NetworkCommunication::connect(const std::string& host, uint16_t port, int timeoutMs)
{
    if (m_isConnected && m_socket >= 0) {
        std::cerr << "[Network] Already connected" << std::endl;
        return 0;
    }
    
    if (m_type == NETWORK_TCP) {
        // TCP连接
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0) {
            std::cerr << "[Network] Failed to create TCP socket: " << strerror(errno) << std::endl;
            return -1;
        }
        
        // 设置地址
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        // 解析主机名或IP地址
        if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
            // 不是IP地址，尝试解析主机名
            struct hostent* he = gethostbyname(host.c_str());
            if (he == nullptr) {
                std::cerr << "[Network] Failed to resolve host: " << host << std::endl;
                ::close(m_socket);
                m_socket = -1;
                return -1;
            }
            memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        }
        
        // 设置非阻塞模式（所有接收操作都是非阻塞的）
        int flags = fcntl(m_socket, F_GETFL, 0);
        fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        
        // 尝试连接
        int ret = ::connect(m_socket, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0) {
            if (errno == EINPROGRESS) {
                // 连接进行中，使用select等待
                fd_set writefds;
                struct timeval timeout;
                FD_ZERO(&writefds);
                FD_SET(m_socket, &writefds);
                
                timeout.tv_sec = timeoutMs / 1000;
                timeout.tv_usec = (timeoutMs % 1000) * 1000;
                
                ret = select(m_socket + 1, nullptr, &writefds, nullptr, &timeout);
                if (ret <= 0) {
                    std::cerr << "[Network] TCP connect timeout or error" << std::endl;
                    ::close(m_socket);
                    m_socket = -1;
                    return -1;
                }
                
                // 检查连接是否成功
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                    std::cerr << "[Network] TCP connect failed: " << strerror(error) << std::endl;
                    ::close(m_socket);
                    m_socket = -1;
                    return -1;
                }
            } else {
                std::cerr << "[Network] TCP connect failed: " << strerror(errno) << std::endl;
                ::close(m_socket);
                m_socket = -1;
                return -1;
            }
        }
        
        // 恢复阻塞模式（可选，根据需求）
        // fcntl(m_socket, F_SETFL, flags);
        
    } else if (m_type == NETWORK_UDP) {
        // UDP绑定
        m_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0) {
            std::cerr << "[Network] Failed to create UDP socket: " << strerror(errno) << std::endl;
            return -1;
        }
        
        // 绑定到本地地址
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (host.empty() || host == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
                std::cerr << "[Network] Invalid UDP bind address: " << host << std::endl;
                ::close(m_socket);
                m_socket = -1;
                return -1;
            }
        }
        
        if (bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Network] UDP bind failed: " << strerror(errno) << std::endl;
            ::close(m_socket);
            m_socket = -1;
            return -1;
        }
        
        // UDP设置为非阻塞模式
        int flags = fcntl(m_socket, F_GETFL, 0);
        fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
    } else {
        std::cerr << "[Network] Unsupported network type" << std::endl;
        return -1;
    }
    
    m_host = host;
    m_port = port;
    m_isConnected = true;
    
    std::cout << "[Network] Successfully " << (m_type == NETWORK_TCP ? "connected to" : "bound to")
              << " " << host << ":" << port << std::endl;
    return 0;
}

void NetworkCommunication::disconnect()
{
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
    m_isConnected = false;
    m_host.clear();
    m_port = 0;
}

int NetworkCommunication::send(const uint8_t* data, size_t dataLen)
{
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not connected" << std::endl;
        return -1;
    }
    
    ssize_t nbytes = ::send(m_socket, data, dataLen, 0);
    if (nbytes < 0) {
        std::cerr << "[Network] Send failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return static_cast<int>(nbytes);
}

int NetworkCommunication::receive(uint8_t* data, size_t maxLen)
{
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not connected" << std::endl;
        return -1;
    }
    
    // 非阻塞接收（socket已设置为O_NONBLOCK）
    ssize_t nbytes = ::recv(m_socket, data, maxLen, 0);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 无数据，非阻塞返回
            return -1;
        }
        std::cerr << "[Network] Receive failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    if (nbytes == 0 && m_type == NETWORK_TCP) {
        // TCP连接关闭
        disconnect();
        return -1;
    }
    
    return static_cast<int>(nbytes);
}

int NetworkCommunication::sendTo(const std::string& host, uint16_t port, const uint8_t* data, size_t dataLen)
{
    if (m_type != NETWORK_UDP) {
        std::cerr << "[Network] sendTo only supported for UDP" << std::endl;
        return -1;
    }
    
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not bound" << std::endl;
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
        struct hostent* he = gethostbyname(host.c_str());
        if (he == nullptr) {
            std::cerr << "[Network] Failed to resolve host: " << host << std::endl;
            return -1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    ssize_t nbytes = sendto(m_socket, data, dataLen, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (nbytes < 0) {
        std::cerr << "[Network] sendTo failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return static_cast<int>(nbytes);
}

int NetworkCommunication::receiveFrom(uint8_t* data, size_t maxLen, std::string& host, uint16_t& port)
{
    if (m_type != NETWORK_UDP) {
        std::cerr << "[Network] receiveFrom only supported for UDP" << std::endl;
        return -1;
    }
    
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not bound" << std::endl;
        return -1;
    }
    
    // 非阻塞接收（socket已设置为O_NONBLOCK）
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    ssize_t nbytes = recvfrom(m_socket, data, maxLen, 0, (struct sockaddr*)&addr, &addrLen);
    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 无数据，非阻塞返回
            return -1;
        }
        std::cerr << "[Network] receiveFrom failed: " << strerror(errno) << std::endl;
        return -1;
    }
    
    // 解析源地址
    host = inet_ntoa(addr.sin_addr);
    port = ntohs(addr.sin_port);
    
    return static_cast<int>(nbytes);
}

int NetworkCommunication::setNoDelay(bool enable)
{
    if (m_type != NETWORK_TCP) {
        std::cerr << "[Network] setNoDelay only supported for TCP" << std::endl;
        return -1;
    }
    
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not connected" << std::endl;
        return -1;
    }
    
    int flag = enable ? 1 : 0;
    if (setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        std::cerr << "[Network] Failed to set TCP_NODELAY: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

int NetworkCommunication::setSendTimeout(int timeoutMs)
{
    if (!m_isConnected || m_socket < 0) {
        std::cerr << "[Network] Not connected" << std::endl;
        return -1;
    }
    
    struct timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    
    if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "[Network] Failed to set send timeout: " << strerror(errno) << std::endl;
        return -1;
    }
    
    return 0;
}

