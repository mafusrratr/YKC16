# 守护进程更新管理功能方案

## 一、需求概述

为守护进程增加更新管理功能：
1. 接收来自其他进程的更新请求（消息中包含更新包路径）
2. 停止守护的所有业务进程
3. 拉起更新处理进程（更新器路径从配置文件读取）
4. 记录更新状态，支持更新后的回滚机制
5. 更新过程中暂停看门狗监控

## 二、设计方案

### 2.1 通信机制

**方案选择：使用系统控制消息队列（MSG_KEY_SYSTEM）**

- **队列Key**: `MSG_KEY_SYSTEM (0x1237)` - 已存在的系统控制队列
- **消息类型**: 新增 `MSG_UPDATE_REQUEST (6)` - 更新请求消息
- **消息内容**: 更新包所在路径（字符串，最大256字节）
  - 更新器（更新处理进程）路径从配置文件 `daemon.ini` 读取
  - 更新包路径仅通过消息队列提供，不从配置文件读取

**理由**：
- 系统控制队列专门用于系统级别的控制命令
- 与看门狗队列（MSG_KEY_WATCHDOG）职责分离
- 复用现有消息队列基础设施

### 2.2 消息格式

```cpp
// 消息类型定义（在 message_queue.h 中）
#define MSG_UPDATE_REQUEST 6  // 更新请求消息

// 消息内容：更新包所在路径（字符串，最大256字节）
// 示例："/usr/app/upgrade/update_package_v1.2.3.tar.gz"
// 注意：更新器（更新处理进程）路径从配置文件读取，不通过消息传递
```

### 2.3 处理流程

```
外部进程发送更新请求（消息包含更新包路径）
    ↓
守护进程接收更新请求消息（MSG_UPDATE_REQUEST）
    ↓
设置更新标志（防止重复处理）
    ↓
暂停看门狗监控（m_watchdogEnabled = false）
    ↓
停止所有业务进程（stopAllProcesses()）
    ↓
等待所有进程完全停止（确保安全）
    ↓
从配置文件读取更新器路径
    ↓
启动更新处理进程（fork + execv，传递更新包路径作为参数）
    ↓
记录更新处理进程PID
    ↓
记录更新状态到持久化文件（标记发生了更新）
    ↓
更新状态标志（m_isUpdating = true）
```

### 2.4 接口设计

#### 2.4.1 头文件新增接口（daemon_process.h）

```cpp
// BY ZF: 处理更新请求消息（在主循环doRun中调用）
void handleUpdateRequest();

// BY ZF: 执行更新管理流程（停止所有进程并启动更新处理进程）
// @param updatePackagePath 更新包所在路径（从消息中获取）
bool performUpdate(const std::string& updatePackagePath);

// BY ZF: 启动更新处理进程（执行更新：update_handler -p <path>）
// @param updatePackagePath 更新包所在路径（作为参数传递给更新器）
bool startUpdateProcess(const std::string& updatePackagePath);

// BY ZF: 启动更新处理进程（执行回滚：update_handler -r）
bool startRollbackProcess();

// BY ZF: 检查是否需要回滚（系统重启后，连续一定时间有进程未正常运行）
// 注意：通过现有的看门狗监控机制判断进程是否正常，无需额外实现检查函数

// BY ZF: 执行回滚操作（调用 update_handler -r）
bool performRollback();

// BY ZF: 记录更新状态到持久化文件
// @param status 更新状态（UPDATING/UPDATED/DONE）
// @param updatePackagePath 更新包路径（可选）
void recordUpdateStatus(const std::string& status, const std::string& updatePackagePath = "");

// BY ZF: 读取更新状态
// @param status 输出：更新状态（UPDATING/UPDATED/DONE）
// @param lastUpdatePackagePath 输出：最后一次更新的包路径
// @return true表示文件存在且有有效状态，false表示文件不存在或无效
bool readUpdateStatus(std::string& status, std::string& lastUpdatePackagePath);
```

#### 2.4.2 成员变量（daemon_process.h）

