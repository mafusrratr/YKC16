# TCU项目接口与基础架构重构方案

## 一、当前问题分析

### 1.1 接口设计问题

**当前状态：**
```
interfaces/
├── iprocess.h          ✅ 所有进程都需要（合理）
├── ilogger.h           ❌ 只有LoggerProcess一个实现（过度设计）
├── itcu_logic.h     ❌ 只有ChargeLogicProcess一个实现（过度设计）
├── icommunication.h    ⚠️  可能有多种实现（但应该放在进程内部）
├── ipile_controller.h ⚠️  可能有多种实现（但应该放在进程内部）
└── imeter.h            ⚠️  可能有多种实现（但应该放在进程内部）
```

**问题：**
1. **一个进程 = 一个接口 = 一个实现**：这是过度设计
2. **接口位置不当**：接口应该用于"进程内部有多种实现"的场景
3. **接口职责不清**：全局接口 vs 进程内部接口混淆

### 1.2 Base目录组织问题

**当前状态：**
```
base/
├── common/          # 通用组件（配置、消息队列）
├── logger/          # 日志相关（LogSender）
└── process/         # 进程管理（BaseProcess）
```

**问题：**
1. **logger/ 目录**：LogSender 是工具类，不是基础类，应该放在 common/ 或 logger 进程内部
2. **组织原则不清**：按功能 vs 按模块 vs 按层次？

### 1.3 依赖关系问题

**当前依赖链：**
```
进程模块 → interfaces/ → base/ → 具体实现
```

**问题：**
- 接口层和基础层职责重叠
- 依赖关系复杂，难以理解

## 二、重构目标

### 2.1 设计原则

1. **接口用于多种实现**：只有需要多种实现的场景才使用接口
2. **接口就近原则**：接口应该放在使用它的模块内部
3. **基础层职责清晰**：只提供真正通用的基础设施
4. **依赖关系简单**：减少不必要的抽象层

### 2.2 重构目标

1. ✅ 保留 `IProcess`（所有进程都需要）
2. ❌ 删除 `ILogger`（只有一个实现）
3. ❌ 删除 `IChargeLogic`（只有一个实现）
4. ⚠️  将 `ICommunication`、`IPileController`、`IMeter` 移到各自进程内部
5. 🔄 优化 `base/` 目录组织

## 三、重构方案

### 3.1 接口层重构

#### 方案A：最小化接口（推荐）

**保留的接口：**
```
core/interfaces/
└── iprocess.h          # 所有进程都需要实现
```

**删除的接口：**
- `ilogger.h` → 删除接口，保留类型定义（LogLevel, LogEntry, TradeRecord, FeeModel）
- `itcu_logic.h` → 完全删除
- `ipile_controller.h` → 移到 `core/pile_controller/ipile_controller.h`
- `imeter.h` → 移到 `core/meter/imeter.h`

**类型定义处理：**
- `ilogger.h` 中的类型定义（LogLevel, LogEntry, TradeRecord, FeeModel）移到 `core/base/common/logger_types.h`
- 所有模块通过 `#include "../base/common/logger_types.h"` 使用

#### 方案B：保留全局接口（备选）

如果其他进程需要调用这些接口，可以保留全局接口，但需要明确用途。

**建议采用方案A**，因为：
- 进程间通过消息队列和共享内存通信，不需要直接调用接口
- 接口应该用于进程内部的多种实现

### 3.2 Base目录重构

#### 当前结构问题

```
base/
├── common/          # ✅ 通用组件（合理）
│   ├── config_manager_lite.h/cpp
│   ├── config_manager.h/cpp        # ⚠️  未使用，可删除
│   └── message_queue.h/cpp
├── logger/          # ❌ LogSender 应该放在 common/ 或 logger 进程内部
│   └── log_sender.h/cpp
└── process/         # ✅ 进程管理（合理）
    ├── base_process.h/cpp
    └── process_monitor.h
```

