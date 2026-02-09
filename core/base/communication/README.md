# 通信基础组件

## 概述

本目录提供可复用的通信基础组件，供多个模块使用：
- **CAN通信** - CAN总线通信基类
- **网络通信** - TCP/UDP网络通信基类
- **串口通信** - RS485/RS232串口通信基类

## 设计原则

1. **基础功能封装**：只提供底层的通信操作（打开、关闭、发送、接收）
2. **协议无关**：不涉及具体的业务协议，协议解析在业务模块中实现
3. **可复用**：多个模块都可以使用这些基础组件

## 使用方式

### CAN通信（基于SocketCAN）

```cpp
#include "../base/communication/can_communication.h"

class CustomCANProtocol : public CANCommunication {
public:
    int sendCommand(uint32_t cmdId, const uint8_t* data, uint8_t len) {
        // 实现具体的协议封装
        uint8_t frame[8];
        // ... 协议封装逻辑
        return send(cmdId, frame, 8);  // 扩展帧（29位ID）
    }
    
    int receiveResponse(uint32_t& respId, uint8_t* data, uint8_t& len) {
        uint32_t canId;
        uint8_t frame[8];
        uint8_t frameLen;
        // 非阻塞接收，需要循环调用
        if (receive(canId, frame, frameLen) == 0) {
            // 实现具体的协议解析
            // ...
            return 0;
        }
        return -1;  // 无数据
    }
    
    // 带延时的接收循环示例
    int receiveResponseWithTimeout(uint32_t& respId, uint8_t* data, uint8_t& len, int timeoutMs) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (receiveResponse(respId, data, len) == 0) {
                return 0;  // 成功接收
            }
            
            // 检查超时
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= timeoutMs) {
                return -1;  // 超时
            }
            
            // 短暂延时，避免CPU占用过高
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

// 使用
// 注意：SocketCAN需要在系统启动时配置，例如：
//   ip link set can0 type can bitrate 250000
//   ip link set up can0

CustomCANProtocol can;
if (can.open("can0", 250000)) {  // 注意：设备名是 "can0"，不是 "/dev/can0"
    can.sendCommand(0x123, data, 8);
    // ...
    can.close();
}
```

**SocketCAN配置说明**：
- 设备名称格式：`can0`, `can1` 等（不是 `/dev/can0`）
- 波特率需要在系统启动时通过 `ip` 命令配置，例如：
  ```bash
  ip link set can0 type can bitrate 250000
  ip link set up can0
  ```
- 或者通过 `canconfig` 工具配置（如果系统支持）

**注意**：本实现仅支持扩展帧（29位ID），不支持标准帧（11位ID）。如果接收到标准帧，会自动忽略。

### 网络通信（TCP/UDP）

```cpp
#include "../base/communication/network_communication.h"

class CustomTCPProtocol : public NetworkCommunication {
public:
    CustomTCPProtocol() : NetworkCommunication(NETWORK_TCP) {}
    
    int sendMessage(const std::string& msg) {
        // 实现具体的协议封装
        uint8_t buffer[1024];
        // ... 协议封装逻辑
        return send(buffer, len);
    }
    
    int receiveMessage(std::string& msg) {
        uint8_t buffer[1024];
        // 非阻塞接收，需要循环调用
        int len = receive(buffer, sizeof(buffer));
        if (len > 0) {
            // 实现具体的协议解析
            // ...
            return 0;
        }
        return -1;  // 无数据
    }
    
    // 带延时的接收循环示例
    int receiveMessageWithTimeout(std::string& msg, int timeoutMs) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (receiveMessage(msg) == 0) {
                return 0;  // 成功接收
            }
            
            // 检查超时
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= timeoutMs) {
                return -1;  // 超时
            }
            
            // 短暂延时，避免CPU占用过高
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

// 使用
CustomTCPProtocol tcp;
if (tcp.connect("192.168.1.100", 8080) == 0) {
    tcp.sendMessage("Hello");
    // ...
    tcp.disconnect();
}
```

### 串口通信

```cpp
#include "../base/communication/serial_communication.h"

class ModbusRTUProtocol : public SerialCommunication {
public:
    int readHoldingRegisters(uint8_t slaveId, uint16_t startAddr, 
                            uint16_t count, uint16_t* values) {
        // 实现Modbus RTU协议
        uint8_t request[8];
        // ... 构建Modbus请求
        if (send(request, 8) == 8) {
            uint8_t response[256];
            // 非阻塞接收，需要循环调用
            int len = receive(response, sizeof(response));
            if (len > 0) {
                // ... 解析Modbus响应
                return 0;
            }
        }
        return -1;
    }
};

// 使用
ModbusRTUProtocol serial;
SerialParams params;
params.baudrate = 9600;
params.dataBits = 8;
params.stopBits = 1;
params.parity = 'E';
if (serial.open("/dev/ttyUSB0", params)) {
    uint16_t values[10];
    serial.readHoldingRegisters(1, 0, 10, values);
    // ...
    serial.close();
}
```