```cpp
// 消息队列
MessageQueue m_systemQueue;           // BY ZF: 系统控制队列（用于接收更新请求）

// 更新管理相关
std::string m_updateProcessPath;      // BY ZF: 更新器（更新处理进程）路径（从配置读取）
std::string m_updatePackagePath;      // BY ZF: 更新包路径（从消息中获取）
pid_t m_updateProcessPid;             // BY ZF: 更新处理进程PID（-1表示未启动）
std::atomic<bool> m_isUpdating;       // BY ZF: 是否正在更新中（防止重复处理）
std::atomic<bool> m_watchdogEnabled;  // BY ZF: 看门狗监控是否启用（更新时暂停）

// 回滚相关
std::string m_updateStatusFile;       // BY ZF: 更新状态持久化文件路径
int m_rollbackCheckInterval;           // BY ZF: 回滚检查间隔（秒）
int m_rollbackThresholdTime;          // BY ZF: 回滚阈值时间（秒，连续此时间进程异常则回滚）
std::atomic<bool> m_rollbackCheckEnabled;  // BY ZF: 是否启用回滚检查（状态为UPDATED时启用）
std::chrono::steady_clock::time_point m_lastNormalTime;  // BY ZF: 上次所有进程正常的时间（用于DONE判断和回滚判断）
```

### 2.5 配置支持

在 `daemon.ini` 配置文件中添加：

```ini
[Daemon]
# ... 其他配置 ...
# BY ZF: 更新器（更新处理进程）路径（必需）
update_process_path=/usr/app/tcu/update_handler
# BY ZF: 更新状态持久化文件路径
update_status_file=/usr/app/data/update_status.flag
# BY ZF: 回滚检查间隔（秒，用于回滚检查的轮询间隔，实际使用现有监控间隔）
rollback_check_interval=10
# BY ZF: 回滚阈值时间（秒，连续此时间进程异常则触发回滚）
rollback_threshold_time=300
# 注意：DONE阈值固定为300秒（5分钟），无需配置，直接利用现有看门狗监控机制判断

```

**配置说明**：
- **更新器路径**：从 `[Daemon]` 节的 `update_process_path` 读取（必需）
- **更新包路径**：仅通过消息队列提供，不从配置文件读取
- **更新状态文件**：用于持久化更新状态，系统重启后可以读取

### 2.6 实现位置

#### 2.6.1 消息处理位置

**采用方案A：在主循环（doRun）中处理**
- 位置：`daemon_process.cpp` 的 `doRun()` 方法
- 优点：与看门狗消息处理分离，逻辑清晰
- 实现：在主循环中非阻塞接收更新请求消息

#### 2.6.2 初始化位置

- 系统控制队列初始化：`doInitialize()` 方法中
- 配置读取：`doInitialize()` 方法中

### 2.7 错误处理

1. **重复更新请求**：通过 `m_isUpdating` 标志防止重复处理
2. **进程停止失败**：记录日志，但继续执行（强制停止）
3. **更新进程启动失败**：记录错误日志，设置标志位，可选是否重启业务进程
4. **消息接收失败**：非阻塞接收，失败不影响主流程

### 2.8 状态管理

- **正常状态**：`m_isUpdating = false`, `m_updateProcessPid = -1`
- **更新中状态**：`m_isUpdating = true`, `m_updateProcessPid = 更新进程PID`
- **更新完成**：由更新处理进程完成后，守护进程重启或根据配置决定

### 2.9 与现有功能的交互

1. **业务进程监控**：更新过程中，暂停对业务进程的监控（已停止）
2. **看门狗处理**：更新过程中，**暂停看门狗监控**（`m_watchdogEnabled = false`），不处理看门狗消息
3. **配置重载**：更新过程中，不接受配置重载请求（可选）

### 2.10 更新状态记录和回滚机制

#### 2.10.1 更新状态标志设计

更新状态标志采用三级状态机制，用于跟踪更新的完整生命周期：

**状态定义**：
1. **`UPDATING`**：更新器正在执行文件更新（调用 `update_handler -p` 成功后设置）
2. **`UPDATED`**：文件更新完成，等待进程启动验证（`update_handler` 成功完成后设置）
3. **`DONE`**：更新完成且进程运行正常（所有进程正常运行后设置）