#### 重构后结构

```
base/
├── common/          # 通用基础设施（所有模块都可能使用）
│   ├── config_manager_lite.h/cpp   # 配置管理
│   ├── message_queue.h/cpp         # 消息队列
│   └── logger_types.h              # 日志类型定义（从 ilogger.h 移入）
│
├── logger/          # 日志相关工具（多个进程都需要使用）
│   └── log_sender.h/cpp            # 日志发送器（供所有业务进程使用）
│
└── process/         # 进程管理基础设施
    ├── base_process.h/cpp          # 进程基类
    └── process_monitor.h           # 进程监控（守护进程使用）
```

**调整说明：**
1. **保留 `base/logger/`**：`LogSender` 是多个进程都需要使用的工具类，保留在 `base/logger/` 目录
2. **更新 `LogSender` 的 include**：从 `../../interfaces/ilogger.h` 改为 `../common/logger_types.h`
3. **删除 `base/common/config_manager.h/cpp`**：未使用，保留 `config_manager_lite.h/cpp`
4. **新增 `base/common/logger_types.h`**：从 `ilogger.h` 提取类型定义

### 3.3 进程模块重构

#### 3.3.1 Logger进程

**当前：**
```
logger/
├── logger_process.h/cpp    # 实现 IProcess 和 ILogger
└── ...
```

**重构后：**
```
logger/
├── logger_process.h/cpp    # 只实现 IProcess，不再实现 ILogger
└── ...
```

**修改点：**
- `LoggerProcess` 不再继承 `ILogger`
- `LoggerProcess` 直接实现日志功能（不需要接口）
- `LogSender` 保留在 `base/logger/`（多个进程都需要使用）

#### 3.3.2 Communication进程

**重构后：**
```
communication/
├── icommunication.h        # 从 interfaces/ 移入（进程内部接口）
├── tcp_communication.h/cpp    # TCP实现
├── udp_communication.h/cpp    # UDP实现
├── mqtt_communication.h/cpp   # MQTT实现
├── rs485_communication.h/cpp  # RS485实现
├── communication_process.h/cpp # 进程主类（使用接口）
└── ...
```

**使用方式：**
```cpp
class CommunicationProcess : public BaseProcess {
private:
    ICommunication* m_comm;  // 根据配置选择实现
    
public:
    bool doInitialize() override {
        std::string commType = getConfig().getString("Communication", "type", "tcp");
        
        if (commType == "tcp") {
            m_comm = new TCPCommunication();
        } else if (commType == "udp") {
            m_comm = new UDPCommunication();
        } else if (commType == "mqtt") {
            m_comm = new MQTTCommunication();
        }
        
        return m_comm->initialize(...);
    }
};
```

#### 3.3.3 PileController进程

**重构后：**
```
pile_controller/
├── ipile_controller.h      # 从 interfaces/ 移入（进程内部接口）
├── can_pile_controller.h/cpp   # CAN实现
├── rs485_pile_controller.h/cpp # RS485实现
├── shm_pile_controller.h/cpp   # 共享内存实现
├── network_pile_controller.h/cpp # 网络实现
├── pile_controller_process.h/cpp # 进程主类
└── ...
```

#### 3.3.4 Meter进程

**重构后：**
```
meter/
├── imeter.h                # 从 interfaces/ 移入（进程内部接口）
├── dl698_meter.h/cpp       # DL698实现
├── modbus_meter.h/cpp      # Modbus实现
├── custom_meter.h/cpp      # 自定义实现
├── meter_process.h/cpp     # 进程主类
└── ...
```

#### 3.3.5 ChargeLogic进程

**重构后：**
```
 tcu_logic/
├── tcu_logic_process.h/cpp  # 直接实现，不需要接口
├── charge_state_machine.h/cpp  # 状态机实现
├── gun_context.h               # 枪上下文
└── ...
```

