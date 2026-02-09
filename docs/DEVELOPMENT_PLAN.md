# TCU项目后续模块开发计划

## 当前完成状态

### ✅ 已完成模块
1. **Logger 模块** - 日志进程（完成）
   - 消息队列接收日志
   - 数据库存储（运行日志、交易记录、计费模型）
   - 共享内存更新（计费模型）
   - 数据库备份机制

2. **Daemon 模块** - 守护进程（完成）
   - 进程监控和管理
   - 看门狗机制
   - 进程启动/重启

3. **基础架构** - 已完成重构
   - 接口设计优化（只保留必要的接口）
   - base 目录重组
   - 统一使用 ConfigManagerLite

### 📋 待开发模块
1. **charge_logic** - 充电逻辑进程（核心业务）
2. **meter** - 计量计费进程（部分代码需重构）
3. **pile_controller** - 主控通信进程
4. **communication** - 平台通信进程
5. **display** - 显示界面进程（Qt）

## 开发顺序建议

### 方案A：自底向上（推荐）

**优先级1：基础数据采集层**
1. **pile_controller（主控通信进程）** - 2-3周
   - **为什么优先**：充电逻辑需要主控的状态数据（电压、电流、功率等）
   - **依赖**：共享内存（已有）、消息队列（已有）
   - **输出**：主控状态数据 → 共享内存
   - **接口**：使用进程内部的 `IPileController` 接口
   - **实现**：先实现一个简单版本（如共享内存方式），后续可扩展 CAN/485

2. **meter（计量计费进程）** - 2-3周
   - **为什么优先**：充电逻辑需要电表数据用于计费
   - **依赖**：共享内存（已有）、logger（已完成）
   - **输出**：电表读数、计费结果 → 共享内存
   - **接口**：使用进程内部的 `IMeter` 接口
   - **重构**：已有部分代码（evs_meter_oop），需要重构为多进程架构

**优先级2：核心业务逻辑层**
3. **charge_logic（充电逻辑进程）** - 4-5周
   - **为什么核心**：这是整个系统的核心业务逻辑
   - **依赖**：
     - 共享内存（已有）
     - pile_controller 的状态数据
     - meter 的电表数据
     - logger（已完成）
   - **功能**：
     - 多枪状态机（Idle → Plugged → Authorize → StartReady → Charging → StopHandle → Settlement → Fault）
     - 订单管理
     - 鉴权处理（调用 communication 或本地处理）
     - 计费计算（调用 meter）
     - 状态更新到共享内存
   - **特点**：不需要接口，直接实现

**优先级3：对外通信层**
4. **communication（平台通信进程）** - 2-3周
   - **为什么后续**：需要 charge_logic 的交易数据上报
   - **依赖**：
     - 共享内存（已有）
     - logger（已完成）
     - charge_logic 的交易数据
   - **接口**：使用进程内部的 `ICommunication` 接口
   - **实现**：先实现 TCP，后续可扩展 UDP/MQTT

5. **display（显示界面进程）** - 2-3周（可选，优先级较低）
   - **为什么最后**：主要是展示，不阻塞核心功能
   - **依赖**：共享内存（已有，读取所有数据）
   - **技术**：Qt 界面

### 方案B：核心优先（备选）

如果希望快速看到核心业务逻辑运行，可以先开发 charge_logic，使用模拟数据：
1. charge_logic（使用模拟的主控和电表数据）
2. pile_controller
3. meter
4. communication
5. display

**不推荐**：因为 charge_logic 需要真实数据才能完整测试。

## 详细开发计划

### 阶段1：pile_controller（主控通信进程）

#### 1.1 需求分析
- **输入**：从共享内存读取控制命令（启动/停止/参数设置）
- **输出**：主控状态数据写入共享内存
- **通信方式**：先实现共享内存方式（最简单），后续扩展 CAN/485/网络