**状态流转流程**：
```
收到更新请求
    ↓
调用 update_handler -p <path> 成功
    ↓
STATUS=UPDATING（记录更新包路径和时间戳）
    ↓
update_handler 执行文件更新完成
    ↓
STATUS=UPDATED（守护进程检测到后，启动业务进程）
    ↓
业务进程启动并运行正常（连续一段时间）
    ↓
STATUS=DONE（清除更新状态文件或保留DONE状态）
```

**状态文件格式**（简单文本格式）：
```ini
# 更新状态文件：/usr/app/data/update_status.flag
STATUS=UPDATED
PACKAGE_PATH=/usr/app/upgrade/update_package_v1.2.3.tar.gz
TIMESTAMP=1704067200
```

**状态记录时机**：
- **UPDATING**：调用 `update_handler -p` 成功后立即记录
- **UPDATED**：`update_handler` 进程成功退出后（返回码0）
- **DONE**：检测到 `UPDATED` 状态后，启动业务进程并运行正常一段时间后

#### 2.10.2 系统重启后的状态处理

**守护进程启动时检查**：
1. 读取更新状态文件（如果存在）
2. 根据状态标志执行相应操作：

**状态为 `UPDATING`**：
- 说明上次更新中断（系统在更新过程中重启）
- 可选的恢复策略：
  - 方案A：直接回滚（推荐，因为更新中断可能不完整）
  - 方案B：继续等待更新完成（需要更新器支持断点续传）

**状态为 `UPDATED`**：
- 说明文件更新已完成，但还未验证进程是否正常运行
- 执行流程：
  1. 启动所有业务进程
  2. 启动回滚检查机制（监控进程状态）
  3. 如果进程正常运行，一段时间后设置为 `DONE`
  4. 如果进程异常，超过阈值时间后执行回滚

**状态为 `DONE`**：
- 说明更新完成且已验证正常
- 清除更新状态文件（或保留作为历史记录）

#### 2.10.3 回滚检查机制

**触发条件**（仅在状态为 `UPDATED` 时）：
- 系统重启后，检测到更新状态文件，且状态为 `UPDATED`
- 启动业务进程后，在监控线程中定期检查进程状态
- 连续一定时间（`rollback_threshold_time`）内，有进程未正常运行
- 满足以上条件时，触发回滚操作

**检查流程**（利用现有看门狗监控机制）：
```
系统重启或检测到 UPDATED 状态
    ↓
启动所有业务进程
    ↓
恢复看门狗监控，启用回滚检查，记录正常时间起点
    ↓
在监控线程中，利用现有的看门狗检查机制（checkExternalProcessWatchdog）
    ↓
如果看门狗超时（进程异常）：
    ↓
    在 checkExternalProcessWatchdog 中重置正常时间计时器
    ↓
    执行原有的进程重启逻辑
    ↓
如果连续5分钟（300秒）没有看门狗超时（正常时间未重置）：
    ↓
    说明所有进程正常，设置为 STATUS=DONE
    ↓
    清除更新状态文件，禁用回滚检查
```

**回滚操作**：
- 调用更新器：`update_handler -r`
- 更新器执行回滚操作（恢复之前的版本）
- 回滚完成后，清除更新状态文件
- 重启系统或重启业务进程

#### 2.10.4 更新完成处理

**更新器进程完成检测**：
- 守护进程在 `monitorThread()` 中检测更新器进程退出
- 如果返回码为0（成功），更新状态为 `UPDATED`
- 如果返回码非0（失败），记录错误，可选的恢复策略

**状态更新时机**：
1. **设置为 UPDATING**：`update_handler -p` 启动成功后
2. **设置为 UPDATED**：`update_handler` 成功完成后（返回码0）
3. **设置为 DONE**：检测到 `UPDATED` 状态后，启动进程并正常运行一段时间后

## 三、外部调用接口

### 3.1 发送更新请求（其他进程）

