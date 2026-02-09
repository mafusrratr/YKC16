# PileController 架构 UML 图

## 概述

本文档包含 `pile_controller` 模块的 UML 架构图，展示了三层架构的设计和类之间的关系。

## 查看 UML 图

### 方法1：使用 PlantUML 工具

如果已安装 PlantUML：

```bash
# 安装 PlantUML（macOS）
brew install plantuml

# 生成 PNG 图片
plantuml ARCHITECTURE_UML.puml

# 生成 SVG 图片（矢量图，推荐）
plantuml -tsvg ARCHITECTURE_UML.puml
```

### 方法2：使用在线工具

1. 访问 [PlantUML Online Server](http://www.plantuml.com/plantuml/uml/)
2. 复制 `ARCHITECTURE_UML.puml` 文件内容
3. 粘贴到在线编辑器中查看

### 方法3：使用 IDE 插件

- **VS Code**: 安装 "PlantUML" 插件
- **IntelliJ IDEA**: 安装 "PlantUML integration" 插件
- **Cursor**: 安装 "PlantUML" 扩展

## 架构说明

### 三层架构

```
┌─────────────────────────────────────┐
│  应用层 (Controller Layer)          │
│  - CANPileController                 │
│  - 实现 IPileController 接口         │
│  - 业务逻辑、状态管理、接收线程       │
└──────────────┬──────────────────────┘
               │ 组合使用
┌──────────────┴──────────────────────┐
│  规约层 (Protocol Layer)             │
│  - CAN2CCUProtocol                   │
│  - 编码/解码、多帧处理、校验和         │
└──────────────┬──────────────────────┘
               │ 通过回调调用
┌──────────────┴──────────────────────┐
│  通信基类层 (Communication Layer)   │
│  - CANCommunication                  │
│  - SocketCAN封装、CAN帧收发          │
└─────────────────────────────────────┘
```

### 关键关系

1. **继承关系**：
   - `CANPileController` 实现 `IPileController`

2. **组合关系**：
   - `CANPileController` 组合 `CANCommunication` 和 `CAN2CCUProtocol`

3. **依赖关系**：
   - `CAN2CCUProtocol` 通过回调函数调用 `CANCommunication::send()`

### 数据流

#### 发送流程（业务 → 硬件）

```
业务调用 startCharge()
    ↓
CANPileController::startCharge()
    ↓ 使用 tcu2ccu_data 中的命令数据
CAN2CCUProtocol::encodeStartCharge()
    ↓ 编码业务数据为CAN帧格式
CAN2CCUProtocol::sendSingleFrame/sendMultiFrame()
    ↓ 调用发送回调
CANPileController::sendCallbackWrapper()
    ↓
CANCommunication::send()
    ↓ SocketCAN
硬件 CAN 总线
```

#### 接收流程（硬件 → 业务）

```
硬件 CAN 总线
    ↓ SocketCAN
CANCommunication::receive()
    ↓ 非阻塞接收循环
CANPileController::receiveThread()
    ↓ 解析CAN ID，识别帧类型
CAN2CCUProtocol::decodeFrame()
    ↓ 解码CAN帧为业务数据
CAN2CCUProtocol::decodeYC20Frame() / decodeYX22Frame() / ...
    ↓ 填充成员变量
CANPileController::onStatusChanged()
    ↓ 业务回调
业务层处理状态变化
```

### 主要类说明

#### CANPileController（应用层）

- **职责**：业务逻辑封装、状态管理、接收线程管理
- **关键成员**：
  - `m_canComm`: CAN通信对象
  - `m_protocol`: CAN2CCU协议对象
  - `m_receiveThread`: 接收线程（非阻塞循环接收）

#### CAN2CCUProtocol（规约层）

- **职责**：协议编码/解码、多帧处理、校验和计算
- **关键成员**：
  - `m_sendCallback`: 发送回调函数（用于调用通信层）
  - `m_yc20Data`, `m_yx22Data` 等：业务数据缓存
  - `m_longFrameContexts`: 多帧接收上下文

#### CANCommunication（通信层）

- **职责**：SocketCAN封装、CAN帧收发
- **关键方法**：
  - `send()`: 发送CAN帧
  - `receive()`: 非阻塞接收CAN帧

## 扩展性

### 添加新协议（相同通信方式）

1. 创建新的 Protocol 类（如 `CAN2CCUProtocol`）
2. 创建新的 Controller 类（实现 `IPileController`，组合新的 Protocol）
3. 复用 `CANCommunication` 通信基类

### 添加新通信方式

1. 创建新的 Communication 基类（如 `SPICommunication`）
2. 创建对应的 Protocol 类
3. 创建对应的 Controller 类

## 相关文档

- [架构扩展指南](ARCHITECTURE_EXTENSION.md)
- [协议分析文档](protocol/PROTOCOL_ANALYSIS.md)
- [业务数据结构定义](protocol/tcu2ccu_data.h)
