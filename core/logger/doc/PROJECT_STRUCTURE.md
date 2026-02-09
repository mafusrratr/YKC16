# Logger 模块项目结构

## 目录结构

```
core/
├── interfaces/          # 接口定义层
│   └── iprocess.h      # ✅ 进程基类接口（所有进程都需要）
│
├── base/               # 基础类层
│   ├── common/                   # ✅ 通用基础组件
│   │   ├── config_manager_lite.h/cpp
│   │   ├── message_queue.h/cpp
│   │   └── logger_types.h        # ✅ 日志类型定义（LogLevel, LogEntry, TradeRecord, FeeModel）
│   ├── logger/                   # ✅ 日志相关基础组件
│   │   └── log_sender.h/cpp
│   └── process/                  # ✅ 进程管理基础组件
│       ├── base_process.h/cpp
│       └── process_monitor.h
│
└── logger/             # Logger 进程实现层
    ├── main.cpp                  # ✅ Logger 进程入口
    ├── logger_process.h/cpp      # ✅ Logger 进程实现
    ├── database_manager.h/cpp    # ✅ 数据库管理
    ├── test_logger.cpp           # ✅ 单元测试程序
    ├── test_all_logs.cpp         # ✅ 完整测试程序
    ├── Makefile                  # ✅ 本地编译
    ├── Makefile.cross            # ✅ 交叉编译
    └── README.md                 # ✅ 使用文档
```

## 核心组件说明

### 1. **类型定义层 (base/common/)**

**logger_types.h** - 定义所有日志相关的类型（从 ilogger.h 提取）
- `enum LogLevel` - 日志级别 (DEBUG/INFO/WARN/ERROR/FATAL)
- `struct LogEntry` - 日志条目结构
- `struct TradeRecord` - 充电交易记录结构
- `struct FeeModel` - 计费模型结构

**注意**：LoggerProcess 不再实现 ILogger 接口，直接实现日志功能

### 2. **基础类层 (base/)**

**LogSender** (`base/logger/`) - 日志发送器（供其他进程使用）
- 异步发送日志（5级日志）
- 发送充电交易记录
- 发送计费模型
- 本地缓冲（100条或2秒自动刷新）

**MessageQueue** (`base/common/`) - System V 消息队列封装
- 消息大小：8192 字节（支持大型 JSON）
- 支持创建、发送、接收、销毁

**ConfigManagerLite** (`base/common/`) - 轻量级配置管理
- 解析 INI 文件
- 不依赖 Qt

### 3. **实现层 (logger/)**

**LoggerProcess** - Logger 主进程
- 接收消息队列中的日志
- 解析 JSON 消息
- 数据验证（TradeRecord、FeeModel）
- 写入数据库
- **更新共享内存**（计费模型）

**DatabaseManager** - 数据库管理
- 管理3个数据库：
  - `tcu.db` - 运行日志
  - `chargerecords.db` - 充电交易记录
  - `feemodel.db` - 计费模型
- 提供查询接口
- **静态方法支持从文件加载数据**

## 数据流

### 日志流程
```
其他进程 → LogSender → MessageQueue → LoggerProcess → DatabaseManager → SQLite
```

### 计费模型流程
```
保存：其他进程 → LogSender.saveFeeModel() → MessageQueue 
     → LoggerProcess.parseAndLogMessage() 
     → DatabaseManager.saveFeeModelToDb() 
     → 立即更新共享内存

读取：其他进程 → CShm.evs_getFeeModel() → 共享内存
```

## 编译方式

### 本地编译（macOS/Linux）
```bash
cd core/logger
make
```

### 交叉编译（iMX6UL）
```bash
cd core/logger
make -f Makefile.cross clean
make -f Makefile.cross
```

## 配置文件

**logger.ini** - 简化后的配置
```ini
[Logger]
log_level=1

[Database]
main_db_path=/usr/app/data/tcu.db
charge_db_path=/usr/app/data/chargerecords.db
fee_db_path=/usr/app/data/feemodel.db
```

## 测试

```bash
# 启动 logger 进程
./tcu_logger ../../config/project_a/logger.ini &

# 运行测试
./test_logger

# 查看数据库
sqlite3 ../../data/tcu.db "SELECT * FROM operation_logs ORDER BY id DESC LIMIT 10;"
sqlite3 ../../data/chargerecords.db "SELECT * FROM charge_trade_info ORDER BY id DESC LIMIT 10;"
sqlite3 ../../data/feemodel.db "SELECT * FROM tbFeeModel ORDER BY id DESC LIMIT 10;"
```

## 依赖库

- `libsqlite3.so` - SQLite 数据库
- `libevsshm.so` - 共享内存库（位于 `libv2gshm/cshm/bin/Release/`）
- `libpthread.so` - 线程库

## 注意事项

1. ✅ 统一使用 `core/base/common/logger_types.h`（类型定义）
2. ✅ `segFlag` 字段保存在数据库，但不写入共享内存
3. ✅ 消息队列缓冲区大小：8192 字节
4. ✅ 计费模型最多支持 96 个时段
5. ✅ Logger 初始化时自动加载最新计费模型到共享内存
6. ✅ 新计费模型保存后立即更新共享内存

---
**最后更新：** 2025-11-07