**修改点：**
- 删除 `IChargeLogic` 接口
- `ChargeLogicProcess` 直接实现业务逻辑

## 四、文件迁移清单

### 4.1 删除的文件

```
core/interfaces/
├── ilogger.h              # 删除，类型定义移到 base/common/logger_types.h
├── itcu_logic.h        # 完全删除
├── icommunication.h       # 移到 communication/ 目录
├── ipile_controller.h     # 移到 pile_controller/ 目录
└── imeter.h               # 移到 meter/ 目录

core/base/common/
└── config_manager.h/cpp  # 删除（未使用）
```

### 4.2 新增的文件

```
core/base/common/
└── logger_types.h         # 从 ilogger.h 提取类型定义

└── icommunication.h       # 从 interfaces/ 移入

core/pile_controller/
└── ipile_controller.h    # 从 interfaces/ 移入

core/meter/
└── imeter.h               # 从 interfaces/ 移入
```

### 4.3 修改的文件（不移动，只更新 include）

```
core/base/logger/
└── log_sender.h/cpp       # 更新 include：从 ilogger.h 改为 logger_types.h
```

### 4.4 修改的文件

**需要更新 include 路径的文件：**

1. **LogSender 本身**
   ```cpp
   // log_sender.h 修改前
   #include "../../interfaces/ilogger.h"
   
   // log_sender.h 修改后
   #include "../common/logger_types.h"
   ```

2. **所有使用 LogSender 的文件**
   ```cpp
   // 路径不变，仍然是：
   #include "../base/logger/log_sender.h"  // 从其他进程
   // 或
   #include "../../base/logger/log_sender.h"  // 从更深层目录
   ```

2. **所有使用 logger_types 的文件（除了 LogSender）**
   ```cpp
   // 修改前
   #include "../../interfaces/ilogger.h"
   
   // 修改后
   #include "../base/common/logger_types.h"  // 从进程目录
   // 或
   #include "../../base/common/logger_types.h"  // 从更深层目录
   ```

3. **LoggerProcess**
   ```cpp
   // 修改前
   class LoggerProcess : public IProcess, public ILogger {
   
   // 修改后
   class LoggerProcess : public BaseProcess {
       // 不再继承 ILogger
   ```

4. **所有使用 ICommunication/IPileController/IMeter 的文件**
   ```cpp
   // 修改前
   #include "../../interfaces/icommunication.h"
   
   // 修改后
   #include "icommunication.h"  // 进程内部
   ```

## 五、重构步骤

### 阶段1：准备阶段（1天）

1. ✅ 创建 `base/common/logger_types.h`，从 `ilogger.h` 提取类型定义
2. ✅ 更新所有使用 logger_types 的文件
3. ✅ 测试编译，确保类型定义可用

### 阶段2：Logger模块重构（1天）

1. ✅ 创建 `base/common/logger_types.h`，从 `ilogger.h` 提取类型定义
2. ✅ 更新 `base/logger/log_sender.h/cpp`，将 include 从 `ilogger.h` 改为 `logger_types.h`
3. ✅ 修改 `LoggerProcess`，移除 `ILogger` 继承
4. ✅ 更新所有使用 logger_types 的文件（除了 LogSender）
5. ✅ 测试编译和运行

### 阶段3：接口迁移（2-3天）

1. ✅ 将 `icommunication.h` 移到 `communication/` 目录
2. ✅ 将 `ipile_controller.h` 移到 `pile_controller/` 目录
3. ✅ 将 `imeter.h` 移到 `meter/` 目录
4. ✅ 更新所有 include 路径
5. ✅ 测试编译

### 阶段4：清理阶段（1天）

1. ✅ 删除 `itcu_logic.h`
2. ✅ 删除 `ilogger.h`（类型定义已移到 logger_types.h）
3. ✅ 删除 `base/common/config_manager.h/cpp`
4. ✅ 更新文档
5. ✅ 最终测试

