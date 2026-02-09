# PileController 模块架构设计

## 三层架构设计

### 1. 通信基类层（base/communication/）
**职责**：底层通信处理，只负责数据收发，不涉及业务协议

- `CANCommunication` - CAN帧的收发（SocketCAN）
- `NetworkCommunication` - TCP/UDP数据包的收发
- `SerialCommunication` - 串口字节流的收发
- `SharedMemory` - 共享内存的读写（libv2gshm）

**特点**：
- 通用性：可被多个模块复用（pile_controller、meter、communication等）
- 无业务语义：只负责原始数据的传输
- 非阻塞：所有接收操作都是非阻塞的

### 2. 规约层（protocol/）
**职责**：协议封装，将业务数据转换为通信数据格式

#### 2.1 CAN规约
- `CAN2CCUProtocol` - CAN总线上的充电桩主控协议
  - 协议格式：CAN ID定义、数据包格式、校验方式
  - 功能：将 `tcu2ccu_data.h` 的业务数据编码/解码为CAN帧

#### 2.2 网络规约
- `UDP2CDZProtocol` - UDP上的CDZ协议
- `TCPPileProtocol` - TCP上的自定义充电桩协议
  - 协议格式：数据包头、命令码、数据体、校验
  - 功能：将业务数据封装为网络数据包

#### 2.3 串口规约
- `ModbusPileProtocol` - Modbus RTU协议
- `SerialPileProtocol` - 自定义串口协议
  - 协议格式：帧头、地址、功能码、数据、CRC
  - 功能：将业务数据封装为串口数据帧

#### 2.4 共享内存规约
- `SharedMemoryPileProtocol` - 共享内存协议
  - 协议格式：直接读写共享内存结构体
  - 功能：将业务数据映射到共享内存结构

**特点**：
- 协议相关：每个规约对应一种具体的通信协议
- 可替换：同一通信方式可以支持多种规约（如CAN上可以有CDZ协议、自定义协议）
- 独立测试：规约层可以独立测试，不依赖具体通信实现

### 3. 应用层（IPileController实现）
**职责**：业务逻辑，实现充电桩主控的完整功能

- `CANPileController` - 使用 `CANCommunication` + `CAN2CCUProtocol`
- `NetworkPileController` - 使用 `NetworkCommunication` + `UDP2CDZProtocol`/`TCPPileProtocol`
- `SerialPileController` - 使用 `SerialCommunication` + `ModbusPileProtocol`/`SerialPileProtocol`
- `SharedMemoryPileController` - 使用 `SharedMemoryPileProtocol`

**扩展能力**：
- `IPileController`：仅保留通用业务接口（初始化、启停、状态）
- CAN 规约相关的 tcu2ccu_data 读写能力由 `CANPileController` 直接提供

**特点**：
- 业务语义：实现 `IPileController` 接口的业务方法
- 组合使用：组合通信基类和规约层
- 配置驱动：根据配置选择通信方式和规约

## 数据流示例

### CAN通信流程
```
业务层：CANPileController::setStartChargeData()
    ↓
业务层：IPileController::startCharge()
    ↓
应用层：CANPileController::startCharge()
    ↓ 使用 tcu2ccu_data 命令数据
规约层：CAN2CCUProtocol::encodeStartCharge()
    ↓ 封装为CAN帧（ID + 数据）
通信层：CANCommunication::send()
    ↓ SocketCAN系统调用
硬件：CAN总线
```

### 状态读取流程（反向）
```
硬件：CAN总线
    ↓ SocketCAN接收
通信层：CANCommunication::receive()
    ↓ 原始CAN帧
规约层：CAN2CCUProtocol::decodeFrame()
    ↓ 更新 tcu2ccu_data 缓存并汇总 PileStatus
应用层：CANPileController::getStatus()
    ↓ 返回业务数据
业务层：IPileController::getStatus()
```

## 目录结构

