# tcu_card 架构设计

## 1. 目标

`tcu_card` 的职责只收敛到读卡器设备侧：

- 管理串口驱动与设备在线状态
- 接收上层原子设备动作请求
- 按读卡器协议组帧、发送、收帧、解帧
- 将结果解析为统一的结构化反馈并上报

它不负责：

- 在线/离线刷卡业务判断
- 启停充、扣费、锁卡、白名单等业务规则
- UI 页面切换
- 主站授权流程

## 2. 模块组成

- `main.cpp`
  - 进程入口，固定读取 `/usr/app/config/tcu_card.ini`
- `card_process.*`
  - 进程主循环、配置加载、MQTT 接入、命令队列、结果上报
- `card_reader.*`
  - 串口驱动适配，负责一次命令的同步收发
- `card_protocol.*`
  - 命令组帧、响应解帧、XOR 校验、结果码映射

整体分层与 `meter` 对齐，但 `tcu_card` 不是轮询采集，而是“请求驱动型串口设备进程”。

## 3. 输入输出模型

### 3.1 设计原则

对外 MQTT 接口只暴露业务动作和业务数据，不暴露协议细节和卡底层访问步骤。

说明：

- 本章定义的是 `tcu_card` 建议收敛后的对外 MQTT 契约
- 当前代码骨架仍保留了一部分原子设备动作接口
- 后续实现应以本章定义的业务接口为准，对内部底层步骤做封装

外部业务可见的只包括：

- RF 开关
- 读卡
- 写卡
- 锁卡
- 解锁

内部流程不对外暴露：

- `active_card`
- `auth_m1`
- `read_block`
- `write_block`
- 扇区密钥
- 块号/扇区认证过程

也就是说，外部只关心“我要读到什么业务数据”或“我要改什么业务字段”，不关心内部经过了几次认证和几次块读写。

当前约定：

- 每次业务读写成功后，模块内部都会附加发送一次 `READER_OP_BUZZER`
- 蜂鸣器只作为本地提示，不额外通过 MQTT 上报单独事件
- 即使蜂鸣器执行失败，也不把已经成功的读写业务改判为失败

### 3.2 对外输入

默认订阅 MQTT Topic：

```text
tcu/card/0/cmd
```

当前接口约束：

- 命令 Topic：`tcu/card/0/cmd`
- 事件 Topic：`tcu/card/0/event`
- 订阅 QoS：`1`
- 发布 QoS：`1`
- 不使用 `requestId`
- 单通道、单队列、串行执行

#### 3.2.1 命令定义

| `op` | 说明 | 主要入参 |
| --- | --- | --- |
| `open_rf` | 打开射频 | 无 |
| `close_rf` | 关闭射频 | 无 |
| `card_read` | 读取卡业务数据 | 无 |
| `card_write` | 写卡业务数据 | `cardNoHex`、`cardBalance` |
| `card_lock` | 锁卡 | 无 |
| `card_unlock` | 解锁 | 无 |

命令公共规则：

- 所有命令都是 JSON
- 不传 `requestId`
- 不传扇区密钥
- 不传 UID
- 不传块号
- 上层只表达业务目标，不表达底层读卡器过程

#### 3.2.2 命令字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `op` | string | 是 | 业务动作名 |
| `cardNoHex` | string | 否 | 16 字节卡号，32 个十六进制字符 |
| `cardBalance` | number | 否 | 卡余额，单位分 |

`card_write` 规则：

- 至少提供一个业务字段
- `cardNoHex` 存到扇区 0 / 块 1
- `cardBalance` 存到扇区 8 / 块 32，并自动写入反码
- 锁卡状态不通过 `card_write` 写，统一用 `card_lock` / `card_unlock`

#### 3.2.3 命令示例

`open_rf`

```json
{
  "op": "open_rf"
}
```

`close_rf`

```json
{
  "op": "close_rf"
}
```

`card_read`

```json
{
  "op": "card_read"
}
```

`card_write`

```json
{
  "op": "card_write",
  "cardNoHex": "31323334353637383930313233343536",
  "cardBalance": 1250
}
```

`card_lock`

```json
{
  "op": "card_lock"
}
```

`card_unlock`

```json
{
  "op": "card_unlock"
}
```

### 3.3 对外输出

默认所有结果和状态都发布到：

```text
tcu/card/0/event
```

#### 3.3.1 事件公共字段

| 字段 | 类型 | 是否总是出现 | 说明 |
| --- | --- | --- | --- |
| `ts` | number | 是 | 毫秒时间戳 |
| `seq` | number | 是 | `tcu_card` 本地递增序号 |
| `source` | string | 是 | 固定为 `tcu_card` |
| `event` | string | 是 | 事件类型 |
| `data` | object | 是 | 业务数据或失败原因 |

当前建议的对外事件类型：

- `device_online`
- `device_offline`
- `cmd_rejected`
- `rf_opened`
- `rf_closed`
- `card_info`
- `card_written`
- `card_locked`
- `card_unlocked`
- `op_failed`