```cpp
#include "core/base/common/message_queue.h"

// BY ZF: 发送更新请求
// @param updatePackagePath 更新包所在路径（必需）
void sendUpdateRequest(const std::string& updatePackagePath) {
    MessageQueue systemQueue(MSG_KEY_SYSTEM);
    if (systemQueue.open()) {
        systemQueue.send(MSG_UPDATE_REQUEST, 
                        updatePackagePath.c_str(), 
                        updatePackagePath.length());
        std::cout << "Update request sent, package path: " << updatePackagePath << std::endl;
    } else {
        std::cout << "Failed to open system queue" << std::endl;
    }
}

// 使用示例
sendUpdateRequest("/usr/app/upgrade/update_package_v1.2.3.tar.gz");
```

### 3.2 消息内容说明

- **消息内容**：更新包所在路径（字符串，必需）
- **更新器路径**：从配置文件 `daemon.ini` 的 `[Daemon]` 节读取 `update_process_path`
- **参数传递**：更新包路径作为命令行参数传递给更新器进程

## 四、安全性考虑

1. **进程路径验证**：启动前检查文件是否存在、是否可执行
2. **权限控制**：确保更新处理进程有足够权限执行更新操作
3. **资源清理**：更新进程异常退出时的清理逻辑
4. **并发控制**：通过原子标志防止并发更新请求

## 五、更新器进程接口规范

### 5.1 命令行参数

更新器进程应接受以下命令行参数：

```bash
# 执行更新
update_handler -p <update_package_path>

# 执行回滚
update_handler -r
```

**参数说明**：
- `-p <update_package_path>`: 更新包所在路径（执行更新时必需）
- `-r`: 回滚标志（执行回滚时使用，不需要路径参数）

**设计优势**：
1. 使用标准命令行选项格式（`-p`, `-r`），符合Unix/Linux习惯
2. 参数含义清晰：`-p` 表示 package，`-r` 表示 rollback
3. 易于扩展：未来可添加其他选项（如 `-v` verbose, `-f` force等）
4. 参数解析规范：可使用 `getopt()` 等标准库解析

### 5.2 执行流程

**正常更新流程**（`update_handler -p <package_path>`）：
1. 解析命令行参数，获取更新包路径
2. 验证更新包完整性
3. 备份当前版本
4. 解压并安装新版本
5. 验证新版本
6. 退出（返回码0表示成功，非0表示失败）

**回滚流程**（`update_handler -r`）：
1. 解析命令行参数，识别回滚标志
2. 从备份恢复之前的版本
3. 验证恢复的版本
4. 退出（返回码0表示成功，非0表示失败）

### 5.3 与守护进程的交互

- 更新器作为独立进程运行，守护进程通过 `waitpid()` 监控其状态
- 更新器完成后，守护进程根据返回码判断更新是否成功
- 更新器不需要向守护进程发送消息（守护进程通过进程退出状态判断）

## 六、扩展性考虑

1. **更新进度反馈**：未来更新进程可通过消息队列向守护进程反馈进度
2. **多版本管理**：支持保留多个版本的备份，支持选择回滚到特定版本
3. **增量更新**：支持增量更新包，减少更新时间和存储空间
4. **更新验证**：支持更新前后的完整性校验和功能验证

## 七、测试方案

1. **单元测试**：
   - 测试消息接收和解析
   - 测试进程停止逻辑
   - 测试更新进程启动逻辑

2. **集成测试**：
   - 模拟发送更新请求消息
   - 验证所有业务进程被正确停止
   - 验证更新处理进程被正确启动
   - 验证重复请求被正确处理

3. **异常测试**：
   - 更新进程路径不存在
   - 更新进程无执行权限
   - 业务进程停止超时
   - 更新进程启动失败

## 八、实现细节补充

### 8.1 更新状态文件格式

```ini
# 更新状态文件：/usr/app/data/update_status.flag
# 状态值：UPDATING（更新中）、UPDATED（已更新，待验证）、DONE（完成且正常）
STATUS=UPDATED
PACKAGE_PATH=/usr/app/upgrade/update_package_v1.2.3.tar.gz
TIMESTAMP=1704067200
```

**状态枚举定义**（建议在代码中定义）：
```cpp
enum UpdateStatus {
    UPDATE_STATUS_NONE = 0,      // 无更新状态
    UPDATE_STATUS_UPDATING,      // 更新中
    UPDATE_STATUS_UPDATED,       // 已更新，待验证
    UPDATE_STATUS_DONE           // 完成且正常
};
```