#### 1.2 设计要点
- 实现 `IPileController` 接口
- 先实现 `SharedMemoryPileController`（最简单）
- 后续可扩展 `CANPileController`、`RS485PileController` 等
- 通过消息队列接收控制命令
- 周期性地从主控读取状态并更新共享内存

#### 1.3 文件结构
```
pile_controller/
├── ipile_controller.h          # ✅ 已存在
├── shared_memory_pile_controller.h/cpp  # 共享内存方式（使用libv2gshm）
├── can_pile_controller.h/cpp            # CAN方式（使用base/communication/can_communication）
├── network_pile_controller.h/cpp        # TCP/UDP方式（使用base/communication/network_communication）
├── serial_pile_controller.h/cpp         # RS485方式（使用base/communication/serial_communication）
├── pile_controller_process.h/cpp
├── main.cpp
├── Makefile
├── Makefile.cross
└── README.md
```

**依赖关系**：
- `can_pile_controller` → `base/communication/can_communication`
- `network_pile_controller` → `base/communication/network_communication`
- `serial_pile_controller` → `base/communication/serial_communication`
- `shared_memory_pile_controller` → `libv2gshm`（已有）

#### 1.4 开发步骤

**步骤1：创建通信基础组件（base/communication/）**
1. 创建 `base/communication/` 目录
2. 实现 `can_communication.h/cpp`（CAN通信基类）
3. 实现 `network_communication.h/cpp`（TCP/UDP通信基类）
4. 实现 `serial_communication.h/cpp`（串口通信基类）
5. 测试基础组件

**步骤2：实现pile_controller**
1. 创建进程框架（继承 BaseProcess）
2. 实现 SharedMemoryPileController（使用libv2gshm）
3. 实现 CANPileController（使用base/communication/can_communication）
4. 实现 NetworkPileController（使用base/communication/network_communication）
5. 实现 SerialPileController（使用base/communication/serial_communication）
6. 实现 PileControllerProcess（根据配置选择实现）
7. 实现消息队列处理（接收控制命令）
8. 实现共享内存读写（状态数据）
9. 测试和调试

---

### 阶段2：meter（计量计费进程）

#### 2.1 需求分析
- **输入**：从电表读取电量数据（DL698/Modbus等）
- **输出**：电表读数、计费结果写入共享内存
- **计费模型**：从共享内存读取（由 logger 更新）

#### 2.2 设计要点
- 实现 `IMeter` 接口
- 先实现 `DL698Meter`（已有部分代码）
- 后续可扩展 `ModbusMeter` 等
- 周期性地读取电表数据
- 根据计费模型计算费用
- 更新共享内存

#### 2.3 文件结构
```
meter/
├── imeter.h                    # ✅ 已存在
├── dl698_meter.h/cpp           # 重构现有代码
├── meter_process.h/cpp
├── main.cpp
├── Makefile
├── Makefile.cross
└── README.md
```

#### 2.4 开发步骤
1. 分析现有代码（evs_meter_oop）
2. 提取核心功能（电表读取、计费计算）
3. 创建进程框架（继承 BaseProcess）
4. 实现 DL698Meter（实现 IMeter）
5. 实现 MeterProcess（根据配置选择实现）
6. 实现共享内存读写
7. 测试和调试

---

### 阶段3：charge_logic（充电逻辑进程）

#### 3.1 需求分析
- **核心功能**：多枪充电状态机管理
- **输入**：
  - 共享内存：插枪遥信、主控状态、电表数据、计费模型
  - 消息队列：平台命令（远程启动/停止）、鉴权结果
- **输出**：
  - 共享内存：充电状态、订单信息
  - 消息队列：交易记录（发送给 logger）

#### 3.2 设计要点
- **多枪支持**：每个枪独立状态机实例
- **状态机**：Idle → Plugged → Authorize → StartReady → Charging → StopHandle → Settlement → Fault
- **数据拉取**：主动从共享内存读取数据（不暴露回调）
- **订单管理**：生成交易记录，发送给 logger
- **鉴权处理**：调用 communication 或本地处理
- **计费计算**：调用 meter 或直接读取共享内存