#### 3.3.2 事件定义

| `event` | 触发时机 | `data` 字段 |
| --- | --- | --- |
| `device_online` | 读卡器可用 | `reason` |
| `device_offline` | 读卡器离线或串口异常 | `reason` |
| `cmd_rejected` | MQTT 消息本身非法 | `reason` |
| `rf_opened` | `open_rf` 成功 | 可为空对象 |
| `rf_closed` | `close_rf` 成功 | 可为空对象 |
| `card_info` | `card_read` 成功，或锁卡/解锁成功后的状态回读 | `uidHex`、`cardNoHex`、`cardBalance`、`locked` |
| `card_written` | `card_write` 成功 | `cardNoHex`、`cardBalance` |
| `card_locked` | `card_lock` 成功 | `locked=true` |
| `card_unlocked` | `card_unlock` 成功 | `locked=false` |
| `op_failed` | 业务命令失败 | `op`、`reason`，必要时附带底层状态 |

#### 3.3.3 事件示例

`device_online`

```json
{
  "ts": 1736150000000,
  "seq": 1,
  "source": "tcu_card",
  "event": "device_online",
  "data": {
    "reason": "reader_ready"
  }
}
```

`rf_opened`

```json
{
  "ts": 1736150001000,
  "seq": 2,
  "source": "tcu_card",
  "event": "rf_opened",
  "data": {}
}
```

`card_info`

```json
{
  "ts": 1736150002000,
  "seq": 3,
  "source": "tcu_card",
  "event": "card_info",
  "data": {
    "uidHex": "11223344",
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 1250,
    "locked": false
  }
}
```

`card_written`

```json
{
  "ts": 1736150002600,
  "seq": 4,
  "source": "tcu_card",
  "event": "card_written",
  "data": {
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 2000
  }
}
```

`card_locked`

```json
{
  "ts": 1736150003200,
  "seq": 5,
  "source": "tcu_card",
  "event": "card_locked",
  "data": {
    "locked": true
  }
}
```

锁卡或解锁成功后，模块应立即再补发一条新的 `card_info`，把当前卡状态同步给外部业务。例如锁卡成功后的回读结果：

```json
{
  "ts": 1736150003300,
  "seq": 6,
  "source": "tcu_card",
  "event": "card_info",
  "data": {
    "uidHex": "11223344",
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 2000,
    "locked": true
  }
}
```

`op_failed`

```json
{
  "ts": 1736150003600,
  "seq": 7,
  "source": "tcu_card",
  "event": "op_failed",
  "data": {
    "op": "card_read",
    "reason": "serial_recv_timeout"
  }
}
```

### 3.4 内部流程与对外接口的映射

外部接口精简后，`tcu_card` 内部仍然会执行原来的底层步骤，但这些步骤不再通过 MQTT 向上暴露。

`card_read` 内部流程：

1. `active_card`
2. 读取 UID
3. `auth_m1` 扇区 0
4. 读取块 1，得到卡号
5. `auth_m1` 扇区 4
6. 读取块 16，得到锁卡标志
7. `auth_m1` 扇区 8
8. 读取块 32，得到余额和反码
9. 整理成 `card_info` 事件上报
10. 蜂鸣器响一声

`card_write` 内部流程：

1. `active_card`
2. 如需写卡号，则认证扇区 0 并写块 1
3. 如需写余额，则认证扇区 8 并写块 32
4. 整理成 `card_written` 事件上报
5. 蜂鸣器响一声

`card_lock` / `card_unlock` 内部流程：

1. `active_card`
2. 认证扇区 4
3. 读块 16 或直接按当前策略重写块 16
4. 修改锁卡标志位
5. 上报 `card_locked` 或 `card_unlocked`
6. 重新读取卡业务数据
7. 上报最新 `card_info`
8. 蜂鸣器响一声

### 3.5 MQTT 消息与读卡操作的关联方式

当前版本仍然采用：

- 单设备
- 单 MQTT 命令通道
- 单飞行命令队列
- 串行执行

因此，外部看到的是：

```text
cmd(card_read)   -> event(card_info/op_failed/cmd_rejected)
cmd(card_write)  -> event(card_written/op_failed/cmd_rejected)
cmd(card_lock)   -> event(card_locked/op_failed/cmd_rejected) -> event(card_info)
cmd(card_unlock) -> event(card_unlocked/op_failed/cmd_rejected) -> event(card_info)
```

### 3.6 上层使用约束

1. 同一时刻只驱动一条业务命令链
2. 一条命令发出后，等待终结事件返回，再发下一条
3. 上层只消费业务事件，不依赖内部原子动作
4. `device_online` / `device_offline` 是链路状态，不是某条业务命令的终结事件

推荐时序：

```text
publish cmd(open_rf)
  -> wait event(rf_opened/op_failed/cmd_rejected)
publish cmd(card_read)
  -> wait event(card_info/op_failed/cmd_rejected)
publish cmd(card_lock)
  -> wait event(card_locked/op_failed/cmd_rejected)
  -> wait event(card_info)
```