## 模块使用场景

### pile_controller 模块
- **CAN通信**：与主控板通过CAN总线通信
- **网络通信**：与主控板通过TCP/UDP通信
- **串口通信**：与主控板通过RS485通信
- **共享内存**：与主控板通过共享内存通信（已有libv2gshm）

### communication 模块
- **网络通信**：与平台服务器通过TCP/UDP/MQTT通信
- **串口通信**：与平台通过RS485通信（较少见）

### meter 模块
- **串口通信**：与电表通过RS485通信（DL698、Modbus等）

## 实现计划

### 阶段1：基础框架（当前）
- ✅ 定义基类接口
- ✅ 实现基础功能

### 阶段2：具体实现
- ✅ CAN通信实现（使用SocketCAN，非阻塞接收）
- ✅ 网络通信实现（使用标准Socket API，非阻塞接收）
- ✅ 串口通信实现（使用termios，非阻塞接收）

### 阶段3：测试和优化
- 单元测试
- 性能优化
- 错误处理完善

## 后续测试验证项

### CAN通信诊断接口测试

以下接口的实现需要在实际测试环境中验证和完善：

1. **`getErrorCount()`** - 获取错误统计
   - 当前实现：简化版，返回固定值0
   - 需要验证：从 `/proc/net/can/stats` 或通过正确的 ioctl 获取实际错误计数
   - 测试方法：在CAN总线上产生错误，验证是否能正确读取错误计数

2. **`getDeviceState()`** - 获取CAN设备状态
   - 当前实现：简化版，返回固定值0
   - 需要验证：正确解析 CAN 设备状态（CAN_STATE_ERROR_ACTIVE、CAN_STATE_ERROR_WARNING、CAN_STATE_ERROR_PASSIVE、CAN_STATE_BUS_OFF 等）
   - 测试方法：模拟CAN总线错误状态，验证是否能正确读取设备状态

## 多进程访问同一CAN设备

### SocketCAN特性

**重要**：SocketCAN允许多个进程同时打开同一个CAN设备（如 `can0`），这是CAN总线的广播特性决定的。

### 行为说明

1. **接收（多进程共享）**
   - 多个进程可以同时打开同一个CAN设备
   - 每个进程都会收到**所有**CAN帧（广播特性）
   - 使用 `setFilter()` 可以在驱动层过滤，减少CPU负载

2. **发送（需要协调）**
   - 多个进程可以同时发送CAN帧
   - SocketCAN本身**不提供**发送互斥锁
   - 如果多个进程同时发送，可能会产生冲突

### 解决方案

#### 方案1：应用层协议协调（推荐）
- 使用CAN ID来区分不同的发送者
- 在应用层实现发送队列或令牌机制
- 例如：不同进程使用不同的CAN ID范围

#### 方案2：单进程发送（简单）
- 只有一个进程负责发送
- 其他进程只负责接收
- 通过消息队列或共享内存协调

#### 方案3：进程间互斥（复杂）
- 使用文件锁（`flock`）或信号量（`semaphore`）来协调发送
- 在 `send()` 方法前后加锁
- 注意：这会影响性能，不推荐

### 当前实现

**当前 `CANCommunication` 类不提供进程间互斥**，因为：
1. SocketCAN本身支持多进程访问
2. 互斥应该在应用层根据具体需求实现
3. 保持基础组件的简单性

### 使用建议

```cpp
// 场景1：单进程使用（最简单）
CANCommunication can;
can.open("can0", 250000);
// 发送和接收都在同一个进程

// 场景2：多进程接收，单进程发送（推荐）
// 进程A：只接收
CANCommunication canA;
canA.open("can0", 250000);
canA.setFilter(0x100, 0x700);  // 只接收特定ID范围

// 进程B：只发送
CANCommunication canB;
canB.open("can0", 250000);
// 发送时使用不同的CAN ID

// 场景3：多进程都需要发送（需要应用层协调）
// 使用文件锁或消息队列协调发送
```

---

**注意**：这些基础组件只提供底层的通信操作，具体的业务协议（如主控协议、平台协议、电表协议）在各自的业务模块中实现。