```
pile_controller/
├── ipile_controller.h              # 业务接口定义
├── pile_controller_process.h/cpp  # 进程主类
├── main.cpp                        # 进程入口
│
├── protocol/                       # 规约层（新增）
│   ├── can2ccu_protocol.h/cpp          # CAN规约
│   ├── tcu2ccu_data.h                  # CAN业务数据定义
│   ├── udp2cdz_protocol.h/cpp          # UDP2CDZ规约
│   ├── tcp_pile_protocol.h/cpp         # TCP规约
│   ├── modbus_pile_protocol.h/cpp      # Modbus规约
│   ├── serial_pile_protocol.h/cpp      # 自定义串口规约
│   └── shm_pile_protocol.h/cpp         # 共享内存规约
│
└── controllers/                    # 应用层（IPileController实现）
    ├── can_pile_controller.h/cpp       # CAN实现
    ├── network_pile_controller.h/cpp    # 网络实现
    ├── serial_pile_controller.h/cpp    # 串口实现
    └── shm_pile_controller.h/cpp        # 共享内存实现
```

## 配置示例（独立配置模式）

**设计原则**：所有枪均采用独立配置方式，每个枪必须明确配置所有参数，不支持全局默认配置。

```ini
[PileController]
# 充电枪数量（1-10）
gun_count=4

# 全局协议参数（所有枪共用）
packet_timeout=5
retry_count=3

# ========== 枪1配置（gun_no=1，从1开始）==========
gun1_comm_type=can
gun1_device_path=can0
gun1_baudrate=250000
gun1_protocol_type=cdz
gun1_can_id_base=0x180      # CAN ID基址（用于计算该枪的CAN ID）

# ========== 枪2配置（gun_no=2）==========
gun2_comm_type=can
gun2_device_path=can0
gun2_baudrate=250000
gun2_protocol_type=cdz
gun2_can_id_base=0x181

# ========== 枪3配置（gun_no=3，使用串口通信）==========
gun3_comm_type=485
gun3_device_path=/dev/ttyS1
gun3_baudrate=9600
gun3_protocol_type=modbus
gun3_modbus_address=1        # Modbus从站地址

# ========== 枪4配置（gun_no=4，使用网络通信）==========
gun4_comm_type=udp
gun4_ip=192.168.1.201
gun4_port=5001
gun4_protocol_type=udp2cdz

# ========== 枪5-10配置（如果存在，格式相同）==========
# gun5_comm_type=can
# gun5_device_path=can0
# gun5_baudrate=250000
# gun5_protocol_type=cdz
# gun5_can_id_base=0x184
# 
# gun6_comm_type=485
# gun6_device_path=/dev/ttyS2
# gun6_baudrate=9600
# gun6_protocol_type=modbus
# gun6_modbus_address=2
# 
# ... 枪7-10配置类似（注意：协议参数使用全局配置）
```

### 配置项说明

#### 必需配置项（每个枪都必须配置）

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| `gun{N}_comm_type` | 通信方式 | `can`, `485`, `tcp`, `udp`, `shm` |
| `gun{N}_protocol_type` | 规约类型 | `cdz`, `custom`, `modbus`, `udp2cdz`, `direct` |

#### CAN通信配置项（comm_type=can时必需）

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| `gun{N}_device_path` | CAN设备路径 | `can0`, `can1` |
| `gun{N}_baudrate` | CAN波特率 | `250000`, `500000` |
| `gun{N}_can_id_base` | CAN ID基址 | `0x180`, `0x200` |

#### 串口通信配置项（comm_type=485时必需）

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| `gun{N}_device_path` | 串口设备路径 | `/dev/ttyS1`, `/dev/ttyUSB0` |
| `gun{N}_baudrate` | 波特率 | `9600`, `115200` |
| `gun{N}_modbus_address` | Modbus从站地址（protocol_type=modbus时） | `1`, `2` |

#### 网络通信配置项（comm_type=tcp/udp时必需）

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| `gun{N}_ip` | 网络地址 | `192.168.1.201` |
| `gun{N}_port` | 网络端口 | `5001`, `8080` |

#### 共享内存配置项（comm_type=shm时必需）

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| `gun{N}_shm_key` | 共享内存Key | `85001` |

### 配置解析规则

1. **枪号范围**：1-10（gun_no从1开始）
2. **配置验证**：
   - 每个枪必须配置 `gun{N}_comm_type` 和 `gun{N}_protocol_type`
   - 根据 `comm_type` 验证对应的必需配置项是否存在
   - 全局协议参数（`packet_timeout`、`retry_count`）必须配置
   - 配置缺失时，初始化失败并报错
3. **CAN ID使用**：
   - 直接使用配置的 `gun{N}_can_id_base` 值
   - 规约层根据命令类型在此基址上计算具体CAN ID
4. **网络端口使用**：
   - 直接使用配置的 `gun{N}_port` 值