### 8.2 看门狗监控暂停实现

在 `monitorThread()` 中：
```cpp
if (m_watchdogEnabled.load()) {
    handleWatchdogMessage();  // 只在启用时处理看门狗消息
}
```

在 `performUpdate()` 中：
```cpp
m_watchdogEnabled = false;  // 暂停看门狗监控
// ... 执行更新流程 ...
```

### 8.3 系统启动时的状态检查实现

在 `doInitialize()` 或启动流程中：
```cpp
// BY ZF: 系统启动时检查更新状态
std::string updateStatus, updatePackagePath;
if (readUpdateStatus(updateStatus, updatePackagePath)) {
    if (updateStatus == "UPDATING") {
        // 更新中断，执行回滚
        std::cout << "Update interrupted, performing rollback..." << std::endl;
        performRollback();
    } else if (updateStatus == "UPDATED") {
        // 文件已更新，需要启动进程并验证
        std::cout << "Update completed, starting processes for verification..." << std::endl;
        // 启动业务进程
        startAllProcesses();
        // 启动回滚检查机制（利用现有的看门狗监控）
        m_rollbackCheckEnabled = true;
        m_lastNormalTime = std::chrono::steady_clock::now();  // 记录正常时间起点
        m_watchdogEnabled = true;  // 恢复看门狗监控（用于判断进程是否正常）
    } else if (updateStatus == "DONE") {
        // 更新完成且已验证正常，清除状态文件
        std::cout << "Update verified successfully, clearing status file..." << std::endl;
        remove(m_updateStatusFile.c_str());
    }
}
```

### 8.3.1 回滚检查和DONE状态检查实现

**核心思想**：直接利用现有的看门狗监控机制，无需额外的进程状态检查逻辑。

**实现方案**：

1. **在 `checkExternalProcessWatchdog()` 中重置正常时间**：
```cpp
// BY ZF: 在checkExternalProcessWatchdog中，如果检测到看门狗超时，重置正常时间计时器
void DaemonProcess::checkExternalProcessWatchdog(const std::string& processName)
{
    // ... 原有的检查逻辑 ...
    auto watchdogIt = m_externalWatchdogs.find(processName);
    if (watchdogIt != m_externalWatchdogs.end()) {
        auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - watchdogIt->second).count();
        
        if (elapsed >= m_watchdogTimeout) {
            // BY ZF: 看门狗超时，说明进程异常，重置正常时间计时器
            if (m_rollbackCheckEnabled.load()) {
                m_lastNormalTime = std::chrono::steady_clock::now();
            }
            
            // ... 原有的重启逻辑 ...
        }
    }
}
```

2. **在 `monitorThread()` 中检查DONE状态**：
```cpp
// BY ZF: 在监控线程中，检查是否需要设置为DONE或回滚
// 利用现有的看门狗监控机制：如果看门狗超时，会在checkExternalProcessWatchdog中重置正常时间
// 如果连续5分钟没有看门狗超时（正常时间未重置），说明所有进程正常
if (m_rollbackCheckEnabled.load()) {
    auto now = std::chrono::steady_clock::now();
    auto normalDuration = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_lastNormalTime).count();
    
    // 如果连续5分钟（300秒）正常（没有看门狗超时），设置为DONE
    if (normalDuration >= 300) {
        recordUpdateStatus("DONE");
        m_rollbackCheckEnabled = false;
        std::cout << "Update verified successfully (5 minutes normal), status set to DONE" << std::endl;
        remove(m_updateStatusFile.c_str());
    }
}
```

**回滚逻辑**：
- 回滚检查可以在 `checkExternalProcessWatchdog()` 中实现
- 如果看门狗超时且累计异常时间超过阈值，则触发回滚
- 或者在 `monitorThread()` 中定期检查异常时间（需要在看门狗超时时记录异常开始时间）

### 8.3.1 回滚操作实现

