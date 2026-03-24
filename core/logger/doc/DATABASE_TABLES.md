# 数据库表结构说明

## 主数据库 (tcu.db)

### 1. `operation_logs` - 统一运行日志表 ⭐
**用途**: 存储所有运行日志，包括日常日志、系统事件、错误信息等，统一通过 `level` 和 `module` 字段区分

**字段说明**:
- `id`: 主键，自增
- `timestamp`: 日志时间戳
- `level`: 日志级别 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL)
- `module`: 模块名称，用于区分日志来源
  - 业务模块：`"daemon"`, `"tcu_logic"`, `"meter"` 等
  - 系统模块：`"system"` (系统事件)
  - 错误模块：`"error"` (错误日志)
- `message`: 日志消息内容，包含事件类型或错误信息
- `details`: 详细信息和额外数据（JSON格式）
  - 系统事件：`{"event_data":"...", "severity":...}`
  - 错误信息：`{"error_code":..., "stack_trace":"..."}`
- `created_at`: 创建时间

**数据来源（统一入口）**:
- ✅ **所有日志都通过消息队列接收**：外部进程使用 `LogSender` 发送日志消息
  - `LogSender.info()` / `warn()` / `error()` 等 → 构造JSON格式日志 → 消息队列 → 日志进程接收 → `operation_logs` 表
- ✅ **发送方负责构造完整信息**：各程序负责提供 `level`、`module`、`message`、`details` 字段
- ✅ **日志进程只负责接收和保存**：日志进程无条件接收消息队列中的日志，解析后保存，不做任何特殊处理或转换

**使用场景**:
- 日常运维查看系统运行状态
- 问题排查和故障分析
- 系统运行轨迹追踪
- 通过 `level` 过滤不同级别的日志（ERROR、WARN等）
- 通过 `module` 过滤不同模块的日志

**查询示例**:
```sql
-- 查看所有错误日志
SELECT * FROM operation_logs WHERE level >= 3 ORDER BY timestamp DESC;

-- 查看系统事件
SELECT * FROM operation_logs WHERE module = 'system' ORDER BY timestamp DESC;

-- 查看特定模块的日志
SELECT * FROM operation_logs WHERE module = 'daemon' ORDER BY timestamp DESC;
```

---

### 2. `performance_logs` - 性能监控日志表
**用途**: 存储系统性能指标，用于性能分析和监控

**字段说明**:
- `id`: 主键，自增
- `timestamp`: 性能数据时间戳
- `cpu_usage`: CPU使用率（百分比）
- `memory_usage`: 内存使用率（百分比）
- `disk_usage`: 磁盘使用率（百分比）
- `network_usage`: 网络使用率（百分比）

**数据来源**:
- 系统监控模块定期采集的性能数据
- 目前测试数据可能为空，待性能监控模块实现后使用

**使用场景**:
- 系统性能监控
- 资源使用分析
- 性能优化参考

---

## 故障数据库 (error.db)

### 1. `fault_records` - MQTT 故障记录表
**用途**: 保存 `tcu/save/{gun}/event` 主题下 `type=Error` 的故障事件，供 HMI/查询模块直接读取。

**字段说明**:
- `id`: 主键，自增
- `gun`: 枪号
- `type`: 事件类型，当前固定为 `Error`
- `occur_time`: 故障发生时间
- `point_key`: 故障点位键
- `fault_message`: 故障描述
- `raw_value`: 原始值

---

## 总结

**简化设计**：
- ✅ **统一日志表**: 所有日志（日常日志、系统事件、错误信息）统一存储到 `operation_logs` 表
- ✅ **通过字段区分**: 使用 `level` 字段区分日志级别，使用 `module` 字段区分日志来源
- ✅ **性能监控**: 单独的 `performance_logs` 表用于性能指标存储
- ✅ **故障记录库**: `error.db` 中的 `fault_records` 表用于保存 MQTT `tcu/save/{gun}/event` 的 `type=Error` 事件

**主要使用场景**:
1. **`operation_logs`** - ⭐ 统一日志表，记录所有运行日志、系统事件、错误信息
2. **`performance_logs`** - 性能监控（待性能监控模块实现）

**数据流向（统一入口设计）**:
```
所有外部进程
    ↓
LogSender (构造完整日志信息: level, module, message, details)
    ↓ JSON格式
消息队列 (MSG_KEY_LOG)
    ↓
LoggerProcess (无条件接收，不做特殊处理)
    ↓ 解析JSON + 缓冲 + 批量写入
数据库 (operation_logs表)
```

**设计原则**:
- ✅ 统一入口：所有日志都通过消息队列，只有一个接收路径
- ✅ 发送方负责：各程序负责构造完整的日志字段信息
- ✅ 无条件保存：日志进程只负责接收和保存，不做任何转换或特殊处理
- ✅ 性能监控例外：`performance_logs` 表有独立的写入接口（因为需要特殊的数据格式）

**查询示例**:
```sql
-- 查看最近的运行日志
SELECT timestamp, level, module, message 
FROM operation_logs 
ORDER BY id DESC LIMIT 20;

-- 查看所有错误日志（level >= 3 表示 ERROR 和 FATAL）
SELECT timestamp, module, message, details 
FROM operation_logs 
WHERE level >= 3
ORDER BY id DESC LIMIT 10;

-- 查看系统事件（module = 'system'）
SELECT timestamp, level, message, details 
FROM operation_logs 
WHERE module = 'system'
ORDER BY id DESC LIMIT 10;

-- 查看特定模块的日志
SELECT timestamp, level, message 
FROM operation_logs 
WHERE module = 'daemon'
ORDER BY id DESC LIMIT 20;
```