5. **Modbus地址使用**：
   - 直接使用配置的 `gun{N}_modbus_address` 值
6. **协议参数使用**：
   - 全局协议参数（`packet_timeout`、`retry_count`）所有枪共用

## 优势

1. **职责分离**：
   - 通信层：只负责数据传输
   - 规约层：只负责协议封装
   - 应用层：只负责业务逻辑

2. **可复用性**：
   - 通信基类可被其他模块复用
   - 规约层可独立测试和替换

3. **可扩展性**：
   - 新增通信方式：只需实现通信基类
   - 新增规约：只需实现规约类
   - 同一通信方式可支持多种规约

4. **可维护性**：
   - 各层职责清晰，易于理解和维护
   - 协议变更只需修改规约层

## 多枪配置设计

### 配置结构体设计

```cpp
// 单个枪的配置
struct GunConfig {
    uint8_t gunNo;                    // 枪号（1-10）
    std::string commType;              // 通信方式
    std::string devicePath;            // 设备路径
    uint32_t baudrate;                 // 波特率
    std::string protocolType;          // 规约类型
    std::string networkHost;           // 网络地址（网络通信时）
    uint16_t networkPort;              // 网络端口（网络通信时）
    uint32_t canIdBase;                // CAN ID基址（CAN通信时）
    uint8_t modbusAddress;             // Modbus地址（串口通信时）
    int shmKey;                        // 共享内存Key（共享内存通信时）
};

// 全局配置
struct PileControllerConfig {
    uint8_t gunCount;                  // 枪数量（1-10）
    std::vector<GunConfig> gunConfigs; // 每个枪的配置（独立配置）
    
    // BY ZF: 全局协议参数（所有枪共用）
    int packetTimeout;                 // 数据包超时（秒）
    int retryCount;                    // 重传次数
};
```

### 配置解析逻辑

```cpp
class PileControllerProcess {
private:
    PileControllerConfig m_config;
    
    bool loadGunConfigs() {
        ConfigManagerLite& config = getConfig();
        
        // 读取枪数量
        m_config.gunCount = config.getInt("PileController", "gun_count", 2);
        if (m_config.gunCount < 1 || m_config.gunCount > 10) {
            std::cerr << "[PileController] Invalid gun_count: " << m_config.gunCount 
                      << " (must be 1-10)" << std::endl;
            return false;
        }
        
        // 为每个枪加载独立配置
        m_config.gunConfigs.resize(m_config.gunCount);
        for (uint8_t i = 0; i < m_config.gunCount; i++) {
            uint8_t gunNo = i + 1;  // 枪号从1开始
            GunConfig& gunCfg = m_config.gunConfigs[i];
            gunCfg.gunNo = gunNo;
            
            std::string gunPrefix = "gun" + std::to_string(gunNo) + "_";
            
            // 读取必需配置项（不存在则报错）
            gunCfg.commType = config.getString("PileController", gunPrefix + "comm_type", "");
            if (gunCfg.commType.empty()) {
                std::cerr << "[PileController] Missing gun" << gunNo << "_comm_type" << std::endl;
                return false;
            }
            
            gunCfg.protocolType = config.getString("PileController", gunPrefix + "protocol_type", "");
            if (gunCfg.protocolType.empty()) {
                std::cerr << "[PileController] Missing gun" << gunNo << "_protocol_type" << std::endl;
                return false;
            }
            
            // 根据通信方式读取对应配置项
            if (gunCfg.commType == "can") {
                gunCfg.devicePath = config.getString("PileController", gunPrefix + "device_path", "");
                gunCfg.baudrate = config.getInt("PileController", gunPrefix + "baudrate", 0);
                gunCfg.canIdBase = config.getInt("PileController", gunPrefix + "can_id_base", 0);
                
                if (gunCfg.devicePath.empty() || gunCfg.baudrate == 0 || gunCfg.canIdBase == 0) {
                    std::cerr << "[PileController] Invalid CAN config for gun" << gunNo << std::endl;
                    return false;
                }
            } else if (gunCfg.commType == "485") {
                gunCfg.devicePath = config.getString("PileController", gunPrefix + "device_path", "");
                gunCfg.baudrate = config.getInt("PileController", gunPrefix + "baudrate", 0);
                gunCfg.modbusAddress = config.getInt("PileController", gunPrefix + "modbus_address", 0);
                
                if (gunCfg.devicePath.empty() || gunCfg.baudrate == 0) {
                    std::cerr << "[PileController] Invalid serial config for gun" << gunNo << std::endl;
                    return false;
                }
            } else if (gunCfg.commType == "tcp" || gunCfg.commType == "udp") {
                gunCfg.networkHost = config.getString("PileController", gunPrefix + "ip", "");
                gunCfg.networkPort = static_cast<uint16_t>(config.getInt("PileController", gunPrefix + "port", 0));
                
                if (gunCfg.networkHost.empty() || gunCfg.networkPort == 0) {
                    std::cerr << "[PileController] Invalid network config for gun" << gunNo << std::endl;
                    return false;
                }
            } else if (gunCfg.commType == "shm") {
                // BY ZF: 共享内存配置可选，使用默认值
                gunCfg.shmKey = config.getInt("PileController", gunPrefix + "shm_key", 0);
            } else {
                std::cerr << "[PileController] Unknown comm_type for gun" << gunNo 
                          << ": " << gunCfg.commType << std::endl;
                return false;
            }
        }
        
        // BY ZF: 读取全局协议参数（所有枪共用）
        m_config.packetTimeout = config.getInt("PileController", "packet_timeout", 0);
        m_config.retryCount = config.getInt("PileController", "retry_count", 0);
        
        if (m_config.packetTimeout == 0 || m_config.retryCount == 0) {
            std::cerr << "[PileController] Missing global protocol parameters: "
                      << "packet_timeout=" << m_config.packetTimeout 
                      << ", retry_count=" << m_config.retryCount << std::endl;
            return false;
        }
        
        return true;
    }
};
```

