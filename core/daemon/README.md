# 守护进程模块 (Daemon Process)

## 概述
守护进程是TCU系统的核心管理模块，负责启动、监控、管理所有业务进程的生命周期。

## 功能特性
- **进程管理**: 自动启动、停止、重启外部进程
- **健康监控**: 通过fork监控和看门狗机制监控进程健康状态
- **配置管理**: 从配置文件加载进程列表和参数
- **固件升级**: 支持固件升级功能
- **统计报告**: 提供详细的进程状态和统计信息

## 架构设计

### 监控机制
1. **外部进程监控**: 使用`waitpid()`检查进程是否退出
2. **看门狗监控**: 通过消息队列接收外部进程的喂狗信号
3. **自动重启**: 进程异常时自动重启，支持重启次数限制

### 消息队列
- **看门狗队列**: `MSG_KEY_WATCHDOG (0x1238)` - 接收外部进程的看门狗喂狗信号
- **消息类型**: `MSG_WATCHDOG_FEED (5)` - 看门狗喂狗消息

## 文件结构
```
core/daemon/
├── main.cpp              # 主程序入口
├── daemon_process.h      # 守护进程类定义
├── daemon_process.cpp    # 守护进程类实现
├── Makefile             # 编译配置
└── README.md            # 本文档
```

## 编译和运行

### 编译
```bash
cd core/daemon
make
```

### 运行
```bash
# 使用默认配置
./tcu_daemon

# 使用指定配置
./tcu_daemon config/project_a
```

### 安装
```bash
make install
```

## 配置说明

### 配置文件格式
```ini
[Daemon]
# 进程监控间隔（秒）
monitor_interval=5
# 看门狗超时时间（秒）
watchdog_timeout=30
# 进程异常重启次数限制
max_restart_count=10
# 升级文件路径
upgrade_dir=/usr/app/upgrade
# 部署目录
deploy_dir=/usr/app/tcu
# 进程可执行文件列表（逗号分隔）
process_list=tcu_meter,tcu_logic,tcu_comm,tcu_cdz,tcu_display
```

### 进程列表
守护进程会自动从`process_list`配置项加载需要管理的进程：
- 自动构造可执行文件路径: `{deploy_dir}/{process_name}`
- 自动启动所有配置的进程
- 自动监控所有进程的健康状态

## 外部进程接口

### 看门狗喂狗
外部进程需要定期向守护进程发送看门狗喂狗信号：

```cpp
// 外部进程喂狗示例
#include "message_queue.h"

void feedWatchdog(const std::string& processName) {
    MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    if (watchdogQueue.open()) {
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName.c_str(), processName.length());
    }
}

// 在主循环中定期调用
void mainLoop() {
    while (running) {
        // 业务逻辑
        doWork();
        
        // 喂狗
        feedWatchdog("tcu_meter");
        
        sleep(1);
    }
}
```

## 状态监控

### 状态报告
守护进程提供详细的状态报告：
- 运行状态
- 进程列表和PID
- 监控统计信息
- 重启次数统计

### 信号处理
- `SIGINT` (Ctrl+C): 优雅停止
- `SIGTERM`: 优雅停止  
- `SIGHUP`: 重新加载配置

## 测试

### 单元测试
```bash
make test
```

### 集成测试
1. 启动守护进程
2. 检查进程启动状态
3. 模拟进程异常退出
4. 验证自动重启功能
5. 测试看门狗超时重启

## 部署

### 目标系统
- 部署目录: `/usr/app/tcu`
- 可执行文件: `tcu_daemon`
- 配置文件: `config/project_a/config.ini`

### 启动脚本
```bash
#!/bin/bash
cd /usr/app/tcu
./tcu_daemon config/project_a &
```

## 故障排除

### 常见问题
1. **进程启动失败**: 检查可执行文件路径和权限
2. **看门狗超时**: 检查外部进程是否正确发送喂狗信号
3. **消息队列错误**: 检查系统资源限制

### 日志
守护进程会输出详细的运行日志，包括：
- 进程启动/停止事件
- 监控检查结果
- 异常重启事件
- 统计信息

## 开发说明

### 依赖关系
- `core/base/process/base_process.h` - 进程基类
- `core/base/common/config_manager.h` - 配置管理
- `core/base/common/message_queue.h` - 消息队列
- `core/interfaces/iprocess.h` - 进程接口

### 扩展接口
- 支持添加新的监控策略
- 支持自定义重启逻辑
- 支持插件化的固件升级

## 版本历史
- v1.0.0: 初始版本，支持基本进程管理和监控