### 3.7 MQTT 测试用例

下面给一组本地联调用的最小测试用例，假设 MQTT Broker 在本机 `127.0.0.1:1883`。

先订阅事件：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t tcu/card/0/event -v
```

#### 3.7.1 打开 RF

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"open_rf"}'
```

预期事件：

```json
{
  "event": "rf_opened",
  "data": {}
}
```

#### 3.7.2 读卡

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_read"}'
```

预期事件：

```json
{
  "event": "card_info",
  "data": {
    "uidHex": "11223344",
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 1250,
    "locked": false
  }
}
```

同时读卡器本地蜂鸣器会响一声。

#### 3.7.3 写余额

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_write","cardBalance":2000}'
```

预期事件：

```json
{
  "event": "card_written",
  "data": {
    "cardBalance": 2000
  }
}
```

建议紧接着再发一次 `card_read`，确认余额已更新。
写卡成功后，读卡器本地蜂鸣器会响一声。

#### 3.7.4 写卡号和余额

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_write","cardNoHex":"31323334353637383930313233343536","cardBalance":3000}'
```

预期事件：

```json
{
  "event": "card_written",
  "data": {
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 3000
  }
}
```

写卡成功后，读卡器本地蜂鸣器会响一声。

#### 3.7.5 锁卡

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_lock"}'
```

预期先后收到两条事件：

```json
{
  "event": "card_locked",
  "data": {
    "locked": true
  }
}
```

```json
{
  "event": "card_info",
  "data": {
    "uidHex": "11223344",
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 3000,
    "locked": true
  }
}
```

锁卡流程成功后，读卡器本地蜂鸣器会响一声。

#### 3.7.6 解锁

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_unlock"}'
```

预期先后收到两条事件：

```json
{
  "event": "card_unlocked",
  "data": {
    "locked": false
  }
}
```

```json
{
  "event": "card_info",
  "data": {
    "uidHex": "11223344",
    "cardNoHex": "31323334353637383930313233343536",
    "cardBalance": 3000,
    "locked": false
  }
}
```

解锁流程成功后，读卡器本地蜂鸣器会响一声。

#### 3.7.7 关闭 RF

发送：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"close_rf"}'
```

预期事件：

```json
{
  "event": "rf_closed",
  "data": {}
}
```

#### 3.7.8 异常样例

非法命令：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"unknown"}'
```

预期事件：

```json
{
  "event": "cmd_rejected",
  "data": {
    "reason": "unsupported_op"
  }
}
```

写卡但未提供任何业务字段：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t tcu/card/0/cmd -m '{"op":"card_write"}'
```

预期事件：

```json
{
  "event": "cmd_rejected",
  "data": {
    "reason": "card_write requires cardNoHex or cardBalance"
  }
}
```

## 4. 协议模型

请求帧格式：

```text
02 LEN_H LEN_L PAYLOAD XOR 03
```

响应帧格式：

```text
02 LEN_H LEN_L STATUS_H STATUS_L DATA... XOR 03
```

协议解析规则：

1. 找到 `0x02`
2. 读取 `LEN`
3. 判断整帧是否收齐
4. 校验 `XOR`
5. 校验 `ETX = 0x03`
6. 解析 `statusCode + payload`

## 5. 运行状态机

设备进程侧状态机保持极简：

```text
Idle
  -> 取一条命令
  -> 组帧并串口发送
  -> 等待响应
  -> 成功: event(op_succeeded) 上报
  -> 失败: event 上报
  -> 回到 Idle
```

串口交互内部状态只保留三态语义：

- `IDLE`
- `SENDING`
- `WAITING_REPLY`

## 6. 当前实现边界

当前骨架版本已包含：

- 串口配置加载
- MQTT 命令接入
- 单飞行命令队列
- 读卡器协议组帧
- 响应解帧与错误分类
- 结果码映射
- 统一事件上报

当前还未做：

- 与上层刷卡业务状态机正式对接
- 读卡器返回 payload 的语义级字段拆解
- 远端 ARM 环境联调验证
- 守护进程配置与发布脚本接入

## 7. 配置示例

```ini
[Card]
serial_device=/dev/ttyS5
serial_baudrate=57600
serial_data_bits=8
serial_stop_bits=1
serial_parity=N
read_timeout_ms=2000

mqtt_host=127.0.0.1
mqtt_port=1883
mqtt_keepalive=60
mqtt_client_id=tcu_card
mqtt_topic_prefix=tcu

cmd_topic=tcu/card/0/cmd
event_topic=tcu/card/0/event

debug_protocol=0
```

当前读卡器串口驱动默认按以下方式打开和配置：

- Linux 设备节点：`/dev/ttyS5`
- 波特率：`57600`
- 数据位：`8`
- 停止位：`1`
- 校验位：`None`
- 流控：关闭
- 打开方式：`O_RDWR | O_NOCTTY | O_NONBLOCK`