```cpp
// BY ZF: 执行回滚操作
bool DaemonProcess::performRollback() {
    std::cout << "Rollback needed, starting rollback process..." << std::endl;
    
    // 暂停看门狗监控
    m_watchdogEnabled = false;
    
    // 停止所有业务进程（如果还在运行）
    stopAllProcesses();
    
    // 启动回滚进程：update_handler -r
    if (!startRollbackProcess()) {
        std::cout << "Failed to start rollback process" << std::endl;
        m_watchdogEnabled = true;  // 恢复看门狗监控
        return false;
    }
    
    // 记录回滚状态
    m_isUpdating = true;
    std::cout << "Rollback process started, PID: " << m_updateProcessPid << std::endl;
    
    return true;
}
```

### 8.4 更新进程启动实现

```cpp
// BY ZF: 启动更新处理进程（执行更新）
bool startUpdateProcess(const std::string& updatePackagePath) {
    // 构造命令行参数：update_handler -p <update_package_path>
    // fork + execv 启动更新器进程
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：执行更新器
        char* args[] = {
            const_cast<char*>(m_updateProcessPath.c_str()),
            const_cast<char*>("-p"),  // 更新选项
            const_cast<char*>(updatePackagePath.c_str()),
            nullptr
        };
        execv(m_updateProcessPath.c_str(), args);
        exit(1);
    } else if (pid > 0) {
        // 父进程：记录PID
        m_updateProcessPid = pid;
        return true;
    }
    return false;
}

// BY ZF: 启动更新处理进程（执行回滚）
bool startRollbackProcess() {
    // 构造命令行参数：update_handler -r
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：执行更新器回滚
        char* args[] = {
            const_cast<char*>(m_updateProcessPath.c_str()),
            const_cast<char*>("-r"),  // 回滚选项
            nullptr
        };
        execv(m_updateProcessPath.c_str(), args);
        exit(1);
    } else if (pid > 0) {
        // 父进程：记录PID
        m_updateProcessPid = pid;
        return true;
    }
    return false;
}
```

### 8.5 更新完成检测

在 `monitorThread()` 中：
```cpp
// BY ZF: 检查更新进程是否完成
if (m_updateProcessPid > 0) {
    int status;
    pid_t result = waitpid(m_updateProcessPid, &status, WNOHANG);
    if (result == m_updateProcessPid) {
        // 更新进程已完成
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // 更新成功，设置状态为 UPDATED
            recordUpdateStatus("UPDATED", m_updatePackagePath);
            std::cout << "Update process completed successfully, status set to UPDATED" << std::endl;
            
        // 启动业务进程并开始回滚检查
        startAllProcesses();
        m_rollbackCheckEnabled = true;
        m_lastNormalTime = std::chrono::steady_clock::now();  // 记录正常时间起点
        } else {
            // 更新失败，记录错误
            std::cout << "Update process failed, exit code: " << WEXITSTATUS(status) << std::endl;
            // 可选：执行回滚或保留UPDATING状态
        }
        m_updateProcessPid = -1;
        m_isUpdating = false;
        m_watchdogEnabled = true;  // 恢复看门狗监控
    }
}
```

### 8.6 状态记录实现

```cpp
// BY ZF: 记录更新状态到持久化文件
void DaemonProcess::recordUpdateStatus(const std::string& status, 
                                       const std::string& updatePackagePath) {
    std::ofstream file(m_updateStatusFile);
    if (file.is_open()) {
        file << "STATUS=" << status << std::endl;
        if (!updatePackagePath.empty()) {
            file << "PACKAGE_PATH=" << updatePackagePath << std::endl;
        }
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        file << "TIMESTAMP=" << timestamp << std::endl;
        file.close();
    }
}

// BY ZF: 读取更新状态
bool DaemonProcess::readUpdateStatus(std::string& status, 
                                     std::string& lastUpdatePackagePath) {
    std::ifstream file(m_updateStatusFile);
    if (!file.is_open()) {
        return false;  // 文件不存在
    }
    
    status.clear();
    lastUpdatePackagePath.clear();
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("STATUS=") == 0) {
            status = line.substr(7);
        } else if (line.find("PACKAGE_PATH=") == 0) {
            lastUpdatePackagePath = line.substr(13);
        }
    }
    
    return !status.empty();  // 至少要有状态字段
}
```