## 六、风险评估

### 6.1 风险点

1. **Include 路径修改**：可能遗漏某些文件
   - **缓解措施**：使用 grep 全面搜索，逐个验证

2. **编译错误**：路径修改后可能出现编译错误
   - **缓解措施**：分阶段重构，每阶段完成后测试编译

3. **运行时错误**：接口移除后可能出现运行时问题
   - **缓解措施**：充分测试，特别是已完成的 logger 和 daemon 模块

### 6.2 回滚方案

如果重构出现问题，可以：
1. 保留原 `interfaces/` 目录作为备份
2. 使用 Git 分支管理，出现问题立即回滚
3. 分阶段提交，每阶段独立可回滚

## 七、预期收益

### 7.1 架构清晰度

- ✅ 接口职责明确：只用于多种实现的场景
- ✅ 依赖关系简单：减少不必要的抽象层
- ✅ 模块组织合理：相关代码放在一起

### 7.2 可维护性

- ✅ 更容易理解：减少抽象层，代码更直观
- ✅ 更容易扩展：新功能按模块组织
- ✅ 更容易测试：模块独立，便于单元测试

### 7.3 性能

- ✅ 减少虚函数调用：移除不必要的接口
- ✅ 编译速度提升：减少头文件依赖

## 八、评审要点

### 8.1 需要确认的问题

1. **接口迁移位置**：是否同意将接口移到进程内部？
2. **类型定义位置**：`logger_types.h` 放在 `base/common/` 是否合适？
3. **LogSender 位置**：保留在 `base/logger/` 目录（多个进程都需要使用）是否合适？
4. **重构顺序**：是否同意分阶段重构？

### 8.2 需要讨论的问题

1. **ICommunication 等接口**：如果其他进程需要调用，是否保留全局接口？
2. **向后兼容**：是否需要保持向后兼容？
3. **文档更新**：哪些文档需要同步更新？

---

## 附录：重构前后对比

### 目录结构对比

**重构前：**
```
core/
├── interfaces/          # 6个接口文件
│   ├── iprocess.h
│   ├── ilogger.h
│   ├── itcu_logic.h
│   ├── icommunication.h
│   ├── ipile_controller.h
│   └── imeter.h
├── base/
│   ├── common/
│   ├── logger/         # LogSender
│   └── process/
└── logger/
    └── logger_process.h/cpp
```

**重构后：**
```
core/
├── interfaces/          # 1个接口文件（仅保留进程基类接口）
│   └── iprocess.h
├── base/
│   ├── common/
│   │   ├── config_manager_lite.h/cpp
│   │   ├── message_queue.h/cpp
│   │   └── logger_types.h        # ✅ 新增：从 ilogger.h 提取
│   ├── logger/                   # ✅ 保留（多个进程都需要）
│   │   └── log_sender.h/cpp      # ✅ 更新 include 路径
│   └── process/
│       ├── base_process.h/cpp
│       └── process_monitor.h
├── logger/
│   └── logger_process.h/cpp      # ✅ 移除 ILogger 继承
├── communication/
│   └── icommunication.h          # ✅ 从 interfaces/ 移入
├── pile_controller/
│   └── ipile_controller.h        # ✅ 从 interfaces/ 移入
└── meter/
    └── imeter.h                   # ✅ 从 interfaces/ 移入
```
├── communication/
│   ├── icommunication.h          # 从 interfaces/ 移入
│   ├── tcp_communication.h/cpp
│   └── communication_process.h/cpp
├── pile_controller/
│   ├── ipile_controller.h        # 从 interfaces/ 移入
│   └── pile_controller_process.h/cpp
└── meter/
    ├── imeter.h                   # 从 interfaces/ 移入
    └── meter_process.h/cpp
```

---

**方案制定时间：** 2025-01-XX  
**方案版本：** v1.0  
**待评审确认后执行**