### 多枪控制器管理

```cpp
class PileControllerProcess {
private:
    // 每个枪一个控制器实例
    std::vector<std::unique_ptr<IPileController>> m_controllers;
    
    bool createControllers() {
        m_controllers.resize(m_config.gunCount);
        
        for (uint8_t i = 0; i < m_config.gunCount; i++) {
            const GunConfig& gunCfg = m_config.gunConfigs[i];
            
            // 根据配置创建对应的控制器
            if (gunCfg.commType == "can") {
                m_controllers[i] = createCANController(gunCfg);
            } else if (gunCfg.commType == "485") {
                m_controllers[i] = createSerialController(gunCfg);
            } else if (gunCfg.commType == "shm") {
                m_controllers[i] = createSharedMemoryController(gunCfg);
            } else if (gunCfg.commType == "tcp" || gunCfg.commType == "udp") {
                m_controllers[i] = createNetworkController(gunCfg);
            } else {
                std::cerr << "Unknown comm_type for gun" << gunCfg.gunNo << std::endl;
                return false;
            }
        }
        
        return true;
    }
    
    void updateStatusFromController() {
        // 遍历所有枪，更新状态
        for (uint8_t i = 0; i < m_config.gunCount; i++) {
            if (m_controllers[i]) {
                PileStatus status;
                if (m_controllers[i]->getStatus(m_config.gunConfigs[i].gunNo, &status) == 0) {
                    // 更新共享内存中对应枪的状态
                    updateSharedMemoryStatus(status);
                }
            }
        }
    }
};
```

### 多枪场景下的数据流

```
控制命令流（下行）：
charge_logic进程
    ↓ (消息队列，包含gun_no)
PileControllerProcess::processCommandMessage()
    ↓ 根据gun_no选择对应控制器
m_controllers[gun_no-1]->startCharge()
    ↓
对应枪的控制器（CAN/485/网络/共享内存）
    ↓
充电桩主控板（对应枪）

状态数据流（上行）：
充电桩主控板（所有枪）
    ↓
PileControllerProcess::updateStatusFromController()
    ↓ 遍历所有枪的控制器
m_controllers[0..gun_count-1]->getStatus()
    ↓ 更新共享内存
共享内存（按gun_no索引）
```

## 实现顺序建议

1. **先实现规约层类**：定义具体协议类（如 `CAN2CCUProtocol`）
2. **实现多枪配置解析**：支持统一配置和独立配置
3. **实现共享内存规约**：最简单，验证架构
4. **实现共享内存控制器**：组合使用验证流程
5. **实现CAN规约**：使用 `CANCommunication` 基类
6. **实现CAN控制器**：组合使用，支持多枪
7. **扩展其他规约**：按需实现