#### 3.3 文件结构
```
charge_logic/
├── charge_state_machine.h/cpp  # 单枪状态机
├── charge_logic_process.h/cpp  # 主进程（管理多枪）
├── order_manager.h/cpp          # 订单管理
├── auth_handler.h/cpp           # 鉴权处理
├── main.cpp
├── Makefile
├── Makefile.cross
└── README.md
```

#### 3.4 开发步骤
1. 设计状态机类（单枪）
2. 实现状态机各状态逻辑
3. 实现多枪管理（ChargeLogicProcess）
4. 实现订单管理
5. 实现鉴权处理（先本地，后续对接 communication）
6. 实现共享内存读写
7. 实现消息队列处理
8. 测试和调试

---

### 阶段4：communication（平台通信进程）

#### 4.1 需求分析
- **输入**：
  - 共享内存：充电状态、交易记录
  - 消息队列：平台命令
- **输出**：
  - 网络：状态上报、交易记录上传
  - 消息队列：鉴权结果、远程启动/停止命令

#### 4.2 设计要点
- 实现 `ICommunication` 接口
- 先实现 `TCPCommunication`（最常用）
- 后续可扩展 `UDPCommunication`、`MQTTCommunication` 等
- 周期性地从共享内存读取数据并上报
- 接收平台命令并发送到消息队列

#### 4.3 文件结构
```
communication/
├── icommunication.h            # ✅ 已存在
├── tcp_communication.h/cpp
├── communication_process.h/cpp
├── main.cpp
├── Makefile
├── Makefile.cross
└── README.md
```

#### 4.4 开发步骤
1. 创建进程框架（继承 BaseProcess）
2. 实现 TCPCommunication（实现 ICommunication）
3. 实现 CommunicationProcess（根据配置选择实现）
4. 实现消息队列处理（接收命令、发送事件）
5. 实现共享内存读取（状态、交易记录）
6. 实现网络通信（连接、心跳、数据上报）
7. 测试和调试

---

### 阶段5：display（显示界面进程，可选）

#### 5.1 需求分析
- **输入**：共享内存（读取所有数据）
- **输出**：Qt 界面显示

#### 5.2 设计要点
- Qt 界面
- 只读共享内存（不修改数据）
- 实时刷新显示

#### 5.3 开发步骤
1. 设计 Qt 界面布局
2. 实现共享内存读取
3. 实现界面更新逻辑
4. 测试和调试

---

## 开发原则

### 1. 增量开发
- 每个模块先实现最小可用版本（MVP）
- 后续逐步完善功能

### 2. 接口优先
- 先定义接口，再实现
- 接口放在进程内部（已重构完成）

### 3. 基础组件复用
- **通信基础组件**：放在 `base/communication/`，供多个模块复用
  - CAN通信基类（pile_controller、meter等可能使用）
  - 网络通信基类（pile_controller、communication等使用）
  - 串口通信基类（pile_controller、meter、communication等使用）
- **共享内存**：已有libv2gshm，直接使用

### 4. 测试驱动
- 每个模块都要有测试程序
- 使用模拟数据测试独立功能

### 5. 文档同步
- 每个模块都要有 README.md
- 重要设计决策记录在文档中

## 时间估算

| 模块 | 预计时间 | 优先级 |
|------|---------|--------|
| pile_controller | 2-3周 | 高 |
| meter | 2-3周 | 高 |
| charge_logic | 4-5周 | 最高 |
| communication | 2-3周 | 中 |
| display | 2-3周 | 低 |

**总计**：12-17周（约3-4个月）

## 下一步行动

### 立即开始：pile_controller
1. 创建目录结构
2. 创建进程框架
3. 实现 SharedMemoryPileController
4. 实现基本功能
5. 测试验证

---

**建议**：从 pile_controller 开始，因为它是最基础的数据采集层，其他模块都依赖它。

